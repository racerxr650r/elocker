#!/usr/bin/env bats
# test/fixtures/elf.bats — measuring the program a build produced (STP §5).
#
# Expected values are worked out by hand and justified in elf/README.md beside
# the fixture. Never regenerate them from elc's output.
#
# **No image is committed.** A binary in the repository is a fixture nobody can
# review, and one built elsewhere pins the reviewer to a toolchain they may not
# have. Every image here is built in $BATS_TEST_TMPDIR from the sources beside
# this file, and a case whose compiler is unavailable skips explicitly, naming
# the requirement that thereby went unverified.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/elf/tree"
	IMPORTED="$BATS_TEST_DIRNAME/elf/imported"
	PRUNED="$BATS_TEST_DIRNAME/elf/pruned"
	CPP="$BATS_TEST_DIRNAME/elf/cpp"
	MANGLED="$BATS_TEST_DIRNAME/elf/mangled"
	DEBUGLINE="$BATS_TEST_DIRNAME/elf/debugline"
	EVIDENCE="$BATS_TEST_DIRNAME/elf/evidence"
}

# The functions elc reported, in the order the report presents them.
#
# awk with a blank-line terminator, or the extractor runs into the section that
# follows; the header row is skipped by name rather than by position, because
# skipping "the first two lines" breaks the moment a column is added.
functions_of() {
	awk '/^Functions$/ {s=1; next}
	     s && /^$/ {exit}
	     s && $1 == "File" {next}
	     s && /^  [^ -]/ {print $3}' "$1" | sort | tr '\n' ' '
}

# The functions the image does not define, from the section of that name.
absent_of() {
	awk '/^Functions the image does not define/ {s=1; next}
	     s && /^$/ {exit}
	     s && $1 == "Function" {next}
	     s && /^  [^ -]/ {print $1}' "$1" | sort | tr '\n' ' '
}

# The functions the image places that the parse did not reach, from the section
# of that name. Written the way absent_of is, and read beside it: the two are
# the two directions of one mismatch.
placed_of() {
	awk '/^Functions the image places/ {s=1; next}
	     s && /^$/ {exit}
	     s && $1 == "Function" {next}
	     s && /^  [^ -]/ {print $1}' "$1" | sort | tr '\n' ' '
}

summary_of() {
	awk -v want="$2" '$0 ~ "^  " want "  *[0-9]+$" {print $NF}' "$1"
}

# A row of the linked-image section, whose values are not all numeric.
filter_of() {
	awk -v want="$2" '$0 ~ "^  " want "  " {print $NF}' "$1"
}

# The C image: a shared object built from kept.c alone, so that dropped.c's
# functions are absent from it and the one it calls is imported rather than
# defined. A shared object links with an undefined symbol left undefined, which
# an executable would not (elf/README.md).
build_image() {
	require_tool cc "HLR-140 image filtering"
	IMAGE="$BATS_TEST_TMPDIR/libkept.so"
	cc -O0 -fPIC -shared -o "$IMAGE" "$TREE/kept.c" 2>/dev/null || \
		skip "cc cannot link a shared object here: HLR-140 unverified"
}

# Verbose, and written to a `.txt`: the extractors read the aligned table, and
# both of those are now stated rather than defaulted. The extension names the
# format (HLR-148), and the per-function tiers these tests measure — including
# the functions the image does not define — are omitted from the default
# composition (HLR-150).
report() {
	local target="$1"
	shift
	run bash -c '"$0" --verbose -o "$1" "${@:3}" "$2" 2>/dev/null' \
		"$ELC" "$BATS_TEST_TMPDIR/report.txt" "$target" "$@"
	OUT="$BATS_TEST_TMPDIR/report.txt"
}

# The debug-line image: covered.c compiled WITH -g and opaque.c WITHOUT it,
# linked together, so that one image carries a covered file and an uncovered
# one. The optimisation level is the argument, because HLR-154's line-folding
# limit is a property of the optimiser and the suite asserts at two levels
# (elf/debugline/README.md).
build_debugline() {
	require_tool cc "HLR-153 debug-line pruning"
	local opt="${1--O0}"
	IMAGE="$BATS_TEST_TMPDIR/dl$opt"
	cc -g "$opt" -c "$DEBUGLINE/covered.c" -o "$BATS_TEST_TMPDIR/c.o" \
		2>/dev/null &&
	cc "$opt" -c "$DEBUGLINE/opaque.c" -o "$BATS_TEST_TMPDIR/o.o" \
		2>/dev/null &&
	cc -o "$IMAGE" "$BATS_TEST_TMPDIR/c.o" "$BATS_TEST_TMPDIR/o.o" \
		2>/dev/null || \
		skip "cc cannot build a debug image here: HLR-153 unverified"
}

# The evidence image: branched.c and macro.c compiled WITH -g, dark.c WITHOUT
# it, linked together. One image therefore carries a file whose regions the
# build can settle, a file whose regions it cannot be asked about, and a
# function only the debug information places (elf/evidence/README.md).
build_evidence() {
	require_tool cc "HLR-211 conditional regions decided from the image"
	IMAGE="$BATS_TEST_TMPDIR/ev"
	cc -O0 -g -c "$EVIDENCE/branched.c" -o "$BATS_TEST_TMPDIR/b.o" \
		2>/dev/null &&
	cc -O0 -g -c "$EVIDENCE/macro.c" -o "$BATS_TEST_TMPDIR/m.o" \
		2>/dev/null &&
	cc -O0 -c "$EVIDENCE/dark.c" -o "$BATS_TEST_TMPDIR/d.o" \
		2>/dev/null &&
	cc -o "$IMAGE" "$BATS_TEST_TMPDIR/b.o" "$BATS_TEST_TMPDIR/m.o" \
		"$BATS_TEST_TMPDIR/d.o" 2>/dev/null || \
		skip "cc cannot build an evidence image here: HLR-211 unverified"
}

# One function's ELOC, from the per-function tier.
eloc_of() {
	awk -v want="$2" '/^Functions$/ {s=1; next}
	                  s && /^$/ {exit}
	                  s && $3 == want {print $6}' "$1"
}

# One function's fan-out, from the same tier and the same row.
fanout_of() {
	awk -v want="$2" '/^Functions$/ {s=1; next}
	                  s && /^$/ {exit}
	                  s && $3 == want {print $9}' "$1"
}

# ------------------------------------------------------- the hand counts --

@test "the hand-counted totals match with no image" {
	report "$TREE"
	assert_success
	assert_equal "$(summary_of "$OUT" Files)" "2"
	assert_equal "$(summary_of "$OUT" "Physical lines")" "61"
	assert_equal "$(summary_of "$OUT" Functions)" "5"
	assert_equal "$(summary_of "$OUT" ELOC)" "13"
	assert_equal "$(summary_of "$OUT" "Unresolved calls")" "1"
}

@test "HLR-140: the image restricts the report to the functions it defines" {
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success
	assert_equal "$(functions_of "$OUT")" "main measure scale "
	assert_equal "$(summary_of "$OUT" Functions)" "3"
	assert_equal "$(summary_of "$OUT" ELOC)" "9"
}

@test "HLR-140: a static function reaches .symtab and is retained" {
	# The case that distinguishes the two symbol tables. `scale` is local, so
	# it is in .symtab and not in .dynsym; a reader taking only the dynamic
	# table would report it missing from an image that plainly contains it.
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success

	run bash -c 'grep -c "  scale  " "$0" || true' "$OUT"
	[ "$output" -ge 1 ]
}

# ------------------------------------------------------ debug-line pruning --

@test "HLR-153: a region the build excluded is pruned from a kept function" {
	# The assertion the phase exists for. `guarded` survives the link, and
	# three statements inside it produced no instruction; the condition
	# that excluded them is one elc cannot decide from the source, so only
	# the image's line information can rule them out.
	build_debugline
	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success

	assert_equal "$(eloc_of "$OUT" guarded)" "4"
	assert_equal "$(summary_of "$OUT" ELOC)" "12"
}

@test "HLR-153: a function every build compiles is kept whole" {
	# The other direction, and what stops the test above passing against an
	# implementation that pruned the file wholesale.
	build_debugline
	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success

	assert_equal "$(eloc_of "$OUT" always)" "3"
	assert_equal "$(eloc_of "$OUT" main)" "1"
	assert_equal "$(summary_of "$OUT" Functions)" "4"
}

@test "HLR-154: a file compiled without debug information loses no line" {
	# The dangerous case, and it is dangerous because it is silent. opaque.c
	# contributes no line entries at all, so a rule keyed on absence would
	# find every line of it uncompiled and delete the file — leaving a
	# report that is smaller, internally consistent, and wrong.
	build_debugline
	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success
	assert_equal "$(eloc_of "$OUT" opaque)" "4"
}

@test "HLR-155: both counts are reported" {
	# One file whose coverage could not be established, and nothing left
	# for the line granularity to remove — the region went whole, at the
	# coarser grain HLR-211 added, before any line inside it was judged.
	#
	# Zero is the figure and it is the right one: the pair states what the
	# filter removed *by line* and where it could not look, and a count
	# that double-counted lines a region had already taken would inflate a
	# figure a reader acts on (LLR-ANL-58).
	build_debugline
	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success

	assert_equal "$(filter_of "$OUT" "Lines not compiled by this build")" "0"
	assert_equal "$(filter_of "$OUT" "Files with no debug coverage")" "1"
}

@test "HLR-155: the line granularity still removes what a -D left active" {
	# The same five lines, taken the other way. Told the symbol is defined,
	# `elc` settles the region from the `-D` and leaves its statements
	# active — and the image, built without that definition, compiled none
	# of them. What the region grain no longer removes, the line grain
	# does, and the count says so.
	#
	# The directive lines are not among them here: the file expands under
	# the same `-D`, and the preprocessor has already taken them out.
	build_debugline
	report "$DEBUGLINE" --elf "$IMAGE" -D ELC_FIXTURE_FEATURE
	assert_success

	assert_equal "$(eloc_of "$OUT" guarded)" "4"
	assert_equal "$(summary_of "$OUT" ELOC)" "12"
	assert_equal "$(filter_of "$OUT" "Lines not compiled by this build")" "3"
}

@test "HLR-211: a -D decides the region, not the image" {
	# Order of authority, asserted rather than assumed. A `-D` is what the
	# user says this configuration is; the image is evidence about one
	# build. The definition is consulted first and the image is never
	# asked, so no region is counted as decided by it.
	build_debugline
	report "$DEBUGLINE" --elf "$IMAGE" -D ELC_FIXTURE_FEATURE
	assert_success

	assert_equal "$(filter_of "$OUT" "Regions decided by this build")" "0"
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "0"
}

@test "HLR-211: the image decides the region the source cannot" {
	# The fixture's whole assertion rests on the condition being
	# undecidable. Were it decidable from the source, Phase 15 would have
	# removed the region before the image was consulted and every test
	# above would pass against an implementation that read no debug
	# information at all.
	#
	# So the region is counted where it belongs: not undecided, and not
	# among the ones a `-D` settled, but as one this build answered.
	build_debugline
	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success

	assert_equal "$(summary_of "$OUT" "Undecided regions")" "0"
	assert_equal "$(filter_of "$OUT" "Regions decided by this build")" "1"
}

@test "HLR-211: with no image the region is undecided exactly as before" {
	# HLR-141 forbids *requiring* debug information, and this is the shape
	# that takes here: the same file, the same condition, no image, and the
	# answer Phase 15 gave.
	report "$DEBUGLINE"
	assert_success

	assert_equal "$(summary_of "$OUT" "Undecided regions")" "1"
	assert_equal "$(eloc_of "$OUT" guarded)" "7"
}

@test "HLR-208: a file whose one undecidable region the image decides expands" {
	# The interaction, confirmed deliberately rather than left to happen.
	# HLR-208 refuses to expand a file holding a region `elc` could not
	# decide, because the preprocessor would silently resolve it. A region
	# the image decides is not one `elc` could not decide, so the gate
	# opens — and covered.c, refused before, is expanded.
	build_debugline
	report "$DEBUGLINE"
	local without
	without="$(summary_of "$OUT" "Files expanded")"

	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success
	assert_equal "$without" "1"
	assert_equal "$(summary_of "$OUT" "Files expanded")" "2"
}

@test "HLR-153: an image without debug information prunes nothing" {
	# HLR-141 forbids *requiring* debug information. Its absence costs the
	# finer granularity and nothing else, so every metric is what the same
	# run reports with no image at all beyond the function filter.
	require_tool cc "HLR-153 debug-line pruning"
	local image="$BATS_TEST_TMPDIR/nodebug"

	cc -O0 -o "$image" "$DEBUGLINE/covered.c" "$DEBUGLINE/opaque.c" \
		2>/dev/null || \
		skip "cc cannot link here: HLR-153 unverified"

	report "$DEBUGLINE" --elf "$image"
	assert_success

	assert_equal "$(summary_of "$OUT" ELOC)" "15"
	assert_equal "$(eloc_of "$OUT" guarded)" "7"
	assert_equal "$(filter_of "$OUT" "Lines not compiled by this build")" "0"
	# Both files uncovered, and said so rather than silently pruned.
	assert_equal "$(filter_of "$OUT" "Files with no debug coverage")" "2"
}

@test "HLR-153: the metrics without debug information are the unfiltered ones" {
	# Stated as an equality rather than left to two hand-counted tables:
	# the figures a run reports for an image carrying no line information
	# are the figures it reported before this phase existed.
	require_tool cc "HLR-153 debug-line pruning"
	local image="$BATS_TEST_TMPDIR/nodebug2"

	cc -O0 -o "$image" "$DEBUGLINE/covered.c" "$DEBUGLINE/opaque.c" \
		2>/dev/null || \
		skip "cc cannot link here: HLR-153 unverified"

	report "$DEBUGLINE"
	local plain
	plain="$(summary_of "$OUT" ELOC) $(summary_of "$OUT" Functions)"

	report "$DEBUGLINE" --elf "$image"
	assert_success
	assert_equal "$(summary_of "$OUT" ELOC) $(summary_of "$OUT" Functions)" \
		"$plain"
}

@test "HLR-154: at -O2 an uncovered file is still untouched" {
	# The invariant that holds whatever the optimiser did, and the one the
	# whole phase is unsafe without: coverage governs pruning, so a file
	# the mapping never described loses nothing at any optimisation level.
	# Every function is still reported for the same reason — pruning
	# removes lines from within a function, never the function itself.
	build_debugline -O2
	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success

	assert_equal "$(eloc_of "$OUT" opaque)" "4"
	assert_equal "$(summary_of "$OUT" Functions)" "4"
	assert_equal "$(filter_of "$OUT" "Files with no debug coverage")" "1"
}

@test "HLR-154: at -O2 the optimiser prunes more than the source excluded" {
	# **The limit HLR-154 states, demonstrated rather than described.** At
	# -O0 exactly the guarded region goes. At -O2 the compiler folds
	# `guarded(2)` to a constant and emits nothing for its body at all, so
	# lines the source plainly holds produce no instruction and are pruned
	# with the rest — indistinguishable, in the mapping alone, from the
	# ones the `#ifdef` excluded.
	#
	# That is not a defect to be corrected: nothing in the image records
	# the difference, and the lines really did contribute nothing to what
	# shipped. It is why HLR-155's count exists, and why no exact figure is
	# asserted for -O2 — one would pin the fixture to a single compiler's
	# optimiser rather than to a requirement.
	#
	# The `-D` is what keeps this test about the *line* granularity. Without
	# it the region is settled whole from the image (HLR-211) and there is
	# nothing left for the optimiser to be observed folding.
	build_debugline -O0
	report "$DEBUGLINE" --elf "$IMAGE" -D ELC_FIXTURE_FEATURE
	assert_success
	local at_o0
	at_o0="$(filter_of "$OUT" "Lines not compiled by this build")"
	assert_equal "$at_o0" "3"

	build_debugline -O2
	report "$DEBUGLINE" --elf "$IMAGE" -D ELC_FIXTURE_FEATURE
	assert_success
	local at_o2
	at_o2="$(filter_of "$OUT" "Lines not compiled by this build")"

	[ "$at_o2" -ge "$at_o0" ] || {
		echo "-O2 pruned $at_o2, -O0 pruned $at_o0" >&2
		false
	}
}

@test "LLR-DWL-02: a path with .. components still resolves to the file" {
	# The compiler records the file name as the command line spelled it,
	# joined to the unit's compilation directory. Compiled from a
	# subdirectory the name carries a `..`, and the image's spelling and
	# elc's canonical path meet only if the join is normalised.
	#
	# Lexically, never through realpath(3): resolving these against the
	# filesystem would stat every path the image happens to name, which is
	# work on files the user did not name (HLR-141). The assertion is that
	# lexical normalisation is enough for the ordinary build.
	require_tool cc "HLR-153 debug-line pruning"
	local work="$BATS_TEST_TMPDIR/sub"
	local image="$BATS_TEST_TMPDIR/relative"

	mkdir -p "$work"
	( cd "$work" &&
	  cc -g -O0 -c "$DEBUGLINE/../debugline/./covered.c" -o c.o \
		2>/dev/null &&
	  cc -O0 -c "$DEBUGLINE/opaque.c" -o o.o 2>/dev/null &&
	  cc -o "$image" c.o o.o 2>/dev/null ) || \
		skip "cc cannot build through a relative path here"

	report "$DEBUGLINE" --elf "$image"
	assert_success

	# Coverage established despite the `..` in the recorded name: the
	# guarded region is pruned, which it could not be from an uncovered
	# file.
	assert_equal "$(eloc_of "$OUT" guarded)" "4"
	assert_equal "$(filter_of "$OUT" "Files with no debug coverage")" "1"
}

@test "HLR-155: the counts survive a record round trip" {
	# None of the three can be recomputed from a record: regeneration has
	# no image, and no debug information to read from one. The pruned-line
	# count is asserted through a `-D` for the reason the -O2 pair is —
	# without one the region goes whole and there is no line count to carry
	# — and the region count is asserted through the run that has one.
	build_debugline
	local record="$BATS_TEST_TMPDIR/dl.xml"

	run bash -c '"$0" --elf "$1" -f xml -D ELC_FIXTURE_FEATURE "$2" > "$3" 2>/dev/null' \
		"$ELC" "$IMAGE" "$DEBUGLINE" "$record"
	assert_success

	run bash -c '"$0" --verbose --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	assert_output --partial "Lines not compiled by this build"
	assert_output --regexp "Lines not compiled by this build *\| *3"
	assert_output --regexp "Files with no debug coverage *\| *1"
}

@test "HLR-211: the region count survives a record round trip" {
	# It cannot be recomputed either: a regenerated report has no image to
	# read the evidence off, so a count the record did not carry would come
	# back as zero and say the source settled a region the build did.
	build_debugline
	local record="$BATS_TEST_TMPDIR/dl2.xml"

	run bash -c '"$0" --elf "$1" -f xml "$2" > "$3" 2>/dev/null' \
		"$ELC" "$IMAGE" "$DEBUGLINE" "$record"
	assert_success

	run bash -c '"$0" --verbose --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	assert_output --regexp "Regions decided by this build *\| *1"
}

# ------------------------------------------------- the image as evidence --
#
# Expected values are worked out by hand in elf/evidence/README.md.

@test "HLR-211: a region with an alternative is decided against its alternative" {
	# The strongest form the evidence takes, and self-contained: exactly
	# one of the two branches produced instructions. `branched` holds one
	# region the build did not compile and one it did, and both are settled
	# — the first losing its body, the second losing its alternative.
	build_evidence
	report "$EVIDENCE" --elf "$IMAGE"
	assert_success

	assert_equal "$(eloc_of "$OUT" branched)" "5"
	assert_equal "$(filter_of "$OUT" "Regions decided by this build")" "2"
}

@test "HLR-211: with no image the same regions are undecidable" {
	# What stops the test above passing against an implementation that
	# guessed. Nothing in the source settles either condition, so with no
	# image both branches of both regions stay and the count says so.
	report "$EVIDENCE"
	assert_success

	assert_equal "$(eloc_of "$OUT" branched)" "8"
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "3"
}

@test "HLR-154: a region in a file the line information never described stands" {
	# The dangerous case, at the region granularity. dark.c is compiled
	# without -g into the same image and contributes no line entries at
	# all, so a rule keyed on absence alone would find its region
	# uncompiled and delete it. Coverage governs, and it is established per
	# file: the region stays, counted undecided, exactly as it is with no
	# image.
	build_evidence
	report "$EVIDENCE" --elf "$IMAGE"
	assert_success

	assert_equal "$(eloc_of "$OUT" dark)" "4"
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "1"
	assert_equal "$(filter_of "$OUT" "Files with no debug coverage")" "1"
}

@test "HLR-212: a function a macro defines is reported with its location" {
	# Tree-sitter finds no function at that line — it is looking at the
	# macro — and repair cannot help, because repair does not know the
	# macro defines one. The debug information does, and says where.
	build_evidence
	report "$EVIDENCE" --elf "$IMAGE" --no-expand
	assert_success

	assert_equal "$(placed_of "$OUT")" "from_macro "
	# The line the macro stands on, which is the one thing that says where
	# in the source it is.
	run grep -F "from_macro" "$OUT"
	assert_output --regexp "macro\.c +26$"
}

@test "HLR-212: a recovered function carries no figure elc did not measure" {
	# The whole of the requirement. `elc` has a name and a line and no body
	# at all, so the row has three columns and the function is in neither
	# the Functions table nor the project's function count: a fan-out of
	# zero for a body nobody read is not a fan-out of zero, and an ELOC of
	# zero for it is not an ELOC (HLR-133, HLR-138).
	build_evidence
	report "$EVIDENCE" --elf "$IMAGE" --no-expand
	assert_success

	assert_equal "$(functions_of "$OUT")" "branched dark main reached "
	assert_equal "$(summary_of "$OUT" Functions)" "4"
	run grep -c "from_macro" "$OUT"
	assert_output "1"
}

@test "HLR-212: a call to a recovered function is unresolved, not an edge" {
	# `main` calls four functions and three of them became edges. The
	# fourth has no node, so the graph cannot carry the edge, and the
	# unresolved-call count is where that is said — the same figure that
	# states how complete the graph is for a call into libc (HLR-077).
	# Inventing a node to hang the edge on is what would produce the
	# fan-out of zero the requirement refuses.
	#
	# Two calls are unresolved, not one: the macro invocation itself is an
	# expression to the grammar, and a call to `ELC_FIXTURE_HANDLER` is
	# exactly what the grammar sees there. That is the same misreading the
	# whole requirement exists to work around, counted where every other
	# call the graph cannot represent is counted.
	build_evidence
	report "$EVIDENCE" --elf "$IMAGE" --no-expand
	assert_success

	assert_equal "$(fanout_of "$OUT" main)" "3"
	assert_equal "$(summary_of "$OUT" "Unresolved calls")" "2"
}

@test "HLR-212: where the preprocessor reaches it there is nothing to recover" {
	# The other half, and the reason the tests above name --no-expand. A
	# cross-compiled tree is the state that option describes: where the
	# expansion succeeds, the definition is put in place at its own line and
	# parsed like any other function, and the table has no rows.
	build_evidence
	report "$EVIDENCE" --elf "$IMAGE"
	assert_success

	assert_equal "$(summary_of "$OUT" Functions)" "5"
	assert_equal "$(placed_of "$OUT")" ""
}

@test "HLR-212: with no image no function is placed" {
	# The section belongs to a filtered run, which is the one place the
	# uniform-composition rule gives way, and it gives way by requirement:
	# a run without the option reports exactly what it reported before the
	# option existed (HLR-140).
	report "$EVIDENCE" --no-expand
	assert_success

	run grep -c "Functions the image places" "$OUT"
	assert_output "0"
}

@test "HLR-212: the placed rows survive a record round trip" {
	# A regenerated report has no image to read them off, so a row the
	# record did not carry would vanish and the two reports would disagree
	# about what the build contains (HLR-056).
	build_evidence
	local record="$BATS_TEST_TMPDIR/ev.xml"

	run bash -c '"$0" --elf "$1" --no-expand -f xml "$2" > "$3" 2>/dev/null' \
		"$ELC" "$IMAGE" "$EVIDENCE" "$record"
	assert_success

	run bash -c '"$0" --verbose --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	assert_output --partial "Functions the image places that the parse did not reach (1"
	assert_output --partial "from_macro"
}

# ---------------------------------------------------- with no image at all --

@test "HLR-140: with no image nothing is filtered and no section is added" {
	# The cheapest regression test available, and the one that holds the
	# opt-in promise: a run without the option reports what it reported
	# before the option existed. An empty section is not nothing, so the
	# uniform-composition rule gives way here and the section is absent.
	report "$TREE"
	assert_success

	run bash -c 'grep -c "Linked-image filter" "$0" || true' "$OUT"
	assert_output "0"
	run bash -c 'grep -c "does not define" "$0" || true' "$OUT"
	assert_output "0"
}

@test "HLR-140: an unfiltered run is byte-identical across formats and runs" {
	report "$TREE"
	assert_success
	cp "$OUT" "$BATS_TEST_TMPDIR/first.md"
	report "$TREE"
	assert_success
	run diff -q "$BATS_TEST_TMPDIR/first.md" "$OUT"
	assert_success
}

# ------------------------------------------ what the build did not keep --

@test "HLR-143: the source functions the image does not define are listed" {
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success
	assert_equal "$(absent_of "$OUT")" "unlinked_add unlinked_max "
}

# A tree in which one name is defined twice, which is ordinary C and the case
# the whole of HLR-193 is about. Built two ways: with debug information, so the
# filter can tell the definitions apart, and without, so it cannot.
build_ambiguous() {
	require_tool cc "HLR-193 debug-information placement"
	AMB="$BATS_TEST_TMPDIR/amb"
	mkdir -p "$AMB"
	printf 'static int helper(void){return 1;}\nint used_a(void){return helper();}\n' \
		> "$AMB/a.c"
	printf 'static int helper(void){return 2;}\nint used_b(void){return helper();}\n' \
		> "$AMB/b.c"
	printf 'int used_a(void);int used_b(void);\nint main(void){return used_a()+used_b();}\n' \
		> "$AMB/m.c"
	# The second image links b.c's replacement, which defines no helper at
	# all — so exactly one of the two definitions survives the link.
	printf 'int used_b(void){return 7;}\n' > "$AMB/b2.c"

	cc -O0 -g -o "$AMB/with" "$AMB/a.c" "$AMB/b.c" "$AMB/m.c" 2>/dev/null || \
		skip "cc cannot link here: HLR-193 unverified"
	cc -O0 -g -o "$AMB/one" "$AMB/a.c" "$AMB/b2.c" "$AMB/m.c" 2>/dev/null || \
		skip "cc cannot link here: HLR-193 unverified"
	cc -O0 -g0 -o "$AMB/without" "$AMB/a.c" "$AMB/b.c" "$AMB/m.c" 2>/dev/null || \
		skip "cc cannot link here: HLR-193 unverified"
}

@test "HLR-193: debug information places each definition in its own file" {
	# Both statics survive this link, so both are retained — each against
	# the file that defines it rather than both against whichever the
	# filter happened to match first.
	build_ambiguous
	elc --verbose --elf "$AMB/with" "$AMB/a.c" "$AMB/b.c" "$AMB/m.c"
	assert_success

	assert_equal "$(functions_of <(printf '%s\n' "$output"))" \
		"helper helper main used_a used_b "
}

@test "HLR-193: a definition the link dropped is absent, its namesake retained" {
	# The case name-only matching gets wrong. a.c's helper is linked and
	# b.c's is not; before HLR-193 both were retained, one of them falsely.
	build_ambiguous
	elc --verbose --elf "$AMB/one" "$AMB/a.c" "$AMB/b.c" "$AMB/m.c"
	assert_success

	# One helper kept, and it is a.c's.
	local kept
	kept="$(printf '%s\n' "$output" |
		awk '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
		     f && $3 == "helper" { sub(/:[0-9]+$/, "", $1); print $1 }')"
	assert_equal "$kept" "$AMB/a.c"

	# And b.c's is reported absent, beside the function that called it.
	printf '%s\n' "$output" | grep -q "helper .*$AMB/b.c"
}

@test "HLR-193: an unplaceable ambiguous name fails the run" {
	# No debug information and two definitions: nothing available says
	# which the link kept, so the run stops rather than guessing.
	build_ambiguous
	elc --elf "$AMB/without" "$AMB/a.c" "$AMB/b.c" "$AMB/m.c"
	assert_equal "$status" 2
}

@test "HLR-193: the diagnostic names both files, the image, and the remedy" {
	build_ambiguous
	run bash -c '"$0" --elf "$1" "$2" "$3" "$4" 2>&1 >/dev/null' \
		"$ELC" "$AMB/without" "$AMB/a.c" "$AMB/b.c" "$AMB/m.c"

	assert_output --partial "helper is defined in"
	assert_output --partial "$AMB/a.c"
	assert_output --partial "$AMB/b.c"
	assert_output --partial "$AMB/without"
	assert_output --partial "rebuild the image with -g"
}

# A tree in which a function *template* is defined in two headers. This is the
# shape HLR-200 is about: DWARF records the instantiation under its
# instantiated name, `serialize_seq<int>`, while the source declares the bare
# `serialize_seq`, and before the two were reduced to one spelling the map
# answered "no such function" for one the image describes completely.
#
# Built twice. `app` carries debug information for the unit that instantiated
# the template; `mixed` carries debug information, but not for that unit — the
# condition HLR-201 must tell apart from an image with none.
build_templated() {
	require_tool c++ "HLR-200 reduced name matching"
	TPL="$BATS_TEST_TMPDIR/tpl"
	mkdir -p "$TPL/micro" "$TPL/pro"
	printf 'template <typename T>\nint serialize_seq(T *out, const T *in)\n{\n\tif (!out || !in)\n\t\treturn -1;\n\t*out = *in;\n\treturn 0;\n}\n' \
		> "$TPL/micro/plugin.hpp"
	cp "$TPL/micro/plugin.hpp" "$TPL/pro/plugin.hpp"
	printf '#include "micro/plugin.hpp"\nint use(int *o, const int *i)\n{\n\treturn serialize_seq<int>(o, i);\n}\n' \
		> "$TPL/unit.cpp"
	printf 'int use(int *, const int *);\nint main(void)\n{\n\tint a = 1, b = 0;\n\treturn use(&b, &a);\n}\n' \
		> "$TPL/main.cpp"

	c++ -O0 -g -o "$TPL/app" "$TPL/unit.cpp" "$TPL/main.cpp" -I"$TPL" \
		2>/dev/null || skip "c++ cannot link here: HLR-200 unverified"

	# One unit without -g, the rest with: debug information is present and
	# says nothing about this name.
	c++ -O0 -g0 -c -o "$TPL/unit.o" "$TPL/unit.cpp" -I"$TPL" 2>/dev/null &&
	c++ -O0 -g   -c -o "$TPL/main.o" "$TPL/main.cpp" 2>/dev/null &&
	c++ -o "$TPL/mixed" "$TPL/main.o" "$TPL/unit.o" 2>/dev/null || \
		skip "c++ cannot link here: HLR-201 unverified"
}

@test "HLR-200: a templated name is placed by the file the debug information names" {
	# The run this reported as impossible. Both headers define
	# serialize_seq; DWARF places the instantiation in micro/, so that is
	# the definition kept and pro/'s is dropped.
	build_templated
	elc --verbose --elf "$TPL/app" "$TPL"
	assert_success

	local kept
	kept="$(printf '%s\n' "$output" |
		awk '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
		     f && $3 == "serialize_seq" { sub(/:[0-9]+$/, "", $1); print $1 }')"
	assert_equal "$kept" "$TPL/micro/plugin.hpp"
}

@test "HLR-201: an image with no debug information at all names -g as the remedy" {
	# The condition the old diagnostic described. It is still described,
	# and rebuilding with -g is still what fixes it.
	build_ambiguous
	run bash -c '"$0" --elf "$1" "$2" "$3" "$4" 2>&1 >/dev/null' \
		"$ELC" "$AMB/without" "$AMB/a.c" "$AMB/b.c" "$AMB/m.c"

	assert_output --partial "carries no debug information at all"
	assert_output --partial "rebuild the image with -g"
}

@test "HLR-201: debug information that does not describe the name says so" {
	# The condition the old diagnostic misreported as the one above. The
	# image carries debug information; it simply has no entry for this
	# function, and rebuilding the whole image with -g is not the fix.
	build_templated
	run bash -c '"$0" --elf "$1" "$2" 2>&1 >/dev/null' \
		"$ELC" "$TPL/mixed" "$TPL"

	assert_output --partial "describes no definition of it"
	refute_output --partial "carries no debug information at all"
}

@test "HLR-193: no report is produced when the run fails" {
	# A filtered figure resting on a guess is indistinguishable from a
	# correct one, which is what makes it worse than no figure at all.
	build_ambiguous
	run bash -c '"$0" --elf "$1" "$2" "$3" "$4" 2>/dev/null' \
		"$ELC" "$AMB/without" "$AMB/a.c" "$AMB/b.c" "$AMB/m.c"
	assert_output ""
}

@test "HLR-141: an unambiguous run still needs no debug information" {
	# The failure is confined to the ambiguous case. Every name here is
	# defined once, so an image built without debug information filters
	# exactly as it did before HLR-193.
	build_ambiguous
	cc -O0 -g0 -o "$AMB/plain" "$AMB/a.c" "$AMB/b2.c" "$AMB/m.c" 2>/dev/null || \
		skip "cc cannot link here"

	elc --verbose --elf "$AMB/plain" "$AMB/a.c" "$AMB/m.c"
	assert_success
	assert_output --partial "used_a"
}

@test "HLR-184: the absent-function table is the last section of the report" {
	# The longest table a filtered run produces, and one a reader consults
	# after the report rather than reading the report through. The image
	# itself stays among the summary tiers, above the figures it qualifies.
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success

	local last
	last="$(grep -E '^[A-Z]' "$OUT" | grep -v '^Nothing to report$' | tail -1)"
	assert_equal "${last%% (*}" "Functions the image does not define"

	# And its provenance is not dragged down with it.
	local filter absent
	filter="$(grep -n '^Linked-image filter$' "$OUT" | cut -d: -f1)"
	absent="$(grep -n '^Functions the image does not define' "$OUT" | cut -d: -f1)"
	[ "$filter" -lt "$absent" ]
}

@test "HLR-143: an absent function is located, not merely named" {
	# The reader's next action is to open the file, so a name with no line
	# is a finding they cannot act on.
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success

	run bash -c 'grep -E "unlinked_max +.*dropped\.c +18" "$0"' "$OUT"
	assert_success
}

@test "HLR-143: the report is not a dead-code claim about the call graph" {
	# Two lists, two claims. A function the linker discarded is established
	# by what the build did; an unreachable function is inferred from a
	# traversal. The unreachable section is untouched by the filter and says
	# so, since no entry point was declared.
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success

	run bash -c 'grep -c "Unreachable functions (omitted" "$0"' "$OUT"
	assert_output "1"
}

@test "LLR-ELF-02: a function the image only imports is not one it defines" {
	# printf is called by kept.c, so the image carries a printf of function
	# type whose section index is SHN_UNDEF. Without that test this
	# definition of it would be retained, and the filter would keep source
	# the build never compiled.
	build_image
	report "$IMPORTED" --elf "$IMAGE"
	assert_success
	assert_equal "$(summary_of "$OUT" Functions)" "0"
	assert_equal "$(absent_of "$OUT")" "printf "
}

@test "HLR-144: a call whose target was filtered out is unresolved" {
	# measure calls unlinked_add, which the image imports and does not
	# define. Inventing the edge would make the reachability claim unsound
	# in the one direction it is not already known to err, so the call is
	# counted unresolved instead. One in the unfiltered run — printf — and
	# two here.
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success
	assert_equal "$(summary_of "$OUT" "Unresolved calls")" "2"
}

@test "LLR-ANL-52: a filtered function contributes no fact of any kind" {
	# unlinked_max calls nothing and branches once. Were its body still
	# measured, its decision point would reach the complexity of some
	# function and its lines the ELOC of its file; neither happens, which
	# the ELOC total above already pins. What this adds is that the function
	# is in no graph-derived table either.
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success

	# Terminated at the blank line, or the extractor runs on into the
	# section that lists those very functions as absent and counts them.
	run bash -c 'awk "/^Fan-out/ {s=1; next}
	                  s && /^\$/ {exit}
	                  s && /unlinked/ {c++}
	                  END {print c+0}" "$0"' "$OUT"
	assert_output "0"
}

@test "LLR-ANL-58: a function no configuration builds is not one the image lacks" {
	# The ordering the three exclusions are gathered in. `never_built` sits
	# inside `#if 0`, so it is gone before the image is consulted; reporting
	# it absent would answer a question about the linker with a fact about
	# the preprocessor. `active` is absent from the image and is reported,
	# so the test cannot pass by reporting nothing at all.
	build_image
	report "$PRUNED" --elf "$IMAGE"
	assert_success
	assert_equal "$(absent_of "$OUT")" "active "
	assert_equal "$(summary_of "$OUT" Functions)" "0"
}

# ------------------------------------------- code outside every function --

@test "HLR-145: file-scope ELOC is retained and reported separately" {
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success
	assert_equal "$(filter_of "$OUT" "ELOC outside any function")" "2"
}

@test "HLR-145: a file the image kept nothing from still reports its data" {
	# dropped.c has no function left, and one line of file-scope ELOC. A
	# reader who could not tell that file from an empty one would have been
	# told nothing by the filter.
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success

	run bash -c 'grep -E "dropped\.c +c +23 +1 +0" "$0"' "$OUT"
	assert_success
}

# ------------------------------------------------------ resolving names --

@test "HLR-142: a C++ image matches through Itanium demangling" {
	require_tool c++ "HLR-142 Itanium demangling"
	local image="$BATS_TEST_TMPDIR/shapes"

	c++ -O0 -o "$image" "$CPP/shapes.cpp" 2>/dev/null || \
		skip "c++ cannot link here: HLR-142 unverified"

	report "$CPP" --elf "$image"
	assert_success
	# Every name but main reaches the image encoded and qualified. Matching
	# raw linkage names would retain nothing at all.
	assert_equal "$(summary_of "$OUT" Functions)" "4"
	assert_equal "$(summary_of "$OUT" ELOC)" "4"
	assert_equal "$(absent_of "$OUT")" "perimeter "
	assert_equal "$(filter_of "$OUT" "Unresolved linkage names")" "0"
}

@test "HLR-143: a name in a scheme this build cannot decode is counted" {
	require_tool cc "HLR-143 unresolved linkage names"
	local image="$BATS_TEST_TMPDIR/opaque"

	cc -O0 -o "$image" "$TREE/kept.c" "$TREE/dropped.c" \
		"$MANGLED/opaque.s" 2>/dev/null || \
		skip "cc cannot assemble here: HLR-143 unverified"

	report "$TREE" --elf "$image"
	assert_success
	# Rust's v0 scheme, which the Itanium demangler rejects. Counted rather
	# than matched against a guess, and said out loud rather than left to be
	# inferred from a filter that quietly kept less.
	assert_equal "$(filter_of "$OUT" "Unresolved linkage names")" "1"
	# And the rest of the image still filters: this one symbol resolved to
	# nothing, not the image to nothing.
	assert_equal "$(summary_of "$OUT" Functions)" "5"
}

# ----------------------------------------------------- an unusable image --

@test "HLR-146: a stripped image is fatal and diagnosed as its own case" {
	require_tool cc "HLR-146 unusable image"
	local image="$BATS_TEST_TMPDIR/stripped"

	cc -O0 -s -o "$image" "$TREE/kept.c" "$TREE/dropped.c" 2>/dev/null || \
		skip "cc cannot link a stripped image here: HLR-146 unverified"

	# The case that most needs to be fatal. An empty function set would
	# filter every function away and report a project containing none —
	# confidently wrong, and indistinguishable from a correct result.
	elc --elf "$image" "$TREE"
	[ "$status" -eq 2 ]
	assert_output --partial "no function symbols"
	refute_output --partial "Project summary"
}

@test "HLR-146: a file that is not an object file is fatal" {
	elc --elf "$TREE/kept.c" "$TREE"
	[ "$status" -eq 2 ]
	assert_output --partial "not an object file"
	refute_output --partial "Project summary"
}

@test "HLR-146: an absent image is fatal and names the path" {
	elc --elf "$BATS_TEST_TMPDIR/no-such-image" "$TREE"
	[ "$status" -eq 2 ]
	assert_output --partial "no-such-image"
	refute_output --partial "Project summary"
}

@test "HLR-146: an unusable image ends the run before anything is measured" {
	# Fatal *before* discovery, not after a walk whose results are then
	# thrown away: nothing about the source tree reaches stderr either.
	elc --elf "$BATS_TEST_TMPDIR/no-such-image" "$TREE"
	[ "$status" -eq 2 ]
	refute_output --partial "kept.c"
}

# -------------------------------------------------- recorded and reported --

@test "HLR-147: the report names the image it was filtered by" {
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success
	assert_equal "$(filter_of "$OUT" Image)" "$IMAGE"
}

@test "HLR-147: --elf with --from-xml is a usage error" {
	elc --from-xml "$BATS_TEST_TMPDIR/record.xml" --elf "$BATS_TEST_TMPDIR/img"
	[ "$status" -eq 2 ]
	assert_output --partial "--elf cannot be combined with --from-xml"
}

@test "HLR-147: a report regenerated from the record is byte-identical" {
	build_image
	local record="$BATS_TEST_TMPDIR/record.xml"

	run bash -c '"$0" --elf "$1" -f xml -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$IMAGE" "$record" "$TREE"
	assert_success

	run bash -c '"$0" --elf "$1" -f md -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$IMAGE" "$BATS_TEST_TMPDIR/direct.md" "$TREE"
	assert_success

	run bash -c '"$0" --from-xml "$1" -o "$2" 2>/dev/null' \
		"$ELC" "$record" "$BATS_TEST_TMPDIR/regenerated.md"
	assert_success

	run diff -u "$BATS_TEST_TMPDIR/direct.md" \
		"$BATS_TEST_TMPDIR/regenerated.md"
	assert_success
}

@test "HLR-147: the record carries the image and both mismatch counts" {
	build_image
	local record="$BATS_TEST_TMPDIR/record.xml"

	run bash -c '"$0" --elf "$1" -f xml -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$IMAGE" "$record" "$TREE"
	assert_success

	run grep -c "<image " "$record"
	assert_output "1"
	run grep -c "<absent " "$record"
	assert_output "2"

	require_tool xmllint "HLR-065 well-formed record"
	run xmllint --noout "$record"
	assert_success
}

@test "HLR-140: an unfiltered record carries no image element" {
	local record="$BATS_TEST_TMPDIR/plain.xml"

	run bash -c '"$0" -f xml -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$record" "$TREE"
	assert_success

	run bash -c 'grep -c "<image " "$0" || true' "$record"
	assert_output "0"
}

# ------------------------------------------------------------ determinism --

@test "HLR-032: two filtered runs over the same tree are byte-identical" {
	build_image
	report "$TREE" --elf "$IMAGE"
	assert_success
	cp "$OUT" "$BATS_TEST_TMPDIR/first.md"
	report "$TREE" --elf "$IMAGE"
	assert_success
	run diff -q "$BATS_TEST_TMPDIR/first.md" "$OUT"
	assert_success
}
