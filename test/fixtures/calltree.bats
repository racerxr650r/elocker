#!/usr/bin/env bats
# test/fixtures/calltree/calltree.bats — the call-tree analyses (STP §5).
#
# Expected values are worked out by hand and justified in README.md beside
# this file. Never regenerate them from elc's output.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/calltree/tree"
}

# Strip the decoration a style adds, so one extractor reads both. The aligned
# table and the Markdown say the same things in the same order — that is
# HLR-031 — and the only differences are a `## ` before a heading and a `|`
# around every cell. Removing those leaves identical whitespace-separated
# columns, which matters here because `--from-xml` regenerates as Markdown
# alone and the round-trip test has to read what it produced.
undecorated() {
	# The disclosure element HLR-190 wraps each Markdown table in is
	# dropped along with the decoration, and it has to be: the blank line
	# after `<summary>` would otherwise terminate a section extractor at
	# the very line the table begins on.
	printf '%s\n' "$output" |
		sed 's/^## /  /; s/|/ /g' |
		grep -vE '^(<details>|<summary>|</details>)'
}

# One column of the Functions section, for one function.
#
# Since HLR-183 there is one per-function table rather than three, so every
# figure this suite reads comes from here: File, Function, Lines, ELOC,
# Complexity, Fan-in, Fan-out.
#
# The section is entered at its heading and left at the first blank line
# *after a row*, not at the first blank line: Markdown puts one between the
# heading and the table. Scoping matters because function names appear in
# several sections, and an unterminated extractor reads whichever one comes
# next.
#
#   $1  function name
#   $2  column: 5 lines, 6 ELOC, 7 complexity, 8 fan-in, 9 fan-out
function_of() {
	undecorated |
		awk -v want="$1" -v col="$2" \
		    '/^ *Functions$/ { f = 1; next }
		     f && /^ *$/ { if (seen) f = 0; next }
		     f { seen = 1; if ($3 == want) print $col }'
}

fan_in_of()  { function_of "$1" 8; }
fan_out_of() { function_of "$1" 9; }

# The Functions section's rows, for assertions about the table as a whole
# rather than about one cell.
function_section() {
	undecorated |
		awk '/^ *Functions$/ { f = 1 }
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
# Read from the closing statement too, since an omitted or unbounded chain has
# no rows and is named there rather than printed (HLR-188, HLR-189).
depth_heading() { heading_of "Deepest call chain"; }

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
	rows="$(function_section | awk '/^ *\// { n++ } END { print n + 0 }')"
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
	rows="$(function_section | awk '/^ *\// { n++ } END { print n + 0 }')"
	assert_equal "$rows" "8"
}

# ------------------------------------------------------ information flow --

@test "HLR-183: one function table carries every per-function figure" {
	# Where there were three tables listing the same functions in the same
	# order, there is one. Three were three chances to disagree about
	# which functions exist.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success

	assert_output --regexp "Function +Visibility +Lines +ELOC +Complexity +Fan-in +Fan-out"
	refute_output --partial "Fan-out (distinct callees)"
	refute_output --partial "Information flow"
}

@test "HLR-183: the Henry-Kafura metric is withdrawn, per function and total" {
	# Phase 24 removed it. What is left is the pair of degrees, reported
	# as they are measured; the fan-out band keeps its Henry-Kafura
	# attribution, which is a citation for a threshold rather than a
	# metric of its own.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success

	refute_output --partial "HK = ELOC"
	refute_output --partial "ordinal, not absolute"
	assert_equal "$(summary_of Henry-Kafura)" ""
}

@test "HLR-188: an empty table is named rather than presented" {
	# The flow tree has no recursion, so the table has no rows — and a
	# table with no rows is not printed at all. The closing statement is
	# where a reader learns the analysis ran and found nothing.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success

	refute_output --regexp "(^|\n)Recursion\n"
	assert_output --partial "Nothing to report"
	assert_output --partial "    - Recursion"
}

@test "HLR-085, HLR-156: the degrees are reported as zero, not as blank" {
	# flow_entry is the longest and widest function in the file and has no
	# caller; leaf_a is called and calls nothing. Neither is an absence,
	# and neither may borrow Instability's `undefined` spelling for a
	# value that is defined.
	elc --verbose --entry flow_entry "$TREE/flow.c"
	assert_success

	assert_equal "$(fan_in_of flow_entry)" "0"
	assert_equal "$(fan_out_of leaf_a)" "0"

	# Scoped to this table: `undefined` is the right answer in the coupling
	# table, where Instability's inputs really do vanish (HLR-082). It is
	# the wrong answer here, and the two sitting near each other is what
	# makes borrowing it an easy mistake.
	run bash -c 'grep -c undefined <<<"$0" || true' "$(function_section)"
	assert_output "0"
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

@test "HLR-156, HLR-183: the flow figures survive a record round trip" {
	# Neither degree can be recomputed from a record: regeneration has no
	# graph, and no source to build one from (LLR-XWR-08). A record that
	# carried only fan-out would regenerate every fan-in as zero, which is
	# a wrong number that renders as an ordinary one — and since HLR-183
	# the two sit in the function table, which the regeneration path has to
	# join them onto for itself.
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
	assert_equal "$(fan_in_of hub)" "3"
	assert_equal "$(fan_out_of hub)" "2"
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
