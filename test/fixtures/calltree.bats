#!/usr/bin/env bats
# test/fixtures/calltree/calltree.bats — the call-tree analyses (STP §5).
#
# Expected values are worked out by hand and justified in README.md beside
# this file. Never regenerate them from elc's output.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/calltree/tree"
}

# The fan-out reported for one function. Scoped to the Fan-out section and
# terminated at its blank line: paths and names appear in other sections too,
# and an unterminated extractor reads whichever one comes next — which is
# exactly how the complexity suite broke when this section was added.
fan_out_of() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Fan-out/ { f = 1; next } f && /^$/ { f = 0 }
		                  f && $2 == want { print $3 }'
}

# The deepest-chain heading, which states which of the four outcomes happened.
depth_heading() {
	printf '%s\n' "$output" | awk '/^Deepest call chain/ { print; exit }'
}

# The chain itself, in order, as function names.
chain() {
	printf '%s\n' "$output" |
		awk '/^Deepest call chain/ { f = 1; next } f && /^$/ { f = 0 }
		     f && $1 ~ /^[0-9]+$/ { print $3 }'
}

# --------------------------------------------------------------- fan-out --

@test "HLR-085: fan-out is exact at every band boundary" {
	# The eight values Phase 12 will band. Asserted individually rather
	# than as a total, so a failure names the boundary that moved.
	elc --entry entry_main "$TREE/fanout.c"
	assert_success

	assert_equal "$(fan_out_of fan02)" "2"
	assert_equal "$(fan_out_of fan03)" "3"
	assert_equal "$(fan_out_of fan07)" "7"
	assert_equal "$(fan_out_of fan08)" "8"
	assert_equal "$(fan_out_of fan10)" "10"
	assert_equal "$(fan_out_of fan11)" "11"
	assert_equal "$(fan_out_of fan15)" "15"
	assert_equal "$(fan_out_of fan16)" "16"
}

@test "HLR-085: a repeated call does not raise fan-out" {
	# Every caller in the fixture invokes h01 twice. If fan-out counted
	# call sites instead of callees, every figure above would be one
	# higher — so this is what the whole table rests on.
	elc --entry entry_main "$TREE/fanout.c"
	assert_success
	assert_equal "$(fan_out_of fan02)" "2"
}

@test "HLR-085: a function that calls nothing has fan-out zero" {
	elc --entry entry_main "$TREE/fanout.c"
	assert_success
	assert_equal "$(fan_out_of h01)" "0"
	assert_equal "$(fan_out_of h16)" "0"
}

@test "HLR-085: every function appears, not only the interesting ones" {
	# 16 helpers plus 8 callers. A table listing only what exceeds some
	# threshold would be a finding list, which is Phase 12's job.
	elc --entry entry_main "$TREE/fanout.c"
	assert_success

	local rows
	rows="$(printf '%s\n' "$output" |
		awk '/^Fan-out/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { n++ } END { print n + 0 }')"
	assert_equal "$rows" "24"
}

# ----------------------------------------------------------------- depth --

@test "HLR-087: the hand-counted depth matches" {
	elc --entry entry_main "$TREE/depth.c"
	assert_success
	assert_output --partial "Deepest call chain (4 layers"
}

@test "HLR-088: the deepest chain is reported in full, in order" {
	elc --entry entry_main "$TREE/depth.c"
	assert_success
	assert_equal "$(chain)" "entry_main
level2
level3
level4"
}

@test "HLR-088: the deepest branch is taken, not the first" {
	# entry_main calls shallow *before* level2. A search following the
	# first edge would report a two-step chain through shallow.
	elc --entry entry_main "$TREE/depth.c"
	assert_success

	run bash -c 'grep -c shallow <<<"$0"' "$(chain)"
	assert_output "0"
}

@test "HLR-087: the depth is presented with the unresolved count" {
	elc --entry entry_main "$TREE/depth.c"
	assert_success
	assert_output --partial "a lower bound, 0 calls unresolved"
}

# ------------------------------------------------------------- recursion --

@test "HLR-089: direct and mutual recursion are both reported" {
	elc --entry recursive_entry "$TREE/recursion.c"
	assert_success

	local kinds
	kinds="$(printf '%s\n' "$output" |
		awk '/^Recursion/ { f = 1; next } f && /^$/ { f = 0 }
		     f && ($1 == "direct" || $1 == "mutual") { print $1 }')"
	assert_equal "$kinds" "direct
mutual"
}

@test "HLR-089: the recursive functions are named" {
	elc --entry recursive_entry "$TREE/recursion.c"
	assert_success
	assert_output --partial "self_calling"
	assert_output --partial "bounce, countdown"
}

@test "HLR-090: recursion yields no depth figure, and the run terminates" {
	# Reaching the assertion at all is half the requirement: a
	# longest-path search over a cyclic graph does not terminate, which is
	# why acyclicity is established before the traversal rather than
	# discovered during it.
	elc --entry recursive_entry "$TREE/recursion.c"
	assert_success
	assert_equal "$(depth_heading)" \
		"Deepest call chain (unbounded: the call graph is recursive)"
	assert_equal "$(chain)" ""
}

@test "HLR-090: no finite depth is invented for a recursive graph" {
	elc --entry recursive_entry "$TREE/recursion.c"
	assert_success
	refute_output --regexp "Deepest call chain \([0-9]+ layers"
}

# ------------------------------------------------------------- omissions --

@test "HLR-115: with no entry points, depth is omitted with its reason" {
	elc "$TREE/depth.c"
	assert_success   # an absent declaration is not a failure
	assert_equal "$(depth_heading)" \
		"Deepest call chain (omitted: no entry points declared, see --entry)"
}

@test "HLR-115: an omitted depth does not omit the other measurements" {
	# Fan-out and recursion need no declaration, so they are still there.
	# Omitting one analysis must not silently omit its neighbours.
	elc "$TREE/fanout.c"
	assert_success
	assert_equal "$(fan_out_of fan16)" "16"
}

@test "HLR-095: main is not inferred, however obvious it looks" {
	# depth.c defines entry_main and nothing else that resembles an entry
	# point; the analysis is still omitted. Guessing would be wrong for a
	# library and for firmware, and wrong silently.
	elc "$TREE/depth.c"
	assert_success
	refute_output --partial "4 layers"
}

@test "HLR-115: an entry point matching nothing is a distinct omission" {
	elc --entry no_such_function "$TREE/depth.c"
	assert_success
	assert_equal "$(depth_heading)" \
		"Deepest call chain (omitted: no declared entry point matches an analysed function)"
}

@test "an unmatched entry point is diagnosed on stderr" {
	run bash -c '"$0" --entry no_such_function "$1" 2>&1 >/dev/null' \
		"$ELC" "$TREE/depth.c"
	assert_success
	assert_output --partial "no_such_function"
}

# -------------------------------------------------------- determinism --

@test "HLR-032: two runs over the same tree agree" {
	elc --entry entry_main "$TREE/depth.c" "$TREE/fanout.c"
	local first="$output"
	elc --entry entry_main "$TREE/depth.c" "$TREE/fanout.c"
	assert_equal "$output" "$first"
}

@test "HLR-033: the targets may be given in either order" {
	elc --entry entry_main "$TREE/depth.c" "$TREE/fanout.c"
	local first="$output"
	elc --entry entry_main "$TREE/fanout.c" "$TREE/depth.c"
	assert_equal "$output" "$first"
}

@test "HLR-056: the measurements survive a record round trip" {
	# The call tree cannot be recomputed from a record — there is no graph
	# and no source to build one from — so every figure must be carried.
	local record="$BATS_TEST_TMPDIR/record.xml"

	run bash -c '"$0" --entry entry_main -f md "$1" 2>/dev/null' \
		"$ELC" "$TREE/depth.c"
	assert_success
	local direct="$output"

	run bash -c '"$0" --entry entry_main -f xml "$1" > "$2" 2>/dev/null' \
		"$ELC" "$TREE/depth.c" "$record"
	assert_success

	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	assert_equal "$output" "$direct"
	assert_output --partial "4 layers"
}
