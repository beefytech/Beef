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


class BeefListPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            size = _safe_int(self.val["mSize"], 0)
            return "size={}".format(size)
        except Exception as e:
            return "<List error: {}>".format(e)

    def display_hint(self):
        return "array"

    def children(self):
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


def _get_or_create_beef_collection():
    """Return the existing global 'Beef' printer collection (or create it).
    Returns (collection, needs_registration)."""
    for pp in gdb.pretty_printers:
        if getattr(pp, 'name', None) == 'Beef':
            return pp, False
    return gdb.printing.RegexpCollectionPrettyPrinter("Beef"), True


_beef_pp, _needs_reg = _get_or_create_beef_collection()
_beef_pp.add_printer(
    "System::Collections::List",
    "^System::Collections::List<.*>$",
    BeefListPrinter,
)
if _needs_reg:
    gdb.printing.register_pretty_printer(None, _beef_pp)

print("[List] pretty-printers registered")
