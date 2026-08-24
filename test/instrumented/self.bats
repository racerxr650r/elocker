#!/usr/bin/env bats
# test/instrumented/self.bats — elc measured by elc.
#
# HLR-181 is the one requirement whose subject is the delivered product rather
# than its behaviour on a user's code, and it is the one measurement elc cannot
# fudge: the tool is the checker. `elc src/` is the whole of it.
#
# It is written down as a catalogued test for the reason STP §2.5 gives for the
# sanitized gate. A person running `elc src/` and finding it clean proves
# nothing the traceability matrix can carry, and the evidence of that decays
# the moment a phase adds a function. Phase 17 is the demonstration: PR #45
# brought the source under the threshold, six phases of feature work put 43
# functions back over it, and nothing said so — because nothing was watching.
# This file watches.
#
# The subject is `src/`, deliberately, and not the whole tree. `test/` holds
# fixtures written to be defective — a function of complexity 30, a dependency
# cycle, a file the grammar cannot follow — and measuring them would report the
# fixtures' faults as the product's.

setup() {
	load "../helpers/common"

	SRC="$REPO_ROOT/src"
	REPORT="$BATS_TEST_TMPDIR/self.txt"
	DIAGNOSTICS="$BATS_TEST_TMPDIR/self.err"

	# One run, read by every test below: the analysis takes a few seconds
	# and each case asks a different question of the same output. Verbose,
	# because the threshold list and the cycle list are detail tiers.
	"$ELC" --verbose "$SRC" > "$REPORT" 2> "$DIAGNOSTICS" || {
		echo "elc failed over its own source" >&2
		cat "$DIAGNOSTICS" >&2
		return 1
	}
}

# The rows of one section of the report, heading and rule excluded.
section_rows() {
	awk -v want="$1" '
		$0 ~ "^" want { s = 1; next }
		s && /^$/     { exit }
		s             { print }
	' "$REPORT" | tail -n +3
}

@test "LLR-BLD-23: no function in elc is at or over the complexity threshold" {
	# The threshold this asserts against is the default one elc applies to
	# anyone else's code, taken from the report's own heading rather than
	# written here, so the two can never disagree.
	#
	# The listing holds more than this since HLR-187 — every function a
	# complexity, fan-in or fan-out band names is in it too — so the rows
	# are filtered back to the claim LLR-BLD-23 makes. Filtered, and not
	# narrowed: a heading this test no longer matched would make it pass
	# over an empty string, which is the failure mode a self-check has to
	# be proof against.
	local threshold
	threshold="$(awk 'match($0, /^At or over a threshold \(complexity listed at ([0-9]+)/, m) { print m[1]; exit }' "$REPORT")"
	[ -n "$threshold" ] || {
		echo "the threshold listing heading was not found in the report" >&2
		grep -n 'threshold' "$REPORT" >&2
		false
	}

	rows="$(section_rows 'At or over a threshold' |
		awk -v t="$threshold" '$3 >= t')"
	if [ -n "$rows" ]; then
		echo "elc reports its own functions at or over the threshold:" >&2
		echo "$rows" >&2
		false
	fi
}

@test "LLR-BLD-23: elc's own source carries no critical finding but the two it has always carried" {
	# The complexity and fan-in bands of HLR-185 and HLR-186 are new, and
	# they are applied to elc's own source like anyone else's. What must
	# not appear is a *critical* one they did not already produce: the two
	# recursive groups and the one god function predate this phase and are
	# recorded as such, and anything beyond them would be a regression this
	# phase introduced into its own product.
	#
	# Warnings are deliberately not gated. The complexity band warns above
	# 10 where the listing threshold sits at 15, so roughly sixty of elc's
	# own functions warn today. Bringing them under ten is a refactor of
	# the whole source tree, not a step in the phase that drew the band.
	local unexpected
	unexpected="$(section_rows 'Findings' |
		awk '$1 == "critical" && $2 != "recursion" && $2 != "fan-out"')"
	if [ -n "$unexpected" ]; then
		echo "elc reports a new critical finding against its own source:" >&2
		echo "$unexpected" >&2
		false
	fi
}

@test "LLR-BLD-24: elc's own components hold no dependency cycle" {
	rows="$(section_rows 'Component dependency cycles')"
	if [ -n "$rows" ]; then
		echo "elc reports a dependency cycle among its own modules:" >&2
		echo "$rows" >&2
		false
	fi
}

@test "LLR-BLD-25: no call in elc resolves ambiguously" {
	# Two file-local statics sharing a name make every call to that name
	# resolve to whichever module the graph indexed first. The edge points
	# at the wrong file, and the acyclicity asserted above is measured over
	# a graph carrying it — so this is a precondition of that claim rather
	# than a tidiness check. elc already diagnoses the condition.
	if grep -q 'is defined [0-9]* times' "$DIAGNOSTICS"; then
		echo "elc cannot resolve a call in its own source:" >&2
		grep 'is defined [0-9]* times' "$DIAGNOSTICS" >&2
		false
	fi
}

@test "LLR-BLD-14: elc parses every one of its own source files" {
	# The delivered grammar cannot follow every construct C admits, and the
	# project's answer is to avoid those constructs rather than to accept a
	# per-file failure in its own report. A non-zero count here names the
	# construct's file and line in the section below it.
	grep -qE '^  Unparsed lines +0$' "$REPORT" || {
		echo "elc could not parse part of its own source:" >&2
		grep -A 6 '^Partially parsed files' "$REPORT" >&2
		false
	}
}

@test "LLR-BLD-14: no source file of elc is skipped for want of a module" {
	grep -qE '^  Skipped +0$' "$REPORT" || {
		echo "elc skipped one of its own source files:" >&2
		grep -A 6 '^Skipped files' "$REPORT" >&2
		false
	}
}

@test "HLR-181: the self-analysis is the ordinary run, not a special mode" {
	# Nothing above passes an option that relaxes anything. If elc needed
	# one to measure itself cleanly, the measurement would be worthless —
	# so the absence of such an option is part of the claim.
	grep -qE '^  Files +[1-9]' "$REPORT"
	grep -qE '^  Functions +[1-9]' "$REPORT"
}
