#!/usr/bin/env bats
# test/integration/language.bats — automatic language detection and the
# function report, black box.
#
# Integration tests drive build/elc as a user would and trace to HLRs. What
# counts as a function in C is the query file's decision and is asserted at
# the fixture level; this level asserts the command-line contract around it —
# what appears in the report, what is skipped, and what the status says.

setup() {
	load "../helpers/common"

	TREE="$BATS_TEST_TMPDIR/tree"
	mkdir -p "$TREE"

	printf 'int first(void)\n{\n\treturn 0;\n}\n\nint second(int n)\n{\n\treturn n;\n}\n' \
		> "$TREE/pair.c"
	printf '#ifndef H\n#define H\nstatic inline int in_header(void) { return 1; }\n#endif\n' \
		> "$TREE/header.h"
}

# --- automatic detection (HLR-007, HLR-008) --------------------------------

@test "HLR-007: the language is determined from the extension, unprompted" {
	elc "$TREE/pair.c"
	assert_success
	assert_output --regexp "pair\.c +c +"
}

@test "HLR-007: a header is detected as well as a source file" {
	elc "$TREE/header.h"
	assert_success
	assert_output --partial "in_header"
}

@test "HLR-008: files sharing a language are analysed in one invocation" {
	elc "$TREE"
	assert_success
	assert_output --partial "first"
	assert_output --partial "second"
	assert_output --partial "in_header"
	assert_output --regexp "Functions +3"
}

# --- function identity (HLR-014) -------------------------------------------

@test "HLR-014: each function is reported with its name and line range" {
	elc "$TREE/pair.c"
	assert_success
	# first() spans lines 1-4, second() lines 6-9.
	assert_output --regexp "first +1-4"
	assert_output --regexp "second +6-9"
}

@test "HLR-014: the line range starts at the signature, not the brace" {
	printf 'int sig(void)\n{\n\treturn 0;\n}\n' > "$TREE/sig.c"
	elc "$TREE/sig.c"
	assert_success
	assert_output --regexp "sig +1-4"
}

@test "HLR-033: functions are presented in start-line order" {
	printf 'int zeta(void) { return 0; }\nint alpha(void) { return 0; }\n' \
		> "$TREE/order.c"
	elc "$TREE/order.c"
	assert_success

	local names
	names="$(awk '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	              f && /^  \// { print $2 }' <<<"$output")"
	assert_equal "$names" "zeta
alpha"
}

# --- unsupported languages (HLR-012, HLR-037) ------------------------------

@test "HLR-012: a file with no language module is listed as skipped" {
	printf '# not source\n' > "$TREE/notes.md"
	elc "$TREE"
	assert_success
	assert_output --partial "Skipped files"
	assert_output --partial "notes.md"
}

@test "HLR-012: a skipped file is also reported on stderr" {
	printf '# not source\n' > "$TREE/notes.md"
	run bash -c '"$0" "$1" 2>&1 >/dev/null' "$ELC" "$TREE"
	assert_output --partial "notes.md"
}

@test "HLR-037: a skip does not make the exit status non-zero" {
	printf '# not source\n' > "$TREE/notes.md"
	elc "$TREE"
	assert_equal "$status" 0
}

@test "HLR-012: a skipped file does not contribute to the totals" {
	elc "$TREE"
	local without="$output"

	printf '# not source\n' > "$TREE/notes.md"
	elc "$TREE"

	assert_equal "$(grep -E '^  (Files|Physical lines|Functions) ' <<<"$output")" \
	             "$(grep -E '^  (Files|Physical lines|Functions) ' <<<"$without")"
}

# --- parse failure (HLR-035, HLR-120) --------------------------------------

@test "HLR-035: a file that fails to parse does not abort the run" {
	printf 'this is not C at all (((\n' > "$TREE/broken.c"
	elc "$TREE"
	assert_output --partial "first" \
		"the report must still cover the files that parsed"
}

@test "HLR-120: a parse failure degrades the run to 1" {
	printf 'this is not C at all (((\n' > "$TREE/broken.c"
	elc "$TREE"
	assert_equal "$status" 1
}

@test "HLR-035: a parse failure names the file on stderr" {
	printf 'this is not C at all (((\n' > "$TREE/broken.c"
	run bash -c '"$0" "$1" 2>&1 >/dev/null' "$ELC" "$TREE"
	assert_output --partial "broken.c"
}

@test "HLR-035: a file with a syntax error is skipped whole, not partially reported" {
	# The file defines a function before the error. Reporting it from a
	# damaged tree is what HLR-035 forbids.
	printf 'int sound(void) { return 0; }\nint broken(void) { ((( \n' \
		> "$TREE/half.c"
	run bash -c '"$0" "$1" 2>/dev/null' "$ELC" "$TREE/half.c"
	refute_output --partial "sound"
}

# --- the report (HLR-006, HLR-019) -----------------------------------------

@test "HLR-019: each file reports its own line and function counts" {
	elc "$TREE/pair.c"
	assert_success
	assert_output --regexp "pair\.c +c +9 +2"
}

@test "HLR-066: a target of only skipped files still reports zero totals" {
	local only="$BATS_TEST_TMPDIR/only"
	mkdir -p "$only"
	printf '# not source\n' > "$only/notes.md"

	elc "$only"
	assert_success
	assert_output --regexp "Files +0"
	assert_output --regexp "Functions +0"
	assert_output --partial "notes.md"
}

@test "HLR-006: the report has the same sections whatever the target type" {
	elc "$TREE/pair.c"
	local file_shape
	file_shape="$(grep -E '^[A-Z]' <<<"$output")"

	elc "$TREE"
	assert_equal "$(grep -E '^[A-Z]' <<<"$output")" "$file_shape"
}

# --- ELOC in the report (HLR-015, HLR-019, HLR-024, HLR-025) ---------------

@test "HLR-015: each function reports its own ELOC" {
	printf 'int f(void)\n{\n\tint n = 1;\n\treturn n;\n}\n' > "$TREE/one.c"
	elc "$TREE/one.c"
	assert_success
	assert_output --regexp "f +1-5 +2"
}

@test "HLR-024: the project summary carries a combined ELOC total" {
	elc "$TREE"
	assert_success
	# pair.c is two functions of one `return` each; header.h is one more.
	assert_equal "$(awk '/^  ELOC/ { print $2 }' <<<"$output")" "3"
}

@test "HLR-025: the totals are broken down by language" {
	elc "$TREE"
	assert_success
	assert_output --partial "Languages"
	assert_equal "$(awk '/^Languages$/ { f = 1; next } f && /^  c / { print $1 }' <<<"$output")" "c"
}

@test "HLR-025: the per-language totals sum to the project totals" {
	# One language present, so its row must equal the summary exactly.
	elc "$TREE"
	assert_success

	local summary_lines summary_eloc row_lines row_eloc
	summary_lines="$(awk '/^  Physical lines/ { print $3 }' <<<"$output")"
	summary_eloc="$(awk '/^  ELOC/ { print $2 }' <<<"$output")"
	row_lines="$(awk '/^  c +/ { print $3 }' <<<"$output")"
	row_eloc="$(awk '/^  c +/ { print $4 }' <<<"$output")"

	assert_equal "$row_lines" "$summary_lines"
	assert_equal "$row_eloc" "$summary_eloc"
}

@test "HLR-019: a header of declarations only reports zero ELOC" {
	elc "$TREE/header.h"
	assert_success
	# The header defines one inline function with a single return.
	assert_output --regexp "header\.h +c +4 +1"
}

# --- determinism over the new sections (HLR-032) ---------------------------

@test "HLR-032: two runs over a parsed tree are byte-identical" {
	run bash -c '"$0" "$1" 2>/dev/null' "$ELC" "$TREE"
	local first="$output"
	run bash -c '"$0" "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_equal "$output" "$first"
}
