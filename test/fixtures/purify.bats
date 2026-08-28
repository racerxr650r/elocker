#!/usr/bin/env bats
# test/fixtures/purify.bats — graph purification and the recovery view (STP §5).
#
# Expected values are worked out by hand and justified in purify/README.md
# beside this file. Never regenerate them from elc's output.
#
# The two tests that matter most here assert an *absence*. Seven of the ten
# functions in the tree are classified as nothing at all, and the whole report
# outside this one section is byte-identical to what it was before purification
# existed. Either would pass against an implementation that did nothing, so
# each is paired with a case that must be reported.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/purify/tree"
}

# The purification heading, or empty.
purify_heading() {
	printf '%s\n' "$output" | awk '/^Graph purification/ { print; exit }'
}

# One classified function's row, as "class metric ... action". The section is
# scoped and terminated at its blank line: a function name appears in half a
# dozen other sections, and an unterminated extractor reads whichever comes
# next.
purify_row() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Graph purification/ { f = 1; next }
		                  f && /^$/ { f = 0 }
		                  f && $2 == want { $1 = ""; sub(/^ +/, "");
		                                    print }'
}

# The class assigned to one function, or empty where it was not classified.
# Matched against the three names rather than by column, because two of them
# are two words and one is not.
class_of() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Graph purification/ { f = 1; next }
		                  f && /^$/ { f = 0 }
		                  f && $2 == want {
		                          if ($3 == "utility") print "utility sink"
		                          else if ($3 == "god") print "god object"
		                          else print $3
		                  }'
}

purify_rows() {
	printf '%s\n' "$output" |
		awk '/^Graph purification/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { n++ } END { print n + 0 }'
}

# One function's ELOC, fan-in and fan-out, from the one function table
# HLR-183 leaves them in.
flow_of() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Functions$/ { f = 1; next }
		                  f && /^$/ { f = 0 }
		                  f && $3 == want { print $6, $8, $9 }'
}

# ------------------------------------------------- the classifications --

@test "HLR-168: the planted utility sink is classified as one" {
	# Six functions call util_log and it calls nothing, so its hub score is
	# exactly zero and its authority the highest in the tree.
	elc --verbose "$TREE"
	assert_success
	assert_equal "$(class_of util_log)" "utility sink"
}

@test "HLR-169: the planted dispatcher is classified as a god object" {
	# Highest betweenness *and* highest hub score. Betweenness alone would
	# not separate it from a legitimate waypoint, which is why HLR-169 asks
	# for both.
	elc --verbose "$TREE"
	assert_success
	assert_equal "$(class_of dispatch)" "god object"
}

@test "HLR-170: the leaf hanging off the tree is peripheral" {
	# helper_c has an undirected degree of one, so it lies in the first
	# core and outside the mutually connected centre.
	elc --verbose "$TREE"
	assert_success
	assert_equal "$(class_of helper_c)" "peripheral"
}

@test "HLR-174: each row names the metric and value that triggered it" {
	# A classification a reader cannot trace back to a number is an
	# assertion, which is the thing this requirement exists to prevent. The
	# figures are the hand-worked ones: betweenness 14 over 14 ordered
	# pairs, an authority of 1.0000, and a coreness of 1.
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "betweenness  14.00, above 100% of functions"
	assert_output --partial "authority    1.0000, above 100% of functions"
	assert_output --partial "coreness     1, below the core depth of 2"
}

@test "HLR-174: each row names the action taken upon it" {
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "all edges masked"
	assert_output --partial "incoming edges masked"
	assert_output --partial "excluded from the view"
}

@test "HLR-168: a utility sink loses its incoming edges and a god object both" {
	# Twelve of the fifteen call edges, worked out edge by edge in
	# README.md. The count is the asymmetry made checkable: masking the
	# sink's outgoing edges as well would remove nothing more here, and
	# masking only the god object's outgoing edges would leave two.
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "9 functions retained, 12 call edges masked"
}

@test "HLR-170: nothing else is classified" {
	# Seven of the ten functions are classified as nothing at all. main and
	# boot are the sharpest of the seven: each has a hub rank of 0 and so
	# passes the *hub* half of the utility-sink test, and neither is a sink
	# because the authority half fails. An implementation testing either
	# half alone reports them.
	elc --verbose "$TREE"
	assert_success
	assert_equal "$(purify_rows)" "3"
	assert_equal "$(class_of main)" ""
	assert_equal "$(class_of boot)" ""
	assert_equal "$(class_of store_put)" ""
}

# ------------------------------------------- purification reaches nothing --

@test "HLR-167: the masked functions keep the degrees the report gives them" {
	# The requirement the rest of the phase is built on. util_log's six
	# incoming edges are masked in the recovery view and every one of them
	# still counts towards its fan-in; dispatch loses all four of its
	# outgoing edges and still reports a fan-out of four.
	elc --verbose "$TREE"
	assert_success
	assert_equal "$(flow_of util_log)" "1 6 0"
	assert_equal "$(flow_of dispatch)" "7 2 4"
}

@test "HLR-167: changing a threshold moves no reported measurement" {
	# The strongest statement of the containment available from outside:
	# purify the tree three different ways and the report is byte-identical
	# but for its own sections. An implementation masking the Sdg in place
	# would differ in the fan-out, the coupling, and the matrix.
	#
	# The architecture-recovery sections are excluded alongside the
	# purification one, and that is not a weakening of the case. HLR-167
	# says purification moves no *measurement*; the recovered layering is
	# not a measurement but the thing purification exists to make possible,
	# and a proposal that did not change when the masking changed would mean
	# the masking had not reached it (HLR-172). Every measured figure in the
	# report is still inside the comparison.
	local without with_shallow with_deep

	elc --verbose "$TREE"
	assert_success
	without="$(printf '%s\n' "$output" |
		awk '/^Graph purification|^Architecture recovery/ { f = 1 }
		     f && /^$/ { f = 0; next } !f { print }')"

	elc --verbose --core-depth 1 "$TREE"
	assert_success
	with_shallow="$(printf '%s\n' "$output" |
		awk '/^Graph purification|^Architecture recovery/ { f = 1 }
		     f && /^$/ { f = 0; next } !f { print }')"

	elc --verbose --core-depth 3 "$TREE"
	assert_success
	with_deep="$(printf '%s\n' "$output" |
		awk '/^Graph purification|^Architecture recovery/ { f = 1 }
		     f && /^$/ { f = 0; next } !f { print }')"

	assert_equal "$with_shallow" "$without"
	assert_equal "$with_deep" "$without"
}

@test "HLR-171: no classification carries a severity or becomes a finding" {
	# A god object is an observation about the shape of a graph, not a
	# measurement banded against an accepted range. Presenting one as a
	# finding would put elc's own opinion in the section whose whole claim
	# is that it holds none (HLR-101).
	elc --verbose "$TREE"
	assert_success

	local findings
	findings="$(printf '%s\n' "$output" |
		awk '/^Findings/ { f = 1; next } f && /^$/ { f = 0 } f { print }')"
	refute [ -n "$(printf '%s\n' "$findings" | grep -i 'god object')" ]
	refute [ -n "$(printf '%s\n' "$findings" | grep -i 'utility sink')" ]
	refute [ -n "$(printf '%s\n' "$findings" | grep -i 'peripheral')" ]
}

# ------------------------------------------------------ the thresholds --

@test "HLR-171: the section names the thresholds as elc's own heuristic" {
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "elc heuristic — not a published standard"
}

@test "HLR-171: the five thresholds in force appear in the heading" {
	elc --verbose "$TREE"
	assert_success
	assert_output --partial \
		"sink at authority >= 90% and hub <= 10%, god object at betweenness >= 90% and hub >= 90%, peripheral below core depth 2"
}

@test "HLR-170: the core depth is configurable in both directions" {
	# Shallower keeps the leaf; deeper strips everything the centrality
	# tests did not already name. The two centrality classifications hold
	# either way, because they are asked first.
	elc --verbose --core-depth 1 "$TREE"
	assert_success
	assert_equal "$(class_of helper_c)" ""
	assert_equal "$(class_of dispatch)" "god object"
	assert_output --partial "10 functions retained, 11 call edges masked"

	elc --verbose --core-depth 3 "$TREE"
	assert_success
	assert_equal "$(class_of store_put)" "peripheral"
	assert_equal "$(class_of util_log)" "utility sink"
	assert_output --partial "2 functions retained, 15 call edges masked"
}

@test "HLR-171: the centrality thresholds are configurable" {
	# A sink must outrank every other function's authority by default. Ask
	# for a hub rank no function in this tree can be under and the sink
	# stops being one, while the god object — which the hub threshold does
	# not govern in that direction — is untouched.
	elc --verbose --sink-authority 100 --god-betweenness 100 "$TREE"
	assert_success
	assert_equal "$(class_of util_log)" "utility sink"
	assert_equal "$(class_of dispatch)" "god object"
}

@test "HLR-063: a rank threshold above 100 is a usage error" {
	# A percentage of the other functions has a ceiling, and a threshold
	# above it names a position no node can occupy. Rejected rather than
	# silently classifying nothing.
	elc --sink-authority 101 "$TREE"
	assert_failure
	assert_output --partial "above 100"
}

# ------------------------------------------------------- the reporting --

@test "HLR-174: the report goes to the results destination, not to stdout" {
	# HLR-038 reserves standard output. A run redirecting its report to a
	# file must not have a second report appear on the terminal, and the
	# purification section is a result like every other.
	local out="$BATS_TEST_TMPDIR/report.txt"

	run bash -c '"$0" --verbose -o "$1" "$2"' "$ELC" "$out" "$TREE"
	assert_success
	assert_output ""
	grep -q "^Graph purification" "$out"
}

@test "HLR-150: the section is a detail tier" {
	# One row per classified function, so the partition rule puts it with
	# the other per-entity tables rather than in the summary.
	elc "$TREE"
	assert_success
	assert_equal "$(purify_heading)" ""

	elc --verbose "$TREE"
	assert_success
	[ -n "$(purify_heading)" ]
}

@test "HLR-031: both human formats present the section" {
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "Graph purification"

	elc --verbose -f md "$TREE"
	assert_success
	assert_output --partial "## Graph purification"
}

@test "HLR-054: the record carries the classifications and regenerates them" {
	# A record has no graph to recompute a centrality over, so a
	# classification absent from it is one a regenerated report cannot
	# present.
	local record="$BATS_TEST_TMPDIR/record.xml"

	run bash -c '"$0" -f xml "$1" > "$2"' "$ELC" "$TREE" "$record"
	assert_success
	grep -q 'class="god object"' "$record"
	grep -q 'class="utility sink"' "$record"
	grep -q 'sink-authority="90"' "$record"

	elc --verbose --from-xml "$record"
	assert_success
	assert_output --partial "god object"
	assert_output --partial "9 functions retained, 12 call edges masked"
	assert_output --partial "elc heuristic — not a published standard"
}

@test "HLR-179: two runs over the same tree classify identically" {
	# HITS is iterative and its scores are approximations, so the ordering
	# they are ranked in is the part that has to be pinned rather than
	# assumed.
	elc --verbose "$TREE"
	assert_success
	local first="$output"

	elc --verbose "$TREE"
	assert_success
	assert_equal "$output" "$first"
}

@test "HLR-179: the classification does not depend on the order of the targets" {
	# The graph is the same graph whichever way its files were reached, so
	# the ranking read off it must be too. Node identifiers run in the
	# report's sorted file order, which is what makes that true.
	elc --verbose "$TREE/app" "$TREE/feat" "$TREE/store" "$TREE/util"
	assert_success
	local first
	first="$(printf '%s\n' "$output" |
		awk '/^Graph purification/ { f = 1; next } f && /^$/ { f = 0 }
		     f { print }')"

	elc --verbose "$TREE/util" "$TREE/store" "$TREE/feat" "$TREE/app"
	assert_success
	local second
	second="$(printf '%s\n' "$output" |
		awk '/^Graph purification/ { f = 1; next } f && /^$/ { f = 0 }
		     f { print }')"

	assert_equal "$second" "$first"
}
