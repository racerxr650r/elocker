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
	printf '%s\n' "$output" |
		grep -v '^Parsing Notifications$' |
		awk '/^[A-Z]/ { sub(/ \([0-9]+\)$/, ""); print }'
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
	# The four HLR-150 enumerates, and no fifth. Asserted against Markdown
	# because the claim used to be a document's alone; since HLR-218 there
	# is one composition, and the test below asserts the aligned table
	# answers identically.
	elc -f md "$TREE"
	assert_success

	for heading in "Project Summary" "Findings" "Files" "Functions"; do
		reaches "$heading" || {
			echo "the summary omitted '$heading'" >&2
			false
		}
	done
}

@test "HLR-218: both formats present the same tiers by default" {
	# One composition, asserted as an equality rather than as two lists:
	# a tier that reached one format and not the other is what this must
	# catch, and neither format's list on its own would catch it.
	elc "$TREE"
	local table
	table="$(headings | sort -u)"

	elc -f md "$TREE"
	local markdown
	markdown="$(printf '%s\n' "$output" |
		awk '/^## / { sub(/^## /, ""); print }' | sort -u)"

	assert_equal "$markdown" "$table"
}

@test "HLR-150: the detail tiers are absent by default" {
	elc -f md "$TREE"
	assert_success

	# "Functions" is deliberately absent from this list: it is the one
	# detail-shaped tier every default report presents, and the test above
	# asserts that it is there. The aggregates beside it — the callouts,
	# the routes, the languages, the threshold listing — are here because
	# they are evidence for a finding rather than one of the four questions
	# a default report answers.
	for heading in "Callouts" "Discovery" "Languages" \
	               "At Or Over A Threshold" "Architecture Conformance" \
	               "Recursion" "Component Coupling" \
	               "Component Dependency" "Global State" \
	               "Unreachable Globals" "Dead Code Within Functions" \
	               "Custom Rule Matches"; do
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
	             "Project Summary|Findings|"
}

# --- the size in the heading (HLR-235) -------------------------------------

@test "HLR-235: the three tables of the minimum state their size" {
	# `a.c` holds two functions, and the tree holds one file. The figures
	# are counted here rather than read back from the table, so a renderer
	# printing the heading and the rows from different places fails.
	elc "$TREE"
	assert_success
	assert_output --partial "Files (1)"
	assert_output --partial "Functions (2)"
}

@test "HLR-235: the size is the number of rows presented" {
	# The property that makes the figure worth printing: it and the table
	# beneath it cannot disagree. Counted off the body rather than asserted
	# as a literal, so a second file in the fixture cannot make this pass
	# against a hard-coded number.
	elc --verbose "$TREE"
	assert_success

	local stated counted
	stated="$(printf '%s\n' "$output" |
		sed -nE 's/^Functions \(([0-9]+)\)$/\1/p')"
	counted="$(printf '%s\n' "$output" |
		awk '/^Functions [(]/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { n++ } END { print n + 0 }')"
	assert_equal "$stated" "$counted"
}

@test "HLR-235: a table that was not rendered is named without a size" {
	# HLR-188 prints no empty table and HLR-189 names it instead, where a
	# size would describe something that was never a table.
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "    - Recursion"
	refute_output --regexp "- Recursion \\([0-9]+\\)"
}

@test "HLR-235: a heading that already carries a clause grows no second one" {
	# The two never appear together: a tier stating thresholds or the
	# reason it was omitted does not ask for a size.
	elc --verbose "$TREE"
	assert_success
	refute_output --regexp "^Component Coupling .*\\) \\([0-9]+\\)$"
	refute_output --regexp "^Unreachable Functions .*\\) \\([0-9]+\\)$"
}

@test "HLR-235: Markdown states the same size in its own idiom" {
	# One fact in two decorations, which is the line HLR-218 draws: the
	# disclosure summary carries the figure the aligned heading carries.
	elc -f md "$TREE"
	assert_success
	assert_output --partial "## Functions"
	assert_output --partial "<summary>2 rows (click to expand)</summary>"
	refute_output --partial "## Functions (2)"
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
	assert_output --partial "Nothing To Report"
	assert_output --partial "    - Recursion"
}

@test "HLR-189: the closing statement is present when nothing was empty" {
	# The statement is not conditional on there being something to say: a
	# section that appears only sometimes is the problem it solves.
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "Nothing To Report"
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
	assert_output --partial "Unreachable Functions (omitted: no entry points declared"
	assert_output --partial "Deepest Call Chain (omitted: no entry points declared"
	assert_output --partial "Layering (omitted: no architectural strata declared"
	assert_output --partial "Cross-Scope Access (omitted: no execution scopes declared"
}

@test "HLR-183: the function table carries the degrees beside the metrics" {
	# One table where there were three: Functions, Fan-out, and
	# Information flow all listed the same functions in the same order.
	elc --verbose "$TREE"
	assert_success
	has_heading "Functions"
	assert_output --regexp "Function +Scope +Reent +Lines +ELOC +CC +In +Out +WTBI +Burden"
	! has_heading "Fan-out \\(distinct callees\\)"
	! has_heading "Information flow"
	refute_output --partial "Henry-Kafura;"
}

@test "HLR-150: an analysis that was measured is not in the summary" {
	# The converse of the notice above, and what keeps that test from
	# passing against a renderer that simply always emits the section.
	elc --entry main "$TREE"
	assert_success
	refute_output --partial "Unreachable Functions (omitted"
	refute_output --partial "Deepest Call Chain (omitted"
}

# --- the verbose report (HLR-151) ------------------------------------------

@test "HLR-151: --verbose presents the detail tiers as well" {
	elc --verbose "$TREE"
	assert_success

	for heading in "Functions" "Recursion" "Component Coupling" \
	               "Component Dependency" "Global State" \
	               "Unreachable Globals" "Dead Code Within Functions" \
	               "Custom Rule Matches"; do
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

	# The counts of HLR-235 are a decoration of the aligned style and are
	# stripped from both sides by `headings`, so this compares the tiers.

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
