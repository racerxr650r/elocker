#!/usr/bin/env bats
# test/integration/verbosity.bats — the summary and verbose compositions, and
# the formats verbosity does not reach (STP §3).
#
# The partition itself is asserted section by section here, because HLR-150
# requires it to be a published property of the report rather than whatever a
# renderer happened to do. A test that only counted lines would pass against a
# renderer that dropped the findings — which is the one section a reader acts
# on, and the one the summary must keep.

setup() {
	load "../helpers/common"

	TREE="$BATS_TEST_TMPDIR/tree"
	mkdir -p "$TREE"
	printf 'int helper(int n)\n{\n\tif (n)\n\t\treturn 1;\n\treturn 0;\n}\n\nint main(void)\n{\n\treturn helper(1);\n}\n' \
		> "$TREE/a.c"
}

# The headings of a report, in order: a section's heading is its presence.
headings() {
	printf '%s\n' "$output" | awk '/^[A-Z]/ { print }'
}

has_heading() {
	printf '%s\n' "$output" | grep -qE "^(## )?$1"
}

# Whether the traversal *reached* a section, printed or not.
#
# Since HLR-188 a section with no rows prints nothing and is named in the
# closing statement instead, so "is this tier in this composition" is a
# question about the union of the two. A tier a verbosity filtered out appears
# in neither, which is what these tests are distinguishing (HLR-189).
reaches() {
	has_heading "$1" ||
		printf '%s\n' "$output" | grep -qE "^ *- $1"
}

# --- the summary tiers (HLR-150) -------------------------------------------

@test "HLR-150: the summary tiers are present by default" {
	# Asserted against Markdown. HLR-150's partition is a document's rule,
	# and since HLR-218 the aligned table answers with its own — the three
	# tests below this one are where that answer is asserted.
	elc -f md "$TREE"
	assert_success

	# Every tier HLR-150 enumerates, printed where it found rows and named
	# in the closing statement where it did not.
	for heading in "Project summary" "Callouts" "Discovery" "Languages" \
	               "Files" "At or over a threshold" \
	               "Findings" "Conditional-compilation definitions" \
	               "Partially parsed files" "Skipped files"; do
		reaches "$heading" || {
			echo "the summary omitted '$heading'" >&2
			false
		}
	done
}

@test "HLR-150: the detail tiers are absent by default" {
	elc -f md "$TREE"
	assert_success

	# One row per function, per global object, per graph edge, per
	# unreachable statement, per custom-rule match: none of them by
	# default.
	for heading in "Functions" "Recursion" "Component coupling" \
	               "Component dependency" "Global state" \
	               "Unreachable globals" "Dead code within functions" \
	               "Custom rule matches"; do
		if reaches "$heading"; then
			echo "the summary presented the detail tier '$heading'" >&2
			false
		fi
	done
}

@test "HLR-150: the summary keeps the findings a reader acts on" {
	# The section whose loss would make the summary shorter and useless.
	# Provoked rather than assumed: `spread` calls sixteen distinct
	# subroutines, which is a critical fan-out, so there is a finding to
	# keep (HLR-086).
	{
		for i in $(seq 1 16); do printf 'int s%d(void){return %d;}\n' "$i" "$i"; done
		printf 'int spread(void)\n{\n\treturn '
		for i in $(seq 1 15); do printf 's%d() + ' "$i"; done
		printf 's16();\n}\n'
	} > "$TREE/spread.c"

	elc "$TREE/spread.c"
	assert_success
	has_heading "Findings"
	assert_output --partial "spread"
}

@test "HLR-182: the findings are the first section after the project summary" {
	# The tier a reader is expected to act on, ahead of the tables that
	# supply its evidence rather than below six hundred rows of them.
	# `branchy` has eleven decision points, which is a complexity warning.
	{
		printf 'int branchy(int n)\n{\n'
		for _ in $(seq 1 10); do printf '\tif (n) n++;\n'; done
		printf '\treturn n;\n}\n'
	} > "$TREE/branchy.c"

	elc "$TREE/branchy.c"
	assert_success
	assert_equal "$(headings | sed -n '1,2p' | tr '\n' '|')" \
	             "Project summary|Findings|"
}

# --- empty tables (HLR-188, HLR-189) ---------------------------------------

@test "HLR-188: a table with no rows is not printed" {
	elc --verbose "$TREE"
	assert_success
	! has_heading "Recursion"
}

@test "HLR-189: the closing statement names every table that was empty" {
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "Nothing to report"
	assert_output --partial "    - Recursion"
}

@test "HLR-189: the closing statement is present when nothing was empty" {
	# The statement is not conditional on there being something to say: a
	# section that appears only sometimes is the problem it solves.
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "Nothing to report"
}

@test "HLR-115: the reason survives the table being omitted" {
	# The heading carried the reason, and the heading is what the closing
	# statement names — so an analysis nobody declared for still says why
	# it did not run.
	elc "$TREE"
	assert_success
	assert_output --partial \
		"- Layering (omitted: no architectural strata declared, see --stratum)"
}

@test "HLR-115: an omitted analysis states its reason in the summary too" {
	# The omission notices are summary tiers even though the sections
	# carrying them are detail tiers: an analysis nobody declared for is a
	# thing the reader must be told, at either verbosity.
	elc "$TREE"
	assert_success
	assert_output --partial "Unreachable functions (omitted: no entry points declared"
	assert_output --partial "Deepest call chain (omitted: no entry points declared"
	assert_output --partial "Layering (omitted: no architectural strata declared"
	assert_output --partial "Cross-scope access (omitted: no execution scopes declared"
}

@test "HLR-183: the function table carries the degrees beside the metrics" {
	# One table where there were three: Functions, Fan-out, and
	# Information flow all listed the same functions in the same order.
	elc --verbose "$TREE"
	assert_success
	has_heading "Functions"
	assert_output --regexp "Function +Visibility +Lines +ELOC +Complexity +Fan-in +Fan-out +MBS +WF-out +TBI +Burden"
	! has_heading "Fan-out \\(distinct callees\\)"
	! has_heading "Information flow"
	refute_output --partial "Henry-Kafura;"
}

@test "HLR-150: an analysis that was measured is not in the summary" {
	# The converse of the notice above, and what keeps that test from
	# passing against a renderer that simply always emits the section.
	elc --entry main "$TREE"
	assert_success
	refute_output --partial "Unreachable functions (omitted"
	refute_output --partial "Deepest call chain (omitted"
}

# --- the verbose report (HLR-151) ------------------------------------------

@test "HLR-151: --verbose presents the detail tiers as well" {
	elc --verbose "$TREE"
	assert_success

	for heading in "Functions" "Recursion" "Component coupling" \
	               "Component dependency" "Global state" \
	               "Unreachable globals" "Dead code within functions" \
	               "Custom rule matches"; do
		reaches "$heading" || {
			echo "the verbose report omitted '$heading'" >&2
			false
		}
	done
}

@test "HLR-151: the verbose report is the summary plus the detail tiers" {
	# Stated as a superset rather than as a list, so that a tier added to
	# the traversal in a later phase cannot satisfy one composition and be
	# forgotten in the other (LLR-SUM-07).
	elc "$TREE"
	local summary_headings
	summary_headings="$(headings)"

	elc --verbose "$TREE"
	local verbose_headings
	verbose_headings="$(headings)"

	# Every heading the summary printed, the verbose report prints too.
	local missing
	missing="$(comm -23 <(sort <<<"$summary_headings") \
	                    <(sort <<<"$verbose_headings"))"
	assert_equal "$missing" ""

	# And it prints strictly more, or the option would select nothing.
	[ "$(wc -l <<<"$verbose_headings")" -gt \
	  "$(wc -l <<<"$summary_headings")" ]
}

@test "HLR-151: -v is the short form of --verbose" {
	elc --verbose "$TREE"
	local long="$output"
	elc -v "$TREE"
	assert_equal "$output" "$long"
}

@test "HLR-151: verbosity changes no measurement and no exit status" {
	# A value absent from a summary is absent because it was not printed,
	# never because it was not computed — so the record, which carries
	# every measurement either composition can present, is identical.
	elc -f xml "$TREE"
	local plain="$output"
	local plain_status="$status"

	elc --verbose -f xml "$TREE"
	assert_equal "$output" "$plain"
	assert_equal "$status" "$plain_status"
}

@test "HLR-031: both human formats present the same tiers when verbose" {
	# Uniformity across formats — never across verbosities, and since
	# HLR-218 never at the default verbosity either, where the two formats
	# deliberately compose differently. What survives, and is the property
	# HLR-031 is actually about, is that a tier exists in both formats:
	# a verbose run of each presents the same tiers in the same order, so
	# a section cannot be present in one format and missing from the other.
	elc --verbose "$TREE"
	local table
	table="$(headings)"

	elc --verbose -f md "$TREE"
	local markdown
	markdown="$(printf '%s\n' "$output" |
		awk '/^## / { sub(/^## /, ""); print }')"

	assert_equal "$markdown" "$table"
}

# --- the complete-record formats (HLR-152) ---------------------------------

@test "HLR-152: --verbose with an xml output is accepted, not rejected" {
	# The one option pairing this project defines that is *not* a usage
	# error. There is nothing contradictory in asking a complete format for
	# detail; the request simply has no effect.
	elc --verbose -f xml "$TREE"
	assert_success
}

@test "HLR-152: --verbose with a csv output is accepted, not rejected" {
	elc --verbose -f csv "$TREE"
	assert_success
}

@test "HLR-152: csv is byte-identical at either verbosity" {
	elc -f csv "$TREE"
	local plain="$output"
	elc --verbose -f csv "$TREE"
	assert_equal "$output" "$plain"
}

# --- regeneration at either verbosity (HLR-056) ----------------------------

@test "HLR-056: a record regenerated summarily matches a direct summary run" {
	elc -f xml -o "$BATS_TEST_TMPDIR/rec.xml" "$TREE"
	assert_success

	elc --from-xml "$BATS_TEST_TMPDIR/rec.xml"
	local regenerated="$output"

	elc -f md "$TREE"
	assert_equal "$output" "$regenerated"
}

@test "HLR-056: a record regenerated verbosely matches a direct verbose run" {
	elc -f xml -o "$BATS_TEST_TMPDIR/rec.xml" "$TREE"
	assert_success

	elc --verbose --from-xml "$BATS_TEST_TMPDIR/rec.xml"
	local regenerated="$output"

	elc --verbose -f md "$TREE"
	assert_equal "$output" "$regenerated"
}

@test "HLR-056: the two verbosities regenerate differently from one record" {
	# What keeps the pair above from passing against an implementation that
	# ignored the option in regeneration mode.
	elc -f xml -o "$BATS_TEST_TMPDIR/rec.xml" "$TREE"
	assert_success

	elc --from-xml "$BATS_TEST_TMPDIR/rec.xml"
	local summary="$output"
	elc --verbose --from-xml "$BATS_TEST_TMPDIR/rec.xml"
	refute_output "$summary"
}
