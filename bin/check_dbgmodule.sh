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

line_rows() {
	llvm-dwarfdump --debug-line "$1" | awk '
		/^debug_line\[/ { version = 0 }
		/^ +version: / { version = $2 }
		/^0x[0-9a-f]+ / { if (version == 4) print $1, $2, ($0 ~ /is_stmt/) ? 1 : 0, ($0 ~ /end_sequence/) ? 1 : 0 }
	' > "$TMPDIR/$2_rows.txt"
	# Rows whose address is unique, so the expected line is unambiguous
	awk 'NR == FNR { count[$1]++; next } count[$1] == 1 && $3 == 1 && $4 == 0 && $2 != 0 { print $1, $2 }' "$TMPDIR/$2_rows.txt" "$TMPDIR/$2_rows.txt" > "$TMPDIR/$2_expected_lines.txt"
	awk '{ print $1 }' "$TMPDIR/$2_expected_lines.txt" > "$TMPDIR/$2_addrs.txt"
}

check_lines() {
	local total bad
	total=$(wc -l < "$TMPDIR/$1_expected_lines.txt")
	bad=$(paste -d' ' "$TMPDIR/$1_expected_lines.txt" <(grep '^addr ' "$2") | awk -v label="$1" '
		{ n = split($NF, parts, ":"); if (parts[n] != $2) { bad++; if (bad <= 10) print "  line mismatch (" label "): " $0 > "/dev/stderr" } }
		END { print bad + 0 }
	')
	if [ "$bad" != "0" ] || [ "$total" == "0" ]; then
		fail "address to line ($1): $bad of $total rows differ from llvm-dwarfdump"
	else
		echo "ok address_to_line $1 $total rows"
	fi
}

line_rows "$ELF" main

"$CHECK" "$ELF" --addr-file "$TMPDIR/main_addrs.txt" > "$TMPDIR/check.txt" || { echo "DbgModuleCheck failed" >&2; exit 1; }
grep -v '^addr ' "$TMPDIR/check.txt"

value() {
	awk -v key="$1" '$1 == key { print $2 }' "$TMPDIR/check.txt"
}

read -r REF_V4 REF_V5 REF_SUB REF_INL REF_DEF REF_IRNG < <(llvm-dwarfdump --debug-info "$ELF" | awk '
	/Compile Unit:/ { version = ($0 ~ /version = 0x0004/) ? 4 : (($0 ~ /version = 0x0005/) ? 5 : 0); units[version]++ }
	/^0x[0-9a-f]+: +DW_TAG_/ { tag = $2; if (version == 4) { if (tag == "DW_TAG_subprogram") subprograms++; if (tag == "DW_TAG_inlined_subroutine") inlined++ } }
	/^0x[0-9a-f]+: +NULL/ { tag = "" }
	version == 4 && tag == "DW_TAG_subprogram" && /DW_AT_low_pc/ { def++ }
	version == 4 && tag == "DW_TAG_inlined_subroutine" && (/DW_AT_low_pc/ || /^ +\[0x[0-9a-f]+, 0x[0-9a-f]+\)\)?$/) { irng++ }
	END { print units[4] + 0, units[5] + 0, subprograms + inlined, inlined + 0, def + 0, irng + 0 }
')
REF_SYMS=$(llvm-readelf -sW "$ELF" | awk '$4 ~ /^(FUNC|OBJECT)$/ && $7 != "UND" && $5 != "LOCAL" && $2 !~ /^0+$/ { n++ } END { print n + 0 }')

expect_eq dwarf4_units "$(value dwarf4_units)" "$REF_V4"
expect_eq dwarf5_units_skipped "$(value dwarf5_units_skipped)" "$REF_V5"
expect_eq subprograms "$(value subprograms)" "$REF_SUB"
expect_eq subprograms_inlined "$(value subprograms_inlined)" "$REF_INL"
expect_eq inlinee_ranges "$(value inlinee_ranges)" "$REF_IRNG"
echo "info subprograms_inlined_with_lines $(value subprograms_inlined_with_lines)"
expect_eq subprograms_defined "$(value subprograms_defined)" "$REF_DEF"
expect_eq subprograms_defined_with_lines "$(value subprograms_defined_with_lines)" "$REF_DEF"
expect_eq global_symbols "$(value global_symbols)" "$REF_SYMS"
expect_eq dwarf_failures "$(value dwarf_failures)" 0

check_lines main "$TMPDIR/check.txt"

# g++ puts each template instance in its own line sequence, which IDETest's Beef output does not
if command -v g++ > /dev/null; then
	cat > "$TMPDIR/gxx.cpp" <<'EOF'
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

struct Entry
{
	std::string mName;
	int mValue;
	bool operator<(const Entry& other) const { return mName < other.mName; }
};

int main(int argc, char** argv)
{
	std::vector<Entry> entries;
	for (int i = 0; i < argc; i++)
		entries.push_back({ argv[i], i });
	std::sort(entries.begin(), entries.end());
	std::string joined;
	for (auto& entry : entries)
		joined += entry.mName + ",";
	printf("%s %d\n", joined.c_str(), (int)joined.length());
	return 0;
}
EOF
	for opt in O0 O1; do
		if ! g++ -$opt -g -gdwarf-4 -no-pie -o "$TMPDIR/gxx_$opt" "$TMPDIR/gxx.cpp"; then
			fail "g++ -$opt build"
			continue
		fi
		line_rows "$TMPDIR/gxx_$opt" "gxx_$opt"
		if ! "$CHECK" "$TMPDIR/gxx_$opt" --addr-file "$TMPDIR/gxx_${opt}_addrs.txt" > "$TMPDIR/gxx_$opt.txt"; then
			fail "g++ -$opt: DbgModuleCheck failed"
			continue
		fi
		check_lines "gxx_$opt" "$TMPDIR/gxx_$opt.txt"
	done
else
	echo "skip address_to_line g++: g++ not found"
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

shnum = struct.unpack_from('<H', data, 0x3C)[0]
shstroff = struct.unpack_from('<Q', data, strhdr + 0x18)[0]
sections = {}
for sectionIdx in range(shnum):
	hdr = shoff + sectionIdx * shentsize
	nameOffset = struct.unpack_from('<I', data, hdr)[0]
	name = data[shstroff + nameOffset:data.index(b'\0', shstroff + nameOffset)].decode()
	sections[name] = struct.unpack_from('<QQ', data, hdr + 0x18)
infoOff, infoSize = sections['.debug_info']
abbrevOff, abbrevSize = sections['.debug_abbrev']
strOff, strSize = sections['.debug_str']
lineOff, lineSize = sections['.debug_line']

def uleb(pos):
	value = 0
	shift = 0
	while True:
		byte = data[pos]
		pos += 1
		value |= (byte & 0x7f) << shift
		shift += 7
		if byte < 0x80:
			return value, pos

unit = infoOff
while struct.unpack_from('<H', data, unit + 4)[0] != 4:
	unit += 4 + struct.unpack_from('<I', data, unit)[0]
abbrevTable = abbrevOff + struct.unpack_from('<I', data, unit + 6)[0]
die = unit + 11
code, attrPos = uleb(die)

pos = abbrevTable
while True:
	entryCode, pos = uleb(pos)
	tag, pos = uleb(pos)
	pos += 1
	attrs = []
	while True:
		attrName, pos = uleb(pos)
		form, pos = uleb(pos)
		if (attrName == 0) and (form == 0):
			break
		attrs.append((attrName, form))
	if entryCode == code:
		break

formSizes = {0x01: 8, 0x0b: 1, 0x05: 2, 0x06: 4, 0x07: 8, 0x0c: 1, 0x0e: 4, 0x17: 4, 0x19: 0}
strpPos = None
stmtList = None
pos = attrPos
for attrName, form in attrs:
	if (form == 0x0e) and (strpPos is None):
		strpPos = pos
	if (attrName == 0x10) and (form == 0x17):
		stmtList = struct.unpack_from('<I', data, pos)[0]
	if form == 0x08:
		pos = data.index(b'\0', pos) + 1
	else:
		pos += formSizes[form]
assert (strpPos is not None) and (stmtList is not None)

copy = bytearray(data)
struct.pack_into('<I', copy, infoOff, 0xFFFFFFF0)
write('info_length', copy)

copy = bytearray(data)
struct.pack_into('<I', copy, unit + 6, abbrevSize + 0x1000)
write('abbrev_offset', copy)

copy = bytearray(data)
struct.pack_into('<I', copy, strpPos, strSize + 0x1000)
write('strp_offset', copy)

copy = bytearray(data)
struct.pack_into('<I', copy, lineOff + stmtList, 0xFFFFFFF0)
write('line_length', copy)

# 0x7FFFFFFF as a ULEB128 takes 5 bytes, which overwrite the code and the start of the first attribute
copy = bytearray(data)
copy[die:die + 5] = bytes([0xFF, 0xFF, 0xFF, 0xFF, 0x07])
write('abbrev_code', copy)
EOF

for malformed in truncated_64 truncated_half huge_shoff unterminated_shstrtab huge_shnum info_length abbrev_offset strp_offset line_length abbrev_code; do
	"$CHECK" "$TMPDIR/malformed_$malformed" > "$TMPDIR/malformed_$malformed.out" 2>&1
	status=$?
	case "$status" in
		0)
			echo "ok malformed $malformed loaded"
			case "$malformed" in
				info_length|abbrev_offset|strp_offset|line_length|abbrev_code)
					failures=$(awk '$1 == "dwarf_failures" { print $2 }' "$TMPDIR/malformed_$malformed.out")
					[ "${failures:-0}" -ge 1 ] || fail "malformed $malformed: loaded without reporting damaged DWARF"
					;;
			esac
			;;
		1) echo "ok malformed $malformed rejected" ;;
		*) fail "malformed $malformed: DbgModuleCheck exited with $status" ;;
	esac
done

if [ "$FAILED" != "0" ]; then
	echo "FAILED"
	exit 1
fi
echo "SUCCESS"
