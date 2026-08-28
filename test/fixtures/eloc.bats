#!/usr/bin/env bats
# test/fixtures/eloc.bats — one instance of each ELOC category (STP §5).
#
# Expected values are hand-counted, line by line, in eloc/README.md beside
# this file. Never regenerate them from elc's output.

setup() {
	load "../helpers/common"
	SUBJECT="$BATS_TEST_DIRNAME/eloc/categories.c"
	GROUP="$BATS_TEST_DIRNAME/eloc"
}

# The file-level and function-level figures elc reports for one fixture.
totals() {
	elc "$1"
	awk '/^Project summary/ { s = 1 } s && /^  ELOC/ { print $2; exit }' \
		<<<"$output"
}

# Verbose, because every extractor below reads the per-function tier, which
# the default composition omits.
subject_row() {
	elc --verbose "$1"
	awk -v want="$2" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	                  f && $3 == want { print $6, $7 }' <<<"$output"
}

# The ELOC figure elc reports for a named function.
function_eloc() {
	elc --verbose "$SUBJECT"
	awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	                  f && $3 == want { print $6 }' <<<"$output"
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

@test "HLR-017: the hand-counted complexity matches" {
	elc --verbose "$SUBJECT"
	assert_success
	# 1 + for + if + else-if's if + while + case 0 = 6. The switch itself,
	# the default label, and the goto are not decisions.
	assert_output --regexp "categories +public +30 +18 +6"
}

@test "HLR-017: a straight-line function is one" {
	local f="$BATS_TEST_TMPDIR/straight.c"
	printf 'int f(int n)\n{\n\tint a = n;\n\treturn a;\n}\n' > "$f"
	elc --verbose "$f"
	assert_success
	# Capturing the function itself as a decision point would report 2.
	assert_output --regexp "f +public +5 +2 +1"
}

@test "HLR-017: a short-circuit operator is a decision point" {
	local f="$BATS_TEST_TMPDIR/logic.c"
	printf 'int f(int a, int b)\n{\n\tif (a && b)\n\t\treturn 1;\n\treturn 0;\n}\n' > "$f"
	elc --verbose "$f"
	assert_success
	# 1 + the if + the && = 3.
	assert_output --regexp "f +public +6 +3 +3"
}

@test "HLR-017: a default label and a goto are not decisions" {
	local f="$BATS_TEST_TMPDIR/switch.c"
	printf 'int f(int n)\n{\n\tswitch (n) {\n\tcase 1:\n\t\tgoto done;\n\tdefault:\n\t\tbreak;\n\t}\ndone:\n\treturn 0;\n}\n' > "$f"
	elc --verbose "$f"
	assert_success
	# 1 + the single `case` = 2. The switch, the default, and the goto add
	# nothing.
	assert_output --regexp "f +public +11 +[0-9]+ +2"
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

# --- the other four languages (HLR-011, HLR-034) ---------------------------
#
# Each fixture holds one instance of every category the language has, counted
# line by line in eloc/README.md. The numbers differ between languages because
# the languages differ — a C `else` is a node and a Rust one is not — and the
# header states each difference rather than leaving it to be inferred.

@test "HLR-011: C++ matches its hand-counted categories" {
	assert_equal "$(totals "$GROUP/categories.cpp")" "25"
	assert_equal "$(subject_row "$GROUP/categories.cpp" categories)" "24 8"
}

@test "HLR-011: Python matches its hand-counted categories" {
	assert_equal "$(totals "$GROUP/categories.py")" "27"
	assert_equal "$(subject_row "$GROUP/categories.py" categories)" "25 8"
}

@test "HLR-011: Rust matches its hand-counted categories" {
	assert_equal "$(totals "$GROUP/categories.rs")" "19"
	assert_equal "$(subject_row "$GROUP/categories.rs" categories)" "18 10"
}

@test "HLR-048: C++ counts exception handling toward ELOC" {
	# The one ELOC category C cannot express. Removing try/catch/throw from
	# the fixture would drop four lines; this asserts they are counted.
	local f="$BATS_TEST_TMPDIR/exc.cpp"
	printf 'int f(int n)\n{\n\ttry {\n\t\tthrow n;\n\t} catch (int e) {\n\t\treturn e;\n\t}\n\treturn 0;\n}\n' > "$f"
	elc "$f"
	assert_success
	# try, throw, catch, return, return = 5
	assert_output --regexp "ELOC +5"
}

@test "HLR-048: a catch clause is a decision point" {
	local f="$BATS_TEST_TMPDIR/exc2.cpp"
	printf 'int f(int n)\n{\n\ttry {\n\t\tthrow n;\n\t} catch (int e) {\n\t\treturn e;\n\t}\n\treturn 0;\n}\n' > "$f"
	elc --verbose "$f"
	assert_success
	# 1 + the catch. `try` and `throw` choose nothing.
	assert_output --regexp "f +public +9 +5 +2"
}

@test "HLR-011: a language with no exception construct still reports" {
	# C++ has exception handling, C does not. Neither is an error.
	elc "$GROUP/categories.c"
	assert_success
}
