#!/usr/bin/env bats
# test/fixtures/comments.bats — the cases no textual approach survives (STP §5).
#
# This is where HLR-013 is verified: the requirement has no observable of its
# own, so the check is source on which every textual approach gives a
# different answer from the parser's. The reasoning is in comments/README.md.

setup() {
	load "../helpers/common"
	SUBJECT="$BATS_TEST_DIRNAME/comments/adversarial.c"
}

# Verbose: the per-function tier this reads is omitted by default.
function_eloc() {
	elc --verbose "$SUBJECT"
	awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	                  f && $2 == want { print $4 }' <<<"$output"
}

@test "the hand-counted comment totals match" {
	elc "$SUBJECT"
	assert_success
	assert_output --regexp "Physical lines +23"
	assert_output --regexp "ELOC +4"
	assert_output --regexp "Functions +3"
}

@test "HLR-013: a block-comment opener inside a string opens nothing" {
	# A textual matcher would swallow the rest of the file from line 6.
	assert_equal "$(function_eloc block_opener_in_string)" "1"
}

@test "HLR-013: a line-comment opener inside a string discards nothing" {
	assert_equal "$(function_eloc line_opener_in_string)" "1"
}

@test "HLR-013: a quote inside a comment opens no string" {
	# Line 16 carries an unbalanced quote inside a comment. A matcher that
	# tracked strings textually would mis-read everything after it.
	assert_equal "$(function_eloc quote_in_comment)" "2"
}

@test "HLR-016: a comment sharing a line with code does not remove that line" {
	# Line 20 is `int n = 1;` with a trailing comment. Excluding by line
	# rather than by byte deletes the statement — which it once did.
	local trailing="$BATS_TEST_TMPDIR/trailing.c"
	printf 'int f(void)\n{\n\tint n = 1;   /* note */\n\treturn n;   /* note */\n}\n' \
		> "$trailing"
	elc "$trailing"
	assert_success
	assert_output --regexp "ELOC +2"
}

@test "HLR-016: inline syntax inside a block comment excludes no line twice" {
	# Three comment-like openers inside one block comment. Subtracting per
	# capture would count lines 2-5 more than once and drive ELOC below the
	# single statement that follows.
	local nested="$BATS_TEST_TMPDIR/nested-syntax.c"
	printf 'int f(void)\n{\n\t/* one // two\n\t   three /* four\n\t*/\n\treturn 0;\n}\n' \
		> "$nested"
	elc "$nested"
	assert_success
	assert_output --regexp "ELOC +1"
}

@test "HLR-020: a file of only comments reports zero ELOC" {
	local only="$BATS_TEST_TMPDIR/only-comments.c"
	printf '/* a comment\n   over several\n   lines */\n// and one more\n' > "$only"
	elc "$only"
	assert_success
	assert_output --regexp "ELOC +0"
}
