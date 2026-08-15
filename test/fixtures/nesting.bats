#!/usr/bin/env bats
# test/fixtures/nesting.bats — each statement to exactly one function (STP §5).
#
# Expected values are hand-counted in nesting/README.md beside this file.

setup() {
	load "../helpers/common"
	SUBJECT="$BATS_TEST_DIRNAME/nesting/nested.c"
}

function_eloc() {
	elc "$SUBJECT"
	awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	                  f && $2 == want { print $4 }' <<<"$output"
}

@test "the hand-counted nesting totals match" {
	elc "$SUBJECT"
	assert_success
	assert_output --regexp "Physical lines +20"
	assert_output --regexp "ELOC +5"
	assert_output --regexp "Functions +3"
}

@test "HLR-068: the innermost function owns its own statements" {
	assert_equal "$(function_eloc inner)" "1"
	assert_equal "$(function_eloc middle)" "1"
}

@test "HLR-068: an enclosing function gains none of the nested one's lines" {
	# Attributing to the outermost enclosing function would report 5 here,
	# and attributing to every enclosing function would report 5, 2, 1.
	assert_equal "$(function_eloc outer)" "3"
}

@test "HLR-067: all three functions are reported in their own right" {
	elc "$SUBJECT"
	assert_success
	assert_output --partial "outer"
	assert_output --partial "middle"
	assert_output --partial "inner"
}

@test "HLR-019: the file counts each statement line once" {
	# Five statements on five distinct lines, however they are attributed.
	elc "$SUBJECT"
	assert_output --regexp "nested\.c +c +20 +5"
}

@test "HLR-032: nested attribution is deterministic across runs" {
	run bash -c '"$0" "$1" 2>/dev/null' "$ELC" "$SUBJECT"
	local first="$output"
	run bash -c '"$0" "$1" 2>/dev/null' "$ELC" "$SUBJECT"
	assert_equal "$output" "$first"
}
