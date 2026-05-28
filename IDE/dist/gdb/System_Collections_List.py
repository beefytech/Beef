# System_Collections_List.py
#
# GDB pretty-printer for System.Collections.List<T> (Beef language)
#
# Memory layout (!BF_LARGE_COLLECTIONS, i.e. int32 mode):
#
#   class List<T> {
#       T*           mItems;               // pointer to element array
#       int_cosize   mSize;                // current element count
#       int_cosize   mAllocSizeAndFlags;   // alloc size (lower 31 bits) | DynAllocFlag
#   };
#
#   SizeFlags    = 0x7FFFFFFF  -- mask to extract allocated capacity
#   DynAllocFlag = 0x80000000  -- set when mItems is a heap-allocated buffer
#
# Children:
#   [Ptr]        mItems pointer
#   [Count]      mSize
#   [AllocSize]  mAllocSizeAndFlags & SizeFlags
#   [0]..[N-1]   elements read from mItems

import gdb
import gdb.printing

_SizeFlags    = 0x7FFFFFFF
_DynAllocFlag = 0x80000000


def _safe_int(value, default=0):
    try:
        return int(value)
    except Exception:
        return default


def _symbol_for_address(addr):
    """Return ' <sym_name>' for the given address, or '' if no symbol is found."""
    try:
        info = gdb.execute(
            "info symbol 0x{:x}".format(addr), to_string=True
        ).strip()
        # Output is e.g. "__bfListObj12 in section .data of /path/to/exe"
        # or "No symbol matches 0x..."
        if not info.startswith("No symbol"):
            sym_name = info.split(" ")[0]
            return " <{}>".format(sym_name)
    except Exception:
        pass
    return ""


class BeefListPrinter:
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

    def to_string(self):
        if self.is_ptr and self.ptr_addr == 0:
            return "0x0"

        try:
            size = _safe_int(self.val["mSize"], 0)
        except gdb.MemoryError:
            if self.is_ptr:
                return "0x{:x} <unreadable>".format(self.ptr_addr)
            return "<List unreadable memory>"
        except Exception as e:
            if self.is_ptr:
                return "0x{:x} <error: {}>".format(self.ptr_addr, e)
            return "<List error: {}>".format(e)

        if self.is_ptr:
            sym = _symbol_for_address(self.ptr_addr)
            return "0x{:x}{} size={}".format(self.ptr_addr, sym, size)

        return "size={}".format(size)

    def display_hint(self):
        return "array"

    def children(self):
        if self.is_ptr and self.ptr_addr == 0:
            return

        try:
            size        = _safe_int(self.val["mSize"], 0)
            alloc_flags = _safe_int(self.val["mAllocSizeAndFlags"], 0)
            alloc_size  = alloc_flags & _SizeFlags
        except Exception:
            return

        try:
            yield ("[Ptr]", self.val["mItems"])
        except Exception:
            pass

        try:
            yield ("[Count]", self.val["mSize"])
        except Exception:
            pass

        yield ("[AllocSize]", alloc_size)

        if size <= 0:
            return

        try:
            items_ptr = self.val["mItems"]
            if _safe_int(items_ptr, 0) == 0:
                return
            for i in range(size):
                yield ("[{}]".format(i), items_ptr[i])
        except Exception as e:
            yield ("<error>", str(e))


def ListPtrLookup(val):
    type_str = str(val.type.unqualified())
    # Match both the direct type and pointer-to-type for any T.
    # Type names look like "System::Collections::List<int32>"
    # or "System::Collections::List<int32> *"
    if (type_str.startswith("System::Collections::List<") and
            (type_str.endswith(">") or type_str.endswith("> *"))):
        return BeefListPrinter(val)
    return None


gdb.pretty_printers.append(ListPtrLookup)

print("[List] pretty-printers registered")
