#!/usr/bin/env bats
# test/fixtures/smoke.bats — placeholder for the fixture-conformance level.
#
# The real fixture groups arrive with the phases that produce values to
# check against hand-counted expectations: eloc/, comments/, and nesting/ in
# Phase 3, calltree/ and graph/ in Phases 8–9, and the rest as their
# analyses land (doc/STP.md §5).
#
# Phase 0 computes nothing, so there is nothing to hand-count yet. This
# suite exists so that `make fixtures` is wired and green from the outset,
# and so the level's conventions are established before the first real
# fixture is written:
#
#   * expected values are counted by hand and justified in the fixture's
#     own header — never generated from elc's output, which would make the
#     fixture agree with the implementation by construction and assert
#     nothing (doc/STP.md §2.4);
#   * scratch files go in $BATS_TEST_TMPDIR, never into this directory.

setup() {
	load "../helpers/common"
}

@test "the fixture level is wired and elc is runnable" {
	elc --help
	assert_success
}

@test "fixture directories carry no generated expected values yet" {
	# Guards the convention above: if a future phase adds an expected.tsv
	# here, it must arrive with a fixture header justifying its numbers.
	run bash -c 'find "$0" -name "expected*" -type f | wc -l' "$BATS_TEST_DIRNAME"
	assert_output "0"
}
