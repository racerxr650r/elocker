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

layering_heading() { heading_of "Layering"; }

# One conformance row as "Violating Conforming Of".
conformance_of() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Architecture conformance/ { f = 1; next }
		                  f && /^$/ { f = 0 }
		                  f && $1 == want { print $2, $3, $4 }'
}

conformance_heading() { heading_of "Architecture conformance"; }

# The matrix rows, with directory paths reduced to their last component so the
# grid reads the same wherever the checkout lives. The corner cell and the
# rule row are dropped; what is left is one line per subject.
matrix() {
	printf '%s\n' "$output" |
		awk '/^Dependency structure matrix/ { f = 1; next }
		     f && /^$/ { f = 0 }
		     f && /^  [^ ]/ && !/^  Rows are callers/ &&
		     !/^  caller/ && !/^  -/ { print }' |
		sed 's#[^ ]*/##g; s/^ *//; s/  */ /g'
}

matrix_heading() { heading_of "Dependency structure matrix"; }

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

# ------------------------------------------------- conformance indices --

@test "HLR-162: the back-call index matches the hand-computed value" {
	# One inverted call over six inter-layer call edges. The denominator
	# and both figures are worked out in arch/README.md.
	elc "${STRATA[@]}" "$TREE"
	assert_success
	assert_equal "$(conformance_of Back-call)" "16.67% 83.33% 6"
}

@test "HLR-163: the skip-call index matches the hand-computed value" {
	elc "${STRATA[@]}" "$TREE"
	assert_success
	assert_equal "$(conformance_of Skip-call)" "16.67% 83.33% 6"
}

@test "HLR-164: the indices agree with the violation table beside them" {
	# The percentages and the rows are two views of one answer, which is
	# the whole of HLR-164: one inverted row and one skip-level row, and
	# each index reporting one violation over the same six edges.
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success

	assert_equal "$(layering | grep -c inverted)" 1
	assert_equal "$(layering | grep -c skip-level)" 1
	assert_equal "$(conformance_of Back-call)" "16.67% 83.33% 6"
	assert_equal "$(conformance_of Skip-call)" "16.67% 83.33% 6"
}

@test "HLR-163: the two indices are reported apart and never summed" {
	# Two rows, no total. A combined score would count a call that both
	# skips and inverts twice, and would name no remedy where each index
	# separately names one.
	elc "${STRATA[@]}" "$TREE"
	assert_success

	refute_output --partial "Conformance score"
	refute_output --partial "33.33%"
}

@test "HLR-162: a project with no inter-layer call reports both undefined" {
	# Both files in one layer. There are two call edges and neither has a
	# direction to invert, so the denominator is zero and the answer is
	# "nothing demonstrated" rather than "perfectly conformant".
	elc --stratum "all:*/cycles/*" "$CYCLES"
	assert_success

	assert_equal "$(conformance_of Back-call)" "undefined undefined 0"
	assert_equal "$(conformance_of Skip-call)" "undefined undefined 0"
	refute_output --partial "100.00%"
	refute_output --partial "0.00%"
}

@test "HLR-115: with no strata the conformance section states the omission" {
	elc "$TREE"
	assert_success
	assert_equal "$(conformance_heading)" \
		"Architecture conformance (omitted: no architectural strata declared, see --stratum)"
}

# ------------------------------------------------------------- the matrix --

@test "HLR-165: the matrix over declared layers matches the hand-drawn grid" {
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success

	assert_equal "$(matrix)" "app 0 2 1
hal 1 0 2
drv 0 0 0"
}

@test "HLR-166: the below-diagonal cells account for exactly the back-calls" {
	# The grid and the list are two views of one fact. Below the diagonal
	# is the single hal -> app cell, and the layering table lists exactly
	# one inverted call.
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success

	local below
	below=$(printf '%s\n' "$(matrix)" |
		awk '{ for (c = 2; c < NR + 1; c++) total += $c } END { print total + 0 }')

	assert_equal "$below" 1
	assert_equal "$(layering | grep -c inverted)" 1
}

@test "HLR-166: subjects run in ascending layer order, not declaration order" {
	# drv is declared last and ordered first, so the call from hal up into
	# app must move above the diagonal rather than staying below it.
	elc --verbose "${STRATA[@]}" --stratum-order "drv>hal>app" "$TREE"
	assert_success

	assert_equal "$(matrix)" "drv 0 0 0
hal 2 0 1
app 1 2 0"
}

@test "HLR-165: with no strata declared the matrix is over directories" {
	elc --verbose "$TREE"
	assert_success

	assert_equal "$(matrix)" "app 0 1 2
drv 0 0 0
hal 1 2 0"
	assert_equal "$(matrix_heading)" \
		"Dependency structure matrix (directories: no strata declared, see --stratum)"
}

@test "HLR-161: a component outside every stratum reaches no matrix cell" {
	# Only the middle layer is declared. Every call touches a file it does
	# not name, so the grid is one subject wide and holds nothing.
	elc --verbose --stratum "hal:*/hal/*" "$TREE"
	assert_success
	assert_equal "$(matrix)" "hal 0"
}

@test "HLR-166: the convention is printed with every rendering" {
	local convention="Rows are callers, columns callees, in ascending order."

	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success
	assert_output --partial "$convention"

	elc --verbose -f md "${STRATA[@]}" "$TREE"
	assert_success
	assert_output --partial "$convention"

	elc --dsm -o "$BATS_TEST_TMPDIR/r.md" "${STRATA[@]}" "$TREE"
	assert_success
	run cat "$BATS_TEST_TMPDIR/r.dsm.csv"
	assert_output --partial "$convention"
}

@test "HLR-190: the Markdown matrix folds the grid and keeps the convention out" {
	# The grid goes behind a disclosure like every other Markdown table.
	# The convention does not: it is the sentence that makes a cell below
	# the diagonal a back-call rather than a number, and the reader who has
	# not expanded the grid is the one deciding whether to (HLR-166).
	local convention="Rows are callers, columns callees, in ascending order."

	elc --verbose -f md "${STRATA[@]}" "$TREE"
	assert_success

	# heading, blank, convention, blank, <details> — in that order, with
	# the convention above the fold.
	local shape
	shape="$(awk '/^## Dependency structure matrix/ { s = 1 }
	              s && /^<details>$/ { print "details"; exit }
	              s && /^<summary>/  { print "summary" }
	              s && /Rows are callers/ { print "convention" }' <<<"$output")"
	assert_equal "$shape" "convention
details"

	assert_output --regexp "<summary>[0-9]+ rows? \(click to expand\)</summary>"

	# The aligned rendering gains no element, and still carries the note.
	elc --verbose "${STRATA[@]}" "$TREE"
	assert_success
	assert_output --partial "$convention"
	refute_output --partial "<details>"
}

@test "HLR-166: the CSV companion carries the grid the report shows" {
	elc --dsm -o "$BATS_TEST_TMPDIR/r.md" "${STRATA[@]}" "$TREE"
	assert_success

	run sed -e 's/\r$//' "$BATS_TEST_TMPDIR/r.dsm.csv"
	assert_line "caller \\ callee,app,hal,drv"
	assert_line "app,0,2,1"
	assert_line "hal,1,0,2"
	assert_line "drv,0,0,0"
}

@test "HLR-064: a directory carrying a comma is quoted in the CSV matrix" {
	# Built here rather than committed, because a directory with a comma in
	# its name is awkward to carry in a repository and the escaping rule is
	# what is under test, not the tree.
	local tree="$BATS_TEST_TMPDIR/comma"

	mkdir -p "$tree/one,two" "$tree/zed"
	printf 'void z_fn(void);\n\nvoid c_fn(void)\n{\n\tz_fn();\n}\n' \
		> "$tree/one,two/c.c"
	printf 'void z_fn(void)\n{\n\treturn;\n}\n' > "$tree/zed/z.c"

	elc --dsm -o "$BATS_TEST_TMPDIR/c.md" "$tree"
	assert_success

	run cat "$BATS_TEST_TMPDIR/c.dsm.csv"
	assert_output --partial "\"$tree/one,two\""
}

@test "HLR-104: the matrix companion needs a name to derive" {
	# Asking for it with the report on standard output produces no file,
	# and is not an error — the same rule the GraphML export follows. The
	# report itself still carries the matrix; only the companion needs a
	# path to be named from.
	cd "$BATS_TEST_TMPDIR"
	elc --dsm --verbose "${STRATA[@]}" "$TREE"
	assert_success
	assert_output --partial "Dependency structure matrix"

	run bash -c 'ls "$1"/*.dsm.csv 2>/dev/null | wc -l' _ "$BATS_TEST_TMPDIR"
	assert_output "0"
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
