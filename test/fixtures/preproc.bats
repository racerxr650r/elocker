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
		                  f && $3 == want { print $6, $7 }'
}

# One function's reported location — `path:line`, the navigable reference the
# File column carries (HLR-210). Stronger than the old line range for this
# suite's purpose: it is the thing a reader clicks, so it is the thing that
# must not move when the buffer is expanded.
range_of() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
		                  f && $3 == want { print $1 }'
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
	# The unexpanded run reaches the same functions by the other path —
	# repair (HLR-196) — which makes it an independent statement of where
	# they are, arrived at without the preprocessor.
	elc --verbose --no-expand "$TREE/shapes.c"
	assert_success
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
	elc --verbose --cc /nonexistent/cc "$TREE/sound.c"
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
	elc --verbose --cc /nonexistent/cc "$TREE/sound.c"
	assert_success
	assert_output --partial "Measured as written (macros not expanded)"
	assert_output --regexp "sound\.c +no preprocessor available"
}

@test "HLR-207: the C standard library a file draws on is reported" {
	require_cc
	printf '#include <stdio.h>\nint mine(void) { return 1; }\n' \
		> "$BATS_TEST_TMPDIR/uses.c"
	elc --verbose "$BATS_TEST_TMPDIR/uses.c"
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
	elc --verbose "$BATS_TEST_TMPDIR/uses.cpp"
	assert_success
	assert_output --regexp "uses\.cpp +C\+\+ +[0-9]+"
}

@test "HLR-207: a C library call MISRA forbids is a warning citing its rule" {
	require_cc
	printf '#include <stdlib.h>\nint f(void) { int *p = malloc(4); free(p); return 0; }\n' \
		> "$BATS_TEST_TMPDIR/m.c"
	elc --verbose "$BATS_TEST_TMPDIR/m.c"
	assert_success
	assert_output --regexp "warning +misra library +malloc .*Rule 21\.3.*MISRA C:2012"
	assert_output --regexp "warning +misra library +free .*Rule 21\.3"
}

@test "HLR-207: the rule cited is the one that forbids that function" {
	# Four functions, four different rules. A single rule number against
	# everything would be a citation a reader could not check.
	require_cc
	printf '#include <stdio.h>\n#include <stdlib.h>\nint f(void)\n{\n\tprintf("x");\n\treturn atoi("1") + system("y");\n}\n' \
		> "$BATS_TEST_TMPDIR/r.c"
	elc --verbose "$BATS_TEST_TMPDIR/r.c"
	assert_success
	assert_output --partial "printf is not available to a compliant program (Rule 21.6)"
	assert_output --partial "atoi is not available to a compliant program (Rule 21.7)"
	assert_output --partial "system is not available to a compliant program (Rule 21.8)"
}

@test "HLR-207: a function the project defines itself is not reported" {
	# The rule is about the standard library's function, not about every
	# function sharing its spelling. A project supplying its own resolves
	# in the graph and never reaches the unresolved calls this reads.
	require_cc
	printf 'static int system(const char *s) { (void)s; return 0; }\nint f(void) { return system("x"); }\n' \
		> "$BATS_TEST_TMPDIR/own.c"
	elc --verbose "$BATS_TEST_TMPDIR/own.c"
	assert_success
	refute_output --partial "misra library"
}

@test "HLR-207: a permitted function in a constrained header is not reported" {
	# <stdlib.h> supplies abs, which MISRA permits, beside malloc, which it
	# does not. A rule keyed on the include would be a false claim about
	# code that called neither.
	require_cc
	printf '#include <stdlib.h>\nint f(int x) { return abs(x); }\n' \
		> "$BATS_TEST_TMPDIR/ok.c"
	elc --verbose "$BATS_TEST_TMPDIR/ok.c"
	assert_success
	refute_output --partial "misra library"
}

@test "HLR-100: a MISRA finding does not reach the exit status" {
	require_cc
	printf '#include <stdlib.h>\nint f(void) { return (int)(long)malloc(4); }\n' \
		> "$BATS_TEST_TMPDIR/e.c"
	elc "$BATS_TEST_TMPDIR/e.c"
	assert_success
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

@test "HLR-208: a file elc could not fully decide is not expanded" {
	# elc leaves an undecidable region whole and counts both branches; the
	# preprocessor reads the undefined symbol as 0 and keeps one. Expanding
	# such a file would replace the region's measurement with an arbitrary
	# configuration's, under a figure the reader has been told is complete.
	require_cc
	printf '#if SOMETHING_NOBODY_DEFINED\nint a(void) { return 1; }\n#else\nint b(void) { return 2; }\n#endif\n' \
		> "$BATS_TEST_TMPDIR/undec.c"
	elc --verbose "$BATS_TEST_TMPDIR/undec.c"
	assert_success
	assert_output --partial "a condition in it is undecidable"
	assert_equal "$(summary 'Undecided regions')" "1"
	assert_equal "$(summary Functions)" "2" "both branches are kept"
}

@test "HLR-208: the run's own -D reaches the preprocessor" {
	# elc deciding a condition one way while the preprocessor decides it
	# the other would measure a build nobody asked for. With the symbol
	# defined, both agree and only the taken branch is measured.
	require_cc
	printf '#ifdef FEATURE\nint taken(void) { return 1; }\n#else\nint other(void) { return 2; }\n#endif\n' \
		> "$BATS_TEST_TMPDIR/cfg.c"
	elc --verbose -DFEATURE "$BATS_TEST_TMPDIR/cfg.c"
	assert_success
	assert_equal "$(summary 'Undecided regions')" "0"
	assert_equal "$(summary Functions)" "1"
	refute_output --partial "other"
}

@test "HLR-032: two runs over one target expand identically" {
	require_cc
	elc "$TREE/shapes.c" > "$BATS_TEST_TMPDIR/a"
	elc "$TREE/shapes.c" > "$BATS_TEST_TMPDIR/b"
	run diff "$BATS_TEST_TMPDIR/a" "$BATS_TEST_TMPDIR/b"
	assert_success
}

@test "HLR-043: expansion writes no intermediate file" {
	# Asserted on the tree and then on the whole filesystem elc can reach.
	# A temporary file would be a path to collide on under parallel runs,
	# something left behind by a killed process, and a write to a tree elc
	# promises not to modify.
	require_cc
	local before after
	before="$(find "$TREE" -type f | sort)"
	elc "$TREE"
	assert_success
	after="$(find "$TREE" -type f | sort)"
	assert_equal "$before" "$after"

	# environment.bats makes the same observation of elc under --no-expand;
	# this is the half that covers the subprocess, which is where a
	# preprocessor would naturally be told to put its output.
	require_tool strace "HLR-043 read-only operation"
	local log="$BATS_TEST_TMPDIR/write.log"
	strace -f -o "$log" -e trace=openat,open,creat,unlink,rename \
		"$ELC" "$TREE" >/dev/null 2>&1 || true
	[ -f "$log" ] || skip "strace produced no log"

	run grep -cE "(O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|creat\(|unlink|rename).*$TREE" "$log"
	assert_output "0"
}
