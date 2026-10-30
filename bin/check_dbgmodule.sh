#!/bin/bash
# Compares what ElfDbgModule reads from an ELF (via DbgModuleCheck) with llvm-dwarfdump, llvm-readelf and lldb.
# Usage: bin/check_dbgmodule.sh [elf]
# Env: DBGMODULECHECK (harness binary), CHECK_NAMES (functions to look up; each must have one Beef definition).

ROOTPATH="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-$ROOTPATH/IDE/Tests/Test1/build/Debug_Linux64/IDETest/IDETest}"
CHECK="${DBGMODULECHECK:-$ROOTPATH/jbuild_d/Debug/bin/DbgModuleCheck}"
NAMES="${CHECK_NAMES:-GetC Main IncVal GetCultureData GetCultureInfo GetCultureByName GetCalendarInstance}"

for tool in llvm-dwarfdump llvm-readelf lldb python3; do
	if ! command -v "$tool" > /dev/null; then
		echo "missing tool: $tool" >&2
		exit 2
	fi
done
if [ ! -x "$CHECK" ] || [ ! -f "$ELF" ]; then
	echo "missing $CHECK or $ELF" >&2
	exit 2
fi

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT
FAILED=0

fail() {
	echo "MISMATCH: $1"
	FAILED=1
}

expect_eq() {
	if [ "$2" != "$3" ]; then
		fail "$1: DbgModuleCheck $2, reference $3"
	else
		echo "ok $1 $2"
	fi
}

llvm-dwarfdump --debug-line "$ELF" | awk '
	/^debug_line\[/ { version = 0 }
	/^ +version: / { version = $2 }
	/^0x[0-9a-f]+ / { if (version == 4) print $1, $2, ($0 ~ /is_stmt/) ? 1 : 0, ($0 ~ /end_sequence/) ? 1 : 0 }
' > "$TMPDIR/rows.txt"
# Rows whose address is unique, so the expected line is unambiguous
awk 'NR == FNR { count[$1]++; next } count[$1] == 1 && $3 == 1 && $4 == 0 && $2 != 0 { print $1, $2 }' "$TMPDIR/rows.txt" "$TMPDIR/rows.txt" > "$TMPDIR/expected_lines.txt"
awk '{ print $1 }' "$TMPDIR/expected_lines.txt" > "$TMPDIR/addrs.txt"

"$CHECK" "$ELF" --addr-file "$TMPDIR/addrs.txt" > "$TMPDIR/check.txt" || { echo "DbgModuleCheck failed" >&2; exit 1; }
grep -v '^addr ' "$TMPDIR/check.txt"

value() {
	awk -v key="$1" '$1 == key { print $2 }' "$TMPDIR/check.txt"
}

read -r REF_V4 REF_V5 REF_SUB REF_INL REF_DEF < <(llvm-dwarfdump --debug-info "$ELF" | awk '
	/Compile Unit:/ { version = ($0 ~ /version = 0x0004/) ? 4 : (($0 ~ /version = 0x0005/) ? 5 : 0); units[version]++ }
	/^0x[0-9a-f]+: +DW_TAG_/ { tag = $2; if (version == 4) { if (tag == "DW_TAG_subprogram") subprograms++; if (tag == "DW_TAG_inlined_subroutine") inlined++ } }
	/^0x[0-9a-f]+: +NULL/ { tag = "" }
	version == 4 && tag == "DW_TAG_subprogram" && /DW_AT_low_pc/ { def++ }
	END { print units[4] + 0, units[5] + 0, subprograms + inlined, inlined + 0, def + 0 }
')
REF_SYMS=$(llvm-readelf -sW "$ELF" | awk '$4 ~ /^(FUNC|OBJECT)$/ && $7 != "UND" && $5 != "LOCAL" && $2 !~ /^0+$/ { n++ } END { print n + 0 }')

expect_eq dwarf4_units "$(value dwarf4_units)" "$REF_V4"
expect_eq dwarf5_units_skipped "$(value dwarf5_units_skipped)" "$REF_V5"
expect_eq subprograms "$(value subprograms)" "$REF_SUB"
expect_eq subprograms_inlined "$(value subprograms_inlined)" "$REF_INL"
expect_eq subprograms_defined "$(value subprograms_defined)" "$REF_DEF"
expect_eq subprograms_defined_with_lines "$(value subprograms_defined_with_lines)" "$REF_DEF"
expect_eq global_symbols "$(value global_symbols)" "$REF_SYMS"

LINE_TOTAL=$(wc -l < "$TMPDIR/expected_lines.txt")
LINE_BAD=$(paste -d' ' "$TMPDIR/expected_lines.txt" <(grep '^addr ' "$TMPDIR/check.txt") | awk '
	{ n = split($NF, parts, ":"); if (parts[n] != $2) { bad++; if (bad <= 10) print "  line mismatch: " $0 > "/dev/stderr" } }
	END { print bad + 0 }
')
if [ "$LINE_BAD" != "0" ] || [ "$LINE_TOTAL" == "0" ]; then
	fail "address to line: $LINE_BAD of $LINE_TOTAL rows differ from llvm-dwarfdump"
else
	echo "ok address_to_line $LINE_TOTAL rows"
fi

for name in $NAMES; do
	lldb --batch -o "image lookup -n $name" "$ELF" 2>/dev/null | awk '
		/Address:/ { match($0, /\[0x[0-9a-f]+\]/); addr = substr($0, RSTART + 1, RLENGTH - 2) }
		/Summary:/ { n = split($NF, parts, ":"); sub(/^0x0*/, "0x", addr); print addr, parts[1] ":" parts[2] }
	' | sort > "$TMPDIR/lldb_$name.txt"
	LLDB_ADDRS=$(awk '{ print $1 }' "$TMPDIR/lldb_$name.txt" | sort | tr '\n' ' ' | sed 's/ $//')
	CHECK_ADDRS=$("$CHECK" "$ELF" --name "$name" | awk -v name="$name" '$1 == "name" && $2 == name { for (i = 3; i <= NF; i++) print $i }' | sort | tr '\n' ' ' | sed 's/ $//')
	if [ -z "$LLDB_ADDRS" ] || [ "$LLDB_ADDRS" != "$CHECK_ADDRS" ]; then
		fail "lookup $name: DbgModuleCheck [$CHECK_ADDRS], lldb [$LLDB_ADDRS]"
		continue
	fi
	while read -r addr location; do
		got=$("$CHECK" "$ELF" --addr "$addr" | awk '$1 == "addr" { n = split($4, parts, "/"); print $3, parts[n] }')
		if [ "$got" != "$name $location" ]; then
			fail "address $addr: DbgModuleCheck [$got], lldb [$name $location]"
		else
			echo "ok lookup $name $addr $location"
		fi
	done < "$TMPDIR/lldb_$name.txt"
done

# Damaged copies must be rejected (exit 1) or loaded (exit 0), never crash
python3 - "$ELF" "$TMPDIR" <<'EOF'
import struct
import sys

src, out = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()

def write(name, content):
	with open(out + '/malformed_' + name, 'wb') as f:
		f.write(content)

write('truncated_64', data[:64])
write('truncated_half', data[:len(data) // 2])

copy = bytearray(data)
struct.pack_into('<Q', copy, 0x28, 0xFFFFFFFFFFFFFFF0)
write('huge_shoff', copy)

shoff = struct.unpack_from('<Q', data, 0x28)[0]
shentsize = struct.unpack_from('<H', data, 0x3A)[0]
shstrndx = struct.unpack_from('<H', data, 0x3E)[0]
strhdr = shoff + shstrndx * shentsize
stroff = struct.unpack_from('<Q', data, strhdr + 0x18)[0]
strsize = struct.unpack_from('<Q', data, strhdr + 0x20)[0]
copy = bytearray(data)
copy[stroff + strsize - 1] = ord('x')
write('unterminated_shstrtab', copy)

copy = bytearray(data)
struct.pack_into('<H', copy, 0x3C, 0xFFFF)
write('huge_shnum', copy)
EOF

for malformed in truncated_64 truncated_half huge_shoff unterminated_shstrtab huge_shnum; do
	"$CHECK" "$TMPDIR/malformed_$malformed" > /dev/null 2>&1
	status=$?
	case "$status" in
		0) echo "ok malformed $malformed loaded" ;;
		1) echo "ok malformed $malformed rejected" ;;
		*) fail "malformed $malformed: DbgModuleCheck exited with $status" ;;
	esac
done

if [ "$FAILED" != "0" ]; then
	echo "FAILED"
	exit 1
fi
echo "SUCCESS"
