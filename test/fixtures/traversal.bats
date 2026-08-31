#!/usr/bin/env bats
# test/fixtures/traversal/traversal.bats — the analysed file set (STP §5).
#
# Expected values are hand-counted and justified in README.md beside this
# file. Never regenerate them from elc's output.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/traversal/tree"
	# elc reports canonical absolute paths, so the prefix to strip is the
	# tree's own canonical path.
	TREE_REAL="$(cd "$TREE" && pwd -P)"
}

# The paths elc reports, one per line, with the fixture prefix stripped so
# the assertions read as the tree does. Goes through the shared elc helper so
# that `make valgrind` instruments these runs too.
#
# Scoped to the Files section: since Phase 2 the Functions section also has
# rows beginning with a path, and an unscoped match would report each file
# once per function it defines.
analysed() {
	# --verbose because the Files table is a detail tier of the aligned
	# table since HLR-218.
	elc --verbose "$@"
	printf '%s\n' "$output" |
		awk '/^Files$/ { f = 1; next } f && /^$/ { f = 0 } f && /^  \// { print $1 }' |
		sed "s|^$TREE_REAL/||"
}

@test "the walk yields exactly the three source files" {
	run analysed "$TREE"
	assert_success
	assert_output "a.c
b.h
sub/c.c"
}

@test "the hand-counted traversal totals match" {
	elc "$TREE"
	assert_success
	assert_output --regexp "Files +3"
	assert_output --regexp "Physical lines +8"
}

@test "HLR-005: a binary extension is excluded" {
	run analysed "$TREE"
	refute_output --partial "image.png"
	refute_output --partial "archive.zip"
}

@test "HLR-005: a hidden directory is excluded" {
	run analysed "$TREE"
	refute_output --partial "secret.c"
}

@test "HLR-005: a hidden file is excluded" {
	run analysed "$TREE"
	refute_output --partial ".elcrc"
}

@test "HLR-069: a symbolic link is not followed during the walk" {
	run analysed "$TREE"
	refute_output --partial "link.c"
}

@test "HLR-069: a cyclic directory link does not hang the walk" {
	# Reaching the assertion is the result. A logical walk would not
	# return, so the harness's own timeout would be the failure.
	elc "$TREE"
	assert_success
	assert_output --regexp "Files +3"
}

@test "HLR-069: a symbolic link named as a target is resolved" {
	elc "$TREE/link.c"
	assert_success
	assert_output --partial "$TREE_REAL/a.c"
	assert_output --regexp "Files +1"
}

@test "HLR-072: naming a file and its directory analyses it once" {
	run analysed "$TREE/a.c" "$TREE"
	assert_output "a.c
b.h
sub/c.c"
}

@test "HLR-071: several targets combine into one report" {
	run analysed "$TREE/sub" "$TREE/a.c"
	assert_output "a.c
sub/c.c"
}

@test "HLR-043: the fixture tree is unchanged by a run" {
	local before after
	before="$(find "$TREE" -type f -exec sha256sum {} + | sort)"
	elc "$TREE"
	assert_success
	after="$(find "$TREE" -type f -exec sha256sum {} + | sort)"
	assert_equal "$after" "$before"
}
