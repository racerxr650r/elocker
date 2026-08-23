#!/usr/bin/env bats
# test/fixtures/regeneration.bats — the saved record round trip (STP §5).
#
# The reasoning, and why byte-identical is achievable rather than
# aspirational, is in regeneration/README.md beside this file.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/regeneration/tree"
	RECORD="$BATS_TEST_TMPDIR/record.xml"
}

# Write the record the round-trip cases read back.
record() {
	run bash -c '"$0" -f xml "$1" > "$2" 2>/dev/null' "$ELC" "$TREE" "$RECORD"
	[ -s "$RECORD" ] || {
		echo "the record is empty" >&2
		return 1
	}
}

@test "the hand-counted subject totals match" {
	elc "$TREE"
	assert_success
	assert_output --regexp "Physical lines +20"
	assert_output --regexp "ELOC +8"
	assert_output --regexp "Functions +3"
	assert_output --regexp "Skipped +1"
}

# --- the round trip (HLR-055, HLR-056) -------------------------------------

@test "HLR-056: regenerated Markdown is byte-identical to a direct run" {
	record
	run bash -c '"$0" -f md "$1" 2>/dev/null' "$ELC" "$TREE"
	local direct="$output"
	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" "$RECORD"
	assert_equal "$output" "$direct"
}

@test "HLR-057: the threshold supplied now is the one applied" {
	record
	run bash -c '"$0" -f md -c 2 "$1" 2>/dev/null' "$ELC" "$TREE"
	local direct="$output"
	run bash -c '"$0" --from-xml "$1" -c 2 2>/dev/null' "$ELC" "$RECORD"
	assert_equal "$output" "$direct"
}

@test "HLR-057: the same record at two thresholds gives two listings" {
	# If it did not, the test above would pass for a build that ignored
	# the threshold entirely.
	record
	run bash -c '"$0" --from-xml "$1" -c 2 2>/dev/null' "$ELC" "$RECORD"
	local low="$output"
	run bash -c '"$0" --from-xml "$1" -c 99 2>/dev/null' "$ELC" "$RECORD"
	refute_output "$low"
}

@test "HLR-055: regeneration reads no source file" {
	# The record alone, with the tree moved out of reach.
	record
	local hidden="$BATS_TEST_TMPDIR/moved"
	cp -r "$TREE" "$hidden"

	run bash -c 'cd / && "$0" --from-xml "$1" 2>/dev/null' "$ELC" "$RECORD"
	assert_success
	assert_output --partial "branchy"
}

@test "HLR-054: the record carries every tier the report presents" {
	record
	run cat "$RECORD"
	assert_output --partial "<summary"
	assert_output --partial "<languages>"
	assert_output --partial "<files>"
	assert_output --partial "<function "
	assert_output --partial "<skipped>"
	assert_output --partial "notes.md"
}

@test "HLR-061: the record carries a format-version identifier" {
	record
	run grep -c 'format-version="1"' "$RECORD"
	assert_output "1"
}

# --- rejection (HLR-058) ---------------------------------------------------

@test "HLR-058: input that is not XML is rejected with no output" {
	printf 'this is not xml <<<\n' > "$BATS_TEST_TMPDIR/bad.xml"
	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/bad.xml"
	assert_equal "$status" 2
	assert_output ""
}

@test "HLR-058: a well-formed document of another shape is rejected" {
	printf '<?xml version="1.0"?>\n<other><file path="a"/></other>\n' \
		> "$BATS_TEST_TMPDIR/foreign.xml"
	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/foreign.xml"
	assert_equal "$status" 2
	assert_output "" "no best-effort partial conversion"
}

@test "HLR-058: an unsupported format version is rejected, naming it" {
	record
	sed 's/format-version="1"/format-version="99"/' "$RECORD" \
		> "$BATS_TEST_TMPDIR/v99.xml"

	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/v99.xml"
	assert_equal "$status" 2
	assert_output ""

	run bash -c '"$0" --from-xml "$1" 2>&1 >/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/v99.xml"
	assert_output --partial "99"
	assert_output --partial "1"
}

@test "LLR-XRD-10: a numeric attribute that is not a number is a malformed record" {
	# Not a zero. A record accepted on those terms renders perfectly
	# cleanly and reports the wrong figure, which is the one failure mode
	# a regenerated report has no way of showing its reader — every other
	# rejection at least tells them something is wrong (HLR-058).
	record
	sed 's/eloc="[0-9]*"/eloc="lots"/' "$RECORD" \
		> "$BATS_TEST_TMPDIR/nan.xml"

	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/nan.xml"
	assert_equal "$status" 2
	assert_output "" "rejected outright, not read as zero"
}

@test "HLR-058: a truncated record is rejected" {
	record
	head -4 "$RECORD" > "$BATS_TEST_TMPDIR/cut.xml"
	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" \
		"$BATS_TEST_TMPDIR/cut.xml"
	assert_equal "$status" 2
	assert_output ""
}

@test "HLR-058: an absent record is rejected" {
	elc --from-xml "$BATS_TEST_TMPDIR/nowhere.xml"
	assert_equal "$status" 2
}

# --- the mode's command line (HLR-055, HLR-063) ----------------------------

@test "HLR-055: regeneration defaults to Markdown without being asked" {
	record
	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" "$RECORD"
	assert_success
	assert_output --partial "## Project summary"
}

@test "HLR-063: a format other than Markdown is rejected in regeneration mode" {
	record
	elc --from-xml "$RECORD" -f csv
	assert_equal "$status" 2
}

@test "HLR-063: an explicit Markdown selection is accepted" {
	record
	elc --from-xml "$RECORD" -f md
	assert_success
}

@test "HLR-063: a target alongside --from-xml is a usage error" {
	record
	elc --from-xml "$RECORD" "$TREE"
	assert_equal "$status" 2
}

# --- no companion from a record (HLR-122) ----------------------------------

@test "HLR-122: regeneration writes no .dot, default-on though it is" {
	# The record carries the findings of a run rather than the topology they
	# were drawn from, so there is no graph to draw. Declining silently is
	# what the requirement asks for here, because nothing was requested.
	record
	run bash -c '"$0" --from-xml "$1" -o "$2/again.md" 2>/dev/null' \
		"$ELC" "$RECORD" "$BATS_TEST_TMPDIR"
	assert_success
	[ -f "$BATS_TEST_TMPDIR/again.md" ]
	[ ! -e "$BATS_TEST_TMPDIR/again.dot" ] || {
		echo "a .dot was reconstructed from a record that has no graph" >&2
		false
	}
}

@test "LLR-CLI-15: an explicit companion request with --from-xml is rejected" {
	# Silently ignoring it would leave a user who asked for a file and got
	# none to discover the absence rather than be told (HLR-063, HLR-122).
	record
	elc --from-xml "$RECORD" --graphml
	assert_equal "$status" 2
}

@test "LLR-CLI-15: declining a companion alongside --from-xml is not an error" {
	# --no-dot is a refusal, not a request, and refusing something that was
	# never going to be produced conflicts with nothing.
	record
	elc --from-xml "$RECORD" --no-dot
	assert_success
}
