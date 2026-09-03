#!/usr/bin/env bats
# test/fixtures/conditional.bats — measuring one configuration (STP §5).
#
# Expected values are worked out by hand and justified in conditional/README.md
# beside the fixture. Never regenerate them from elc's output.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/conditional/tree"
	CHAIN="$BATS_TEST_DIRNAME/conditional/chain"
	FACTS="$BATS_TEST_DIRNAME/conditional/facts"
	NOCOND="$BATS_TEST_DIRNAME/conditional/nocond"
}

# The functions elc reported, in the order the report presents them.
#
# awk with a blank-line terminator, or the extractor runs into the section that
# follows; the header row is skipped by name rather than by position, because
# skipping "the first two lines" breaks the moment a column is added.
functions_of() {
	awk '/^Functions/ {s=1; next}
	     s && /^$/ {exit}
	     s && $1 == "File" {next}
	     s && /^  [^ -]/ {print $3}' "$1" | sort | tr '\n' ' '
}

summary_of() {
	awk -v want="$2" '$0 ~ "^  " want "  *[0-9]+$" {print $NF}' "$1"
}

# Run elc over a target, capturing the report for the extractors above.
#
# Verbose, and written to a `.txt`: the extractors read the aligned table,
# and both of those are now stated rather than defaulted. The extension names
# the format (HLR-148), and the per-function tiers these tests measure are
# omitted from the default composition (HLR-150).
report() {
	local target="$1"
	shift
	run bash -c '"$0" --verbose -o "$1" "${@:3}" "$2" 2>/dev/null' \
		"$ELC" "$BATS_TEST_TMPDIR/report.txt" "$target" "$@"
	OUT="$BATS_TEST_TMPDIR/report.txt"
}

# ------------------------------------------------------- the hand counts --

@test "the hand-counted totals match with no definitions" {
	report "$TREE"
	assert_success
	assert_equal "$(summary_of "$OUT" Functions)" "9"
	assert_equal "$(summary_of "$OUT" ELOC)" "9"
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "5"
}

@test "HLR-132: -DFEATURE measures the configuration in which it is defined" {
	report "$TREE" -DFEATURE
	assert_success
	assert_equal "$(functions_of "$OUT")" \
		"always always_rust fat only_a outer undecidable with_feature without_b "
	assert_equal "$(summary_of "$OUT" ELOC)" "8"
}

@test "HLR-132: a definition can make a condition false as well as true" {
	# #ifndef LEAN with -DLEAN: the symbol is defined, the negation makes
	# the condition false, and the consequence goes. A rule that only ever
	# pruned #else branches would pass every other case and fail this one.
	report "$TREE" -DLEAN
	assert_success
	assert_equal "$(functions_of "$OUT")" \
		"always always_rust only_a outer undecidable with_feature without_b without_feature "
}

@test "HLR-132: two definitions compose" {
	report "$TREE" -DFEATURE -DLEAN
	assert_success
	assert_equal "$(summary_of "$OUT" Functions)" "7"
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "3"
}

# --------------------------------------------------------- what decides --

@test "HLR-131: #if 0 prunes with no definitions supplied" {
	# The one place "with no definitions nothing changes" does not read
	# literally, and deliberately: a constant condition is the same in
	# every configuration, so it needs no configuration to decide. This
	# reverses the Phase 3 judgement in doc/notes.md §3.
	report "$TREE"
	assert_success

	run bash -c 'grep -c "never_built" "$0" || true' "$OUT"
	assert_output "0"
}

@test "HLR-133: a symbol no -D mentions leaves both branches counted" {
	# Not false — undecidable. A build may define FEATURE in a header elc
	# never sees, so both branches stay and the count says so.
	report "$TREE"
	assert_success

	run bash -c 'grep -c "with_feature\|without_feature" "$0"' "$OUT"
	[ "$output" -ge 2 ]
}

@test "HLR-133: an undecidable condition is counted, not silently kept" {
	# #if VERSION > 2 needs macro values elc does not have. Leaving it
	# active is the safe direction; leaving it *unreported* would hide how
	# incomplete the pruning was.
	report "$TREE"
	assert_success
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "5"

	run bash -c 'grep -c "undecidable" "$0"' "$OUT"
	[ "$output" -ge 1 ]
}

@test "HLR-133: a region inside an uncompiled region is not counted undecided" {
	# nested.c's #ifdef INNER is undecidable on its own terms and lies
	# inside #if 0. A region nobody builds has no condition worth
	# reporting, and counting it would inflate the one figure a reader uses
	# to judge the pruning.
	report "$BATS_TEST_DIRNAME/conditional/tree/nested.c"
	assert_success
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "0"
	assert_equal "$(summary_of "$OUT" Functions)" "1"
}

@test "HLR-131: defining a symbol the tree never mentions changes nothing" {
	report "$TREE"
	assert_success
	local before
	before="$(functions_of "$OUT")"

	report "$TREE" -DNOTHING_USES_THIS
	assert_success
	assert_equal "$(functions_of "$OUT")" "$before"
}

# ------------------------------------------------------------ the chain --

@test "the alternative of a leading #if is the whole rest of the chain" {
	report "$CHAIN" -DALPHA
	assert_success
	assert_equal "$(functions_of "$OUT")" "alpha "
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "0"
}

@test "an #elif is decided on its own terms when the head is undecided" {
	report "$CHAIN" -DBETA
	assert_success
	assert_equal "$(functions_of "$OUT")" "alpha beta "
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "1"
}

@test "an undecided chain keeps every branch" {
	report "$CHAIN"
	assert_success
	assert_equal "$(functions_of "$OUT")" "alpha beta neither "
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "2"
}

# ------------------------------------------------------- another syntax --

@test "HLR-134: a Rust cfg attribute prunes by the same mechanism" {
	# #[cfg(not(feature_b))] with -Dfeature_b: the symbol is known, the
	# negation makes the condition false, and the item goes. No line of
	# src/ changed to support Rust's shape.
	report "$TREE" -Dfeature_b
	assert_success

	run bash -c 'grep -c "without_b" "$0" || true' "$OUT"
	assert_output "0"
}

@test "HLR-134: a cfg with no else cannot be pruned by defining its symbol" {
	# #[cfg(feature_a)] with -Dfeature_a decides the condition *true* and
	# so prunes an alternative that does not exist. The asymmetry is the
	# language's, not elc's, and is worth pinning so it is not read as a
	# defect later.
	report "$TREE" -Dfeature_a
	assert_success

	run bash -c 'grep -c "only_a" "$0"' "$OUT"
	[ "$output" -ge 1 ]
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "4"
}

# --------------------------------------------------- reported and recorded --

@test "HLR-136: the definitions in force are reported" {
	report "$TREE" -DFEATURE -DLEAN
	assert_success

	run bash -c 'grep -c "^Conditional-compilation definitions (2)$" "$0"' \
		"$OUT"
	assert_output "1"
}

@test "HLR-031: the section is emitted with no definitions supplied" {
	# "Measured with no definitions" and "measured with these" are
	# different claims, and a reader of a report showing neither could not
	# tell which they had.
	report "$TREE"
	assert_success

	# With none supplied the table has no rows, so it is named in the
	# closing statement rather than printed — which is still the claim
	# being made, in the same words (HLR-188, HLR-189).
	run bash -c 'grep -c "^    - Conditional-compilation definitions (0)$" "$0"' \
		"$OUT"
	assert_output "1"
}

@test "HLR-136: the definitions are reported in a stable order" {
	# The order they were typed in is not a property of the run.
	run bash -c '"$0" -DZULU -DALPHA "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success
	local first="$output"

	run bash -c '"$0" -DALPHA -DZULU "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success
	assert_equal "$output" "$first"
}

@test "HLR-056: a configured run survives a record round trip" {
	local record="$BATS_TEST_TMPDIR/record.xml"
	local direct="$BATS_TEST_TMPDIR/direct.md"
	local again="$BATS_TEST_TMPDIR/again.md"

	run bash -c '"$0" -DFEATURE -f md "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success
	printf '%s\n' "$output" > "$direct"

	run bash -c '"$0" -DFEATURE -f xml "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success
	printf '%s\n' "$output" > "$record"

	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	printf '%s\n' "$output" > "$again"

	run diff -u "$direct" "$again"
	assert_success
}

@test "HLR-136: the record names the configuration it describes" {
	run bash -c '"$0" -DFEATURE -f xml "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success

	local record="$BATS_TEST_TMPDIR/record.xml"
	printf '%s\n' "$output" > "$record"

	run bash -c 'grep -c "value=\"FEATURE\"" "$0"' "$record"
	assert_output "1"
	run bash -c 'grep -c "undecided-regions=\"4\"" "$0"' "$record"
	assert_output "1"
}

@test "HLR-063: -D with --from-xml is a usage error" {
	# Pruning happens when a file is measured, so a record already
	# describes one configuration and cannot be re-cut into another.
	run bash -c '"$0" -f xml "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success

	local record="$BATS_TEST_TMPDIR/record.xml"
	printf '%s\n' "$output" > "$record"

	elc --from-xml "$record" -DFEATURE
	assert_equal "$status" 2
}

@test "HLR-032: two configured runs produce identical output" {
	run bash -c '"$0" -DFEATURE -f md "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success
	local first="$output"

	run bash -c '"$0" -DFEATURE -f md "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success
	assert_equal "$output" "$first"
}

# ------------------------------------------------------ every graph fact --

@test "HLR-132: a call inside an uncompiled region is not a call of this build" {
	# caller survives; the call to helper inside its #if 0 does not. A
	# pruning that only removed whole functions would pass every other case
	# in this group and fail this one.
	report "$FACTS" --entry caller
	assert_success

	# The column is found by *name* in the header. Counting from the right
	# was tried and was wrong for the same reason counting from the left
	# would be: the table gains columns on both sides, and a count from
	# either end silently starts reading a different measurement rather
	# than failing. The name is the only part of this that is stable.
	local fanout
	fanout="$(awk '/^Functions$/ {s=1; next}
	               s && /^$/     {exit}
	               s && !col     {for (i = 1; i <= NF; i++)
	                                      if ($i == "Out") col = i
	                              next}
	               s && col && $3 == "caller" {print $col}' "$OUT")"
	assert_equal "$fanout" "0"
}

@test "HLR-132: a decision point inside an uncompiled region is not counted" {
	report "$FACTS" --entry caller
	assert_success

	local complexity
	complexity="$(awk '/^Functions$/ {s=1; next}
	                   s && /^$/ {exit}
	                   s && $3 == "caller" {print $7}' "$OUT")"
	assert_equal "$complexity" "1"
}

@test "HLR-132: a global access inside an uncompiled region is not a fact" {
	report "$FACTS" --entry caller
	assert_success

	# shared_flag is declared outside the region and read only inside it,
	# so this build touches it nowhere and no row describes it.
	local rows
	rows="$(awk '/^Global state/ {s=1; next}
	             s && /^$/ {exit}
	             s && /^  [^ -]/ && $1 != "Object" {print}' "$OUT" | wc -l)"
	assert_equal "$rows" "0"
}

# --------------------------------------------- a language without the file --

@test "LLR-CND-08: a language supplying no conditional query has none" {
	# Python ships no conditionals.scm, and that is a choice the contract
	# allows rather than a broken module: it is measured normally and
	# nothing is excluded or counted undecided.
	report "$NOCOND"
	assert_success
	assert_equal "$(summary_of "$OUT" Functions)" "1"
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "0"
}

@test "LLR-CND-08: a definition changes nothing for such a language" {
	report "$NOCOND"
	assert_success
	local before
	before="$(cat "$OUT")"

	report "$NOCOND" -DANYTHING
	assert_success
	# The definitions section names the definition; nothing else moves.
	run diff <(printf '%s\n' "$before") "$OUT"
	assert_output --partial "ANYTHING"
}
