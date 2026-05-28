# beef_dbgvis.py
#
# GDB pretty-printers driven by the Beef debug visualizer dump (dbgvis_dump.txt).
# The dump file must live in the same directory as this script.
# Loaded automatically by GDBDebugger.cpp at startup.

import gdb
import os
import re
import traceback as _traceback

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_DUMP_PATH  = os.path.join(_SCRIPT_DIR, 'dbgvis_dump.txt')

_MAX_ITEMS  = 200   # maximum collection items to expand

# ─────────────────────────────────────────────────────────────────────────────
# 1.  Dump-file data model + parser
# ─────────────────────────────────────────────────────────────────────────────

class _DvEntry:
    __slots__ = (
        'name', 'flavor', 'collection_type', 'condition',
        'display_strings', 'string_views', 'action',
        'size', 'lower_dim_sizes',
        'head_pointer', 'end_pointer', 'next_pointer',
        'left_pointer', 'right_pointer',
        'value_type', 'value_pointer', 'target_pointer',
        'buckets', 'entries', 'key',
        'dyn_value_type', 'dyn_value_type_id_append',
        'show_element_addrs', 'items',
    )
    def __init__(self, name):
        self.name = name
        self.flavor = None          # None | 'GNU' | 'MS'
        self.collection_type = None
        self.condition = None
        self.display_strings = []   # [(cond_str, template_str), ...]
        self.string_views    = []   # [(cond_str, template_str), ...]
        self.action = None
        self.size = None
        self.lower_dim_sizes = []
        self.head_pointer    = None
        self.end_pointer     = None
        self.next_pointer    = None
        self.left_pointer    = None
        self.right_pointer   = None
        self.value_type      = None
        self.value_pointer   = None
        self.target_pointer  = None
        self.buckets         = None
        self.entries         = None
        self.key             = None
        self.dyn_value_type         = None
        self.dyn_value_type_id_append = None
        self.show_element_addrs = False
        self.items = []             # [(label, value_expr, cond_expr), ...]


def _parse_dump_text(text):
    """Parse dump text (as produced by DebugVisualizers::Dump()) into a list of _DvEntry."""
    result = []
    cur = None
    for raw in text.splitlines():
        p = raw.split('\t')
        k = p[0]
        def g(n, d=''):
            return p[n] if len(p) > n else d
        if k == 'Entry':
            cur = _DvEntry(g(1))
            result.append(cur)
        elif cur is None:
            pass
        elif k == 'Flavor':               cur.flavor = g(1) or None
        elif k == 'CollectionType':       cur.collection_type = g(1) or None
        elif k == 'DisplayString':        cur.display_strings.append((g(1), g(2)))
        elif k == 'StringView':           cur.string_views.append((g(1), g(2)))
        elif k == 'Action':               cur.action = g(1)
        elif k == 'Condition':            cur.condition = g(1)
        elif k == 'Size':                 cur.size = g(1)
        elif k == 'LowerDimSize':         cur.lower_dim_sizes.append(g(1))
        elif k == 'HeadPointer':          cur.head_pointer = g(1)
        elif k == 'EndPointer':           cur.end_pointer = g(1)
        elif k == 'NextPointer':          cur.next_pointer = g(1)
        elif k == 'LeftPointer':          cur.left_pointer = g(1)
        elif k == 'RightPointer':         cur.right_pointer = g(1)
        elif k == 'ValueType':            cur.value_type = g(1)
        elif k == 'ValuePointer':         cur.value_pointer = g(1)
        elif k == 'TargetPointer':        cur.target_pointer = g(1)
        elif k == 'Buckets':              cur.buckets = g(1)
        elif k == 'Entries':              cur.entries = g(1)
        elif k == 'Key':                  cur.key = g(1)
        elif k == 'DynValueType':         cur.dyn_value_type = g(1)
        elif k == 'DynValueTypeIdAppend': cur.dyn_value_type_id_append = g(1)
        elif k == 'ShowElementAddrs':     cur.show_element_addrs = (g(1) == '1')
        elif k == 'Item':                 cur.items.append((g(1), g(2), g(3)))
    return result


def _load_dump(path):
    """Load dump entries, preferring the injected _beef_dbgvis_data global over the file."""
    # When beef_dbgvis.py is sourced after GDBDebugger.cpp injects the dump,
    # _beef_dbgvis_data will already be a global in __main__ (same namespace).
    injected = globals().get('_beef_dbgvis_data')
    if injected is not None:
        try:
            result = _parse_dump_text(injected)
            print("[beef_dbgvis] Loaded {} entries from injected data".format(len(result)))
            return result
        except Exception as ex:
            print("[beef_dbgvis] Could not parse injected dump data: {}".format(ex))
    # Fall back to reading the dump file (useful during development / testing)
    try:
        with open(path, 'r', encoding='utf-8') as fh:
            result = _parse_dump_text(fh.read())
            print("[beef_dbgvis] Loaded {} entries from '{}'".format(len(result), path))
            return result
    except Exception as ex:
        print("[beef_dbgvis] Could not load '{}': {}".format(path, ex))
    return []


_ALL_ENTRIES = _load_dump(_DUMP_PATH)


# ─────────────────────────────────────────────────────────────────────────────
# 2.  Wildcard matching  (mirrors C++ FindEntryForType)
# ─────────────────────────────────────────────────────────────────────────────

def _dots_to_colons(name):
    """Convert Beef '.' namespace separators to '::' (not inside <...>)."""
    out = []
    depth = 0
    for c in name:
        if   c == '<': depth += 1; out.append(c)
        elif c == '>': depth -= 1; out.append(c)
        elif c == '.' and depth == 0: out.append('::')
        else: out.append(c)
    return ''.join(out)


def _match_entry(entry_name, type_name):
    """
    Match type_name against entry_name (which may contain '*' wildcards).
    Returns list of wildcard captures on match, or None on mismatch.
    Faithfully mirrors C++ FindEntryForType logic.
    """
    ename = _dots_to_colons(entry_name)
    tname = type_name
    ei = ti = 0
    is_template = False
    captures = []

    while True:
        while ei < len(ename) and ename[ei] == ' ': ei += 1
        while ti < len(tname) and tname[ti] == ' ': ti += 1

        ec = ename[ei] if ei < len(ename) else '\0'
        tc = tname[ti] if ti < len(tname) else '\0'

        if tc == '\0':
            if ec == '\0': return captures   # full match
            break

        if ec == '\0': break

        if ec == '<': is_template = True

        if ec == '*':
            cap = []
            open_depth = 0
            while ti < len(tname):
                c = tname[ti]
                is_sep = (c == ',')
                if not is_template and c in '[?': is_sep = True
                if c in '<(':   open_depth += 1
                elif c in '>)':
                    open_depth -= 1
                    if open_depth < 0: ti -= 1; break
                elif is_sep and open_depth == 0: ti -= 1; break
                cap.append(c)
                ti += 1
            captures.append(''.join(cap).strip())
            # both exhausted after wildcard?
            if ti >= len(tname) and (ei + 1) >= len(ename): return captures
            if ti >= len(tname): break
            ei += 1; ti += 1      # advance past '*' and the separator
            continue

        elif ec != tc: break

        ei += 1; ti += 1

    return None


# ─────────────────────────────────────────────────────────────────────────────
# 3.  Flavor detection + entry lookup
# ─────────────────────────────────────────────────────────────────────────────

def _is_windows_target():
    try:
        info = gdb.execute('info target', to_string=True).lower()
        if '.exe' in info or 'pe ' in info or 'coff' in info: return True
    except Exception: pass
    try:
        osabi = gdb.execute('show osabi', to_string=True).lower()
        if 'windows' in osabi or 'cygwin' in osabi: return True
    except Exception: pass
    return False


def _find_entry(type_name):
    """Return (DvEntry, captures) for the best matching entry, or (None, [])."""
    want = 'MS' if _is_windows_target() else 'GNU'
    best = best_caps = None
    for e in _ALL_ENTRIES:
        if e.flavor is not None and e.flavor != want:
            continue
        caps = _match_entry(e.name, type_name)
        if caps is None: continue
        if best is None:
            best = e; best_caps = caps
        elif e.flavor == want and best.flavor is None:
            best = e; best_caps = caps
            break
    return best, (best_caps or [])


# ─────────────────────────────────────────────────────────────────────────────
# 4.  Expression evaluation
# ─────────────────────────────────────────────────────────────────────────────

_SPECIAL_FUNCS = frozenset([
    '__clearHighBits', '__getHighBits', '__bitcast', '__cast',
    '__funcName', '__funcTarget', '__stringView',
    '__hasField', '__demangleFakeMember',
])


def _get_field_names(gdb_type):
    """Return the set of direct field names on a GDB struct/class type."""
    names = set()
    try:
        t = gdb_type.strip_typedefs()
        if t.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
            for f in t.fields():
                if f.name: names.add(f.name)
    except Exception: pass
    return names


def _preprocess_specials(expr):
    """
    Expand Beef display-string special functions into GDB-parseable C expressions.
    Assumes 32-bit field widths for clearHighBits/getHighBits (covers most cases).
    """
    # __clearHighBits(val, n)  →  (val & ~(~0u << (32-n)))
    def _chb(m):
        v, n = m.group(1).strip(), m.group(2).strip()
        return '({} & ~((~0u) << (32 - ({}))))'.format(v, n)

    # __getHighBits(val, n)  →  ((unsigned)(val) >> (32-n))
    def _ghb(m):
        v, n = m.group(1).strip(), m.group(2).strip()
        return '((unsigned int)({}) >> (32 - ({})))'.format(v, n)

    expr = re.sub(r'__clearHighBits\s*\(([^,)]+),([^)]+)\)', _chb, expr)
    expr = re.sub(r'__getHighBits\s*\(([^,)]+),([^)]+)\)', _ghb, expr)

    # __bitcast("TypeName", value)
    def _bcast_str(m):
        tn = m.group(1).replace('.', '::')
        v  = m.group(2).strip()
        return '*(({}*)&({}))'.format(tn, v)
    expr = re.sub(r'__bitcast\s*\(\s*"([^"]+)"\s*,\s*([^)]+)\)', _bcast_str, expr)

    # __cast("TypeName", "*", value)  →  (TypeName*)value
    def _cast_ptr(m):
        tn = m.group(1).replace('.', '::')
        v  = m.group(2).strip()
        return '(({} *)({}))'.format(tn, v)
    expr = re.sub(r'__cast\s*\(\s*"([^"]+)"\s*,\s*"\*"\s*,\s*([^)]+)\)', _cast_ptr, expr)

    # __cast("TypeName", value)  →  ((TypeName)value)
    def _cast_str(m):
        tn = m.group(1).replace('.', '::')
        v  = m.group(2).strip()
        return '(({})({}))'.format(tn, v)
    expr = re.sub(r'__cast\s*\(\s*"([^"]+)"\s*,\s*([^)]+)\)', _cast_str, expr)

    # Strip remaining unhandled special-function calls gracefully
    for fn in _SPECIAL_FUNCS:
        if fn in expr:
            expr = re.sub(r'\b' + re.escape(fn) + r'\s*\([^)]*\)', '0', expr)


    return expr


_SPEC_RE = re.compile(
    r'^(?:s8|x|X|u|d|b|na|ne|nv|count=.+|arraysize=.+)$',
    re.IGNORECASE,
)


def _parse_format_specs(expr):
    """
    Peel all trailing Beef/GDB format specifiers from *expr* (at depth 0).
    Returns (bare_expr, specs) where *specs* is a list of spec strings in
    left-to-right order, e.g. ('mPtr', ['s8', 'count=mLength']).
    Handles chained specs such as 'ptr,s8,count=mLength'.
    """
    specs = []
    while True:
        # Find the rightmost comma that is at brace/paren/bracket depth 0
        depth = 0
        last_comma = -1
        for i, c in enumerate(expr):
            if c in '(<[':  depth += 1
            elif c in ')>]': depth -= 1
            elif c == ',' and depth == 0:
                last_comma = i
        if last_comma == -1:
            break
        suffix = expr[last_comma + 1:].strip()
        if _SPEC_RE.match(suffix):
            specs.insert(0, suffix)
            expr = expr[:last_comma].strip()
        else:
            break
    return expr, specs


def _strip_format_spec(expr):
    """Strip all trailing format specifiers; return bare expression."""
    bare, _ = _parse_format_specs(expr)
    return bare


def _transform_members(expr, field_names, ptr_var):
    """
    Rewrite bare field-name identifiers in `expr` to `ptr_var->fieldname`
    so that GDB can evaluate them.  Only identifiers that appear in
    field_names and are NOT preceded by '.' or '->' get the prefix.
    """

    # Handle &this / this first
    expr = re.sub(r'&\s*\bthis\b', '$__bv_addr', expr)
    expr = re.sub(r'\bthis\b', '(*$__bv)', expr)

    if not field_names:
        return expr

    tokens = []
    i = 0
    n = len(expr)
    prev_member_op = False

    while i < n:
        c = expr[i]
        if c == '-' and i+1 < n and expr[i+1] == '>':
            tokens.append('->')
            i += 2
            prev_member_op = True
            continue
        if c == '.':
            tokens.append('.')
            i += 1
            prev_member_op = True
            continue
        # Identifier
        if c.isalpha() or c == '_':
            j = i
            while j < n and (expr[j].isalnum() or expr[j] == '_'): j += 1
            ident = expr[i:j]
            # Look ahead past spaces for '(' (function call)
            k = j
            while k < n and expr[k] == ' ': k += 1
            is_call = k < n and expr[k] == '('
            if (not prev_member_op and not is_call
                    and ident in field_names
                    and ident not in _SPECIAL_FUNCS):
                tokens.append(ptr_var + '->' + ident)
            else:
                tokens.append(ident)
            i = j
            prev_member_op = False
            continue
        # All other chars
        if c not in ' \t':
            prev_member_op = False
        tokens.append(c)
        i += 1

    return ''.join(tokens)


def _eval_expr(expr_raw, ctx_val, wildcards=None, index=None):
    """
    Evaluate a visualiser expression in the context of ctx_val (a gdb.Value).
    Returns a gdb.Value, or None on failure.
    Wildcards is a list of captured type-name strings ($T1, $T2, …).
    index is the current loop index for $i substitutions.
    """
    if wildcards is None: wildcards = []
    expr = expr_raw.strip()
    if not expr: return None

    # Strip Beef/GDB format specifiers
    core = _strip_format_spec(expr)

    # $Tn substitutions
    for idx, cap in enumerate(wildcards):
        core = core.replace('$T{}'.format(idx + 1), cap)

    # $i substitution
    if index is not None:
        core = core.replace('$i', str(index))

    # Expand special functions
    core = _preprocess_specials(core)

    # Normalise Beef '.' type separators (e.g. in cast type names) → '::'
    # Only safe for known patterns like "__cast" results; done in _preprocess_specials

    # Try simple field access: bare identifier
    simple = core.strip()
    if re.match(r'^[A-Za-z_]\w*$', simple):
        try: return ctx_val[simple]
        except Exception: pass

    # Set GDB convenience variables for the context value
    try:
        addr = int(ctx_val.address)
        type_str = str(ctx_val.type.unqualified())
        gdb.execute("set $__bv = ({}*){:#x}".format(type_str, addr), to_string=True)
        gdb.execute("set $__bv_addr = (void*){:#x}".format(addr), to_string=True)
    except Exception:
        try:
            addr = int(ctx_val.address)
            gdb.execute("set $__bv = (void*){:#x}".format(addr), to_string=True)
            gdb.execute("set $__bv_addr = (void*){:#x}".format(addr), to_string=True)
        except Exception:
            pass

    field_names = _get_field_names(ctx_val.type)
    transformed = _transform_members(core, field_names, '$__bv')

    try:
        return gdb.parse_and_eval(transformed)
    except Exception:
        pass
    return None


def _quote_string(text):
    """
    Wrap *text* in double-quotes and escape special characters so the result
    looks like a C/Beef string literal, e.g. "hello\nworld".
    """
    out = ['"']
    for ch in text:
        o = ord(ch)
        if   ch == '"':  out.append('\\"')
        elif ch == '\\': out.append('\\\\')
        elif ch == '\n': out.append('\\n')
        elif ch == '\r': out.append('\\r')
        elif ch == '\t': out.append('\\t')
        elif o < 32 or o == 127:
            out.append('\\x{:02x}'.format(o))
        else:
            out.append(ch)
    out.append('"')
    return ''.join(out)


def _eval_bool(expr_raw, ctx_val, wildcards=None, index=None):
    """Evaluate a condition expression; returns True, False, or None on error."""
    if not expr_raw: return True
    v = _eval_expr(expr_raw, ctx_val, wildcards, index)
    if v is None: return None
    try: return bool(v)
    except Exception: return None


def _gdb_val_to_str(gv):
    """Convert a gdb.Value to a human-readable string."""
    try:
        t = gv.type.strip_typedefs()
        # char* / char8* → read as C string
        if t.code == gdb.TYPE_CODE_PTR:
            inner = t.target().strip_typedefs()
            if inner.code == gdb.TYPE_CODE_INT and inner.sizeof == 1:
                addr = int(gv)
                if addr == 0: return 'null'
                try:
                    raw = gdb.selected_inferior().read_memory(addr, 512)
                    b = bytes(raw)
                    end = b.find(b'\0')
                    return b[:end].decode('utf-8', errors='replace') if end >= 0 else b.decode('utf-8', errors='replace')
                except Exception: pass
        return str(gv)
    except Exception:
        return '<?>'


def _apply_format_specs(gv, specs, ctx_val, wildcards, index):
    """
    Format *gv* (a gdb.Value) according to a list of Beef format specifiers.
    *ctx_val* / *wildcards* / *index* are needed to evaluate count=/arraySize=
    expressions that reference fields of the surrounding value.
    Returns a plain Python string.
    """
    if not specs:
        return _gdb_val_to_str(gv)

    is_s8      = any(s.lower() == 's8' for s in specs)
    hex_upper  = 'X' in specs
    hex_lower  = 'x' in specs

    # Resolve count= and arraySize= expressions against ctx_val
    count_val      = None
    array_size_val = None
    for s in specs:
        sl = s.lower()
        if sl.startswith('count='):
            cv = _eval_expr(s[len('count='):], ctx_val, wildcards, index)
            if cv is not None:
                try: count_val = int(cv)
                except Exception: pass
        elif sl.startswith('arraysize='):
            av = _eval_expr(s[len('arraysize='):], ctx_val, wildcards, index)
            print("Evaluated arraySize= expression '{}' to {}".format(s[len('arraysize='):], av))
            if av is not None:
                try: array_size_val = int(av)
                except Exception: pass

    try:
        t = gv.type.strip_typedefs()

        # ── Pointer + s8 → quoted UTF-8 string, optionally length-limited by count= ──
        if is_s8 and t.code == gdb.TYPE_CODE_PTR:
            addr = int(gv)
            if addr == 0:
                return '""'
            try:
                inf = gdb.selected_inferior()
                if count_val is not None and count_val >= 0:
                    raw = bytes(inf.read_memory(addr, min(count_val, 4096)))
                else:
                    # Null-terminated; read up to 512 bytes
                    raw = bytes(inf.read_memory(addr, 512))
                    nul = raw.find(b'\0')
                    if nul >= 0: raw = raw[:nul]
                return _quote_string(raw.decode('utf-8', errors='replace'))
            except Exception:
                return '<unreadable>'

        # ── Integer/char + s8 → display as a single character ──
        if is_s8:
            try:
                ch = int(gv) & 0xFF
                if 32 <= ch < 127 and ch not in (34, 92):
                    return chr(ch)
                return '\\x{:02x}'.format(ch)
            except Exception:
                pass

        # ── Hex formatting ──
        if hex_upper or hex_lower:
            try:
                n   = int(gv)
                bits = gv.type.strip_typedefs().sizeof * 8
                mask = (1 << bits) - 1
                n   &= mask
                return ('0x{:X}' if hex_upper else '0x{:x}').format(n)
            except Exception:
                pass

        # ── arraySize= on a pointer → show element count ──
        if array_size_val is not None:
            return '{} item{}'.format(array_size_val,
                                      '' if array_size_val == 1 else 's')

    except Exception:
        pass

    return _gdb_val_to_str(gv)


# ─────────────────────────────────────────────────────────────────────────────
# 5.  Display-string formatter
# ─────────────────────────────────────────────────────────────────────────────

def _try_eval_stringview(expr, ctx_val, wildcards, index):
    """
    If *expr* is a bare ``__stringView(ptr, length)`` call, evaluate both
    arguments, read *length* bytes from *ptr*, and return a quoted UTF-8
    string.  Returns None if the expression is not a __stringView call so
    the caller can fall back to normal GDB evaluation.

    The optional trailing ',s8' format specifier that may follow the call in
    the source template is already stripped by _parse_format_specs before we
    are called, so we do not need to handle it here.
    """
    s = expr.strip()
    if not s.startswith('__stringView'):
        return None
    rest = s[len('__stringView'):].lstrip()
    if not rest.startswith('('):
        return None

    # Find the closing ')' that matches the opening '('
    depth = 0
    end = -1
    for i, c in enumerate(rest):
        if   c == '(': depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0: end = i; break
    if end == -1:
        return None
    # Nothing significant should follow the closing ')'
    if rest[end + 1:].strip():
        return None

    args_str = rest[1:end]

    # Split at the first depth-0 comma to get ptr and length arguments
    depth = 0
    split = -1
    for i, c in enumerate(args_str):
        if c in '(<[':  depth += 1
        elif c in ')>]': depth -= 1
        elif c == ',' and depth == 0: split = i; break
    if split == -1:
        return None     # need exactly two arguments

    ptr_expr = args_str[:split].strip()
    len_expr = args_str[split + 1:].strip()

    ptr_v = _eval_expr(ptr_expr, ctx_val, wildcards, index)
    len_v = _eval_expr(len_expr, ctx_val, wildcards, index)
    if ptr_v is None or len_v is None:
        return '<??>'

    try:
        addr   = int(ptr_v)
        length = int(len_v)
        if addr == 0 or length <= 0:
            return '""'
        raw = bytes(gdb.selected_inferior().read_memory(addr, min(length, 4096)))
        return _quote_string(raw.decode('utf-8', errors='replace'))
    except Exception:
        return '<??>'


def _expand_template(tmpl, ctx_val, wildcards, index=None):
    """
    Expand a display-string template:
      {{        →  {
      }}        →  }
      {expr}    →  evaluate expr, convert to string
      {expr,x}  →  evaluate expr, format as lowercase hex
      {expr,X}  →  evaluate expr, format as uppercase hex
      {ptr,s8,count=N}  →  read N bytes from ptr as UTF-8 string
      {ptr,arraySize=N} →  show as "N items"
    """
    out = []
    i = 0
    n = len(tmpl)
    while i < n:
        c = tmpl[i]
        if c == '{':
            if i+1 < n and tmpl[i+1] == '{':
                out.append('{'); i += 2
            else:
                # find matching '}'
                j = i + 1; depth = 0
                while j < n:
                    if   tmpl[j] == '{': depth += 1
                    elif tmpl[j] == '}':
                        if depth == 0: break
                        depth -= 1
                    j += 1
                raw_expr = tmpl[i+1:j]
                # Strip format specifiers (e.g. ',s8') before evaluating.
                bare_expr, fmt_specs = _parse_format_specs(raw_expr)
                # __stringView(ptr, length) is a virtual display function that
                # reads memory directly; intercept it before GDB sees the expr.
                sv = _try_eval_stringview(bare_expr, ctx_val, wildcards, index)
                if sv is not None:
                    out.append(sv)
                else:
                    v = _eval_expr(bare_expr, ctx_val, wildcards, index)
                    if v is not None:
                        out.append(_apply_format_specs(v, fmt_specs, ctx_val, wildcards, index))
                    else:
                        out.append('<??>')
                i = j + 1
        elif c == '}' and i+1 < n and tmpl[i+1] == '}':
            out.append('}'); i += 2
        else:
            out.append(c); i += 1
    return ''.join(out)


def _pick_display_string(entry, ctx_val, wildcards):
    """Return the first matching display string, or None."""
    for cond, tmpl in entry.display_strings:
        if not cond or _eval_bool(cond, ctx_val, wildcards) is True:
            return _expand_template(tmpl, ctx_val, wildcards)
    return None


# ─────────────────────────────────────────────────────────────────────────────
# 6.  Collection-type children generators
# ─────────────────────────────────────────────────────────────────────────────

def _deref_ptr(gv):
    """Dereference a gdb.Value pointer; return None if null or error."""
    try:
        addr = int(gv)
        if addr == 0: return None
        return gv.dereference()
    except Exception:
        return None


def _eval_in(expr, node_val, wildcards, index=None):
    """Evaluate expr in the context of node_val (a dereferenced node)."""
    expr = expr.strip()
    if expr == 'this':   return node_val
    if expr == '&this':
        try: return node_val.address
        except Exception: return None
    return _eval_expr(expr, node_val, wildcards, index)


def _gen_array_items(entry, val, wildcards):
    """ArrayItems / multi-dim ArrayItems."""
    try:
        size = min(int(_eval_expr(entry.size, val, wildcards) or 0), _MAX_ITEMS)
    except Exception: return
    if size <= 0: return

    base = _eval_expr(entry.value_pointer, val, wildcards)
    if base is None: return

    dim_sizes = []
    for ds in entry.lower_dim_sizes:
        try: dim_sizes.append(max(1, int(_eval_expr(ds, val, wildcards) or 1)))
        except Exception: dim_sizes.append(1)

    for i in range(size):
        try:
            item = base[i]
        except Exception: break

        if dim_sizes:
            idxs, rem = [], i
            for d in reversed(dim_sizes): idxs.insert(0, rem % d); rem //= d
            idxs.insert(0, rem)
            label = '[{}]'.format(', '.join(map(str, idxs)))
        else:
            label = '[{}]'.format(i)
        yield (label, item)


def _gen_index_list_items(entry, val, wildcards):
    """IndexListItems: evaluate ValuePointer with $i substituted."""
    try:
        size = min(int(_eval_expr(entry.size, val, wildcards) or 0), _MAX_ITEMS)
    except Exception: return
    for i in range(size):
        v = _eval_expr(entry.value_pointer, val, wildcards, index=i)
        if v is not None:
            yield ('[{}]'.format(i), v)


def _gen_linked_list_items(entry, val, wildcards):
    """LinkedListItems: follow NextPointer from HeadPointer to EndPointer."""
    # HeadPointer is evaluated in context of the collection (val)
    hp = (entry.head_pointer or '').strip()
    if hp == '&this':
        node_ptr = val.address
    else:
        node_ptr = _eval_expr(hp, val, wildcards) if hp else None
    if node_ptr is None: return

    end_addr = None
    if entry.end_pointer:
        ev = _eval_expr(entry.end_pointer, val, wildcards)
        if ev is not None:
            try: end_addr = int(ev)
            except Exception: pass

    try:
        size_limit = min(int(_eval_expr(entry.size, val, wildcards) or _MAX_ITEMS), _MAX_ITEMS) if entry.size else _MAX_ITEMS
    except Exception:
        size_limit = _MAX_ITEMS

    seen = set()
    i = 0
    while i < size_limit:
        try: node_addr = int(node_ptr)
        except Exception: break
        if node_addr == 0: break
        if end_addr is not None and node_addr == end_addr: break
        if node_addr in seen: break
        seen.add(node_addr)

        # Dereference node pointer to access its fields
        node = _deref_ptr(node_ptr)
        if node is None: break

        # Evaluate ValuePointer in node context
        vp = (entry.value_pointer or '').strip()
        if vp == 'this':
            item_val = node
        elif vp:
            item_val = _eval_in(vp, node, wildcards)
        else:
            item_val = node

        if entry.show_element_addrs:
            label = '[{}] 0x{:x}'.format(i, node_addr)
        else:
            label = '[{}]'.format(i)

        if item_val is not None:
            # If not showing addrs, check if item is a valid pointer before yielding
            if not entry.show_element_addrs:
                try:
                    t = item_val.type.strip_typedefs()
                    if t.code == gdb.TYPE_CODE_PTR and int(item_val) == 0:
                        pass  # null pointer — still yield, let printer handle it
                except Exception: pass
            yield (label, item_val)

        # Advance to next node
        np = (entry.next_pointer or '').strip()
        if not np: break
        node_ptr = _eval_in(np, node, wildcards)
        if node_ptr is None: break
        i += 1


def _gen_tree_items(entry, val, wildcards):
    """TreeItems: in-order traversal of a binary tree."""
    try:
        size_limit = min(int(_eval_expr(entry.size, val, wildcards) or _MAX_ITEMS), _MAX_ITEMS)
    except Exception:
        size_limit = _MAX_ITEMS

    head_ptr = _eval_expr(entry.head_pointer, val, wildcards)
    if head_ptr is None: return
    try:
        if int(head_ptr) == 0: return
    except Exception: return

    seen = set()
    counter = [0]

    def _inorder(ptr):
        if counter[0] >= size_limit: return
        try: addr = int(ptr)
        except Exception: return
        if addr == 0 or addr in seen: return
        seen.add(addr)

        node = _deref_ptr(ptr)
        if node is None: return

        if entry.left_pointer:
            lp = _eval_in(entry.left_pointer, node, wildcards)
            if lp is not None: yield from _inorder(lp)

        if counter[0] < size_limit:
            vp = (entry.value_pointer or '').strip()
            if vp == 'this': item = node
            elif vp: item = _eval_in(vp, node, wildcards)
            else: item = node
            if item is not None:
                yield ('[{}]'.format(counter[0]), item)
                counter[0] += 1

        if entry.right_pointer:
            rp = _eval_in(entry.right_pointer, node, wildcards)
            if rp is not None: yield from _inorder(rp)

    yield from _inorder(head_ptr)


def _gen_dictionary_items(entry, val, wildcards):
    """
    DictionaryItems: iterate entries array, skip free slots (mHashCode < 0),
    yield key/value pairs.
    """
    try:
        # Size = actual live entry count; scan entries until we find that many
        size = min(int(_eval_expr(entry.size, val, wildcards) or 0), _MAX_ITEMS)
    except Exception: return
    if size <= 0: return

    entries_ptr = _eval_expr(entry.entries, val, wildcards) if entry.entries else None
    if entries_ptr is None: return

    yielded = 0
    idx = 0
    max_scan = size + size + 64   # entries array may be larger than live count

    while yielded < size and idx < max_scan:
        try:
            e = entries_ptr[idx]
        except Exception: break

        # Skip free entries by checking mHashCode (Beef/C# convention: < 0 = free)
        try:
            hc = int(e['mHashCode'])
            if hc < 0: idx += 1; continue
        except Exception:
            pass  # type has no mHashCode — iterate sequentially

        yield ('[{}]'.format(yielded), e)
        yielded += 1
        idx += 1


def _gen_expanded_item(entry, val, wildcards):
    """ExpandedItem: inline the children of ValuePointer into our own children."""
    target = _eval_expr(entry.value_pointer, val, wildcards)
    if target is None: return
    pp = gdb.default_visualizer(target)
    if pp and hasattr(pp, 'children'):
        try:
            yield from pp.children()
            return
        except Exception: pass
    # Fallback: expose struct fields directly
    try:
        for f in target.type.strip_typedefs().fields():
            if f.name:
                try: yield (f.name, target[f.name])
                except Exception: pass
    except Exception: pass


def _cast_ptr_to_array(ptr_val, count):
    """
    Cast a pointer gdb.Value to a dereferenced typed array of *count* elements.
    Returns *(ElemType (*)[count])ptr — a gdb.Value of type ElemType[count]
    that GDB will expand as array children.
    Returns the original value unchanged on any error.
    """
    try:
        t = ptr_val.type.strip_typedefs()
        if t.code == gdb.TYPE_CODE_PTR and count > 0:
            elem_type = t.target().strip_typedefs()
            arr_type  = elem_type.array(count - 1)      # ElemType[count]
            return ptr_val.cast(arr_type.pointer()).dereference()
    except Exception:
        pass
    return ptr_val


def _gen_collection_children(entry, val, wildcards):
    ct = entry.collection_type
    if   ct == 'ArrayItems':      yield from _gen_array_items(entry, val, wildcards)
    elif ct == 'IndexListItems':  yield from _gen_index_list_items(entry, val, wildcards)
    elif ct == 'LinkedListItems': yield from _gen_linked_list_items(entry, val, wildcards)
    elif ct == 'TreeItems':       yield from _gen_tree_items(entry, val, wildcards)
    elif ct == 'DictionaryItems': yield from _gen_dictionary_items(entry, val, wildcards)
    elif ct == 'ExpandedItem':    yield from _gen_expanded_item(entry, val, wildcards)
    # CallStackList: no standard GDB representation; skip


# ─────────────────────────────────────────────────────────────────────────────
# 7.  Main printer class
# ─────────────────────────────────────────────────────────────────────────────

class _BeefDbgVisPrinter:
    def __init__(self, val, entry, wildcards, ptr_addr=0):
        self.val      = val
        self.entry    = entry
        self.wildcards = wildcards
        self.ptr_addr  = ptr_addr   # non-zero when the original value was a pointer

    def to_string(self):
        try:
            s = _pick_display_string(self.entry, self.val, self.wildcards)
            if s is None:
                s = ''
            if self.ptr_addr:
                return '0x{:x} {}'.format(self.ptr_addr, s)
            return s
        except Exception:
            gdb.write('[beef_dbgvis] to_string error for {}:\n{}'.format(
                self.entry.name, _traceback.format_exc()), gdb.STDERR)
            return '<error>'

    def display_hint(self):
        ct = self.entry.collection_type
        if ct in ('ArrayItems', 'IndexListItems', 'DictionaryItems',
                  'LinkedListItems', 'TreeItems'):
            return 'array'
        return None

    def children(self):
        entry = self.entry
        val   = self.val
        wc    = self.wildcards
        # Named items (with optional condition)
        for label, val_expr, cond in entry.items:
            if cond:
                r = _eval_bool(cond, val, wc)
                if r is not True: continue
            if val_expr:
                try:
                    bare_expr, item_specs = _parse_format_specs(val_expr)
                    v = _eval_expr(bare_expr, val, wc)
                    if v is not None:
                        # If arraySize=N is specified, cast the pointer to a
                        # typed array so GDB expands it as indexed children.
                        for s in item_specs:
                            if s.lower().startswith('arraysize='):
                                sz = _eval_expr(s[len('arraysize='):], val, wc)
                                if sz is not None:
                                    try:
                                        v = _cast_ptr_to_array(v, int(sz))
                                    except Exception:
                                        pass
                                break
                        yield (label, v)
                except Exception:
                    gdb.write('[beef_dbgvis] children item error for {} expr={}:\n{}'.format(
                        entry.name, val_expr, _traceback.format_exc()), gdb.STDERR)
        # Collection items appended last
        if entry.collection_type:
            try:
                yield from _gen_collection_children(entry, val, wc)
            except Exception:
                gdb.write('[beef_dbgvis] collection children error for {} type={}:\n{}'.format(
                    entry.name, entry.collection_type, _traceback.format_exc()), gdb.STDERR)


# ─────────────────────────────────────────────────────────────────────────────
# 8.  Lookup function + registration
# ─────────────────────────────────────────────────────────────────────────────

_TYPE_NORM_RE = re.compile(r'\s*([<>,])\s*')

def _norm_type(name):
    """Normalise spaces around template punctuation."""
    return _TYPE_NORM_RE.sub(lambda m: m.group(1) + (' ' if m.group(1) == ',' else ''), name).strip()


def _beef_dbgvis_lookup(val):
    try:
        t = val.type.unqualified()
        # Auto-dereference class pointers
        is_ptr = (t.code == gdb.TYPE_CODE_PTR)
        ptr_addr = 0
        if is_ptr:
            inner = t.target().unqualified()
            if inner.code == gdb.TYPE_CODE_VOID: return None
            t = inner
            try:
                ptr_addr = int(val)
            except Exception:
                pass
            if ptr_addr == 0:
                return None   # let GDB render null pointers natively

        if t.code not in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
            return None

        type_name = _norm_type(str(t.unqualified()))
        entry, wildcards = _find_entry(type_name)
        if entry is None: return None

        actual_val = val.dereference() if is_ptr else val

        # Check overall entry condition
        if entry.condition:
            if _eval_bool(entry.condition, actual_val, wildcards) is False:
                return None

        return _BeefDbgVisPrinter(actual_val, entry, wildcards, ptr_addr)
    except Exception:
        gdb.write('[beef_dbgvis] lookup error for {}:\n{}'.format(
            val.type, _traceback.format_exc()), gdb.STDERR)
        return None


gdb.pretty_printers.append(_beef_dbgvis_lookup)

