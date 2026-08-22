#!/usr/bin/env bats
# test/fixtures/nesting.bats — each statement to exactly one function (STP §5).
#
# Expected values are hand-counted in nesting/README.md beside this file.

setup() {
	load "../helpers/common"
	SUBJECT="$BATS_TEST_DIRNAME/nesting/nested.c"
	GROUP="$BATS_TEST_DIRNAME/nesting"
}

# "<eloc> <complexity>" for one function of one fixture.
#
# Every extractor here reads the per-function tier, which is a detail tier and
# so needs the verbose composition. These tests are about what was measured,
# not about what a default report presents.
metrics() {
	elc --verbose "$1"
	awk -v want="$2" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	                  f && $2 == want { print $4, $5 }' <<<"$output"
}

reported() {
	elc --verbose "$1"
	awk '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	     f && /^  \// { print $2 }' <<<"$output" | sort | tr '\n' ' '
}

function_eloc() {
	elc --verbose "$SUBJECT"
	awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
	                  f && $2 == want { print $4 }' <<<"$output"
}

function_complexity() {
	elc --verbose "$SUBJECT"
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
	elc --verbose "$SUBJECT"
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

# --- anonymous callables (HLR-018) -----------------------------------------
#
# The other half of the attribution rule, and the half C could not exercise at
# all. A closure or lambda is not reported as a function, so its decision
# points belong to the nearest enclosing *named* one. Each case below asserts
# both halves: the anonymous callable is absent from the report, and the
# enclosing function's complexity includes its branch.

@test "HLR-018: a Rust closure is not reported as a function" {
	assert_equal "$(reported "$GROUP/nested.rs")" "inner outer "
}

@test "HLR-018: a Rust closure's decision point lands on the enclosing function" {
	# outer branches twice of its own — the `if` and the `&&` — and once
	# more inside the closure. Without HLR-018 it would report 3.
	assert_equal "$(metrics "$GROUP/nested.rs" outer)" "5 4"
	assert_equal "$(metrics "$GROUP/nested.rs" inner)" "3 2"
}

@test "HLR-018: a Python lambda is not reported as a function" {
	assert_equal "$(reported "$GROUP/nested.py")" "inner outer "
}

@test "HLR-018: a Python lambda's conditional lands on the enclosing function" {
	assert_equal "$(metrics "$GROUP/nested.py" outer)" "5 4"
	assert_equal "$(metrics "$GROUP/nested.py" inner)" "3 2"
}

@test "HLR-018: a C++ lambda is not reported as a function" {
	assert_equal "$(reported "$GROUP/nested.cpp")" "outer "
}

@test "HLR-018: a C++ lambda's conditional lands on the enclosing function" {
	assert_equal "$(metrics "$GROUP/nested.cpp" outer)" "5 4"
}

@test "HLR-067: a nested named function is reported where a lambda is not" {
	# The distinction the two requirements draw, in one language: Rust's
	# `fn inner` is reported and its closure is not, though both sit in the
	# same body.
	elc --verbose "$GROUP/nested.rs"
	assert_success
	assert_output --partial "inner"
	assert_output --regexp "Functions +2"
}
