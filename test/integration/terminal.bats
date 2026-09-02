#!/usr/bin/env bats
# test/integration/terminal.bats — the report a terminal gets (HLR-218 – HLR-220).
#
# Two properties, decided by two different things, and the split is the point:
#
#   * **What the aligned table presents** is a property of the *format*
#     (HLR-218) and is the same wherever it is written. It is asserted through
#     ordinary runs, here and in verbosity.bats.
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
	printf 'int helper(int n)\n{\n\tif (n)\n\t\treturn 1;\n\treturn 0;\n}\n\nint main(void)\n{\n\treturn helper(1);\n}\n' \
		> "$TREE/a.c"

	# One function over a published band, so the Findings tier has a row.
	# A report with nothing to report would let the composition tests below
	# pass against a renderer that had dropped the findings entirely, which
	# is the one section of the three a reader is expected to act on.
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
	section="$(sed -n '/^Functions$/,$p' <<<"$output")"
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
	rejoined="$(sed -n '/^Functions$/,$p' <<<"$output" |
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
	assert_output --regexp "Lines +ELOC +Complexity +Fan-in +Fan-out +MBS"
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

# --- the composition (HLR-218) ---------------------------------------------

@test "HLR-218: the report a terminal gets is three sections" {
	on_a_terminal "$TREE"
	assert_success

	local sections
	sections="$(grep -E '^[A-Z]' <<<"$output" | grep -v '^Nothing to report$')"
	assert_equal "$sections" "$(printf 'Project summary\nFindings\nFunctions')"
}

@test "HLR-218: the aligned table composes the same way into a file" {
	# The composition is the format's and not the destination's, which is
	# the opposite of the width above. `-o report.txt` selects the aligned
	# table (HLR-148), and it presents what the aligned table presents.
	elc -o "$BATS_TEST_TMPDIR/report.txt" "$TREE"
	assert_success
	run cat "$BATS_TEST_TMPDIR/report.txt"

	local sections
	sections="$(grep -E '^[A-Z]' <<<"$output" | grep -v '^Nothing to report$')"
	assert_equal "$sections" "$(printf 'Project summary\nFindings\nFunctions')"
}

@test "HLR-218: Markdown keeps the tiers the terminal report drops" {
	# Nothing is removed from the tool, only from one format's default.
	elc -o "$BATS_TEST_TMPDIR/report.md" "$TREE"
	assert_success
	run cat "$BATS_TEST_TMPDIR/report.md"

	assert_output --partial "## Files"
	assert_output --partial "## Languages"
	assert_output --partial "## Discovery"
	refute_output --partial "## Functions"
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
	# differ, what a tier says may not. The Functions row for `helper` is
	# the same row in the terminal report and in the verbose Markdown one.
	elc "$TREE"
	assert_success
	local terminal
	terminal="$(awk '$3 == "helper" { $1 = ""; print }' <<<"$output")"
	[ -n "$terminal" ]

	elc --verbose -f md "$TREE"
	assert_success
	local markdown
	markdown="$(awk -F'|' '$4 ~ /^ *helper *$/ {
		for (i = 5; i <= NF - 1; i++) printf "%s ", $i; print "" }' \
		<<<"$output")"
	[ -n "$markdown" ]

	# Same figures, whitespace normalised: the decoration differs and the
	# measurements do not.
	local a b
	a="$(tr -s ' ' <<<"$terminal" | sed 's/^ *//; s/ *$//' | cut -d' ' -f3-)"
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
