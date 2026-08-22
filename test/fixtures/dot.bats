#!/usr/bin/env bats
# test/fixtures/dot.bats — the annotated Graphviz call tree (STP §5).
#
# Expected values are worked out by hand and justified in dot/README.md beside
# the trees. Never regenerate them from elc's output.
#
# Two claims are kept apart throughout. That the file is *valid DOT* is settled
# by handing it to Graphviz and requiring it to render, never by matching text
# that looks like DOT. That the *annotations* say what the analyses found is
# settled against the hand-worked table in the README.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/dot/tree"
	RECURSIVE="$BATS_TEST_DIRNAME/dot/recursive"
	# `.md` now names Markdown rather than falling back to the table
	# (HLR-148), and that is the right choice here: this suite is about
	# the `.dot` call tree, and an --output of report.md still yielding a
	# companion beside it is exactly the substitution HLR-148 preserves.
	# Nothing below reads a tier whose decoration differs between the two.
	OUT="$BATS_TEST_TMPDIR/report.md"
	DOT="$BATS_TEST_TMPDIR/report.dot"
}

# The main tree, with reachability rooted at `run` and the bottleneck floor
# dropped to 1 — at the default of 5 a two-file cycle would need ten more
# files to also be a bottleneck (dot/README.md).
run_elc() {
	run bash -c '"$0" --entry run -b 1 -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$OUT" "$TREE"
}

run_recursive() {
	run bash -c '"$0" --entry kick -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$OUT" "$RECURSIVE"
}

# The node and edge lines of the emitted file, as bare identifiers. Extracted
# and compared with assert_equal rather than matched with a line assertion,
# because bats-assert's line matchers read the last `run` and not a captured
# string, and would pass vacuously against one.
nodes() {
	grep -oE '^[[:space:]]+n[0-9]+ \[' "$DOT" |
		sed -e 's/^[[:space:]]*//' -e 's/ \[$//' || true
}

edges() {
	grep -oE '^[[:space:]]+n[0-9]+ -> n[0-9]+' "$DOT" |
		sed 's/^[[:space:]]*//' || true
}

# ------------------------------------------------------------- valid DOT --

@test "HLR-102: the emitted call tree renders under Graphviz" {
	require_tool dot "HLR-102 .dot validity"
	run_elc
	assert_success

	run dot -Tsvg -o /dev/null "$DOT"
	assert_success
}

@test "HLR-102: the recursive tree renders too" {
	require_tool dot "HLR-102 .dot validity"
	run_recursive
	assert_success

	run dot -Tsvg -o /dev/null "$DOT"
	assert_success
}

@test "LLR-STY-02: stripping every annotation leaves the same valid tree" {
	# The claim HLR-105 makes about degradation, tested by performing the
	# degradation: delete every attribute list and every standalone
	# attribute statement, and what is left must still be a graph with the
	# same nodes and the same edges.
	require_tool dot "HLR-105 graceful degradation"
	run_elc
	assert_success

	local stripped="$BATS_TEST_TMPDIR/stripped.dot"

	# The default-attribute statements go whole, because deleting only their
	# attribute list would leave a bare `graph;` — which no renderer would
	# ever produce and which says nothing about degradation.
	sed -e '/^[[:space:]]*\(graph\|node\|edge\) \[/d' \
	    -e 's/ \[[^]]*\]//g' \
	    -e '/^[[:space:]]*\(label\|style\|bgcolor\|tooltip\|penwidth\)=/d' \
	    "$DOT" > "$stripped"

	run dot -Tsvg -o /dev/null "$stripped"
	assert_success

	# Nothing structural went with the decoration.
	run bash -c 'grep -cE "^\s+n[0-9]+;" "$0"' "$stripped"
	assert_output "25"
	run bash -c 'grep -cE "^\s+n[0-9]+ -> n[0-9]+;" "$0"' "$stripped"
	assert_output "19"
}

# --------------------------------------------------------- when it exists --

@test "HLR-103: the call tree is written without being asked for" {
	run_elc
	assert_success
	[ -f "$DOT" ]
}

@test "HLR-103: --no-dot suppresses it" {
	run bash -c '"$0" --no-dot --entry run -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$OUT" "$TREE"
	assert_success
	[ -f "$OUT" ]
	[ ! -e "$DOT" ] || {
		echo "a .dot file was written despite --no-dot" >&2
		false
	}
}

@test "HLR-104: no call tree is written when the report goes to stdout" {
	run bash -c 'cd "$1" && "$0" --entry run "$2" >/dev/null 2>&1' \
		"$ELC" "$BATS_TEST_TMPDIR" "$TREE"
	assert_success

	run bash -c 'ls "$0"/*.dot 2>/dev/null | wc -l' "$BATS_TEST_TMPDIR"
	assert_output "0"
}

@test "HLR-104: still none on stdout with generation disabled" {
	# "whether or not generation was disabled" is the requirement's wording,
	# and the two paths reaching the same result is what it asks for: the
	# absence of an output path decides it, not the switch.
	run bash -c 'cd "$1" && "$0" --no-dot --entry run "$2" >/dev/null 2>&1' \
		"$ELC" "$BATS_TEST_TMPDIR" "$TREE"
	assert_success

	run bash -c 'ls "$0"/*.dot 2>/dev/null | wc -l' "$BATS_TEST_TMPDIR"
	assert_output "0"
}

@test "HLR-119: the companion is named from the report by substitution" {
	run bash -c '"$0" --entry run -o "$1/analysis.md" "$2" 2>/dev/null' \
		"$ELC" "$BATS_TEST_TMPDIR" "$TREE"
	assert_success
	[ -f "$BATS_TEST_TMPDIR/analysis.dot" ]
	[ ! -e "$BATS_TEST_TMPDIR/analysis.md.dot" ]
}

@test "HLR-119: the .dot and the GraphML are siblings of one report" {
	run bash -c '"$0" --graphml --entry run -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$OUT" "$TREE"
	assert_success
	[ -f "$DOT" ]
	[ -f "$BATS_TEST_TMPDIR/report.graphml" ]
}

# ------------------------------------------------------------- the shape --

@test "the tree holds one cluster per source file and every function" {
	run_elc
	assert_success

	run bash -c 'grep -c "^	subgraph cluster_" "$0"' "$DOT"
	assert_output "6"

	assert_equal "$(nodes | wc -l)" "25"
}

@test "LLR-DOT-04: nodes are emitted in ascending identifier order" {
	# The one renderer that walks the graph rather than the sorted report
	# model, so it imposes its own order. Without this the graph library's
	# internal enumeration would reach the output and HLR-032 would fail.
	run_elc
	assert_success

	local ids sorted
	ids="$(nodes | sed 's/^n//')"
	sorted="$(printf '%s\n' "$ids" | sort -n)"
	assert_equal "$ids" "$sorted"
}

@test "LLR-DOT-04: each node's adjacency is in ascending target order" {
	run_elc
	assert_success

	local listed sorted
	listed="$(edges)"
	sorted="$(printf '%s\n' "$listed" | sort -t n -k2,2n -k3,3n)"
	assert_equal "$listed" "$sorted"
}

@test "HLR-032: two runs over the same tree produce an identical call tree" {
	run_elc
	assert_success
	local first
	first="$(cat "$DOT")"

	rm -f "$DOT"
	run_elc
	assert_success
	assert_equal "$(cat "$DOT")" "$first"
}

@test "only call edges are drawn; a shared global is not a call" {
	# The artefact is the *call tree* (HLR-102). producer and consumer are
	# joined by shared_flag and by nothing else, and no edge joins them —
	# their coupling reaches the drawing as a property of each node.
	run_elc
	assert_success

	assert_equal "$(edges | wc -l)" "19"
	run bash -c 'grep -c "n11 -> n12\|n12 -> n11" "$0" || true' "$DOT"
	assert_output "0"
}

# ------------------------------------------------------- the annotations --

@test "HLR-105: the deepest call chain is annotated, nodes and edges" {
	# run -> helper -> step1 -> step2 -> step3 -> step4: six functions and
	# the five steps between them. Inside the accepted band, so the chain is
	# annotated because it is the chain and not because it warned.
	run_elc
	assert_success

	run bash -c 'grep -cE "^\s+n[0-9]+ \[.*penwidth=3" "$0"' "$DOT"
	assert_output "6"

	run bash -c 'grep -cE "^\s+n[0-9]+ -> n[0-9]+ \[" "$0"' "$DOT"
	assert_output "5"
}

@test "HLR-105: every unreachable function is annotated" {
	run_elc
	assert_success

	run bash -c 'grep -cE "^\s+n[0-9]+ \[.*dashed" "$0"' "$DOT"
	assert_output "7"
}

@test "HLR-105: a fan-out over the band is annotated with its severity" {
	# reader calls eleven distinct subroutines, and eleven is the first
	# value that warns (HLR-086). The severity is the catalogue's; the
	# drawing colours it and decides nothing.
	run_elc
	assert_success

	run bash -c 'grep -c "label=\"reader\".*fan-out" "$0"' "$DOT"
	assert_output "1"
	run bash -c 'grep -c "label=\"reader\".*warning: fan-out" "$0"' "$DOT"
	assert_output "1"
}

@test "HLR-105: both participants in a hidden channel are annotated" {
	run_elc
	assert_success

	run bash -c 'grep -cE "^\s+n[0-9]+ \[.*shape=octagon" "$0"' "$DOT"
	assert_output "2"
	run bash -c 'grep -c "hidden channel" "$0"' "$DOT"
	assert_output "3" # two nodes, and the key in the preamble
}

@test "HLR-105: two findings on one node ride two attributes" {
	# producer takes part in a hidden channel *and* is unreachable. Each
	# rides a different Graphviz attribute, so neither overwrites the other;
	# a drawing that showed one of the two would be silently incomplete.
	run_elc
	assert_success

	run bash -c 'grep -c "label=\"producer\".*shape=octagon.*dashed" "$0"' \
		"$DOT"
	assert_output "1"
}

@test "HLR-105: every member of a dependency cycle is annotated" {
	# The catalogue locates a cycle at one component, because a finding has
	# one subject; HLR-105 asks for the members. left.c and right.c depend
	# on each other and both clusters say so.
	run_elc
	assert_success

	run bash -c 'grep -c "component dependency cycle" "$0"' "$DOT"
	assert_output "2"
}

@test "HLR-105: a bottleneck component is annotated and attributed" {
	run_elc
	assert_success

	run bash -c 'grep -c "warning: bottleneck" "$0"' "$DOT"
	assert_output "2"
}

@test "HLR-105: every member of a recursive cycle is annotated" {
	# ping and pong call each other. Both carry the second border and both
	# carry the finding, for the reason the dependency cycle does.
	run_recursive
	assert_success

	run bash -c 'grep -cE "^\s+n[0-9]+ \[.*peripheries=2" "$0"' "$DOT"
	assert_output "2"
	run bash -c 'grep -c "critical: recursion" "$0"' "$DOT"
	assert_output "2"
}

@test "HLR-100: a critical annotation does not become an exit status" {
	# The recursive tree is a run full of critical findings and a run in
	# which every file was read. Severity is a label (HLR-100).
	run_recursive
	assert_success
	assert_equal "$status" "0"
}

@test "a forward declaration is not a node" {
	# `static void pong(int n);` precedes ping. Three nodes, not four.
	run_recursive
	assert_success
	assert_equal "$(nodes | wc -l)" "3"
}
