# System_String.py
#
# GDB pretty-printer for System::String (Beef language)
#
# Memory layout (!BF_LARGE_STRINGS, i.e. int32/uint32 mode):
#
#   class String {                        // [Ordered] Beef class
#       int32   mLength;                  // character count in char8 units
#       uint32  mAllocSizeAndFlags;       // alloc size (lower 30 bits) | flags (upper 2 bits)
#       char8*  mPtrOrBuffer;             // pointer when cStrPtrFlag set;
#                                         // otherwise the char8 data lives inline starting
#                                         // at this field's address, extending past it
#   };
#
#   cSizeFlags    = 0x3FFFFFFF  -- mask to extract alloc size
#   cDynAllocFlag = 0x80000000  -- heap-allocated buffer; mPtrOrBuffer is the pointer
#   cStrPtrFlag   = 0x40000000  -- mPtrOrBuffer is a pointer (heap OR static/fixed ref)
#
# Children:
#   [Length]       always present
#   [AllocSize]    when cDynAllocFlag is set            (heap buffer)
#   [RefSize]      when cStrPtrFlag set, cDynAllocFlag not set  (non-owning pointer)
#   [InternalSize] when neither flag is set             (inline buffer)

import gdb
import gdb.printing

MAX_PREVIEW_BYTES = 256

_cSizeFlags    = 0x3FFFFFFF
_cDynAllocFlag = 0x80000000
_cStrPtrFlag   = 0x40000000


def _safe_int(value, default=0):
    try:
        return int(value)
    except Exception:
        return default


def _escape_bytes(data):
    """Decode bytes as UTF-8; fall back to hex-escaping for non-UTF-8 bytes.
    Returns a str that may contain raw control characters (newlines etc.)."""
    try:
        return data.decode("utf-8")
    except Exception:
        return "".join(
            chr(b) if 32 <= b <= 126 and b not in (34, 92)
            else "\\x{:02x}".format(b)
            for b in data
        )


def _escape_for_display(text):
    """Escape control characters so the string can be shown inside double-quotes.
    e.g. newline -> \\n, tab -> \\t, backslash -> \\\\, quote -> \\\"."""
    result = []
    for ch in text:
        o = ord(ch)
        if   ch == '\n': result.append('\\n')
        elif ch == '\r': result.append('\\r')
        elif ch == '\t': result.append('\\t')
        elif ch == '\\': result.append('\\\\')
        elif ch == '"':  result.append('\\"')
        elif o < 32 or o == 127:
            result.append('\\x{:02x}'.format(o))
        else:
            result.append(ch)
    return ''.join(result)


def _symbol_for_address(addr):
    """Return ' <sym_name>' for the given address, or '' if no symbol is found."""
    try:
        info = gdb.execute(
            "info symbol 0x{:x}".format(addr), to_string=True
        ).strip()
        # Output is e.g. "__bfStrObj96 in section .data of /path/to/exe"
        # or "No symbol matches 0x4b49a0."
        if not info.startswith("No symbol"):
            sym_name = info.split(" ")[0]
            return " <{}>".format(sym_name)
    except Exception:
        pass
    return ""


class BeefStringPrinter:
    def __init__(self, val):
        # Record whether the original value was a pointer so that to_string()
        # can prefix the address and symbol name in that case.
        self.is_ptr = (val.type.code == gdb.TYPE_CODE_PTR)
        if self.is_ptr:
            self.ptr_addr = _safe_int(val, 0)
            if self.ptr_addr != 0:
                val = val.dereference()
        else:
            self.ptr_addr = 0
        self.val = val

    def _decode(self):
        """Returns (length, has_str_ptr, has_dyn_alloc, alloc_size)."""
        length      = _safe_int(self.val["mLength"], -1)
        alloc_flags = _safe_int(self.val["mAllocSizeAndFlags"], 0)
        has_str_ptr = bool(alloc_flags & _cStrPtrFlag)
        has_dyn     = bool(alloc_flags & _cDynAllocFlag)
        alloc_size  = alloc_flags & _cSizeFlags
        return length, has_str_ptr, has_dyn, alloc_size

    def _read_content(self):
        """Read and return the string content as a Python str, or raise."""
        length, has_str_ptr, has_dyn, alloc_size = self._decode()

        if length < 0:
            raise ValueError("invalid length={}".format(length))
        if length == 0:
            return ""

        preview_len = min(length, MAX_PREVIEW_BYTES)
        inferior = gdb.selected_inferior()

        if has_str_ptr:
            ptr_addr = _safe_int(self.val["mPtrOrBuffer"], 0)
            if ptr_addr == 0:
                raise ValueError("null mPtrOrBuffer, length={}".format(length))
            mem = inferior.read_memory(ptr_addr, preview_len)
        else:
            # Inline buffer: char8 data lives at the address of mPtrOrBuffer
            # itself and extends past it into appended allocation.
            buf_addr = int(self.val["mPtrOrBuffer"].address)
            mem = inferior.read_memory(buf_addr, preview_len)

        content = _escape_bytes(bytes(mem))
        if length > MAX_PREVIEW_BYTES:
            content += "..."
        return content

    def to_string(self):
        # Null pointer: nothing to dereference.
        if self.is_ptr and self.ptr_addr == 0:
            return "0x0"

        try:
            if (self.val["mLength"].is_optimized_out or
                    self.val["mAllocSizeAndFlags"].is_optimized_out):
                if self.is_ptr:
                    return "0x{:x} <optimized out>".format(self.ptr_addr)
                return "<String optimized out>"
        except Exception:
            pass

        try:
            content = self._read_content()
        except gdb.MemoryError:
            if self.is_ptr:
                return "0x{:x} <unreadable>".format(self.ptr_addr)
            return "<String unreadable memory>"
        except Exception as e:
            if self.is_ptr:
                return "0x{:x} <error: {}>".format(self.ptr_addr, e)
            return "<String error: {}>".format(e)

        if self.is_ptr:
            escaped = _escape_for_display(content)
            return '0x{:x} "{}"'.format(self.ptr_addr, escaped)

        # Direct value: return raw content; display_hint="string" lets the IDE
        # wrap it in quotes and handle any further escaping.
        return content

    def display_hint(self):
        # For pointers, to_string() already builds the full formatted line
        # (address + symbol + quoted string), so we must NOT return "string"
        # or the IDE would wrap our entire formatted result in another layer of
        # quotes.  Returning None causes the value to be shown verbatim.
        if self.is_ptr:
            return None
        return "string"

    def children(self):
        if self.is_ptr and self.ptr_addr == 0:
            return

        try:
            length, has_str_ptr, has_dyn, alloc_size = self._decode()
        except Exception:
            return

        try:
            yield ("[Length]", self.val["mLength"])
        except Exception:
            pass

        if has_dyn:
            yield ("[AllocSize]", alloc_size)
        elif has_str_ptr:
            yield ("[RefSize]", alloc_size)
        else:
            yield ("[InternalSize]", alloc_size)


def StringPtrLookup(val):
    type_str = str(val.type.unqualified())
    if type_str == "System::String *" or type_str == "System::String":
        return BeefStringPrinter(val)
    return None


gdb.pretty_printers.append(StringPtrLookup)

print("[String] pretty-printers registered")
