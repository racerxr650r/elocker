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

function_complexity() {
	elc "$SUBJECT"
	awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	                  f && $2 == want { print $5 }' <<<"$output"
}

@test "the hand-counted nesting totals match" {
	elc "$SUBJECT"
	assert_success
	assert_output --regexp "Physical lines +23"
	assert_output --regexp "ELOC +8"
	assert_output --regexp "Functions +3"
}

@test "HLR-068: the innermost function owns its own statements" {
	assert_equal "$(function_eloc inner)" "1"
	assert_equal "$(function_eloc middle)" "3"
}

@test "HLR-068: the innermost function owns its own decision points" {
	# Running the query against each body without attribution would give
	# outer 5 and middle 3 — everything its nested functions branch on.
	assert_equal "$(function_complexity inner)" "2"
	assert_equal "$(function_complexity middle)" "2"
}

@test "HLR-017: complexity is one plus the decision points" {
	# outer branches twice on line 20: the `if`, and the `&&` that
	# short-circuits inside its condition.
	assert_equal "$(function_complexity outer)" "3"
}

@test "HLR-068: an enclosing function gains none of the nested one's lines" {
	# Attributing to the outermost enclosing function would report 8 here.
	assert_equal "$(function_eloc outer)" "4"
}

@test "HLR-067: all three functions are reported in their own right" {
	elc "$SUBJECT"
	assert_success
	assert_output --partial "outer"
	assert_output --partial "middle"
	assert_output --partial "inner"
}

@test "HLR-019: the file counts each statement line once" {
	# Eight statements on eight distinct lines, however they are attributed.
	elc "$SUBJECT"
	assert_output --regexp "nested\.c +c +23 +8"
}

@test "HLR-032: nested attribution is deterministic across runs" {
	run bash -c '"$0" "$1" 2>/dev/null' "$ELC" "$SUBJECT"
	local first="$output"
	run bash -c '"$0" "$1" 2>/dev/null' "$ELC" "$SUBJECT"
	assert_equal "$output" "$first"
}
