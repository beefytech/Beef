# stringview_printers.py
#
# GDB pretty-printer for:
#
#   struct StringView {
#       const char* mPtr;
#       intptr_t    mLength;
#   };
#
# Features:
# - Exact-length string reading
# - Safe handling of invalid memory
# - UTF-8 decoding with fallback escaping
# - Embedded NUL support
# - Truncation for large strings
# - display_hint = "string"
# - Expandable children
# - Optimized-out checks
#
# Usage:
#
#   (gdb) source stringview_printers.py
#   (gdb) enable pretty-printer global my_stringview_printers
#
# Or auto-load from .gdbinit:
#
#   python
#   import sys
#   sys.path.insert(0, "/path/to/printers")
#   import stringview_printers
#   end
#

import codecs
import gdb
import gdb.printing

MAX_PREVIEW_BYTES = 256


def _safe_int(value, default=0):
    try:
        return int(value)
    except Exception:
        return default


def _is_null_ptr(ptr):
    try:
        return int(ptr) == 0
    except Exception:
        return False


def _escape_bytes(data: bytes) -> str:
    try:
        text = data.decode("utf-8")
        return text;
    except Exception:
        # Fallback: escape raw bytes
        return "".join(
            chr(b) if 32 <= b <= 126 and b not in (34, 92)
            else f"\\x{b:02x}"
            for b in data
        )


class StringViewPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            ptr = self.val["mPtr"]
            length = self.val["mLength"]
        except Exception as e:
            return f"<StringView field error: {e}>"

        # Optimized out
        try:
            if ptr.is_optimized_out or length.is_optimized_out:
                return "<StringView optimized out>"
        except Exception:
            pass

        length = _safe_int(length, -1)

        if length < 0:
            return f"<StringView invalid length={length}>"

        if _is_null_ptr(ptr):
            if length == 0:
                return '""'
            return f"<StringView null ptr length={length}>"

        preview_len = min(length, MAX_PREVIEW_BYTES)

        try:
            inferior = gdb.selected_inferior()

            mem = inferior.read_memory(ptr, preview_len)
            data = bytes(mem)

        except gdb.MemoryError:
            return (
                f"<StringView unreadable memory "
                f"ptr={ptr} length={length}>"
            )

        except Exception as e:
            return f"<StringView read error: {e}>"

        escaped = _escape_bytes(data)

        truncated = length > MAX_PREVIEW_BYTES
        suffix = "..." if truncated else ""

        return escaped
        #return f'"{escaped}{suffix}" (len={length})'

    def display_hint(self):
        return "string"

    def children(self):
        try:
            yield ("mPtr", self.val["mPtr"])
        except Exception:
            pass

        try:
            yield ("mLength", self.val["mLength"])
        except Exception:
            pass


def _get_or_create_beef_collection():
    """Return the existing global 'Beef' printer collection (or create it).
    Returns (collection, needs_registration)."""
    for pp in gdb.pretty_printers:
        if getattr(pp, 'name', None) == 'Beef':
            return pp, False
    return gdb.printing.RegexpCollectionPrettyPrinter("Beef"), True


_beef_pp, _needs_reg = _get_or_create_beef_collection()
_beef_pp.add_printer("System::StringView", "^System::StringView$", StringViewPrinter)
if _needs_reg:
    gdb.printing.register_pretty_printer(None, _beef_pp)

print("[StringView] pretty-printers registered")