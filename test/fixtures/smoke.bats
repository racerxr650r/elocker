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

@test "every expected-value file has a fixture header beside it" {
	# Guards the convention above. Phase 0 wrote this as "there are none
	# yet", with a note that a future phase adding one must bring a header
	# justifying its numbers. Phase 8 is that phase: graph/expected.graphml
	# is the first, because a graph's *topology* cannot be asserted from a
	# rendered report — the report states conclusions, and two different
	# graphs can reach the same conclusion.
	#
	# So the guard now checks the invariant its own comment describes,
	# rather than the count that happened to satisfy it while no expected
	# file existed. An expected file with no README beside it is a set of
	# numbers with nothing to justify them, which is the thing being
	# forbidden.
	local orphaned=0

	while read -r expected; do
		[ -n "$expected" ] || continue
		if [ ! -f "$(dirname "$expected")/README.md" ]; then
			echo "no fixture header beside $expected" >&2
			orphaned=1
		fi
	done < <(find "$BATS_TEST_DIRNAME" -name "expected*" -type f)

	[ "$orphaned" -eq 0 ]
}
