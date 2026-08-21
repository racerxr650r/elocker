#!/usr/bin/env bats
# test/fixtures/graph/graph.bats — the System Dependence Graph (STP §5).
#
# Expected values are worked out by hand and justified in README.md beside
# this file. Never regenerate them from elc's output.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/graph/tree"
	TREE_REAL="$(cd "$TREE" && pwd -P)"
	EXPECTED="$BATS_TEST_DIRNAME/graph/expected.graphml"
	OUT="$BATS_TEST_TMPDIR/report.md"
	GRAPHML="$BATS_TEST_TMPDIR/report.graphml"
}

# Run elc over the fixture tree with the export on. The companion's name is
# derived from --output, so naming the report names the graph too.
run_elc() {
	run bash -c '"$0" --graphml -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$OUT" "$TREE"
}

# The emitted GraphML with the fixture's absolute path replaced by TREE, so
# the expected file is comparable from any checkout. Nothing else is touched:
# the comparison is byte for byte on everything that describes the graph.
normalised() {
	sed "s|$TREE_REAL|TREE|g" "$GRAPHML"
}

# --------------------------------------------------------------- topology --

@test "HLR-106: the exported graph matches the expected topology exactly" {
	# The assertion this group exists for. Node for node and edge for edge,
	# against a file written from the source rather than captured from a
	# run — so a change in what elc believes about the code shows up here
	# as a diff rather than as a number nobody questions.
	run_elc
	assert_success

	run diff -u "$EXPECTED" <(normalised)
	assert_success
}

@test "HLR-106: the export is off unless asked for" {
	run bash -c '"$0" -o "$1" "$2" 2>/dev/null' "$ELC" "$OUT" "$TREE"
	assert_success
	[ ! -e "$GRAPHML" ] || {
		echo "a GraphML file was written without --graphml" >&2
		false
	}
}

@test "HLR-106: no companion is written when the report goes to stdout" {
	# There is no output path to derive the companion's name from, which is
	# the reason the requirement gives — not an oversight to be worked
	# around with a default name.
	run bash -c 'cd "$1" && "$0" --graphml "$2" >/dev/null 2>&1' \
		"$ELC" "$BATS_TEST_TMPDIR" "$TREE"
	assert_success

	run bash -c 'ls "$0"/*.graphml 2>/dev/null | wc -l' "$BATS_TEST_TMPDIR"
	assert_output "0"
}

@test "LLR-GML-03: the companion is named from the report by substitution" {
	run bash -c '"$0" --graphml -o "$1/analysis.md" "$2" 2>/dev/null' \
		"$ELC" "$BATS_TEST_TMPDIR" "$TREE"
	assert_success
	[ -f "$BATS_TEST_TMPDIR/analysis.graphml" ]
	[ ! -e "$BATS_TEST_TMPDIR/analysis.md.graphml" ]
}

# ----------------------------------------------------------------- edges --

@test "HLR-085: repeated calls are one edge carrying a call-site count" {
	run_elc
	assert_success

	# tick calls bump twice. One edge, count 2 — and tick's fan-out is 1.
	run bash -c 'grep -c "e_sites\">2<" "$0"' "$GRAPHML"
	assert_output "1"

	run bash -c 'grep -A2 "n_name\">tick<" "$0" | head -1' "$GRAPHML"
	assert_output --partial "tick"
}

@test "HLR-074: a global links its writer to its reader across files" {
	run_elc
	assert_success

	# The two functions are in different files and neither names the
	# other; the shared object is the only thing connecting them.
	run bash -c 'grep -c "e_global\">shared_counter<" "$0"' "$GRAPHML"
	assert_output "1"
}

@test "HLR-074: call edges and global edges are distinguishable" {
	run_elc
	assert_success

	run bash -c 'grep -c "e_kind\">call<" "$0"' "$GRAPHML"
	assert_output "1"
	run bash -c 'grep -c "e_kind\">global<" "$0"' "$GRAPHML"
	assert_output "1"
}

@test "HLR-096: a function whose address is taken is marked" {
	run_elc
	assert_success

	# `hook = report;` assigns without calling. Exactly one function in the
	# tree is a reachability root, and it is not the one that installs it.
	run bash -c 'grep -c "n_address\">true<" "$0"' "$GRAPHML"
	assert_output "1"
}

# ----------------------------------------------------------- unresolved --

@test "HLR-077: an unresolvable call is counted, not fatal" {
	run_elc
	assert_success   # a library call does not fail the run

	run bash -c 'grep -c "g_unresolved\">1<" "$0"' "$GRAPHML"
	assert_output "1"
}

@test "HLR-077: the unresolved count reaches the report" {
	run bash -c '"$0" -o "$1" "$2" 2>/dev/null' "$ELC" "$OUT" "$TREE"
	assert_success

	run bash -c 'grep -c "Unresolved calls" "$0"' "$OUT"
	assert_output "1"
}

@test "HLR-077: no destination is invented for an unresolved call" {
	run_elc
	assert_success

	# Two edges, and the README accounts for both. A tool that guessed at
	# external_log would have more.
	run bash -c 'grep -c "<edge " "$0"' "$GRAPHML"
	assert_output "2"
}

# ------------------------------------------------------------- ordering --

@test "HLR-033: node identifiers follow sorted file order" {
	run_elc
	assert_success

	# core.c then reader.c, and within each file by start line — which is
	# not the order a directory walk yields, so a graph numbering by arrival
	# would differ.
	run bash -c 'grep "n_name\">" "$0" | sed "s/.*>\\(.*\\)<.*/\\1/" | tr "\\n" " "' \
		"$GRAPHML"
	assert_output "bump tick report install "
}

@test "HLR-032: two runs over the same tree produce identical GraphML" {
	run_elc
	assert_success
	local first
	first="$(normalised)"

	rm -f "$GRAPHML"
	run_elc
	assert_success
	assert_equal "$(normalised)" "$first"
}

# ------------------------------------------------------------ graph scope --

@test "HLR-075: the graph spans every target argument, not each one alone" {
	# The graph describes the project, not the target that happened to
	# introduce a file. Naming the two sources as separate arguments must
	# therefore build the same graph as naming the directory that holds
	# them — cross-file edges and all.
	#
	# Byte-identical is the right assertion and not an over-strong one:
	# node identifiers run in sorted file order rather than in the order
	# the arguments arrived (LLR-SDG-09), so the two runs agree on
	# numbering as well as on topology. A resolver that scoped resolution
	# to one target would drop the core.c -> reader.c edges and show up
	# here as a diff, having counted them unresolved instead.
	run_elc
	assert_success
	local whole_tree
	whole_tree="$(normalised)"

	rm -f "$GRAPHML" "$OUT"
	run bash -c '"$0" --graphml -o "$1" "$2" "$3" 2>/dev/null' \
		"$ELC" "$OUT" "$TREE/core.c" "$TREE/reader.c"
	assert_success

	assert_equal "$(normalised)" "$whole_tree"
}

@test "HLR-075: a cross-target call resolves rather than counting unresolved" {
	# The half of the requirement the identity above cannot see on its
	# own: were both runs to resolve nothing across the boundary they
	# would still match each other. `install` takes the address of
	# `report` and `tick` calls `bump` across the file boundary, so the
	# graph must carry edges whose ends lie in different targets.
	rm -f "$GRAPHML" "$OUT"
	run bash -c '"$0" --graphml -o "$1" "$2" "$3" 2>/dev/null' \
		"$ELC" "$OUT" "$TREE/core.c" "$TREE/reader.c"
	assert_success

	# reader.c's report() reads the global core.c's bump() writes: an
	# edge between two nodes the two separate targets contributed.
	run bash -c 'grep -c "kind\">global<" "$0"' "$GRAPHML"
	assert_output "1"
}

# ----------------------------------------------------------- one parse --

@test "HLR-076: the graph is built without reopening a source file" {
	require_tool strace "HLR-076 single parse"

	local log="$BATS_TEST_TMPDIR/trace.log"

	strace_elc "$log" "openat" --graphml -o "$OUT" "$TREE"

	# Each of the three sources is opened exactly once, with the graph
	# built from what that one parse produced. A resolver that re-read a
	# file to chase a cross-file reference would show a second open here.
	for f in core.c reader.c; do
		run bash -c 'grep -c "openat(.*/'"$f"'\"" "$0" || true' "$log"
		assert_output "1"
	done
}
