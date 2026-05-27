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
    try:
        return data.decode("utf-8")
    except Exception:
        return "".join(
            chr(b) if 32 <= b <= 126 and b not in (34, 92)
            else "\\x{:02x}".format(b)
            for b in data
        )


class BeefStringPrinter:
    def __init__(self, val):
        self.val = val

    def _decode(self):
        """Returns (length, has_str_ptr, has_dyn_alloc, alloc_size)."""
        length      = _safe_int(self.val["mLength"], -1)
        alloc_flags = _safe_int(self.val["mAllocSizeAndFlags"], 0)
        has_str_ptr = bool(alloc_flags & _cStrPtrFlag)
        has_dyn     = bool(alloc_flags & _cDynAllocFlag)
        alloc_size  = alloc_flags & _cSizeFlags
        return length, has_str_ptr, has_dyn, alloc_size

    def to_string(self):
        print("String to_string called")

        try:
            length, has_str_ptr, has_dyn, alloc_size = self._decode()
        except Exception as e:
            return "<String field error: {}>".format(e)

        try:
            if (self.val["mLength"].is_optimized_out or
                    self.val["mAllocSizeAndFlags"].is_optimized_out):
                return "<String optimized out>"
        except Exception:
            pass

        if length < 0:
            return "<String invalid length={}>".format(length)

        if length == 0:
            return ""

        preview_len = min(length, MAX_PREVIEW_BYTES)

        try:
            inferior = gdb.selected_inferior()

            if has_str_ptr:
                # mPtrOrBuffer holds a real pointer to the character data
                ptr_addr = _safe_int(self.val["mPtrOrBuffer"], 0)
                if ptr_addr == 0:
                    return "<String null ptr length={}>".format(length)
                mem = inferior.read_memory(ptr_addr, preview_len)
            else:
                # Inline buffer: char8 data lives at the address of mPtrOrBuffer
                # itself and extends past it into appended allocation.
                buf_addr = int(self.val["mPtrOrBuffer"].address)
                mem = inferior.read_memory(buf_addr, preview_len)

        except gdb.MemoryError:
            return "<String unreadable memory length={}>".format(length)
        except Exception as e:
            return "<String read error: {}>".format(e)

        escaped = _escape_bytes(bytes(mem))
        if length > MAX_PREVIEW_BYTES:
            escaped += "..."
        return escaped

    def display_hint(self):
        return "string"

    def children(self):
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


def _get_or_create_beef_collection():
    """Return the existing global 'Beef' printer collection (or create it).
    Returns (collection, needs_registration)."""
    for pp in gdb.pretty_printers:
        if getattr(pp, 'name', None) == 'Beef':
            return pp, False
    return gdb.printing.RegexpCollectionPrettyPrinter("Beef"), True


_beef_pp, _needs_reg = _get_or_create_beef_collection()
_beef_pp.add_printer("System::String", "^System::String$", BeefStringPrinter)
if _needs_reg:
    gdb.printing.register_pretty_printer(None, _beef_pp)

print("[String] pretty-printers registered")
