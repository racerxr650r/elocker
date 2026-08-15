#!/usr/bin/env bats
# test/integration/discovery.bats — discovery and the table, black box.
#
# Integration tests drive build/elc as a user would and trace to HLRs. The
# fixture level checks the same behaviours against hand-counted trees; this
# level checks the command-line contract around them: exit status, which
# stream carries what, and where the report is written.

setup() {
	load "../helpers/common"

	TREE="$BATS_TEST_TMPDIR/tree"
	mkdir -p "$TREE/sub"
	printf 'one\ntwo\nthree\n' > "$TREE/a.c"
	printf 'only\n'            > "$TREE/sub/b.c"
}

# --- the walking skeleton (HLR-027, HLR-071) ------------------------------

@test "a directory target prints a table of its files" {
	elc "$TREE"
	assert_success
	assert_output --partial "Project summary"
	assert_output --partial "$TREE/a.c"
	assert_output --partial "$TREE/sub/b.c"
}

@test "the table reports physical line counts" {
	elc "$TREE"
	assert_success
	# a.c is three lines, sub/b.c is one, so the project total is four.
	assert_output --regexp "Physical lines +4"
}

@test "a single file target reports that file alone" {
	elc "$TREE/a.c"
	assert_success
	assert_output --partial "$TREE/a.c"
	refute_output --partial "sub/b.c"
}

@test "files and directories may be intermixed on one command line" {
	elc "$TREE/a.c" "$TREE/sub"
	assert_success
	assert_output --partial "$TREE/a.c"
	assert_output --partial "$TREE/sub/b.c"
}

@test "the output shape does not depend on the type of the target" {
	# HLR-006: a file target and a directory target produce the same
	# structure — the same headings and the same columns.
	elc "$TREE/a.c"
	local file_shape
	file_shape="$(grep -oE '^(Project summary|Files|  File .*)$' <<<"$output")"

	elc "$TREE"
	local dir_shape
	dir_shape="$(grep -oE '^(Project summary|Files|  File .*)$' <<<"$output")"

	# The File column is padded to the longest path, so compare the
	# headings rather than their widths.
	assert_equal "$(sed 's/  */ /g' <<<"$dir_shape")" \
	             "$(sed 's/  */ /g' <<<"$file_shape")"
}

# --- duplicate elimination (HLR-072) --------------------------------------

@test "a file named alongside a directory containing it is counted once" {
	elc "$TREE/a.c" "$TREE"
	assert_success
	assert_output --regexp "Files +2"
	assert_equal "$(awk -v p="$TREE/a.c" '$1 == p' <<<"$output" | wc -l)" "1"
}

# --- determinism (HLR-032, HLR-033) ---------------------------------------

@test "two runs over the same target are byte-identical" {
	run bash -c '"$0" "$1"' "$ELC" "$TREE"
	local first="$output"
	run bash -c '"$0" "$1"' "$ELC" "$TREE"
	assert_equal "$output" "$first"
}

@test "targets given in a different order produce identical output" {
	run bash -c '"$0" "$1" "$2"' "$ELC" "$TREE/a.c" "$TREE/sub"
	local first="$output"
	run bash -c '"$0" "$1" "$2"' "$ELC" "$TREE/sub" "$TREE/a.c"
	assert_equal "$output" "$first"
}

# --- invalid targets (HLR-062, HLR-120) -----------------------------------

@test "a target that does not exist exits 2" {
	elc "$TREE/absent.c"
	assert_equal "$status" 2
}

@test "a target that is neither a file nor a directory exits 2" {
	mkfifo "$BATS_TEST_TMPDIR/pipe"
	elc "$BATS_TEST_TMPDIR/pipe"
	assert_equal "$status" 2
}

@test "an unreadable target exits 2" {
	local locked="$BATS_TEST_TMPDIR/locked.c"
	printf 'x\n' > "$locked"
	chmod a-r "$locked"

	run "$ELC" "$locked"
	local rc=$status
	chmod u+r "$locked"           # so the harness can clean up
	[ "$rc" -eq 2 ]
}

@test "an invalid target produces no report at all" {
	run bash -c '"$0" "$1" "$2" 2>/dev/null' "$ELC" "$TREE" "$TREE/absent.c"
	assert_output "" \
		"no report may cover fewer targets than the user named"
}

@test "an invalid target is diagnosed on stderr, naming it" {
	run bash -c '"$0" "$1" 2>&1 >/dev/null' "$ELC" "$TREE/absent.c"
	assert_output --partial "absent.c"
}

# --- an empty run (HLR-066) -----------------------------------------------

@test "a target with no files reports zero totals and exits 0" {
	mkdir -p "$BATS_TEST_TMPDIR/empty"
	elc "$BATS_TEST_TMPDIR/empty"
	assert_success
	assert_output --regexp "Files +0"
	assert_output --regexp "Physical lines +0"
}

# --- the stream split and output redirection (HLR-038, HLR-030) -----------

@test "the report goes to stdout and nothing else does" {
	run bash -c '"$0" "$1" 2>&1 >/dev/null' "$ELC" "$TREE"
	assert_output ""
}

@test "--output writes the report to the named file" {
	elc --output "$BATS_TEST_TMPDIR/report.txt" "$TREE"
	assert_success
	assert_output ""
	run cat "$BATS_TEST_TMPDIR/report.txt"
	assert_output --partial "Project summary"
	assert_output --partial "$TREE/a.c"
}

@test "-o is the short form of --output" {
	elc -o "$BATS_TEST_TMPDIR/short.txt" "$TREE"
	assert_success
	run cat "$BATS_TEST_TMPDIR/short.txt"
	assert_output --partial "Project summary"
}

@test "a redirected report is byte-identical to the one on stdout" {
	run bash -c '"$0" "$1"' "$ELC" "$TREE"
	local piped="$output"
	run "$ELC" -o "$BATS_TEST_TMPDIR/redirected.txt" "$TREE"
	assert_equal "$(cat "$BATS_TEST_TMPDIR/redirected.txt")" "$piped"
}

@test "an output file that cannot be opened exits 2" {
	elc -o "$BATS_TEST_TMPDIR/absent-dir/report.txt" "$TREE"
	assert_equal "$status" 2
}

@test "an output file that cannot be opened is diagnosed on stderr" {
	run bash -c '"$0" -o "$1" "$2" 2>&1 >/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/absent-dir/report.txt" "$TREE"
	assert_output --partial "absent-dir"
}

# --- per-file failure tolerance (HLR-035, HLR-037, HLR-120) ---------------

@test "an unreadable file inside a target degrades the run to 1" {
	printf 'x\n' > "$TREE/locked.c"
	chmod a-r "$TREE/locked.c"

	run "$ELC" "$TREE"
	local rc=$status out="$output"
	chmod u+r "$TREE/locked.c"

	[ "$rc" -eq 1 ] || {
		echo "expected exit 1, got $rc" >&2
		false
	}
	grep -q "a.c" <<<"$out" || {
		echo "the report must still cover the files that succeeded" >&2
		false
	}
}
