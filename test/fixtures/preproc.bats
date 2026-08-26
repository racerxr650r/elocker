#!/usr/bin/env bats
# test/fixtures/preproc.bats — expanding macros before parsing (STP §5).
#
# The expected values come from `expanded.c`, a hand-written equivalent of
# `shapes.c` with the macros already expanded. Comparing the two is what makes
# this a conformance test rather than a golden file: an expansion that measured
# something other than what the source means fails, whatever elc reports.
#
# No compiler is committed to, and a case whose compiler is unavailable skips
# explicitly, naming the requirement that thereby went unverified.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/preproc/tree"
}

# One function's ELOC and complexity from the one function table.
figures() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
		                  f && $2 == want { print $4, $5 }'
}

# One function's reported line range.
range_of() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
		                  f && $2 == want { print $3 }'
}

summary() {
	printf '%s\n' "$output" | awk -v want="$1" \
		'$0 ~ "^  " want "  *[0-9]+$" { print $NF }'
}

require_cc() {
	require_tool gcc "HLR-202 macro expansion"
	gcc -E -C "$TREE/sound.c" >/dev/null 2>&1 || \
		skip "gcc cannot preprocess here: HLR-202 unverified"
}

@test "HLR-202: a macro shape the grammar rejects parses once expanded" {
	require_cc
	elc --verbose "$TREE/shapes.c"
	assert_success
	assert_equal "$(summary 'Unparsed lines')" "0"
}

@test "HLR-202: an expanded file measures what the expanded source measures" {
	# branchy is `local int` in one and `static int` in the other; report
	# concatenates through macros in one and directly in the other. Both
	# pairs must measure identically.
	require_cc
	elc --verbose "$TREE/shapes.c"
	assert_success
	local got_branchy got_report
	got_branchy="$(figures branchy)"
	got_report="$(figures report)"

	elc --verbose "$TREE/expanded.c"
	assert_success
	assert_equal "$got_branchy" "$(figures branchy2)"
	assert_equal "$got_report" "$(figures report2)"
}

@test "HLR-204: expansion moves no reported location" {
	# The property that makes an expanded measurement usable rather than
	# merely obtainable. Asserted against the unexpanded run of the same
	# file, which is the only independent statement of where these
	# functions are.
	require_cc
	# The unexpanded run exits 1 because the file is partly unparsed —
	# which is the condition this phase removes, and is what makes this
	# run an independent statement of where the functions are.
	elc --verbose --no-expand "$TREE/shapes.c"
	assert_failure 1
	local raw_branchy raw_report
	raw_branchy="$(range_of branchy)"
	raw_report="$(range_of report)"
	[ -n "$raw_branchy" ] || fail "no unexpanded range to compare against"

	elc --verbose "$TREE/shapes.c"
	assert_success
	assert_equal "$(range_of branchy)" "$raw_branchy"
	assert_equal "$(range_of report)" "$raw_report"
}

@test "HLR-203: a project header contributes nothing to the file including it" {
	# local.h defines helper_from_header. shapes.c includes it, and must
	# gain no function, no line and no ELOC from it — the header is a file
	# in its own right, measured on its own account.
	require_cc
	elc --verbose "$TREE/shapes.c"
	assert_success
	refute_output --partial "helper_from_header"
}

@test "HLR-203: a system header contributes nothing either" {
	require_cc
	printf '#include <stdio.h>\nint mine(void) { return 1; }\n' \
		> "$BATS_TEST_TMPDIR/uses.c"
	elc --verbose "$BATS_TEST_TMPDIR/uses.c"
	assert_success
	assert_equal "$(summary Functions)" "1"
	assert_equal "$(summary ELOC)" "1"
}

@test "HLR-204: expansion changes no figure on source that already parsed" {
	# The invariant that makes expansion safe to leave on. sound.c needs no
	# expansion, so every figure must be what it was — including ELOC,
	# which would move if the comments the preprocessor strips by default
	# were not preserved and something later counted them.
	require_cc
	elc --verbose "$TREE/sound.c"
	assert_success
	local a
	a="$(printf '%s\n' "$output" | sed -n '/^Project summary/,/^$/p' |
	     grep -v 'Files expanded\|Measured as written')"

	elc --verbose --no-expand "$TREE/sound.c"
	assert_success
	assert_equal "$a" "$(printf '%s\n' "$output" |
	     sed -n '/^Project summary/,/^$/p' |
	     grep -v 'Files expanded\|Measured as written')"
}

@test "HLR-202: source the grammar accepts measures the same either way" {
	require_cc
	elc --verbose "$TREE/sound.c"
	assert_success
	local with
	with="$(figures add)"

	elc --verbose --no-expand "$TREE/sound.c"
	assert_success
	assert_equal "$with" "$(figures add)"
}

@test "HLR-205: a preprocessor that cannot be run falls back and completes" {
	elc --cc /nonexistent/cc "$TREE/sound.c"
	assert_success
	assert_output --partial "no preprocessor available"
}

@test "HLR-205: a header the preprocessor cannot find falls back" {
	# The cross-compiled case, which is the ordinary condition of a tree
	# analysed away from its build environment rather than an error.
	require_cc
	printf '#include <avr/io.h>\nint f(void) { return 1; }\n' \
		> "$BATS_TEST_TMPDIR/cross.c"
	elc --verbose "$BATS_TEST_TMPDIR/cross.c"
	assert_success
	assert_equal "$(summary Functions)" "1"
	assert_output --partial "the preprocessor rejected the file"
}

@test "HLR-205: --no-expand produces the report a build with no toolchain does" {
	require_cc
	elc --no-expand "$TREE/sound.c"
	assert_success
	assert_equal "$(summary 'Files expanded')" "0"
}

@test "HLR-206: the summary counts both ways a file may have been measured" {
	require_cc
	elc "$TREE/sound.c"
	assert_success
	assert_equal "$(summary 'Files expanded')" "1"
	assert_equal "$(summary 'Measured as written')" "0"
}

@test "HLR-206: a fallen-back file is named with its reason" {
	elc --cc /nonexistent/cc "$TREE/sound.c"
	assert_success
	assert_output --partial "Measured as written (macros not expanded)"
	assert_output --regexp "sound\.c +no preprocessor available"
}

@test "HLR-207: the C standard library a file draws on is reported" {
	require_cc
	printf '#include <stdio.h>\nint mine(void) { return 1; }\n' \
		> "$BATS_TEST_TMPDIR/uses.c"
	elc "$BATS_TEST_TMPDIR/uses.c"
	assert_success
	assert_output --partial "Standard-library dependence"
	assert_output --regexp "uses\.c +C +[0-9]+ +.*stdio\.h"
}

@test "HLR-207: the C++ standard library is distinguished from the C one" {
	require_tool g++ "HLR-207 standard-library reporting"
	printf '#include <vector>\n#include <cstdio>\nint mine(void) { return 1; }\n' \
		> "$BATS_TEST_TMPDIR/uses.cpp"
	g++ -E -C "$BATS_TEST_TMPDIR/uses.cpp" >/dev/null 2>&1 || \
		skip "g++ cannot preprocess here: HLR-207 unverified"
	elc "$BATS_TEST_TMPDIR/uses.cpp"
	assert_success
	assert_output --regexp "uses\.cpp +C\+\+ +[0-9]+"
}

@test "HLR-207: a file that fell back claims no dependence at all" {
	# Absence of an answer, not the answer "none". The provenance table is
	# what tells a reader which files could be asked.
	elc --cc /nonexistent/cc "$TREE/sound.c"
	assert_success
	refute_output --regexp "^Standard-library dependence"
}

@test "LLR-PRE-02: comments are preserved and no flag is invented" {
	# Asserted against the invocation itself rather than against a figure,
	# because both halves are about what elc asks for: a preprocessor
	# discards comments unless told not to, and an include path elc
	# invented would read a header the user never named (HLR-039).
	cat > "$BATS_TEST_TMPDIR/spy" <<-'SPY'
	#!/bin/sh
	printf '%s\n' "$@" > "$SPY_LOG"
	exec gcc "$@"
	SPY
	chmod +x "$BATS_TEST_TMPDIR/spy"
	SPY_LOG="$BATS_TEST_TMPDIR/args" \
		elc --cc "$BATS_TEST_TMPDIR/spy" "$TREE/sound.c"
	assert_success

	run cat "$BATS_TEST_TMPDIR/args"
	assert_line "-C"
	refute_output --regexp '^-I'
	refute_output --regexp '^-D'
}

@test "LLR-PRE-02: a flag the user supplies is forwarded" {
	# The other half of the same rule. elc invents nothing and forwards
	# what it is told, which is what lets a project whose headers are not
	# beside its sources be expanded at all.
	cat > "$BATS_TEST_TMPDIR/spy" <<-'SPY'
	#!/bin/sh
	printf '%s\n' "$@" > "$SPY_LOG"
	exec gcc "$@"
	SPY
	chmod +x "$BATS_TEST_TMPDIR/spy"
	SPY_LOG="$BATS_TEST_TMPDIR/args" \
		elc --cc "$BATS_TEST_TMPDIR/spy" --cc-flag -DWANTED=1 \
		    "$TREE/sound.c"
	assert_success

	run cat "$BATS_TEST_TMPDIR/args"
	assert_line "-DWANTED=1"
}

@test "HLR-032: two runs over one target expand identically" {
	require_cc
	elc "$TREE/shapes.c" > "$BATS_TEST_TMPDIR/a"
	elc "$TREE/shapes.c" > "$BATS_TEST_TMPDIR/b"
	run diff "$BATS_TEST_TMPDIR/a" "$BATS_TEST_TMPDIR/b"
	assert_success
}

@test "HLR-043: expansion writes no intermediate file" {
	require_cc
	local before after
	before="$(find "$TREE" -type f | sort)"
	elc "$TREE"
	assert_success
	after="$(find "$TREE" -type f | sort)"
	assert_equal "$before" "$after"
}
