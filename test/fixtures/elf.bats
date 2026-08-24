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
	     s && /^  [^ -]/ {print $2}' "$1" | sort | tr '\n' ' '
}

# The functions the image does not define, from the section of that name.
absent_of() {
	awk '/^Functions the image does not define/ {s=1; next}
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

# One function's ELOC, from the per-function tier.
eloc_of() {
	awk -v want="$2" '/^Functions$/ {s=1; next}
	                  s && /^$/ {exit}
	                  s && $2 == want {print $4}' "$1"
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
	# Three statements and the two directives around them; one file whose
	# coverage could not be established. The pair is read as the
	# unresolved-call count and the undecided-region count are: the first
	# states what the filter removed, the second where it could not look.
	build_debugline
	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success

	assert_equal "$(filter_of "$OUT" "Lines not compiled by this build")" "5"
	assert_equal "$(filter_of "$OUT" "Files with no debug coverage")" "1"
}

@test "HLR-133: the region is undecided, so only the image ruled it out" {
	# The fixture's whole assertion rests on this. Were the condition
	# decidable from the source, Phase 15 would have removed the region
	# before the image was consulted and every test above would pass
	# against an implementation that read no debug information at all.
	build_debugline
	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success
	assert_equal "$(summary_of "$OUT" "Undecided regions")" "1"
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
	# asserted here — one would pin the fixture to a single compiler's
	# optimiser rather than to a requirement.
	build_debugline -O0
	report "$DEBUGLINE" --elf "$IMAGE"
	assert_success
	local at_o0
	at_o0="$(filter_of "$OUT" "Lines not compiled by this build")"
	assert_equal "$at_o0" "5"

	build_debugline -O2
	report "$DEBUGLINE" --elf "$IMAGE"
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
	# Neither can be recomputed from a record: regeneration has no image,
	# and no debug information to read from one.
	build_debugline
	local record="$BATS_TEST_TMPDIR/dl.xml"

	run bash -c '"$0" --elf "$1" -f xml "$2" > "$3" 2>/dev/null' \
		"$ELC" "$IMAGE" "$DEBUGLINE" "$record"
	assert_success

	run bash -c '"$0" --verbose --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	assert_output --partial "Lines not compiled by this build"
	assert_output --regexp "Lines not compiled by this build *\| *5"
	assert_output --regexp "Files with no debug coverage *\| *1"
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
