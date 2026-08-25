#!/usr/bin/env bats
# test/fixtures/repair.bats — repairing what the grammar could not follow.
#
# The expected values come from `expanded.c`, a hand-written equivalent of
# `shapes.c` with the macros already expanded. Comparing the two is what makes
# this a conformance test rather than a golden file: a repair that measured
# something other than what the source means fails, whatever elc reports.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/repair/tree"
	OUT="$BATS_TEST_TMPDIR/report.md"
	DBG="$BATS_TEST_TMPDIR/report.dbg"
}

# One function's ELOC and complexity from the one function table.
figures() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Functions$/ { f = 1; next } f && /^$/ { f = 0 }
		                  f && $2 == want { print $4, $5 }'
}

@test "HLR-196: each macro shape parses where it did not before" {
	elc --verbose "$TREE/shapes.c"
	assert_success
	refute_output --partial "could not be parsed"
}

@test "HLR-197: a repaired file measures what the expanded source measures" {
	# The oracle is expanded.c, written by hand. branchy/report against
	# branchy2/report2: same ELOC, same complexity, or the repair changed
	# the meaning of the code it repaired.
	elc --verbose "$TREE"
	assert_success
	assert_equal "$(figures branchy)" "$(figures branchy2)"
	assert_equal "$(figures report)"  "$(figures report2)"
}

@test "HLR-197: a repair does not move the lines beneath it" {
	# The whole defence of the approach. Every rule replaces text within
	# the line it sits on, so a function below a repair keeps its range.
	elc --verbose "$TREE/shapes.c"
	assert_success
	assert_output --regexp "report +2[0-9]-2[0-9]"
}

@test "LLR-RPR-01: a file with nothing to repair is untouched" {
	elc --dbg -o "$OUT" "$TREE/sound.c"
	assert_success
	run cat "$DBG"
	refute_output --partial "repair:"
}

@test "LLR-RPR-02: the source file is never modified" {
	local before after
	before="$(md5sum < "$TREE/shapes.c")"
	elc --verbose "$TREE/shapes.c"
	assert_success
	after="$(md5sum < "$TREE/shapes.c")"
	assert_equal "$after" "$before"
}

@test "LLR-RPR-03: every rule fires on the shape it is for" {
	elc --dbg -o "$OUT" "$TREE"
	assert_success
	run cat "$DBG"
	assert_output --partial "macro adjacent to a string"
	assert_output --partial "macro before a declaration"
	assert_output --partial "macro as a declarator"
}

@test "LLR-RPR-05: two runs over one target repair identically" {
	# Rules in a fixed order over regions in a fixed order, or every figure
	# downstream of a repair varies between runs (HLR-032).
	elc --dbg -o "$BATS_TEST_TMPDIR/a.md" "$TREE"
	local first
	first="$(grep -c 'repair:' "$BATS_TEST_TMPDIR/a.dbg")"
	elc --dbg -o "$BATS_TEST_TMPDIR/b.md" "$TREE"
	assert_equal "$(grep -c 'repair:' "$BATS_TEST_TMPDIR/b.dbg")" "$first"
	run diff "$BATS_TEST_TMPDIR/a.md" "$BATS_TEST_TMPDIR/b.md"
	assert_success
}

@test "LLR-RPR-06, HLR-199: each repair is recorded with its rule" {
	elc --dbg -o "$OUT" "$TREE/shapes.c"
	assert_success
	run cat "$DBG"
	assert_output --regexp "repair: macro [a-z ]+ at .*shapes\.c byte [0-9]+"
}

@test "HLR-199: the report states how many repairs and under which rule" {
	# The companion of the test above is for a maintainer with the file to
	# hand. This is the half a reader of the report gets: a figure resting
	# on a guess says so where the figures are read, not only in a debug
	# artefact nobody opens.
	elc "$TREE/shapes.c"
	assert_success
	assert_output --partial "Repaired regions"
	assert_output --regexp "shapes\.c +macro adjacent to a string +1"
	assert_output --regexp "shapes\.c +macro before a declaration +1"
	assert_output --regexp "shapes\.c +macro as a declarator +1"
}

@test "HLR-199: a report rebuilt from XML still declares its repairs" {
	# A reconstruction that dropped the declaration would read as measured
	# source, which is the failure HLR-199 exists to prevent.
	elc --format xml -o "$BATS_TEST_TMPDIR/r.xml" "$TREE/shapes.c"
	assert_success
	# A saved record regenerates as Markdown and no other format, so the
	# rows come back inside a table rather than a column layout.
	elc --from-xml "$BATS_TEST_TMPDIR/r.xml"
	assert_success
	assert_output --partial "Repaired regions"
	assert_output --regexp "\| macro as a declarator +\| +1 \|"
}

@test "HLR-199: a sound file declares no repairs at all" {
	elc "$TREE/sound.c"
	assert_success
	refute_output --regexp "^Repaired regions"
	# Named among the tables that were empty, which is how the report
	# distinguishes "none found" from "not looked for".
	assert_output --partial "    - Repaired regions"
}

@test "HLR-198: repair terminates on source it cannot fix" {
	# A file of shapes no rule matches must not loop: the pass that fails
	# to reduce the damage ends it, and the run completes.
	printf 'int f(void) { @@@ ((( ~~~ }\nint g(void){return 1;}\n' \
		> "$BATS_TEST_TMPDIR/bad.c"
	run timeout 20 "$ELC" --verbose "$BATS_TEST_TMPDIR/bad.c"
	[ "$status" -ne 124 ] || {
		echo "repair did not terminate" >&2
		false
	}
	assert_output --partial "could not be parsed"
}

@test "LLR-RPR-04: a pass that does not help is withdrawn, not left half-done" {
	# The mechanism termination rests on, and the one that makes a wrong
	# rule cheap: the file is measured unrepaired, which is where it
	# started. Observed from outside — the sound function must report the
	# same figures whether or not damage sits beside it.
	{
		echo "int g(int n){ if(n) return 1; return 0; }"
		echo "@@@ ~~~ ###"
	} > "$BATS_TEST_TMPDIR/bad.c"
	echo "int g(int n){ if(n) return 1; return 0; }" > "$BATS_TEST_TMPDIR/good.c"

	# Status 1 is the degraded run of HLR-035 rather than a failure: the
	# file genuinely holds a line nothing could parse.
	elc --verbose "$BATS_TEST_TMPDIR/bad.c"
	local damaged
	damaged="$(figures g)"

	elc --verbose "$BATS_TEST_TMPDIR/good.c"
	assert_success
	assert_equal "$damaged" "$(figures g)"

	# And no repair was kept, because none of them helped.
	elc --dbg -o "$BATS_TEST_TMPDIR/bad.md" "$BATS_TEST_TMPDIR/bad.c"
	run cat "$BATS_TEST_TMPDIR/bad.dbg"
	refute_output --partial "repair:"
}

@test "HLR-035: the unparsed count is what remains after repair" {
	# shapes.c is fully repaired, so nothing is reported unparsed — the
	# count exists to say how much was not measured, and repaired lines
	# were.
	elc "$TREE/shapes.c"
	assert_success
	refute_output --partial "could not be parsed"
}
