#!/usr/bin/env bats
# test/integration/complexity.bats — the threshold and the callouts, black box.
#
# What counts as a decision point is the query file's decision and is asserted
# at the fixture level. This level asserts the command-line contract around
# it: what the threshold changes, what it does not, and that the callouts are
# the same on every run.

setup() {
	load "../helpers/common"

	TREE="$BATS_TEST_TMPDIR/tree"
	mkdir -p "$TREE"

	# simple() never branches; branchy() branches four times.
	printf 'int simple(int n)\n{\n\treturn n;\n}\n\nint branchy(int a, int b)\n{\n\tif (a && b)\n\t\treturn 1;\n\twhile (a--)\n\t\tb++;\n\treturn b;\n}\n' \
		> "$TREE/pair.c"
}

# --- the metric (HLR-017) --------------------------------------------------

@test "HLR-017: complexity is reported per function" {
	elc "$TREE/pair.c"
	assert_success
	assert_output --regexp "simple +1-4 +1 +1"
	assert_output --regexp "branchy +6-13 +5 +4"
}

@test "HLR-026: the summary names the most complex function and the largest file" {
	elc "$TREE/pair.c"
	assert_success
	assert_output --partial "Callouts"
	assert_output --partial "Most complex"
	assert_output --partial "branchy"
	assert_output --partial "Largest file"
}

# --- the threshold (HLR-021, HLR-022) --------------------------------------

@test "HLR-022: the threshold defaults to 15" {
	elc "$TREE/pair.c"
	assert_success
	assert_output --partial "At or over the complexity threshold (15)"
}

@test "HLR-021: a function at or over the threshold is listed for its file" {
	elc -c 4 "$TREE/pair.c"
	assert_success

	local listed
	listed="$(awk '/^At or over/ { f = 1; next } f && /^  \// { print $2 }' \
		<<<"$output")"
	assert_equal "$listed" "branchy"
}

@test "HLR-021: the listing is at-or-over, not strictly over" {
	# branchy is exactly 4, so -c 4 lists it and -c 5 does not.
	elc -c 5 "$TREE/pair.c"
	assert_success

	local listed
	listed="$(awk '/^At or over/ { f = 1; next } f && /^  \// { print $2 }' \
		<<<"$output")"
	assert_equal "$listed" ""
}

@test "HLR-022: a lower threshold lists more" {
	elc -c 1 "$TREE/pair.c"
	assert_success

	local count
	count="$(awk '/^At or over/ { f = 1; next } f && /^  \// { n++ }
	              END { print n + 0 }' <<<"$output")"
	assert_equal "$count" "2"
}

@test "HLR-063: a malformed threshold is a usage error" {
	elc -c wat "$TREE/pair.c"
	assert_equal "$status" 2
}

# --- reporting only (HLR-023) ----------------------------------------------

@test "HLR-023: the threshold does not affect the exit status" {
	# Every function breaches at 1 and none at 100; both must exit 0.
	elc -c 1 "$TREE/pair.c"
	assert_equal "$status" 0
	elc -c 100 "$TREE/pair.c"
	assert_equal "$status" 0
}

@test "HLR-023: the threshold changes the listing and nothing else" {
	# Everything above the listing section must be byte-identical.
	run bash -c '"$0" -c 1 "$1" 2>/dev/null | sed "/^At or over/,\$d"' \
		"$ELC" "$TREE/pair.c"
	local low="$output"
	run bash -c '"$0" -c 100 "$1" 2>/dev/null | sed "/^At or over/,\$d"' \
		"$ELC" "$TREE/pair.c"

	assert_equal "$output" "$low"
}

@test "HLR-023: the threshold does not change the totals or the callouts" {
	elc -c 100 "$TREE/pair.c"
	assert_success
	assert_output --partial "branchy"
	assert_output --regexp "Functions +2"
}

# --- determinism (HLR-026, HLR-032) ----------------------------------------

@test "HLR-026: a tie for most complex resolves the same way every run" {
	# Two functions of equal complexity; the one sorting first under the
	# presentation order wins, and must win every time.
	printf 'int bb(int n)\n{\n\tif (n)\n\t\treturn 1;\n\treturn 0;\n}\n\nint aa(int n)\n{\n\tif (n)\n\t\treturn 1;\n\treturn 0;\n}\n' \
		> "$TREE/tie.c"

	local first=""
	for _ in 1 2 3; do
		elc "$TREE/tie.c"
		assert_success
		local got
		# "  Most complex      4  branchy in /path" — the name is $4.
		got="$(awk '/^  Most complex/ { print $4 }' <<<"$output")"
		[ -n "$first" ] || first="$got"
		assert_equal "$got" "$first"
	done

	# bb is declared first, so it sorts first by start line.
	assert_equal "$first" "bb"
}

@test "HLR-032: two runs with a threshold are byte-identical" {
	run bash -c '"$0" -c 3 "$1" 2>/dev/null' "$ELC" "$TREE/pair.c"
	local first="$output"
	run bash -c '"$0" -c 3 "$1" 2>/dev/null' "$ELC" "$TREE/pair.c"
	assert_equal "$output" "$first"
}

@test "HLR-066: an empty run still renders the callouts and the listing" {
	mkdir -p "$BATS_TEST_TMPDIR/empty"
	elc "$BATS_TEST_TMPDIR/empty"
	assert_success
	assert_output --partial "Callouts"
	assert_output --partial "At or over the complexity threshold"
}
