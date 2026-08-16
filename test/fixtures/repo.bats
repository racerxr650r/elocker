#!/usr/bin/env bats
# test/fixtures/repo/repo.bats — the repository discovery route (STP §5).
#
# Expected values are hand-counted and justified in README.md beside this
# file. Never regenerate them from elc's output.

setup() {
	load "../helpers/common"
	require_tool git "the repository discovery route"

	# Built fresh for every test, in that test's own temporary directory,
	# which bats removes afterwards. Rebuilding per test costs a few
	# milliseconds and buys independence: a test that ruins the fixture —
	# and two below deliberately do, by removing .git — cannot affect the
	# next one.
	REPO="$BATS_TEST_TMPDIR/repo"
	"$BATS_TEST_DIRNAME/repo/build.sh" "$REPO"
	REPO_REAL="$(cd "$REPO" && pwd -P)"
}

# The paths elc reports, one per line, with the fixture prefix stripped so
# the assertions read as the tree does.
analysed() {
	elc "$@"
	printf '%s\n' "$output" |
		awk '/^Files$/ { f = 1; next } f && /^$/ { f = 0 } f && /^  \// { print $1 }' |
		sed "s|^$REPO_REAL/||"
}

# The route elc reports for a target, from the Discovery section (HLR-127).
# Reading it back is what makes every fallback test below able to state which
# route ran, rather than inferring it from a file count that two different
# routes could produce.
route_for() {
	elc "$@"
	printf '%s\n' "$output" |
		awk '/^Discovery$/ { d = 1; next } d && /^$/ { d = 0 }
		     d && NF == 2 && $1 !~ /^-/ && $1 != "Target" { print $2 }'
}

# ------------------------------------------------------------ enumeration ---

@test "HLR-002: the repository route yields exactly the tracked source files" {
	run analysed "$REPO"
	assert_success
	assert_output "docs/d.c
src/a.c
src/b.c"
}

@test "the hand-counted repository totals match" {
	elc "$REPO"
	assert_success
	assert_output --regexp "Files +3"
	assert_output --regexp "Physical lines +8"
}

@test "HLR-003: an untracked file is excluded" {
	run analysed "$REPO"
	assert_success
	refute_line "src/untracked.c"

	# Present, readable, and a perfectly ordinary source file: nothing but
	# its absence from HEAD keeps it out.
	[ -f "$REPO/src/untracked.c" ]
}

@test "HLR-003: a gitignored build directory is excluded" {
	run analysed "$REPO"
	assert_success
	refute_line "build/gen.c"
}

@test "HLR-003: .gitignore is never read to decide this" {
	# The file is excluded because it is untracked, not because elc parsed
	# the ignore rules. Removing .gitignore entirely must change nothing —
	# if it did, elc would be consulting it, and would then disagree with
	# git about anything the ignore rules do not spell out the same way
	# (a nested .gitignore, a negation, core.excludesFile).
	run analysed "$REPO"
	local with_ignore="$output"

	rm "$REPO/.gitignore"
	run analysed "$REPO"
	assert_success
	assert_equal "$output" "$with_ignore"
}

@test "HLR-003: a tracked file with binary content is excluded despite its extension" {
	run analysed "$REPO"
	assert_success
	refute_line "src/blob.c"

	# The extension list cannot have done this: the name ends in .c, which
	# is a language elc supports. Git's own content check did.
	run grep -c '^\.c$' "$ELC_RUNTIME_DIR/binary.exts"
	assert_output "0"
}

@test "HLR-005: the exclusions of the filesystem walk apply to this route too" {
	run analysed "$REPO"
	assert_success
	refute_line "logo.png"     # excluded by extension
	refute_line ".hidden.c"    # excluded as a hidden entry
	refute_line ".gitignore"   # excluded as a hidden entry
}

# ---------------------------------------------------------------- scoping ---

@test "HLR-126: naming a subdirectory analyses that subdirectory and nothing above it" {
	run analysed "$REPO/src"
	assert_success
	assert_output "src/a.c
src/b.c"
}

@test "HLR-126: the scoped totals are the subdirectory's, not the repository's" {
	elc "$REPO/src"
	assert_success
	assert_output --regexp "Files +2"
	assert_output --regexp "Physical lines +7"
}

@test "HLR-126: a directory denotes the same files by either route" {
	# The property the scoping requirement exists to guarantee, asserted
	# directly: enumerate src/ through the repository, then destroy the
	# repository and enumerate the same directory through the filesystem
	# walk. The two answers must agree.
	#
	# The tree is arranged so that they *can* agree: the files that only
	# one route excludes — untracked, ignored, binary-by-content — are all
	# outside src/ or absent from it. Everything left is excluded by both
	# routes or by neither, which is what makes disagreement a real defect
	# rather than a difference the fixture built in.
	rm -f "$REPO/src/untracked.c" "$REPO/src/blob.c"

	run analysed "$REPO/src"
	assert_success
	local via_repository="$output"
	assert_equal "$(route_for "$REPO/src")" "repository"

	rm -rf "$REPO/.git"
	run analysed "$REPO/src"
	assert_success
	assert_equal "$(route_for "$REPO/src")" "filesystem"

	assert_equal "$output" "$via_repository"
}

# --------------------------------------------------------------- fallback ---

@test "HLR-004: a directory in no repository is walked at the filesystem level" {
	rm -rf "$REPO/.git"
	assert_equal "$(route_for "$REPO")" "filesystem"
}

@test "HLR-002: an enclosing repository that does not track the target is disregarded" {
	# The build directory: inside the work tree, excluded by .gitignore, so
	# the repository is found and is not applicable. Falling back is what
	# makes `elc build/` analyse anything at all — enumerating HEAD under
	# that prefix yields nothing, and reporting an empty result would be
	# both wrong and hard to distinguish from a directory with no source.
	assert_equal "$(route_for "$REPO/build")" "filesystem"

	run analysed "$REPO/build"
	assert_success
	assert_output "build/gen.c"
}

@test "HLR-002: a directory tracked by no repository above it falls back" {
	# The unrelated-enclosing-repository case, of which a version-
	# controlled home directory is the everyday example: the search finds a
	# repository, but nothing at or beneath the target is in it.
	mkdir -p "$REPO/fresh"
	printf 'int fresh(void) { return 0; }\n' > "$REPO/fresh/f.c"

	assert_equal "$(route_for "$REPO/fresh")" "filesystem"

	run analysed "$REPO/fresh"
	assert_success
	assert_output "fresh/f.c"
}

@test "HLR-002: a repository with no commits falls back rather than failing" {
	# HEAD exists but resolves to nothing. There is no tree to walk, and a
	# run that reported an error here would fail on a repository the user
	# had just initialised — an ordinary state, not a broken one.
	local empty="$BATS_TEST_TMPDIR/empty"
	mkdir -p "$empty"
	git -c init.defaultBranch=main init -q "$empty"
	printf 'int e(void) { return 0; }\n' > "$empty/e.c"

	assert_equal "$(route_for "$empty")" "filesystem"

	elc "$empty"
	assert_success
	assert_output --regexp "Files +1"
}

# ------------------------------------------------------------------ route ---

@test "HLR-127: the route is reported for each directory target" {
	rm -rf "$BATS_TEST_TMPDIR/plain"
	mkdir -p "$BATS_TEST_TMPDIR/plain"
	printf 'int p(void) { return 0; }\n' > "$BATS_TEST_TMPDIR/plain/p.c"

	elc "$REPO" "$BATS_TEST_TMPDIR/plain"
	assert_success
	assert_output --regexp "Discovery"
	assert_output --regexp "$REPO_REAL +repository"
	assert_output --regexp "plain +filesystem"
}

@test "HLR-072: one directory named twice is one row, canonically spelled" {
	# The Discovery section reports directories, not arguments. Both
	# spellings name the same directory, so a second row would report a
	# run over two targets that was a run over one — and it would sit
	# beside a Files section that had already collapsed them.
	elc "$REPO" "$REPO/."
	assert_success

	local rows
	rows="$(printf '%s\n' "$output" |
		awk '/^Discovery$/ { d = 1; next } d && /^$/ { d = 0 }
		     d && /repository/ { n++ } END { print n + 0 }')"
	assert_equal "$rows" "1"
	assert_output --regexp "$REPO_REAL +repository"
}

@test "HLR-033: the discovery section does not depend on the order of the targets" {
	# The section lists targets, so it is a collection, so it is ordered by
	# a key rather than by the order the user happened to type. Named here
	# because a section rendered in argument order is the easiest way for
	# this requirement to regress, and the failure looks like a diff in a
	# report nobody thought was order-dependent.
	mkdir -p "$BATS_TEST_TMPDIR/plain"
	printf 'int p(void) { return 0; }\n' > "$BATS_TEST_TMPDIR/plain/p.c"

	elc "$REPO" "$BATS_TEST_TMPDIR/plain"
	local first="$output"
	elc "$BATS_TEST_TMPDIR/plain" "$REPO"
	assert_equal "$output" "$first"
}

@test "HLR-006: a repository target produces the same report shape as any other" {
	# The man page has claimed since Phase 5 that a single file, a plain
	# directory, and a repository all produce the same headings. Two
	# thirds of that were tested; this is the third. Column widths vary
	# with content, so the headings are what is compared.
	mkdir -p "$BATS_TEST_TMPDIR/plain"
	printf 'int p(void) { return 0; }\n' > "$BATS_TEST_TMPDIR/plain/p.c"

	elc "$BATS_TEST_TMPDIR/plain"
	local plain_shape
	plain_shape="$(grep -E '^[A-Z]' <<<"$output")"

	elc "$REPO"
	local repo_shape
	repo_shape="$(grep -E '^[A-Z]' <<<"$output")"

	assert_equal "$repo_shape" "$plain_shape"
}

@test "HLR-056: the record carries the route, so regeneration is the same report" {
	# A record that dropped the route would regenerate into a report with
	# an empty Discovery section — still well-formed, still carrying every
	# measurement, and not the same report. Byte-identity is what catches
	# that; a test asserting on the numbers alone would not.
	#
	# Compared against Markdown because that is what regeneration renders
	# (regeneration/README.md).
	local record="$BATS_TEST_TMPDIR/record.xml"

	run bash -c '"$0" -f md "$1" 2>/dev/null' "$ELC" "$REPO"
	assert_success
	local direct="$output"

	run bash -c '"$0" -f xml "$1" > "$2" 2>/dev/null' "$ELC" "$REPO" "$record"
	assert_success

	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	assert_equal "$output" "$direct"

	# And the route survived rather than both reports being equally empty:
	# exactly one line names the target and the route together. Counted
	# with grep rather than asserted with --regexp, because that matcher
	# tests the whole output as a single string, where a pattern spanning
	# two lines would match and prove nothing.
	local rows
	rows="$(printf '%s\n' "$output" | grep -F "$REPO_REAL" | grep -c repository)"
	assert_equal "$rows" "1"
}
