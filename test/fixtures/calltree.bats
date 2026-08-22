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

# Strip the decoration a style adds, so one extractor reads both. The aligned
# table and the Markdown say the same things in the same order — that is
# HLR-031 — and the only differences are a `## ` before a heading and a `|`
# around every cell. Removing those leaves identical whitespace-separated
# columns, which matters here because `--from-xml` regenerates as Markdown
# alone and the round-trip test has to read what it produced.
undecorated() {
	printf '%s\n' "$output" | sed 's/^## /  /; s/|/ /g'
}

# One column of the Information flow section, for one function.
#
# The section is entered at its heading and left at the first blank line
# *after a row*, not at the first blank line: Markdown puts one between the
# heading and the table. Scoping matters for the reason it does in
# fan_out_of — function names appear in several sections, and an unterminated
# extractor reads whichever one comes next.
#
#   $1  function name
#   $2  column: 3 ELOC, 4 fan-in, 5 fan-out, 6 Henry-Kafura
flow_of() {
	undecorated |
		awk -v want="$1" -v col="$2" \
		    '/^ *Information flow/ { f = 1; next }
		     f && /^ *$/ { if (seen) f = 0; next }
		     f { seen = 1; if ($2 == want) print $col }'
}

fan_in_of() { flow_of "$1" 4; }
hk_of()     { flow_of "$1" 6; }

# The Information flow section's rows, for assertions about the table as a
# whole rather than about one cell.
flow_section() {
	undecorated |
		awk '/^ *Information flow/ { f = 1 }
		     f && /^ *$/ { if (seen) f = 0; next }
		     f { seen = 1; print }'
}

# One row of the project summary, by its label. Single-word labels only,
# which every figure this suite reads has.
summary_of() {
	undecorated |
		awk -v want="$1" '/^ *Project summary/ { f = 1; next }
		                  f && /^ *$/ { if (seen) f = 0; next }
		                  f { seen = 1; if ($1 == want) print $2 }'
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
	elc --verbose --entry entry_main "$TREE/fanout.c"
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
	elc --verbose --entry entry_main "$TREE/fanout.c"
	assert_success
	assert_equal "$(fan_out_of fan02)" "2"
}

@test "HLR-085: a function that calls nothing has fan-out zero" {
	elc --verbose --entry entry_main "$TREE/fanout.c"
	assert_success
	assert_equal "$(fan_out_of h01)" "0"
	assert_equal "$(fan_out_of h16)" "0"
}

@test "HLR-085: every function appears, not only the interesting ones" {
	# 16 helpers plus 8 callers. A table listing only what exceeds some
	# threshold would be a finding list, which is Phase 12's job.
	elc --verbose --entry entry_main "$TREE/fanout.c"
	assert_success

	local rows
	rows="$(printf '%s\n' "$output" |
		awk '/^Fan-out/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { n++ } END { print n + 0 }')"
	assert_equal "$rows" "24"
}

# --------------------------------------------------------------- fan-in --

@test "HLR-156: fan-in counts the distinct functions that call one" {
	# hub is called by three functions; the leaves by one apiece.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success

	assert_equal "$(fan_in_of hub)" "3"
	assert_equal "$(fan_in_of caller_one)" "1"
	assert_equal "$(fan_in_of leaf_c)" "1"
}

@test "HLR-156: a repeated call does not raise fan-in" {
	# hub calls leaf_a twice. Fan-in counts callers, not call sites — the
	# same distinctness rule fan-out is built on, read backwards.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success
	assert_equal "$(fan_in_of leaf_a)" "1"
	assert_equal "$(fan_in_of leaf_b)" "1"
}

@test "HLR-156: a function nothing calls has fan-in zero" {
	# An entry point, an exported API boundary and an interrupt handler all
	# legitimately have no callers. Drawing a conclusion from that is the
	# reachability analysis's job, not this measurement's.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success
	assert_equal "$(fan_in_of flow_entry)" "0"
}

@test "HLR-156: every function reports a fan-in, not only the connected ones" {
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success

	local rows
	rows="$(printf '%s\n' "$output" |
		awk '/^Information flow/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { n++ } END { print n + 0 }')"
	assert_equal "$rows" "8"
}

# ------------------------------------------------------ information flow --

@test "HLR-157: the hand-computed Henry-Kafura value matches, function by function" {
	# Every figure is worked out in README.md from the source. hub is the
	# only function with both a caller and a callee, so it is the only one
	# that can score anything at all.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success

	assert_equal "$(hk_of hub)" "144"           # 4 x (3 x 2)^2
	assert_equal "$(hk_of caller_one)" "1"      # 1 x (1 x 1)^2
	assert_equal "$(hk_of caller_two)" "1"      # 1 x (1 x 1)^2
	assert_equal "$(hk_of caller_three)" "8"    # 2 x (1 x 2)^2
}

@test "HLR-159: an entry point and a leaf each report a zero, not a blank" {
	# The property a reader misreads if it is not stated. flow_entry is the
	# longest and widest function in the file and scores nothing because
	# nothing calls it; leaf_a is called and scores nothing because it
	# calls nothing. Neither is an absence of code, and neither may borrow
	# Instability's `undefined` spelling for a value that is defined.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success

	assert_equal "$(hk_of flow_entry)" "0"
	assert_equal "$(hk_of leaf_a)" "0"

	# Scoped to this table: `undefined` is the right answer two tables
	# down, where Instability's inputs really do vanish (HLR-082). It is
	# the wrong answer here, and the two sitting near each other is what
	# makes borrowing it an easy mistake.
	run bash -c 'grep -c undefined <<<"$0" || true' "$(flow_section)"
	assert_output "0"
}

@test "HLR-159: the report states why a zero is a zero" {
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success
	assert_output --partial "zero at either end of the call graph"
}

@test "HLR-159: the formula and its attribution travel with the figures" {
	# The squared term is Henry and Kafura's, not elc's (HLR-099), and a
	# metric whose name reads as a citation must carry it where it is read.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success
	assert_output --partial "HK = ELOC x (Fan-in x Fan-out)^2"
	assert_output --partial "Henry-Kafura"
	assert_output --partial "ordinal, not absolute"
}

@test "HLR-159: no Henry-Kafura figure is reported as a finding" {
	# No published source bands the metric, so the catalogue holds no row
	# and nothing here may acquire a severity (LLR-THR-08).
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success

	local findings
	findings="$(printf '%s\n' "$output" |
		awk '/^Findings/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /Henry-Kafura/ { n++ } END { print n + 0 }')"
	assert_equal "$findings" "0"
}

@test "HLR-158: the project total is the sum of the per-function values" {
	# 144 + 1 + 1 + 8, and zero from the four at the ends of the graph.
	elc --entry flow_entry "$TREE/flow.c"
	assert_success
	assert_equal "$(summary_of Henry-Kafura)" "154"
}

@test "HLR-158: the project total is a summary figure, not a detail one" {
	# The per-function table is a detail tier and the total is not: a
	# summary report carries the figure without the table (HLR-024).
	elc --entry flow_entry "$TREE/flow.c"
	assert_success
	assert_equal "$(summary_of Henry-Kafura)" "154"
	refute_output --partial "Information flow ("
}

# ----------------------------------------------------------------- depth --

@test "HLR-087: the hand-counted depth matches" {
	elc --verbose --entry entry_main "$TREE/depth.c"
	assert_success
	assert_output --partial "Deepest call chain (4 layers"
}

@test "HLR-088: the deepest chain is reported in full, in order" {
	elc --verbose --entry entry_main "$TREE/depth.c"
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
	elc --verbose --entry entry_main "$TREE/depth.c"
	assert_success
	assert_output --partial "a lower bound, 0 calls unresolved"
}

# ------------------------------------------------------------- recursion --

@test "HLR-089: direct and mutual recursion are both reported" {
	elc --verbose --entry recursive_entry "$TREE/recursion.c"
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
	elc --verbose --entry recursive_entry "$TREE/recursion.c"
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
	elc --verbose "$TREE/fanout.c"
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

@test "HLR-156, HLR-157: the flow figures survive a record round trip" {
	# Neither can be recomputed from a record: regeneration has no graph,
	# and no source to build one from (LLR-XWR-08). A record that carried
	# only fan-out would regenerate every Henry-Kafura value as zero, which
	# is a wrong number that renders as an ordinary one.
	local record="$BATS_TEST_TMPDIR/flow.xml"

	run bash -c '"$0" --verbose --entry flow_entry -f md "$1" 2>/dev/null' \
		"$ELC" "$TREE/flow.c"
	assert_success
	local direct="$output"

	run bash -c '"$0" --entry flow_entry -f xml "$1" > "$2" 2>/dev/null' \
		"$ELC" "$TREE/flow.c" "$record"
	assert_success

	run bash -c '"$0" --verbose --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	assert_equal "$output" "$direct"
	assert_equal "$(hk_of hub)" "144"
	assert_equal "$(fan_in_of hub)" "3"
	assert_equal "$(summary_of Henry-Kafura)" "154"
}

@test "HLR-056: the measurements survive a record round trip" {
	# The call tree cannot be recomputed from a record — there is no graph
	# and no source to build one from — so every figure must be carried.
	local record="$BATS_TEST_TMPDIR/record.xml"

	run bash -c '"$0" --verbose --entry entry_main -f md "$1" 2>/dev/null' \
		"$ELC" "$TREE/depth.c"
	assert_success
	local direct="$output"

	# No --verbose on the record: it carries every measurement whatever the
	# verbosity, which is what lets one record answer both questions.
	run bash -c '"$0" --entry entry_main -f xml "$1" > "$2" 2>/dev/null' \
		"$ELC" "$TREE/depth.c" "$record"
	assert_success

	run bash -c '"$0" --verbose --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	assert_equal "$output" "$direct"
	assert_output --partial "4 layers"
}
