#!/usr/bin/env bats
# test/fixtures/thresholds.bats — banding, severity, and attribution (STP §5).
#
# Expected values are worked out by hand and justified in thresholds/README.md
# beside this file. Never regenerate them from elc's output.
#
# This is the only module in elc that judges. The tests that matter most are
# the ones asserting it stays silent: five of the eight fan-out boundaries
# produce nothing, and an implementation banding them all would pass a suite
# that only checked the two that warn.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/thresholds/tree"
	ARCH="$BATS_TEST_DIRNAME/arch"
	REACH="$BATS_TEST_DIRNAME/reachability/tree"
}

# The findings, as "severity|subject", scoped to the section and terminated at
# its blank line.
findings() {
	printf '%s\n' "$output" |
		awk '/^Findings/ { f = 1; next } f && /^$/ { f = 0 }
		     f && ($1 == "critical" || $1 == "warning" || $1 == "info") {
			     print $1 }'
}

# One subject's severity, or empty when it produced no finding.
#
# The subject is matched as a whole field anywhere in the row rather than at a
# fixed position: a measurement name may be several words ("single-function
# global"), so awk's positional fields do not line up with the columns.
# The severity of one subject's finding, for one measurement.
#
# Scoped to the measurement as well as the subject since HLR-192: a function
# can now carry a fan-out finding and a maintainability one at once, and a
# lookup by subject alone returns both and compares equal to neither.
# Defaults to fan-out, which is what this group is about.
severity_of() {
	local measure="${2:-fan-out}"
	printf '%s\n' "$output" |
		awk -v want="$1" -v measure="$measure" \
		    '/^Findings/ { f = 1; next } f && /^$/ { f = 0 }
		     f && ($1 == "critical" || $1 == "warning" ||
		           $1 == "info") && index($0, measure) &&
		     $0 ~ ("(^| )" want "( |$)") { print $1 }'
}

# The Source column of one subject's finding — everything after the last
# column gap, the detail's own words being single-spaced.
source_of() {
	local measure="${2:-fan-out}"
	printf '%s\n' "$output" |
		awk -v want="$1" -v measure="$measure" \
		    '/^Findings/ { f = 1; next } f && /^$/ { f = 0 }
		     f && ($1 == "critical" || $1 == "warning" ||
		           $1 == "info") && index($0, measure) &&
		     $0 ~ ("(^| )" want "( |$)") { print }' |
		sed 's/ *$//; s/^.*  //'
}

# The findings for one measurement, counted. Scoped since HLR-192, for the
# reason `severity_of` is: one function can carry several findings now, and a
# count over all of them stops being a statement about the band under test.
finding_rows() {
	local measure="${1:-fan-out}"
	printf '%s\n' "$output" |
		awk -v measure="$measure" \
		    '/^Findings/ { f = 1; next } f && /^$/ { f = 0 }
		     f && ($1 == "critical" || $1 == "warning" ||
		           $1 == "info") && $2 == measure { n++ }
		     END { print n + 0 }'
}

# ------------------------------------------------------- the fan-out bands --

@test "HLR-086: each boundary value classifies into exactly one band" {
	elc --entry bands_entry "$TREE/bands.c"
	assert_success

	assert_equal "$(severity_of band_warn_low)" "warning"
	assert_equal "$(severity_of band_warn_high)" "warning"
	assert_equal "$(severity_of band_critical_one)" "critical"
}

@test "HLR-086: the bands below the warning threshold produce nothing" {
	# Five of the eight boundaries. An implementation banding 8 and 10 as
	# warnings would pass the test above and fail this one.
	elc --entry bands_entry "$TREE/bands.c"
	assert_success

	assert_equal "$(severity_of band_below)" ""
	assert_equal "$(severity_of band_healthy_low)" ""
	assert_equal "$(severity_of band_healthy_high)" ""
	assert_equal "$(severity_of band_acceptable_low)" ""
	assert_equal "$(severity_of band_acceptable_high)" ""
}

@test "HLR-086: the acceptable band is silent, not a gap" {
	# 8-10 was missing from an earlier reading of the thresholds, which is
	# why the requirement states the bands are exhaustive.
	elc --entry bands_entry "$TREE/bands.c"
	assert_success
	assert_equal "$(severity_of band_acceptable_high)" ""
}

@test "HLR-086: exactly three fan-out findings come out of the boundary file" {
	# The count is what catches a band claiming a value twice. Scoped to
	# fan-out: the same file now also produces maintainability findings,
	# which are a different band's business (HLR-192).
	elc --entry bands_entry "$TREE/bands.c"
	assert_success
	assert_equal "$(finding_rows fan-out)" "3"
}

@test "HLR-031: a measurement inside its band is still reported" {
	# The Findings section is the subset that crossed a line. Every value
	# is still in the table that measured it.
	elc --verbose --entry bands_entry "$TREE/bands.c"
	assert_success

	# The column is found by *name* in the header rather than counted from
	# either end of the row. It used to be read as `NF-1`, on the grounds
	# that the Maintainability Index was last — which stopped being true
	# the moment a column was added after it, and would stop being true
	# again on the next such change. What this test is about is the value
	# under `Fan-out`, so that is what it looks up.
	local fanout
	fanout="$(printf '%s\n' "$output" |
		awk '/^Functions$/ { f = 1; next }
		     f && /^$/    { f = 0 }
		     f && !col    { for (i = 1; i <= NF; i++)
		                            if ($i == "Out") col = i
		                    next }
		     f && col && $3 == "band_acceptable_high" { print $col }')"
	assert_equal "$fanout" "10"
}

# ------------------------------------------------------------- attribution --

@test "HLR-099: every finding names its source" {
	elc --entry bands_entry "$TREE/bands.c"
	assert_success

	local unattributed
	unattributed="$(printf '%s\n' "$output" |
		awk '/^Findings/ { f = 1; next } f && /^$/ { f = 0 }
		     f && ($1 == "critical" || $1 == "warning") && NF < 5 { n++ }
		     END { print n + 0 }')"
	assert_equal "$unattributed" "0"
	assert_equal "$(source_of band_critical_one)" "Henry-Kafura"
}

@test "HLR-099: recursion is attributed to MISRA C Rule 17.2" {
	elc "$ARCH/cycles"
	assert_success
	assert_output --partial "MISRA C Rule 17.2"
}

@test "HLR-099: a single-function global is attributed to MISRA C Rule 8.9" {
	elc "$REACH/globals.c"
	assert_success
	assert_equal "$(severity_of solo_owned "single-function global")" "warning"
	assert_output --partial "MISRA C Rule 8.9"
}

@test "HLR-099: the bottleneck threshold is marked as elc's own" {
	# The label that keeps the "no built-in opinion" claim honest while
	# shipping MISRA and Martin values beside it.
	elc -b 1 --stratum "app:*/app/*" --stratum "hal:*/hal/*" \
	    --stratum "drv:*/drv/*" "$ARCH/tree"
	assert_success
	assert_output --partial "elc heuristic — not a published standard"
}

@test "HLR-099: the label appears on exactly the rows that earn it" {
	# Paired with the test above. Stated as a correspondence rather than an
	# absence, because three measurements now earn the marker — the
	# bottleneck, fan-in, and maintainability — and a report containing one
	# of them legitimately contains the words (HLR-186, HLR-192).
	#
	# What must never happen is a row citing MISRA, Martin, McCabe or
	# Henry-Kafura carrying it, or a row of elc's own going without.
	elc --entry bands_entry "$TREE/bands.c"
	assert_success

	local mismatched
	mismatched="$(printf '%s\n' "$output" |
		awk '/^Findings/ { f = 1; next } f && /^$/ { f = 0 }
		     f && ($1 == "critical" || $1 == "warning" ||
		           $1 == "info") {
			own = index($0, "elc heuristic") > 0
			earns = ($2 == "bottleneck" || $2 == "fan-in" ||
			         $2 == "maintainability")
			if (own != earns) n++
		     }
		     END { print n + 0 }')"
	assert_equal "$mismatched" "0"
}

# ---------------------------------------------------------------- severity --

@test "HLR-084: every dependency cycle is reported at critical severity" {
	elc "$ARCH/cycles"
	assert_success

	local kinds
	kinds="$(printf '%s\n' "$output" |
		awk '/^Findings/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /component dependency cycle/ { print $1 }')"
	assert_equal "$kinds" "critical"
}

@test "HLR-123: every finding carries a severity from the closed set" {
	elc -b 1 --stratum "app:*/app/*" --stratum "hal:*/hal/*" \
	    --stratum "drv:*/drv/*" "$ARCH/tree"
	assert_success

	local outside
	outside="$(findings | awk '
		$1 != "info" && $1 != "warning" && $1 != "critical" { n++ }
		END { print n + 0 }')"
	assert_equal "$outside" "0"
}

@test "HLR-123: findings are ranked most severe first" {
	elc -b 1 --stratum "app:*/app/*" --stratum "hal:*/hal/*" \
	    --stratum "drv:*/drv/*" "$ARCH/tree"
	assert_success

	# critical rows precede warning rows; the list is worked from the top.
	local order
	order="$(findings | uniq)"
	assert_equal "$order" "critical
warning"
}

@test "HLR-100: severity does not move the exit status" {
	# A project full of critical findings still exits 0 when every file was
	# read. The exit status is reserved for failures; deciding what a
	# critical finding warrants is the caller's business.
	run "$ELC" "$ARCH/cycles"
	assert_equal "$status" 0
	assert_output --partial "critical"
}

@test "HLR-100: the summary counts findings without gating on them" {
	elc "$ARCH/cycles"
	assert_success
	# Two criticals here: the component cycle and the recursion, which are
	# different facts about the same pair of files.
	assert_output --regexp "Critical findings +2"
	assert_output --regexp "Warnings +[0-9]"
}

# ------------------------------------------------------- the empty result --

@test "HLR-098: a project inside every band reports no findings" {
	elc --entry clean_entry "$TREE/clean.c"
	assert_success
	assert_equal "$(finding_rows)" "0"
}

@test "HLR-031: the empty Findings section is still emitted" {
	# A result, not an absence. A suppressed section is indistinguishable
	# from a renderer that forgot.
	elc --entry clean_entry "$TREE/clean.c"
	assert_success
	assert_output --partial "Findings"
	assert_output --regexp "Critical findings +0"
}

# ---------------------------------------------------------- no advice given --

@test "HLR-101: no finding proposes a fix" {
	# elc reports where a measurement falls and which standard says so. It
	# does not say what to do about it. Imperative openings are the shape
	# that advice takes, so their absence is what is asserted.
	elc -b 1 --stratum "app:*/app/*" --stratum "hal:*/hal/*" \
	    --stratum "drv:*/drv/*" "$ARCH/tree"
	assert_success

	local advice
	advice="$(printf '%s\n' "$output" |
		awk '/^Findings/ { f = 1; next } f && /^$/ { f = 0 }
		     f' |
		grep -ciE "you should|consider |refactor|split this|rewrite|must be fixed|recommend" || true)"
	assert_equal "$advice" "0"
}

# --------------------------------------------------------------- the record */

@test "HLR-032: findings survive a record round trip byte-identically" {
	elc -b 1 --stratum "app:*/app/*" --stratum "hal:*/hal/*" \
	    --stratum "drv:*/drv/*" -f xml -o "$BATS_TEST_TMPDIR/rec.xml" \
	    "$ARCH/tree"
	assert_success

	run "$ELC" -b 1 --stratum "app:*/app/*" --stratum "hal:*/hal/*" \
	    --stratum "drv:*/drv/*" -f md "$ARCH/tree"
	assert_success
	printf '%s\n' "$output" > "$BATS_TEST_TMPDIR/live.md"

	run "$ELC" --from-xml "$BATS_TEST_TMPDIR/rec.xml"
	assert_success
	printf '%s\n' "$output" > "$BATS_TEST_TMPDIR/regen.md"

	run diff "$BATS_TEST_TMPDIR/live.md" "$BATS_TEST_TMPDIR/regen.md"
	assert_success
}

@test "HLR-056: a regenerated report keeps each finding's attribution" {
	# The citation cannot be re-derived on regeneration, which has no
	# measurements to band, so the record carries it.
	elc -f xml -o "$BATS_TEST_TMPDIR/c.xml" "$ARCH/cycles"
	assert_success

	run "$ELC" --from-xml "$BATS_TEST_TMPDIR/c.xml"
	assert_success
	assert_output --partial "MISRA C Rule 17.2"
	assert_output --partial "Martin, acyclic dependencies"
}
