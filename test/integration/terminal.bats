#!/usr/bin/env bats
# test/integration/terminal.bats — the report a terminal gets (HLR-218 – HLR-220).
#
# Two properties, decided by two different things, and the split is the point:
#
#   * **What a report presents** is a property of neither the format nor the
#     destination (HLR-218): there is one composition, and it is the same
#     wherever it is written and however it is decorated.
#   * **How wide its lines are** is a property of the *destination* (HLR-219).
#     A file has no width and a pipe has no width; a terminal does.
#
# The second is what makes this suite unusual. Bats captures output through a
# pipe, so `isatty` is false in every ordinary `run` and the wrapping never
# fires — a test written the usual way would assert the unwrapped table and
# pass for the wrong reason forever. So the runs below allocate a pty with
# `script(1)`. There is deliberately no environment variable in `src/` to force
# the behaviour: that would be a test seam in the product, and the seam would
# be the only thing most runs exercised.

setup() {
	load "../helpers/common"

	# Paths long enough to force the File column past any sane cap, so the
	# wrapping under test is provoked rather than hoped for.
	TREE="$BATS_TEST_TMPDIR/a-project-with/a-deliberately-long/directory-path/src"
	mkdir -p "$TREE"
	# One global at file scope, so the Globals tier has a row. Without one
	# that table is empty, is therefore not printed (HLR-188), and the
	# composition tests below would compare four tiers against five and
	# call the difference a defect.
	printf 'int shared;\n\nint helper(int n)\n{\n\tif (n)\n\t\treturn 1;\n\treturn 0;\n}\n\nint main(void)\n{\n\treturn helper(1);\n}\n' \
		> "$TREE/a.c"

	# One function over a published band, so the Findings tier has a row.
	# A report with nothing to report would let the composition tests below
	# pass against a renderer that had dropped the findings entirely, which
	# is the one section of the four a reader is expected to act on.
	{
		printf 'int busy(int n)\n{\n'
		for _ in $(seq 1 11); do printf '\tif (n) n++;\n'; done
		printf '\treturn n;\n}\n'
	} > "$TREE/busy.c"
}

# Run elc on a pseudo-terminal, so the report is written to something isatty()
# answers yes for. `script` writes CR-LF; the CRs are stripped so assertions
# compare the report rather than the line discipline.
on_a_terminal() {
	require_tool script "HLR-219 terminal width unverified on this platform"
	run script -qec "$ELC $*" /dev/null
	output="$(printf '%s' "$output" | tr -d '\r')"
	# The raw bytes, kept for the tests that are *about* the colour.
	raw_output="$output"
	# And the report as displayed. HLR-219 bounds the width of a line in
	# columns, and an SGR sequence occupies none of them — a test measuring
	# the bytes would be asserting something about the escapes rather than
	# about the table, and would fail on a report that fits perfectly.
	output="$(printf '%s' "$output" | sed 's/\x1b\[[0-9;]*m//g')"
	lines=()
	while IFS= read -r line; do lines+=("$line"); done <<<"$output"
}

widest() {
	awk '{ if (length > n) n = length } END { print n + 0 }' <<<"$output"
}

# --- the width (HLR-219) ---------------------------------------------------

@test "HLR-219: no line of a terminal report exceeds 128 columns" {
	on_a_terminal "$TREE"
	assert_success
	[ "$(widest)" -le 128 ] || {
		echo "widest line was $(widest) columns" >&2
		false
	}
}

@test "HLR-219: the same report redirected is not wrapped" {
	# The converse, and the half that makes the width a property of the
	# destination rather than a new default: redirected, the table keeps
	# its natural width, and this fixture's paths put that over the limit.
	elc "$TREE"
	assert_success
	[ "$(widest)" -gt 128 ] || {
		echo "a redirected report was wrapped: widest $(widest)" >&2
		false
	}
}

@test "HLR-032: two runs to the same destination are byte-identical" {
	# Determinism is per destination. 128 is a constant rather than the
	# terminal's own width, so nothing here varies with the window.
	on_a_terminal "$TREE"
	local first="$output"
	on_a_terminal "$TREE"
	assert_equal "$output" "$first"

	elc "$TREE"
	local piped="$output"
	elc "$TREE"
	assert_equal "$output" "$piped"
}

@test "HLR-219: wrapping elides nothing and breaks a path at a separator" {
	on_a_terminal "$TREE"
	assert_success

	# No ellipsis anywhere: a cell that does not fit is continued, never
	# shortened. A truncated path is a path the reader cannot open.
	refute_output --partial "..."
	refute_output --partial "…"

	# The break is taken after a `/`, so a continuation line begins at a
	# directory boundary — and the file's own name, which is the part a
	# reader is looking for, is never split down the middle.
	#
	# Read out of the File column rather than off the end of a line: how
	# many continuation lines a path needs depends on how long the
	# temporary directory's name happens to be, and an assertion that
	# depended on that would pass or fail for reasons having nothing to do
	# with the break rule.
	local section width cell
	section="$(sed -n '/^Functions [(]/,$p' <<<"$output")"
	width="$(awk '/^  -+ /{ print length($1); exit }' <<<"$section")"
	[ -n "$width" ]

	# The first line of the first wrapped row, cut to the File column.
	cell="$(grep -m1 -E '^  /' <<<"$section" |
		cut -c3-$((2 + width)) | sed 's/ *$//')"
	[ -n "$cell" ]
	case "$cell" in
	*/)	;;
	*)	echo "the File cell broke mid-segment: '$cell'" >&2; false ;;
	esac

	# And nothing is lost in the breaking: the File column, read down and
	# rejoined with its padding removed, is the path exactly.
	#
	# This used to assert that the substring "src/a.c:" appeared somewhere
	# in the output, on the grounds that the last segment should survive
	# intact. That depended on where the breaks happened to fall, which
	# depends on both the width of the table and the length of the
	# temporary directory — the very fragility the comment above warns
	# about, and it duly broke when a column was removed. Reassembling the
	# column asserts the property that was actually meant, and asserts it
	# whatever the width.
	local rejoined
	rejoined="$(sed -n '/^Functions [(]/,$p' <<<"$output" |
		sed -n '4,$p' |
		cut -c3-$((2 + width)) |
		sed 's/ *$//' |
		tr -d '\n')"
	case "$rejoined" in
	*"$TREE/a.c:"*)	;;
	*)	echo "the File column did not reassemble to the path" >&2
		echo "  wanted to find: $TREE/a.c:" >&2
		echo "  rejoined:       $rejoined" >&2
		false ;;
	esac
}

@test "HLR-219: a numeric column is never wrapped" {
	# Every heading of the function table's numeric columns intact on one
	# line: a number split across two lines is not a number, and a heading
	# split across two is the visible symptom.
	on_a_terminal "$TREE"
	assert_success
	assert_output --regexp "Lines +ELOC +CC +In +Out +WTBI"
}

@test "HLR-219: a cell with no separator in it is broken hard" {
	# Every other cell in this report breaks at a `/` or a space. A C
	# identifier has neither, so this is the branch that ships untested if
	# only real output is looked at — and the one that would split a
	# multi-byte character if it were written carelessly.
	local name
	name="$(printf 'a%.0s' $(seq 1 120))"
	printf 'int %s(void)\n{\n\treturn 0;\n}\n' "$name" > "$TREE/long.c"

	on_a_terminal "$TREE"
	assert_success

	# Present, and not on one line: the name is continued rather than cut.
	assert_output --partial "aaaaaaaaaa"
	refute_output --partial "$name"
}

# --- the frame (HLR-236) ---------------------------------------------------

@test "HLR-236: the diagnostics are headed and ruled" {
	# A file the runtime has no module for, so the run has something to
	# notify. A clean run is the case the test below covers, and the two
	# together are what make the heading conditional rather than absent.
	printf 'notes\n' > "$TREE/notes.md"

	elc "$TREE"
	assert_success
	assert_output --partial "Parsing Notifications"
	assert_output --partial "elc: $TREE/notes.md: no usable language module"

	# The rule is 128 columns, matching the width the table is held to on a
	# terminal, so the two banners of a run read as a pair (HLR-219).
	local rule
	rule="$(printf -- '-%.0s' $(seq 1 128))"
	assert_output --partial "$rule"
}

@test "HLR-236: a run with nothing to notify prints no heading" {
	# The converse, and what keeps the test above from passing against a
	# renderer that always prints the banner. HLR-188 refuses a heading over
	# an empty table for the same reason.
	elc "$TREE"
	assert_success
	refute_output --partial "Parsing Notifications"
}

@test "HLR-236: the report is headed and ruled beneath the notifications" {
	printf 'notes\n' > "$TREE/notes.md"

	on_a_terminal "$TREE"
	assert_success

	# Notifications, a blank line, then the report's own banner. Read as an
	# ordered sequence rather than as three separate presences, because the
	# order is the whole of what the frame is.
	local framed
	framed="$(grep -nE '^(Parsing Notifications|Project Summary|-{128}$)$' \
		<<<"$output" | cut -d: -f2 | tr '\n' '|')"
	assert_equal "$framed" \
		"Parsing Notifications|$(printf -- '-%.0s' $(seq 1 128))|Project Summary|$(printf -- '-%.0s' $(seq 1 128))|"

	# And a blank line between the two blocks: the line above the report's
	# banner is empty, which is what closes the diagnostic block.
	run awk '/^Project Summary$/ { print (prev == "") ? "blank" : prev; exit }
	         { prev = $0 }' <<<"$output"
	assert_output "blank"
}

@test "HLR-236: the banner is on stderr, so a redirected report opens clean" {
	# HLR-038's line, asserted where it now matters most: the banner heads
	# the diagnostics, and a report captured to a file must carry neither it
	# nor the blank line beneath it.
	printf 'notes\n' > "$TREE/notes.md"

	elc -o "$BATS_TEST_TMPDIR/report.txt" "$TREE"
	assert_success
	assert_output --partial "Parsing Notifications"

	run head -1 "$BATS_TEST_TMPDIR/report.txt"
	assert_output "Project Summary"
}

@test "HLR-236: Markdown is given no frame" {
	# The frame belongs to the aligned table: a saved document has no
	# terminal session to frame, and a banner on its stderr would be
	# decoration nobody asked for.
	printf 'notes\n' > "$TREE/notes.md"

	elc -f md "$TREE"
	assert_success
	refute_output --partial "Parsing Notifications"
	assert_output --partial "no usable language module"
}

@test "HLR-236: every word of a section title is capitalised" {
	elc --verbose "$TREE"
	assert_success

	# Read off the report rather than asserted as a list, so a section
	# added later is held to the rule without this test being edited. The
	# title is the part before any clause that follows it — parenthesised,
	# or set off by an em dash — which is prose either way.
	local offenders
	offenders="$(grep -E '^[A-Z]' <<<"$output" |
		sed -E 's/ [(—].*$//' |
		grep -E ' [a-z]' || true)"
	assert_equal "$offenders" ""
}

# --- the composition (HLR-218) ---------------------------------------------

# The section headings of a report, with the row counts of HLR-235 stripped,
# so a composition is compared as a list of tiers rather than of figures.
composition() {
	grep -E '^[A-Z]' <<<"$output" |
		grep -vE '^(Nothing To Report|Parsing Notifications)$' |
		sed -E 's/ \([0-9]+\)$//'
}

@test "HLR-218: the report a terminal gets is five sections" {
	on_a_terminal "$TREE"
	assert_success
	assert_equal "$(composition)" \
		"$(printf 'Project Summary\nFindings\nFiles\nGlobals\nFunctions')"
}

@test "HLR-218: the aligned table composes the same way into a file" {
	# The composition is the format's and not the destination's, which is
	# the opposite of the width above. `-o report.txt` selects the aligned
	# table (HLR-148), and it presents what the aligned table presents.
	elc -o "$BATS_TEST_TMPDIR/report.txt" "$TREE"
	assert_success
	run cat "$BATS_TEST_TMPDIR/report.txt"

	assert_equal "$(composition)" \
		"$(printf 'Project Summary\nFindings\nFiles\nGlobals\nFunctions')"
}

@test "HLR-218: Markdown drops the same tiers the terminal report drops" {
	# Markdown kept its own wider default until HLR-218 was rewritten.
	# Nothing is removed from the tool by that — every tier below is a
	# --verbose away in either format — but neither format has a default of
	# its own to keep them in any longer.
	elc -o "$BATS_TEST_TMPDIR/report.md" "$TREE"
	assert_success
	run cat "$BATS_TEST_TMPDIR/report.md"

	refute_output --partial "## Languages"
	refute_output --partial "## Discovery"
	refute_output --partial "## Callouts"
	refute_output --partial "## Global State"

	elc --verbose -o "$BATS_TEST_TMPDIR/verbose.md" "$TREE"
	assert_success
	run cat "$BATS_TEST_TMPDIR/verbose.md"
	assert_output --partial "## Languages"
	assert_output --partial "## Discovery"
	assert_output --partial "## Callouts"
}

@test "HLR-218: the two formats' defaults are the same five tiers exactly" {
	# Equality rather than intersection. An intersection would still pass
	# if one format quietly kept a fifth tier of its own, which is the
	# state HLR-218 was rewritten to end.
	elc -o "$BATS_TEST_TMPDIR/four.txt" "$TREE"
	assert_success
	run cat "$BATS_TEST_TMPDIR/four.txt"
	local table
	table="$(composition | sort)"

	elc -o "$BATS_TEST_TMPDIR/four.md" "$TREE"
	assert_success
	run cat "$BATS_TEST_TMPDIR/four.md"
	local markdown
	markdown="$(grep -E '^## ' <<<"$output" | sed 's/^## //' |
		sed -E 's/ \([0-9]+\)$//' |
		grep -v '^Nothing To Report$' | sort)"

	# Globals joined them in Phase 35: what state a project declares is a
	# fact about the source, so it is reported wherever the source was read
	# rather than only where a graph was built (HLR-242).
	assert_equal "$table" \
		"$(printf 'Files\nFindings\nFunctions\nGlobals\nProject Summary')"
	assert_equal "$markdown" "$table"
}

@test "HLR-218: --verbose restores every tier to the terminal report" {
	on_a_terminal "--verbose $TREE"
	assert_success
	assert_output --partial "Files"
	assert_output --partial "Languages"
	assert_output --partial "Discovery"
	assert_output --partial "Functions"
}

@test "HLR-218: a function's figures are the same in both compositions" {
	# The line HLR-218 does not cross: which tiers a format presents may
	# differ, what a tier says may not. Every figure the terminal reports
	# for `helper` is the figure the verbose Markdown reports.
	#
	# The Markdown row carries one column the terminal's does not — the
	# burden band, which the aligned table renders as the colour of the
	# figure beside it rather than as a word (HLR-227). That is a
	# difference of presentation and not of measurement, so it is excluded
	# here rather than asserted away.
	elc "$TREE"
	assert_success
	local terminal
	terminal="$(awk '/^Functions [(]/ { f = 1; next } f && /^$/ { f = 0 }
		f && $2 == "helper" {
		for (i = 2; i <= NF; i++) printf "%s ", $i; print "" }' \
		<<<"$output")"
	[ -n "$terminal" ]

	elc --verbose -f md "$TREE"
	assert_success
	local markdown
	# The Markdown name cell also carries the anchor a finding links to
	# (HLR-241), which is addressed to the renderer and is not one of the
	# figures being compared.
	markdown="$(awk -F'|' '/^## / { f = ($0 ~ /^## Functions( \([0-9]+\))?$/) }
		f && $3 ~ /(^| |>)helper *$/ {
		for (i = 3; i <= NF - 2; i++) {
			t = $i; gsub(/<[^>]*>/, "", t); printf "%s ", t
		}
		print "" }' <<<"$output")"
	[ -n "$markdown" ]

	# Same figures, whitespace normalised: the decoration differs and the
	# measurements do not.
	local a b
	a="$(tr -s ' ' <<<"$terminal" | sed 's/^ *//; s/ *$//')"
	b="$(tr -s ' ' <<<"$markdown" | sed 's/^ *//; s/ *$//')"
	assert_equal "$a" "$b"
}

# --- the version (HLR-220) -------------------------------------------------

@test "HLR-220: --version prints a version and exits 0" {
	elc --version
	assert_success
	assert_output --regexp "^elc [0-9]+\.[0-9]+\.[0-9]+$"
}

@test "HLR-220: --version answers before the rest of the line is validated" {
	# The state a user is in when they are asked what version they run is
	# often one where the command did not work.
	elc --version --format nonsense /nonexistent
	assert_success
	assert_output --regexp "^elc [0-9]+\.[0-9]+\.[0-9]+$"
}

# --- colour (HLR-226) ------------------------------------------------------
#
# `on_a_terminal` strips the escapes from `$output`, because every other test
# here is about the table rather than about how it is painted. These four read
# `$raw_output`, which is what was actually written.

@test "HLR-226: a terminal report alternates two backgrounds with white text" {
	on_a_terminal "$TREE"
	assert_success

	# Both grounds appear, and both carry the same foreground — the text
	# must not change weight from one row to the next.
	[[ "$raw_output" == *$'\e[40;97m'* ]] ||
		{ echo "no black-ground row" >&2; false; }
	[[ "$raw_output" == *$'\e[100;97m'* ]] ||
		{ echo "no grey-ground row" >&2; false; }

	# The first row of a body is the dark grey one. Read off the Functions
	# table, whose first row is the line after its rule.
	local first
	first="$(printf '%s\n' "$raw_output" |
		sed -n '/^Functions [(]/,$p' | sed -n '4p')"
	[[ "$first" == $'\e[100;97m'* ]] ||
		{ echo "the body did not begin with the dark ground" >&2
		  printf '%s\n' "$first" | cat -v >&2; false; }
}

@test "HLR-226: every line of a coloured row is the full width" {
	# A wrapped row's continuation lines carry cells that have run out, and
	# a line that stopped at the last one with anything left in it would
	# stop its background there too — a ragged staircase down the right of
	# the table. Every line of the body is the same displayed width.
	on_a_terminal "$TREE"
	assert_success

	local widths
	widths="$(printf '%s\n' "$output" |
		sed -n '/^Functions [(]/,/^$/p' | sed -n '4,$p' |
		awk 'NF { print length($0) }' | sort -u | wc -l)"
	assert_equal "$widths" "1"
}

@test "HLR-226: a redirected report carries no escape sequence at all" {
	# The property every consumer of these bytes depends on, and the one
	# that keeps every other test in the suite reading what it always read.
	elc "$TREE"
	assert_success
	refute_output --partial $'\e['

	run bash -c '"$0" -f csv "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success
	refute_output --partial $'\e['
}

@test "HLR-226: a band name is coloured by what it says" {
	on_a_terminal "$TREE"
	assert_success

	# busy() is over the complexity band, so the report carries a warning
	# severity; every function carries a burden word, and on this tree they
	# are all healthy. Two of the three bands, which is what this fixture
	# can show — the third is asserted in the unit tests, where a band can
	# be chosen rather than provoked.
	[[ "$raw_output" == *$'\e[93mwarning'* ]] ||
		{ echo "warning was not yellow" >&2; false; }
	# The healthy band is no longer a word in this table: it is the colour
	# of the weighted-burden figure, which is the one cell whose band the
	# aligned table shows without spelling it (HLR-227).
	[[ "$raw_output" =~ $'\e'\[92m[[:space:]]*[0-9] ]] ||
		{ echo "a healthy figure was not green" >&2; false; }
}

@test "HLR-226: colour says nothing the text does not" {
	# The words survive the escapes being removed, which is what makes the
	# colour an aid rather than the only way to read the report.
	on_a_terminal "$TREE"
	assert_success
	assert_output --partial "warning"
	refute_output --partial $'\e['
}

@test "HLR-227: the band the table colours is a word in every plain format" {
	# The one band the aligned table does not spell out, so the formats
	# with no colour to carry it must. Losing it in all three at once is
	# the regression this guards: a reader of a redirected report, of the
	# Markdown, or of the CSV would have no band at all.
	on_a_terminal "$TREE"
	assert_success
	refute_output --partial "healthy"

	elc --verbose -f md "$TREE"
	assert_success
	assert_output --partial "healthy"

	elc -f csv "$TREE"
	assert_success
	assert_output --partial "healthy"
}

@test "HLR-226: Markdown stays plain even on a terminal" {
	# Colour reaches the aligned table and nothing else. Markdown has no
	# styling of its own, so a coloured one would have to be HTML carrying
	# inline styles — which would stop it being the GitHub-Flavored
	# Markdown HLR-029 requires, and would render uncoloured on GitHub
	# anyway. Asserted on a pty, because the destination is exactly what
	# would have made the difference had the rule been the table's.
	on_a_terminal -f md "$TREE"
	assert_success
	[[ "$raw_output" != *$'\e['* ]] ||
		{ echo "Markdown carried an escape sequence" >&2; false; }
	# And it is still a pipe table rather than an HTML one.
	assert_output --partial "| File "
}
