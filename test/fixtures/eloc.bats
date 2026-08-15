#!/usr/bin/env bats
# test/fixtures/eloc.bats — one instance of each ELOC category (STP §5).
#
# Expected values are hand-counted, line by line, in eloc/README.md beside
# this file. Never regenerate them from elc's output.

setup() {
	load "../helpers/common"
	SUBJECT="$BATS_TEST_DIRNAME/eloc/categories.c"
}

# The ELOC figure elc reports for a named function.
function_eloc() {
	elc "$SUBJECT"
	awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	                  f && $2 == want { print $4 }' <<<"$output"
}

@test "the hand-counted category totals match" {
	elc "$SUBJECT"
	assert_success
	assert_output --regexp "Physical lines +40"
	assert_output --regexp "ELOC +19"
	assert_output --regexp "Functions +1"
}

@test "HLR-015: the function's ELOC excludes the file-scope statement" {
	assert_equal "$(function_eloc categories)" "18"
}

@test "HLR-019: the file's ELOC includes code outside any function" {
	elc "$SUBJECT"
	# 18 inside categories(), plus the initialised global on line 8.
	assert_output --regexp "categories\.c +c +40 +19"
}

@test "HLR-049 – HLR-052: blanks, braces, bare declarations and directives are excluded" {
	# Were any of those counted, ELOC would exceed the hand count: the file
	# holds 2 directives, 3 bare declarations, 5 lone braces and 6 blanks.
	elc "$SUBJECT"
	assert_output --regexp "ELOC +19"
}

@test "HLR-044: an assignment or operation counts" {
	local f="$BATS_TEST_TMPDIR/assign.c"
	# An initialising declaration, a plain assignment, and an operation.
	printf 'int f(int n)\n{\n\tint a = 1;\n\ta = n;\n\ta += n;\n\treturn a;\n}\n' > "$f"
	elc "$f"
	assert_success
	assert_output --regexp "ELOC +4"
}

@test "HLR-046: a call counts whether or not its result is used" {
	local f="$BATS_TEST_TMPDIR/call.c"
	printf 'void g(void);\nint h(void);\nvoid f(void)\n{\n\tg();\n\th();\n}\n' > "$f"
	elc "$f"
	assert_success
	# The two prototypes declare and do nothing; the two calls each count.
	assert_output --regexp "ELOC +2"
}

@test "HLR-047: a return counts, with or without a value" {
	local f="$BATS_TEST_TMPDIR/ret.c"
	printf 'void bare(void)\n{\n\treturn;\n}\n\nint valued(void)\n{\n\treturn 0;\n}\n' > "$f"
	elc "$f"
	assert_success
	assert_output --regexp "ELOC +2"
}

@test "HLR-045: else-if on one line counts once" {
	# Line 19 is captured as both an else and an if. ELOC counts lines.
	local split="$BATS_TEST_TMPDIR/split.c"
	printf 'int f(int n)\n{\n\tif (n)\n\t\treturn 1;\n\telse if (n > 1)\n\t\treturn 2;\n\treturn 0;\n}\n' \
		> "$split"
	elc "$split"
	assert_success
	# if(3), return(4), else-if(5), return(6), return(7) = 5
	assert_output --regexp "ELOC +5"
}

@test "HLR-020: a file with nothing executable reports zero, without error" {
	local quiet="$BATS_TEST_TMPDIR/quiet.c"
	printf '/* nothing but this */\n#include <stddef.h>\nint declared;\nint prototype(void);\n' \
		> "$quiet"
	elc "$quiet"
	assert_success
	assert_output --regexp "ELOC +0"
}
