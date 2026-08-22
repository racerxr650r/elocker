#!/usr/bin/env bats
# test/fixtures/arch.bats — component coupling, cycles, and layering (STP §5).
#
# Expected values are worked out by hand and justified in arch/README.md beside
# this file. Never regenerate them from elc's output.
#
# Several tests assert an absence: a call descending exactly one layer is not a
# finding, a recursion inside one file is not a component cycle, and no
# component is a bottleneck at the default threshold. Each of those would pass
# against an implementation that reported nothing, so each is paired with a
# case that must be reported.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/arch/tree"
	CYCLES="$BATS_TEST_DIRNAME/arch/cycles"
	LONE="$BATS_TEST_DIRNAME/arch/lone"

	STRATA=(--stratum "app:*/app/*"
	        --stratum "hal:*/hal/*"
	        --stratum "drv:*/drv/*")
}

# One component's coupling row, as "Ca Ce Instability". Scoped to the section
# and terminated at its blank line: a path in column one appears in the Files
# section too, and an unterminated extractor reads whichever comes next.
coupling_of() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Component coupling/ { f = 1; next }
		                  f && /^$/ { f = 0 }
		                  f && $1 ~ want"$" { print $2, $3, $4 }'
}

# The Finding column of one component's row, or empty.
finding_of() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Component coupling/ { f = 1; next }
		                  f && /^$/ { f = 0 }
		                  f && $1 ~ want"$" { $1 = ""; $2 = ""; $3 = "";
		                                      $4 = ""; sub(/^ +/, "");
		                                      print }'
}

# The example loop of the one dependency cycle, with paths reduced to
# basenames. The row carries the group first and the loop second, separated by
# the column gap, so everything up to the last run of two spaces is dropped —
# the loop's own separators are single-spaced arrows.
cycle_path() {
	printf '%s\n' "$output" |
		awk '/^Component dependency/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /->/ { print }' |
		sed 's#[^ ,]*/##g; s/^ *//; s/^.*  //'
}

cycle_rows() {
	printf '%s\n' "$output" |
		awk '/^Component dependency/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { n++ } END { print n + 0 }'
}

# The layering rows as "kind from-layer from-fn to-layer to-fn distance".
layering() {
	printf '%s\n' "$output" |
		awk '/^Layering/ { f = 1; next } f && /^$/ { f = 0 }
		     f && $1 != "Kind" && $1 !~ /^-+$/ && NF { print }'
}

layering_heading() {
	printf '%s\n' "$output" | awk '/^Layering/ { print; exit }'
}

# --------------------------------------------------------------- coupling --

@test "HLR-080: Ca and Ce match the hand-computed table" {
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success

	assert_equal "$(coupling_of app.c)" "1 2 0.67"
	assert_equal "$(coupling_of hal.c)" "1 2 0.67"
	assert_equal "$(coupling_of drv.c)" "2 0 0.00"
}

@test "HLR-080: a repeated call does not raise efferent coupling" {
	# app_run calls hal_read twice. Coupling counts components depended
	# upon, not calls made — if it counted call sites, app.c's Ce would be
	# 3 and the whole table above would be wrong in the same way.
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success
	assert_equal "$(coupling_of app.c)" "1 2 0.67"
}

@test "HLR-082: instability is Ce over Ce plus Ca" {
	# drv.c is depended on by both others and depends on nothing: maximally
	# stable, and 0.00 rather than undefined, because Ca is not zero.
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success
	assert_equal "$(coupling_of drv.c)" "2 0 0.00"
}

@test "HLR-082: a component with both couplings zero is undefined, not zero" {
	# The division the requirement forbids. A lone file in a single-file
	# target has no relationships at all, and reporting 0.00 would claim
	# maximum stability for it.
	elc --verbose "$LONE/only.c"
	assert_success
	assert_equal "$(coupling_of only.c)" "0 0 undefined"
}

@test "HLR-080: every component appears, not only the interesting ones" {
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success

	local rows
	rows="$(printf '%s\n' "$output" |
		awk '/^Component coupling/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { n++ } END { print n + 0 }')"
	assert_equal "$rows" "3"
}

# ------------------------------------------------------------ bottlenecks --

@test "HLR-081: no component is a bottleneck at the default threshold" {
	# Asserted so the flag has to be earned. A test that only ever saw a
	# flagged component would pass against an implementation flagging all.
	elc "${STRATA[@]}" "$TREE"
	assert_success
	assert_equal "$(finding_of app.c)" ""
	assert_equal "$(finding_of hal.c)" ""
	assert_equal "$(finding_of drv.c)" ""
}

@test "HLR-081: a component is a bottleneck when both couplings meet the threshold" {
	# app.c and hal.c each have Ca 1 and Ce 2, so a threshold of 1 flags
	# both — and drv.c, with Ce 0, stays clear. That last part is the
	# comparison: *both* couplings must meet it, not either.
	elc "${STRATA[@]}" -b 1 "$TREE"
	assert_success
	assert_output --partial "elc heuristic"
	assert_equal "$(finding_of drv.c)" ""
}

@test "HLR-099: the bottleneck threshold is marked as elc's own heuristic" {
	elc --verbose "${STRATA[@]}" -b 1 "$TREE"
	assert_success
	assert_equal "$(finding_of app.c)" \
		"elc heuristic — not a published standard"
}

@test "HLR-081: the threshold is configurable and appears in the heading" {
	elc --verbose "${STRATA[@]}" -b 3 "$TREE"
	assert_success
	assert_output --partial "bottleneck at Ca and Ce >= 3"
}

# ---------------------------------------------------------------- cycles --

@test "HLR-083: a cycle between two components is reported as an ordered loop" {
	elc --verbose "$CYCLES"
	assert_success
	assert_equal "$(cycle_path)" "a.c -> b.c -> a.c"
}

@test "HLR-083: the same pair is a recursion finding as well" {
	# Both, because they are different facts: one says the stack depth has
	# no finite bound, the other says the two files cannot be built or
	# understood apart.
	elc --verbose "$CYCLES"
	assert_success

	local kinds
	kinds="$(printf '%s\n' "$output" |
		awk '/^Recursion/ { f = 1; next } f && /^$/ { f = 0 }
		     f && ($1 == "direct" || $1 == "mutual") { print $1 }')"
	assert_equal "$kinds" "mutual"
	assert_equal "$(cycle_rows)" "1"
}

@test "LLR-CYC-03: mutual recursion within one file is NOT a component cycle" {
	# The case HLR-083 calls out by name. ping and pong call each other, so
	# the call view holds a cycle; they live in one file, so the component
	# projection holds none — a file does not depend on itself.
	elc "$LONE/only.c"
	assert_success
	assert_equal "$(cycle_rows)" "0"
}

@test "LLR-CYC-03: that same file still reports the recursion" {
	# Paired with the test above: without this one, an implementation that
	# detected nothing anywhere would pass.
	elc --verbose "$LONE/only.c"
	assert_success

	local kinds
	kinds="$(printf '%s\n' "$output" |
		awk '/^Recursion/ { f = 1; next } f && /^$/ { f = 0 }
		     f && ($1 == "direct" || $1 == "mutual") { print $1 }')"
	assert_equal "$kinds" "mutual"
}

@test "HLR-083: an acyclic project reports no cycles" {
	elc "$LONE/only.c"
	assert_success
	assert_equal "$(cycle_rows)" "0"
}

# -------------------------------------------------------------- layering --

@test "HLR-079: a call bypassing an intervening layer is skip-level" {
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success
	assert_output --partial "skip-level  app   app_shortcut  drv  drv_poke"
}

@test "HLR-118: a call inverting the declared direction is reported separately" {
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success
	assert_output --partial "inverted    hal   hal_callback  app  app_notify"
}

@test "LLR-LAY-03: the two are distinct findings, each without the other" {
	# The whole of HLR-118's second sentence. app_shortcut descends two
	# layers and inverts nothing; hal_callback ascends one and bypasses
	# nothing. An implementation folding them into one "layering violation"
	# fails here, and so does one that only reported a skip when the call
	# also inverted.
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success

	local kinds
	kinds="$(layering | awk '{ print $1 }')"
	assert_equal "$kinds" "skip-level
inverted"
}

@test "HLR-079: a call descending exactly one layer is not reported" {
	# hal_read calls drv_poke, one layer down and in the declared
	# direction. Without this the suite would pass against an
	# implementation flagging every inter-layer call.
	elc "${STRATA[@]}" "$TREE"
	assert_success

	run bash -c 'grep -c hal_read <<<"$0" || true' "$(layering)"
	assert_output "0"
}

@test "HLR-079: the layers crossed are reported with the finding" {
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success

	local distances
	distances="$(layering | awk '{ print $NF }')"
	assert_equal "$distances" "2
1"
}

@test "HLR-078: the declared order determines the direction" {
	# Declaring the layers bottom-up inverts every judgement: the call that
	# skipped downward now ascends two layers, so it is both inverted and
	# skip-level, and the one that inverted now descends one and is
	# nothing at all.
	elc --verbose --stratum "drv:*/drv/*" --stratum "hal:*/hal/*" \
	    --stratum "app:*/app/*" "$TREE"
	assert_success

	# app_shortcut now ascends two layers, so it is reported twice — both
	# inverted and skip-level, because both statements are true of it.
	run bash -c 'grep -c app_shortcut <<<"$0" || true' "$(layering)"
	assert_output "2"

	# hal_callback, the inversion under the original declaration, now
	# descends one layer in the declared direction and is nothing at all.
	run bash -c 'grep -c hal_callback <<<"$0" || true' "$(layering)"
	assert_output "0"
}

@test "HLR-078: --stratum-order states the direction explicitly" {
	# The same three layers declared in the wrong order, put right by the
	# order option — which may be given before or after the layers.
	elc --verbose --stratum-order "app>hal>drv" \
	    --stratum "drv:*/drv/*" --stratum "hal:*/hal/*" \
	    --stratum "app:*/app/*" "$TREE"
	assert_success

	local kinds
	kinds="$(layering | awk '{ print $1 }')"
	assert_equal "$kinds" "skip-level
inverted"
}

@test "HLR-063: --stratum-order naming an undeclared layer is a usage error" {
	run "$ELC" --stratum "app:*/app/*" --stratum-order "app>nope" "$TREE"
	assert_equal "$status" 2
	assert_output --partial "not a declared stratum"
}

@test "HLR-063: a partial --stratum-order is a usage error" {
	# A partial order determines no direction, and silently completing it
	# would validate the layering against something the user did not write.
	run "$ELC" "${STRATA[@]}" --stratum-order "app>hal" "$TREE"
	assert_equal "$status" 2
	assert_output --partial "partial order"
}

@test "HLR-063: a malformed stratum declaration is a usage error" {
	run "$ELC" --stratum "no-colon" "$TREE"
	assert_equal "$status" 2
	assert_output --partial "is not a stratum"
}

@test "HLR-115: with no strata declared the analysis is omitted with a reason" {
	elc "$TREE"
	assert_success
	assert_equal "$(layering)" ""
	assert_equal "$(layering_heading)" \
		"Layering (omitted: no architectural strata declared, see --stratum)"
}

@test "LLR-CTR-09: omitting layering does not omit the coupling table" {
	elc --verbose "$TREE"
	assert_success
	assert_equal "$(coupling_of drv.c)" "2 0 0.00"
}

@test "LLR-ARC-04: a stratum matching no component is diagnosed and retained" {
	# Retained, not dropped: dropping it would renumber the layers below
	# and change what the remaining calls are compared against, turning a
	# typo into a wrong answer rather than a warning.
	run "$ELC" "${STRATA[@]}" --stratum "empty:*/nowhere/*" "$TREE"
	assert_success
	assert_output --partial "stratum empty matches no analysed component"
}

@test "HLR-094: a component in no declared stratum is outside the partition" {
	# Only the middle layer is declared, so hal's calls have nothing to be
	# compared against and nothing is reported.
	elc --stratum "hal:*/hal/*" "$TREE"
	assert_success
	assert_equal "$(layering)" ""
}

# ------------------------------------------------------------ determinism --

@test "HLR-032: the architecture sections survive a record round trip" {
	elc "${STRATA[@]}" -f xml -o "$BATS_TEST_TMPDIR/rec.xml" "$TREE"
	assert_success

	run "$ELC" "${STRATA[@]}" -f md "$TREE"
	assert_success
	printf '%s\n' "$output" > "$BATS_TEST_TMPDIR/live.md"

	run "$ELC" --from-xml "$BATS_TEST_TMPDIR/rec.xml"
	assert_success
	printf '%s\n' "$output" > "$BATS_TEST_TMPDIR/regen.md"

	run diff "$BATS_TEST_TMPDIR/live.md" "$BATS_TEST_TMPDIR/regen.md"
	assert_success
}

@test "HLR-032: a cycle and its loop survive the round trip" {
	elc --verbose -f xml -o "$BATS_TEST_TMPDIR/c.xml" "$CYCLES"
	assert_success

	run "$ELC" --verbose --from-xml "$BATS_TEST_TMPDIR/c.xml"
	assert_success

	# Asserted against the raw output rather than through the extractors:
	# regeneration renders Markdown, whose headings and row delimiters
	# differ, and the point here is that the *values* came back rather than
	# how the table draws them. The byte-identical check above covers the
	# rendering.
	#
	# Each file depends on the other, so Ca and Ce are both 1 and
	# Instability is 0.50 — not undefined, which needs both to be zero.
	assert_output --partial "a.c -> "
	assert_output --partial "b.c -> "
	assert_output --partial "0.50"
}
