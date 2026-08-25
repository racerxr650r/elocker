#!/usr/bin/env bats
# test/fixtures/debug.bats — the debug companion (STP §5).
#
# The companion exists for runs nobody can reproduce: a tree on someone else's
# machine, a grammar failing on source that cannot be shared. So what is
# asserted here is not only that a file appears, but that it holds the three
# things a maintainer holding nothing but the file would need — what was run,
# what went wrong, and the source that caused it.

setup() {
	load "../helpers/common"

	TREE="$BATS_TEST_TMPDIR/tree"
	mkdir -p "$TREE"
	printf 'int sound(void)\n{\n\treturn 1;\n}\n' > "$TREE/sound.c"
	# A construct the C grammar cannot follow, between two it can.
	printf 'int before(void){return 1;}\n\nint broken(void) { @@ ; ((( \n\nint after(void){return 2;}\n' \
		> "$TREE/bad.c"
	OUT="$BATS_TEST_TMPDIR/report.md"
	DBG="$BATS_TEST_TMPDIR/report.dbg"
}

# --- the companion rule (HLR-119, HLR-194) ---------------------------------

@test "HLR-194: the companion is named from the report and written beside it" {
	run "$ELC" --dbg -o "$OUT" "$TREE"
	[ -f "$DBG" ]
}

@test "HLR-119: no companion is written when the report goes to stdout" {
	# Not a usage error: the request produces no file, which is what the
	# companion rule says happens when there is no name to derive one from.
	cd "$BATS_TEST_TMPDIR"
	run "$ELC" --dbg "$TREE"
	[ "$status" -ne 2 ]
	assert_equal "$(ls "$BATS_TEST_TMPDIR"/*.dbg 2>/dev/null | wc -l)" "0"
}

@test "HLR-194: no companion is written without the option" {
	run "$ELC" -o "$OUT" "$TREE"
	[ ! -f "$DBG" ]
}

# --- what it carries (HLR-194, HLR-195) ------------------------------------

@test "HLR-194: the companion records the invocation" {
	# The first question asked of a log from a machine nobody has is what
	# was actually run.
	run "$ELC" --dbg -o "$OUT" "$TREE"
	run cat "$DBG"
	assert_output --partial "invocation:"
	assert_output --partial "--dbg"
	assert_output --partial "$TREE"
}

@test "HLR-194: every message sent to standard error is in the companion" {
	run bash -c '"$0" --dbg -o "$1" "$2" 2>&1 >/dev/null' \
		"$ELC" "$OUT" "$TREE"
	local on_stderr="$output"
	[ -n "$on_stderr" ]

	# Each line standard error received appears in the companion.
	while IFS= read -r line; do
		[ -n "$line" ] || continue
		grep -qF "$line" "$DBG" || {
			echo "missing from the companion: $line" >&2
			false
		}
	done <<<"$on_stderr"
}

@test "HLR-195: an unparsable region is recorded with its source" {
	# The text is the point. A grammar that fails on a construct is
	# debugged from the construct, and the construct is the one thing a
	# maintainer holding only a bug report does not have.
	run "$ELC" --dbg -o "$OUT" "$TREE/bad.c"
	run cat "$DBG"
	assert_output --partial "parse failure"
	assert_output --partial "bad.c"
	assert_output --partial "@@ ; ((("
}

@test "HLR-195: a sound file records no parse failure" {
	run "$ELC" --dbg -o "$OUT" "$TREE/sound.c"
	run cat "$DBG"
	refute_output --partial "parse failure"
}

# --- it changes nothing (HLR-194) ------------------------------------------

@test "HLR-194: standard error is identical with the option and without" {
	run bash -c '"$0" -o "$1" "$2" 2>&1 >/dev/null' \
		"$ELC" "$BATS_TEST_TMPDIR/a.md" "$TREE"
	local without="$output"
	run bash -c '"$0" --dbg -o "$1" "$2" 2>&1 >/dev/null' \
		"$ELC" "$BATS_TEST_TMPDIR/b.md" "$TREE"
	assert_equal "$output" "$without"
}

@test "HLR-194: the report is identical with the option and without" {
	run bash -c '"$0" -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$BATS_TEST_TMPDIR/a.md" "$TREE"
	run bash -c '"$0" --dbg -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$BATS_TEST_TMPDIR/b.md" "$TREE"
	run diff "$BATS_TEST_TMPDIR/a.md" "$BATS_TEST_TMPDIR/b.md"
	assert_success
}

@test "HLR-194: the exit status is identical with the option and without" {
	run "$ELC" -o "$BATS_TEST_TMPDIR/a.md" "$TREE"
	local without="$status"
	run "$ELC" --dbg -o "$BATS_TEST_TMPDIR/b.md" "$TREE"
	assert_equal "$status" "$without"
}

@test "HLR-032: the timestamps stay out of the report" {
	# A log nobody watched being produced needs them; a report must be
	# byte-identical across two runs over one target.
	run "$ELC" --dbg -o "$OUT" "$TREE"
	run cat "$OUT"
	refute_output --regexp "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z"

	run cat "$DBG"
	assert_output --regexp "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z"
}

@test "HLR-194: the companion is readable while the run is still going" {
	# Written as messages occur rather than flushed at exit, which is the
	# property the feature exists for: a run that faults on a tree nobody
	# can reproduce still leaves everything up to the fault on disk.
	#
	# Observed from the outside by killing the run and reading what
	# survived. A buffered implementation would leave an empty file.
	local big="$BATS_TEST_TMPDIR/big"
	mkdir -p "$big"
	for i in $(seq 1 400); do
		printf 'int broken%d(void) { @@ ; ((( \n' "$i" > "$big/f$i.c"
	done

	"$ELC" --dbg -o "$BATS_TEST_TMPDIR/big.md" "$big" >/dev/null 2>&1 &
	local pid=$!
	# Let it get past the invocation header and into the files.
	local waited=0
	while [ ! -s "$BATS_TEST_TMPDIR/big.dbg" ] && [ "$waited" -lt 50 ]; do
		sleep 0.1
		waited=$((waited + 1))
	done
	kill -9 "$pid" 2>/dev/null || true
	wait "$pid" 2>/dev/null || true

	[ -s "$BATS_TEST_TMPDIR/big.dbg" ]
	run cat "$BATS_TEST_TMPDIR/big.dbg"
	assert_output --partial "invocation:"
}
