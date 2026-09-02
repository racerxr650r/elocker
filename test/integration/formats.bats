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

@test "HLR-014: the csv columns are the Functions table's columns" {
	# One view of one set of rows, and it must be spelled one way. The two
	# had drifted — the table gained a visibility, a navigable location and
	# the flow degrees, and the record still wrote a language and a line
	# range nothing else reported — so the whole header is asserted rather
	# than a prefix of it.
	# The carriage return is RFC 4180's record terminator and is taken off
	# here rather than written into every expectation; that the terminator
	# is CRLF is asserted in test/unit/format_csv.c, where the bytes are.
	run bash -c '"$0" -f csv "$1" 2>/dev/null | tr -d "\r"' "$ELC" "$TREE"
	assert_success
	assert_line --index 0 \
		"file,language,function,visibility,lines,eloc,complexity,fan_in,fan_out,mi,mock_burden,wf_out,tbi,tbi_status"
	# And the location is the table's location: `path:line`, which an
	# editor acts on, with the extent beside it as a count (HLR-210).
	assert_output --regexp "$TREE/a\.c:[0-9]+,c,f,public,[0-9]+,"
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
	assert_output --partial "table, csv, xml, md, or html"
}

@test "HLR-063: a format option without its argument is a usage error" {
	elc -f
	assert_equal "$status" 2
}

# --- uniform composition (HLR-031) -----------------------------------------

@test "HLR-031: table and Markdown present the same tiers" {
	# The same headings, in the same order, differing only in decoration.
	#
	# Compared at --verbose, which is where the two formats present the
	# same tiers since HLR-218 gave the aligned table its own default
	# composition. The uniformity HLR-031 is about is that a tier exists in
	# both formats and says the same thing there; which tiers each shows
	# *by default* is the second axis HLR-150 opened and HLR-218 widened,
	# and it is asserted in verbosity.bats rather than here.
	elc --verbose "$TREE"
	local plain
	plain="$(grep -E '^[A-Z]' <<<"$output")"

	elc --verbose -f md "$TREE"
	local marked
	marked="$(sed -n 's/^## //p' <<<"$output")"

	assert_equal "$marked" "$plain"
}

@test "HLR-031: a tier with no rows is named in both" {
	# Nothing is over the default threshold and nothing is banded, so the
	# tier is not printed — and both formats say so, in the same words
	# (HLR-188, HLR-189).
	elc --verbose "$TREE"
	refute_output --regexp "^At or over a threshold"
	assert_output --partial "- At or over a threshold"
	elc --verbose -f md "$TREE"
	refute_output --regexp "^## At or over a threshold"
	assert_output --partial "- At or over a threshold"
}

@test "HLR-190: every Markdown table sits inside a disclosure element" {
	elc --verbose -f md "$TREE"
	assert_success

	# One `<details>` per `##` heading, minus the one section that is not
	# a table: the closing statement of HLR-189 is prose.
	local headings details
	headings="$(grep -c '^## ' <<<"$output")"
	details="$(grep -c '^<details>$' <<<"$output")"
	assert_equal "$details" "$(( headings - 1 ))"

	# Every summary states a row count, and every element is closed.
	assert_equal \
		"$(grep -cE '^<summary>[0-9]+ rows? \(click to expand\)</summary>$' \
			<<<"$output")" "$details"
	assert_equal "$(grep -c '^</details>$' <<<"$output")" "$details"
}

@test "HLR-190: the heading stays outside the element and stays a heading" {
	# A section keeps its anchor, and the composition is still readable
	# off the `##` lines — which is what the uniformity tests above do.
	elc -f md "$TREE"
	assert_success
	assert_output --partial "## Files"
	refute_output --partial "<summary><strong>"
}

@test "HLR-190: the complete-record formats carry no HTML" {
	# CSV and XML are parsed by their consumers; a disclosure element in
	# either would be a defect rather than a convenience.
	for format in table csv xml; do
		elc --verbose -f "$format" "$TREE"
		assert_success
		refute_output --partial "<details>"
		refute_output --partial "<summary>"
	done
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
	# The extension is the format's name in a filename, so the pairs are
	# not interchangeable: `.table` names no format at all and would be
	# rejected before anything was written (HLR-148). `-f` is given as
	# well, in agreement, because this test is about --output rather than
	# about which of the two ways of naming a format is used (HLR-149).
	for pair in table:txt md:md csv:csv xml:xml; do
		local format="${pair%%:*}" extension="${pair##*:}"
		local file="$BATS_TEST_TMPDIR/out.$extension"

		# stderr carries the skipped-file notice; stdout must be empty.
		run bash -c '"$0" -f "$1" -o "$2" "$3" 2>/dev/null' "$ELC" \
			"$format" "$file" "$TREE"
		assert_success
		assert_output ""
		[ -s "$file" ] || {
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

# --- the format an output filename names (HLR-148, HLR-149) ----------------

@test "HLR-148: each recognised extension selects its format with no option" {
	# The filename has already said what the format is; nothing should have
	# to say it twice. Each file is identified by a marker only that format
	# produces.
	for pair in "txt:Project summary" "md:## Project summary" \
	            "csv:file,language,function" "xml:<?xml"; do
		local extension="${pair%%:*}" marker="${pair#*:}"
		local file="$BATS_TEST_TMPDIR/named.$extension"

		run bash -c '"$0" -o "$1" "$2" 2>/dev/null' "$ELC" "$file" \
			"$TREE"
		assert_success
		run head -1 "$file"
		case "$extension" in
		txt|csv|xml) assert_output --partial "$marker" ;;
		md)          run grep -c "^## Project summary$" "$file"
		             assert_output "1" ;;
		esac
	done
}

@test "HLR-148: an unrecognised extension is a usage error naming both" {
	run bash -c '"$0" -o "$1" "$2" 2>&1 >/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/report.json" "$TREE"
	assert_failure 2
	# The extension found, and the ones that would have worked. Guessing
	# would write a report.json holding no JSON.
	assert_output --partial ".json"
	assert_output --partial ".txt"
	assert_output --partial ".md"
	assert_output --partial ".csv"
	assert_output --partial ".xml"
	[ ! -e "$BATS_TEST_TMPDIR/report.json" ]
}

@test "HLR-148: a filename with no extension is a usage error too" {
	run bash -c '"$0" -o "$1" "$2" 2>&1 >/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/report" "$TREE"
	assert_failure 2
	assert_output --partial "no extension"
	[ ! -e "$BATS_TEST_TMPDIR/report" ]
}

@test "HLR-148: a dotfile has no extension, and a trailing dot names nothing" {
	for name in ".report" "report."; do
		run bash -c '"$0" -o "$1" "$2" 2>&1 >/dev/null' "$ELC" \
			"$BATS_TEST_TMPDIR/$name" "$TREE"
		assert_failure 2
		assert_output --partial "no extension"
	done
}

@test "HLR-148: a directory carrying a dot does not lend its extension" {
	# The last dot of the *basename*. A `build.d/report` has no extension
	# of its own, and reading one out of the directory would pick `.d`.
	mkdir -p "$BATS_TEST_TMPDIR/build.d"
	run bash -c '"$0" -o "$1" "$2" 2>&1 >/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/build.d/report" "$TREE"
	assert_failure 2
	assert_output --partial "no extension"
}

@test "HLR-149: a format option contradicting the filename is a usage error" {
	run bash -c '"$0" -f csv -o "$1" "$2" 2>&1 >/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/report.md" "$TREE"
	assert_failure 2
	# Both are named; neither is silently preferred.
	assert_output --partial "csv"
	assert_output --partial "report.md"
	[ ! -e "$BATS_TEST_TMPDIR/report.md" ]
}

@test "HLR-149: a format option agreeing with the filename is accepted" {
	# Nothing is ambiguous about saying a thing twice.
	run bash -c '"$0" -f md -o "$1" "$2" 2>/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/agree.md" "$TREE"
	assert_success
	run grep -c "^## Project summary$" "$BATS_TEST_TMPDIR/agree.md"
	assert_output "1"
}

@test "HLR-149: with no output file the option still selects the format" {
	# Standard output has no filename and so no extension, which is what
	# keeps a machine-readable format available to a caller that pipes.
	elc -f csv "$TREE"
	assert_success
	assert_output --partial "file,language,function"
}

@test "HLR-148: the companion artefacts keep their own extensions" {
	# The extension governs the format alone. HLR-119 substitutes its own
	# on the same path, so an output of report.md still yields report.dot.
	run bash -c '"$0" --graphml -o "$1" "$2" 2>/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/companions.md" "$TREE"
	assert_success
	[ -f "$BATS_TEST_TMPDIR/companions.dot" ]
	[ -f "$BATS_TEST_TMPDIR/companions.graphml" ]
}

@test "LLR-CLI-10: a record cannot be regenerated into a filename naming a table" {
	# An output path's extension is a format selection, differently spelt.
	# Reading it as anything less would write Markdown into a file called
	# out.txt — one format under a name promising another.
	elc -f xml -o "$BATS_TEST_TMPDIR/rec.xml" "$TREE"
	assert_success

	run bash -c '"$0" --from-xml "$1" -o "$2" 2>&1 >/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/rec.xml" "$BATS_TEST_TMPDIR/out.txt"
	assert_failure 2
	assert_output --partial "Markdown"
	assert_output --partial "out.txt"
	[ ! -e "$BATS_TEST_TMPDIR/out.txt" ]
}

@test "LLR-CLI-10: a filename naming Markdown regenerates as it always did" {
	elc -f xml -o "$BATS_TEST_TMPDIR/rec.xml" "$TREE"
	assert_success

	run bash -c '"$0" --from-xml "$1" -o "$2" 2>/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/rec.xml" "$BATS_TEST_TMPDIR/out.md"
	assert_success
	run grep -c "^## Project summary$" "$BATS_TEST_TMPDIR/out.md"
	assert_output "1"
}

@test "LLR-SUM-04: no line of the aligned table carries trailing whitespace" {
	# The final column of a table is left-aligned and holds the longest
	# values — a path, a detail sentence. Padding it to the column width
	# would put spaces at the end of almost every line, which shows up in
	# a diff, in a review, and in every tool that strips them. Asserted
	# over the verbose report so that every tier is present.
	local out="$BATS_TEST_TMPDIR/table.txt"

	run bash -c '"$0" --verbose -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$out" "$TREE"
	assert_success

	run bash -c 'grep -n "[[:space:]]$" "$0" || true' "$out"
	assert_output ""
}
