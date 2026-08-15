#!/usr/bin/env bats
# test/fixtures/determinism/determinism.bats — byte-identical output (STP §5).
#
# Expected values are hand-counted and justified in README.md beside this
# file. A failure here is a product defect, never a flaky test.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/determinism/tree"
}

# Run elc and echo its stdout, so two invocations can be compared as strings.
report() {
	elc "$@"
	printf '%s\n' "$output"
}

@test "the hand-counted determinism totals match" {
	elc "$TREE"
	assert_success
	assert_output --regexp "Files +3"
	assert_output --regexp "Physical lines +3"
}

@test "HLR-033: files are presented in byte order, not creation order" {
	elc "$TREE"
	assert_success
	# a.c, then m/n.c, then z.c — z.c was written to the tree first.
	local paths
	# Scoped to the Files section; the Functions section repeats each path.
	paths="$(awk '/^Files$/ { f = 1; next } f && /^$/ { f = 0 } f && /^  \// { print $1 }' <<<"$output")"
	assert_equal "$paths" "$(sort <<<"$paths")"
}

@test "HLR-032: two runs over the same target are byte-identical" {
	run report "$TREE"
	local first="$output"
	run report "$TREE"
	assert_equal "$output" "$first"
}

@test "HLR-033: two directory targets in either order agree" {
	run report "$TREE" "$TREE/m"
	local first="$output"
	run report "$TREE/m" "$TREE"
	assert_equal "$output" "$first"
}

@test "HLR-033: a file target and a directory target in either order agree" {
	run report "$TREE/a.c" "$TREE/m"
	local first="$output"
	run report "$TREE/m" "$TREE/a.c"
	assert_equal "$output" "$first"
}

# --- zero configuration (HLR-039) -----------------------------------------
#
# The decoys are planted in a copy of the tree, so the same command can be run
# with and without them and the two outputs compared. A decoy committed beside
# the fixture could only ever be present.

@test "HLR-039: decoys in the working directory, the target, and an ancestor change nothing" {
	local ancestor="$BATS_TEST_TMPDIR/ancestor"
	local copy="$ancestor/tree"
	mkdir -p "$ancestor"
	cp -r "$TREE" "$copy"

	cd "$ancestor"
	run report "$copy"
	local clean="$output"

	printf 'format = nonsense\n' > "$ancestor/.elcrc"
	printf 'format = nonsense\n' > "$copy/.elcrc"
	printf 'format = nonsense\n' > "$copy/.elc.toml"
	printf 'threshold = 1\n'     > "$ancestor/.editorconfig"

	run report "$copy"
	assert_equal "$output" "$clean" \
		"a configuration-like file must produce output identical to \
its absence"
}

@test "HLR-039: a decoy does not change the file count either" {
	local copy="$BATS_TEST_TMPDIR/target"
	cp -r "$TREE" "$copy"
	printf 'format = nonsense\n' > "$copy/.elcrc"

	elc "$copy"
	assert_success
	assert_output --regexp "Files +3"
}
