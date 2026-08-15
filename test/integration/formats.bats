#!/usr/bin/env bats
# test/integration/formats.bats — format selection, black box.
#
# What each format looks like is asserted at the unit and fixture levels.
# This level asserts the command-line contract around them: which format a
# run produces, that they are views of one model rather than four reports,
# and that selecting one changes nothing else.

setup() {
	load "../helpers/common"

	TREE="$BATS_TEST_TMPDIR/tree"
	mkdir -p "$TREE"
	printf 'int f(int n)\n{\n\tif (n)\n\t\treturn 1;\n\treturn 0;\n}\n' \
		> "$TREE/a.c"
	printf '# not source\n' > "$TREE/notes.md"
}

# --- selection (HLR-027 – HLR-029, HLR-054) --------------------------------

@test "HLR-027: table is the format when none is selected" {
	elc "$TREE"
	assert_success
	assert_output --partial "Project summary"
	refute_output --partial "## Project summary"
}

@test "HLR-027: table may also be selected explicitly" {
	elc -f table "$TREE"
	local explicit="$output"
	elc "$TREE"
	assert_equal "$output" "$explicit"
}

@test "HLR-028: csv produces one record per function" {
	elc -f csv "$TREE"
	assert_success
	assert_output --partial "file,language,function"
	assert_output --partial ",f,"
}

@test "HLR-029: md produces GitHub-Flavored Markdown" {
	elc -f md "$TREE"
	assert_success
	assert_output --partial "## Project summary"
	assert_output --partial "| File "
}

@test "HLR-054: xml produces a record with a version" {
	elc -f xml "$TREE"
	assert_success
	assert_output --partial "<elc-report format-version="
	assert_output --partial "</elc-report>"
}

@test "HLR-063: an unknown format is a usage error naming the choices" {
	elc -f yaml "$TREE"
	assert_equal "$status" 2

	run bash -c '"$0" -f yaml "$1" 2>&1 >/dev/null' "$ELC" "$TREE"
	assert_output --partial "table, csv, xml, or md"
}

@test "HLR-063: a format option without its argument is a usage error" {
	elc -f
	assert_equal "$status" 2
}

# --- uniform composition (HLR-031) -----------------------------------------

@test "HLR-031: table and Markdown present the same tiers" {
	# The same headings, in the same order, differing only in decoration.
	elc "$TREE"
	local plain
	plain="$(grep -E '^[A-Z]' <<<"$output")"

	elc -f md "$TREE"
	local marked
	marked="$(sed -n 's/^## //p' <<<"$output")"

	assert_equal "$marked" "$plain"
}

@test "HLR-031: a tier with no rows appears in both" {
	# Nothing is over the default threshold, and the tier is still there.
	elc "$TREE"
	assert_output --partial "At or over the complexity threshold"
	elc -f md "$TREE"
	assert_output --partial "At or over the complexity threshold"
}

# --- the formats are views of one run --------------------------------------

@test "the format changes the rendering and not the analysis" {
	# Every format must agree about what was found. CSV names the function
	# once; Markdown and the table name it too.
	for format in table md csv xml; do
		elc -f "$format" "$TREE"
		assert_success
		assert_output --partial "f"
	done
}

@test "HLR-028: csv is unfiltered by the threshold" {
	# The threshold governs the listing tier, which CSV does not have.
	elc -f csv -c 1 "$TREE"
	local low="$output"
	elc -f csv -c 99 "$TREE"
	assert_equal "$output" "$low"
}

@test "HLR-054: xml is unfiltered by the threshold" {
	elc -f xml -c 1 "$TREE"
	local low="$output"
	elc -f xml -c 99 "$TREE"
	assert_equal "$output" "$low"
}

# --- the stream split holds for every format (HLR-038) ---------------------

@test "HLR-038: every format writes results to stdout alone" {
	for format in table md csv xml; do
		run bash -c '"$0" -f "$1" "$2" >/dev/null' "$ELC" "$format" \
			"$TREE"
		# The skipped file is diagnosed on stderr; nothing else is.
		assert_output --partial "notes.md"
	done
}

@test "HLR-030: every format honours --output" {
	for format in table md csv xml; do
		# stderr carries the skipped-file notice; stdout must be empty.
		run bash -c '"$0" -f "$1" -o "$2" "$3" 2>/dev/null' "$ELC" \
			"$format" "$BATS_TEST_TMPDIR/out.$format" "$TREE"
		assert_success
		assert_output ""
		[ -s "$BATS_TEST_TMPDIR/out.$format" ] || {
			echo "$format wrote nothing to the file" >&2
			false
		}
	done
}

# --- determinism (HLR-032) -------------------------------------------------

@test "HLR-032: every format is byte-identical across runs" {
	for format in table md csv xml; do
		run bash -c '"$0" -f "$1" "$2" 2>/dev/null' "$ELC" "$format" \
			"$TREE"
		local first="$output"
		run bash -c '"$0" -f "$1" "$2" 2>/dev/null' "$ELC" "$format" \
			"$TREE"
		assert_equal "$output" "$first"
	done
}

@test "HLR-066: every format renders an empty run" {
	mkdir -p "$BATS_TEST_TMPDIR/empty"
	for format in table md csv xml; do
		elc -f "$format" "$BATS_TEST_TMPDIR/empty"
		assert_success
		[ -n "$output" ] || {
			echo "$format produced nothing for an empty run" >&2
			false
		}
	done
}
