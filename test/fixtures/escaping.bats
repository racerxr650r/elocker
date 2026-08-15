#!/usr/bin/env bats
# test/fixtures/escaping.bats — characters that corrupt a document quietly.
#
# The reasoning, and why a path rather than an identifier carries the hostile
# characters today, is in escaping/README.md beside this file.

setup() {
	load "../helpers/common"

	# A comma, both angle brackets, an ampersand, and a quotation mark, in
	# a name every renderer must emit. Built here rather than committed: a
	# directory name containing a quote is portable to fewer tools than it
	# is to filesystems.
	HOSTILE="$BATS_TEST_TMPDIR/tmpl<int, long> & \"quoted\""
	mkdir -p "$HOSTILE"
	printf 'int f(int n)\n{\n\tif (n)\n\t\treturn 1;\n\treturn 0;\n}\n' \
		> "$HOSTILE/a.c"

	PLAIN="$BATS_TEST_TMPDIR/plain"
	mkdir -p "$PLAIN"
	cp "$HOSTILE/a.c" "$PLAIN/a.c"
}

# --- CSV (HLR-064) ---------------------------------------------------------

@test "HLR-064: a path containing a comma does not split a record" {
	run bash -c '"$0" -f csv "$1" 2>/dev/null' "$ELC" "$HOSTILE"
	assert_success

	# Every row must carry the same field count as the header, which a
	# split field would break. python's csv module is the reference
	# implementation here, not elc's own reading of its output.
	run python3 -c '
import csv, sys
rows = list(csv.reader(sys.stdin))
widths = {len(r) for r in rows if r}
print(",".join(str(w) for w in sorted(widths)))' <<<"$output"
	assert_output "7"
}

@test "HLR-064: the path survives the round trip intact" {
	run bash -c '"$0" -f csv "$1" 2>/dev/null' "$ELC" "$HOSTILE"
	assert_success

	run python3 -c '
import csv, sys
rows = [r for r in csv.reader(sys.stdin) if r]
print(rows[1][0])' <<<"$output"
	assert_output --partial 'tmpl<int, long> & "quoted"'
}

@test "HLR-064: a hostile path changes the field count of nothing" {
	run bash -c '"$0" -f csv "$1" 2>/dev/null' "$ELC" "$PLAIN"
	local plain_fields
	plain_fields="$(python3 -c '
import csv, sys
print(len(list(csv.reader(sys.stdin))[1]))' <<<"$output")"

	run bash -c '"$0" -f csv "$1" 2>/dev/null' "$ELC" "$HOSTILE"
	local hostile_fields
	hostile_fields="$(python3 -c '
import csv, sys
print(len(list(csv.reader(sys.stdin))[1]))' <<<"$output")"

	assert_equal "$hostile_fields" "$plain_fields"
}

# --- XML (HLR-065) ---------------------------------------------------------

@test "HLR-065: XML over a hostile path is well-formed" {
	require_tool xmllint "HLR-065 XML well-formedness"
	run bash -c '"$0" -f xml "$1" 2>/dev/null | xmllint --noout -' \
		"$ELC" "$HOSTILE"
	assert_success
}

@test "HLR-065: the structural characters are escaped, not emitted raw" {
	run bash -c '"$0" -f xml "$1" 2>/dev/null' "$ELC" "$HOSTILE"
	assert_success
	assert_output --partial "&lt;int, long&gt;"
	assert_output --partial "&amp;"
	assert_output --partial "&quot;quoted&quot;"
}

@test "HLR-065: the path arrives intact after unescaping" {
	run bash -c '"$0" -f xml "$1" 2>/dev/null' "$ELC" "$HOSTILE"
	assert_success

	run python3 -c '
import sys, xml.etree.ElementTree as ET
root = ET.fromstring(sys.stdin.read())
print(root.find("files/file").get("path"))' <<<"$output"
	assert_output --partial 'tmpl<int, long> & "quoted"'
}

# --- the round trip --------------------------------------------------------

@test "HLR-056: a hostile path survives a record round trip" {
	# An asymmetry — escaped on the way out, not unescaped on the way in —
	# passes xmllint and still corrupts the report.
	local record="$BATS_TEST_TMPDIR/record.xml"

	run bash -c '"$0" -f xml "$1" > "$2" 2>/dev/null' "$ELC" "$HOSTILE" \
		"$record"
	run bash -c '"$0" -f md "$1" 2>/dev/null' "$ELC" "$HOSTILE"
	local direct="$output"
	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_equal "$output" "$direct"
}

@test "HLR-027: the table renders a hostile path unchanged" {
	# The aligned table escapes nothing, and must not: it is read by a
	# person, and a path is what it is.
	elc "$HOSTILE"
	assert_success
	assert_output --partial 'tmpl<int, long> & "quoted"'
}

# --- the identifier the requirements were written for (HLR-064, HLR-065) ---
#
# Until C++ arrived, no shipped language could produce an identifier carrying
# a comma or an angle bracket, and these requirements could only be exercised
# against a contrived path. An explicit template specialisation names itself
# with its template arguments, so `combine<int, long>` is one identifier
# holding both.

@test "HLR-014: a template specialisation is reported under its full name" {
	elc "$BATS_TEST_DIRNAME/escaping/templates.cpp"
	assert_success
	assert_output --partial "combine<int, long>"
}

@test "HLR-064: an identifier containing a comma stays one CSV field" {
	run bash -c '"$0" -f csv "$1" 2>/dev/null' "$ELC" \
		"$BATS_TEST_DIRNAME/escaping/templates.cpp"
	assert_success

	run python3 -c '
import csv, sys
rows = [r for r in csv.reader(sys.stdin) if r]
names = [r[2] for r in rows[1:]]
widths = {len(r) for r in rows}
print(",".join(str(w) for w in sorted(widths)), "|", "|".join(sorted(names)))' \
		<<<"$output"
	assert_output "7 | combine|combine<int, long>"
}

@test "HLR-065: an identifier containing angle brackets is escaped" {
	run bash -c '"$0" -f xml "$1" 2>/dev/null' "$ELC" \
		"$BATS_TEST_DIRNAME/escaping/templates.cpp"
	assert_success
	assert_output --partial "combine&lt;int, long&gt;"
	refute_output --partial 'name="combine<int'
}

@test "HLR-065: XML carrying such an identifier is well-formed" {
	require_tool xmllint "HLR-065 XML well-formedness"
	run bash -c '"$0" -f xml "$1" 2>/dev/null | xmllint --noout -' "$ELC" \
		"$BATS_TEST_DIRNAME/escaping/templates.cpp"
	assert_success
}

@test "HLR-056: such an identifier survives a record round trip" {
	local record="$BATS_TEST_TMPDIR/tmpl.xml"
	local subject="$BATS_TEST_DIRNAME/escaping/templates.cpp"

	run bash -c '"$0" -f xml "$1" > "$2" 2>/dev/null' "$ELC" "$subject" \
		"$record"
	run bash -c '"$0" -f md "$1" 2>/dev/null' "$ELC" "$subject"
	local direct="$output"
	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_equal "$output" "$direct"
}
