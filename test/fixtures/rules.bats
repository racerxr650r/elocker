#!/usr/bin/env bats
# test/fixtures/rules.bats — user-supplied custom rules (STP §5).
#
# Expected values are worked out by hand and justified in rules/README.md
# beside the fixture. Never regenerate them from elc's output.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/rules/tree"
	RULE="$BATS_TEST_DIRNAME/rules/house-style.scm"
	BROKEN="$BATS_TEST_DIRNAME/rules/broken.scm"
	OUT="$BATS_TEST_TMPDIR/report.txt"
}

# A runtime directory of this test's own, with a rules/ directory in it.
#
# Built rather than borrowed: planting a rule in the in-tree runtime would
# change what every other suite measures. The contents are symlinked, so this
# costs a directory and two links rather than a copy of every grammar.
runtime_with_rule() {
	local rule="$1" name="${2:-located.scm}"
	local dir="$BATS_TEST_TMPDIR/runtime"

	mkdir -p "$dir/queries/c/rules"
	ln -sf "$ELC_RUNTIME_DIR/parsers" "$dir/parsers"
	ln -sf "$ELC_RUNTIME_DIR/extensions.map" "$dir/extensions.map"
	ln -sf "$ELC_RUNTIME_DIR/binary.exts" "$dir/binary.exts" 2>/dev/null || true
	for f in "$ELC_RUNTIME_DIR"/queries/c/*.scm; do
		ln -sf "$f" "$dir/queries/c/$(basename "$f")"
	done
	for lang in "$ELC_RUNTIME_DIR"/queries/*/; do
		local base
		base="$(basename "$lang")"
		[ "$base" = "c" ] && continue
		[ -d "$lang" ] || continue
		ln -sf "$lang" "$dir/queries/$base"
	done
	cp "$rule" "$dir/queries/c/rules/$name"
	echo "$dir"
}

# The rows of the Custom rule matches section, "rule lines" per line.
#
# awk with a blank-line terminator, or the extractor runs into the section
# that follows. Compared with assert_equal rather than a line matcher, because
# bats-assert's line matchers read the last `run` and would pass vacuously.
matches() {
	awk '/^Custom rule matches/ {s=1; next}
	     s && /^$/ {exit}
	     s && $1 == "Rule" {next}
	     s && /^  [^ -]/ {print $1, $NF}' "$1"
}

# ------------------------------------------------------------- the matches --

@test "HLR-109: one rule file expresses several independently named rules" {
	# Three matches under two identities. A rule's identity is the file's
	# basename plus the capture name that matched.
	run bash -c '"$0" --verbose --rules "c:$1" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$RULE" "$OUT" "$TREE"
	assert_success

	assert_equal "$(matches "$OUT")" "house-style.allocation 12-12
house-style.jump 15-15
house-style.jump 30-30"
}

@test "HLR-109: the match count is reported in the heading" {
	run bash -c '"$0" --verbose --rules "c:$1" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$RULE" "$OUT" "$TREE"
	assert_success

	run bash -c 'grep -c "^Custom rule matches (3)$" "$0"' "$OUT"
	assert_output "1"
}

@test "HLR-107: a rule's predicate is evaluated, so free is not an allocation" {
	# tree-sitter's C library returns predicates as step data and evaluates
	# none of them. Without elc evaluating (#eq? @allocation "malloc") this
	# capture matches every call in the file and release's free appears as
	# an allocation — a rule author's filter silently discarded.
	run bash -c '"$0" --verbose --rules "c:$1" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$RULE" "$OUT" "$TREE"
	assert_success

	run bash -c 'grep -c "allocation" "$0"' "$OUT"
	assert_output "1"
}

@test "LLR-ANL-47: a directive carries information and does not filter" {
	# `#set!` attaches a property to a match. It is not a filter, and a
	# match carrying one must survive — an evaluator that treated every
	# unrecognised predicate alike would discard the match and the rule
	# would silently find nothing.
	local rule="$BATS_TEST_TMPDIR/directive.scm"

	printf '((goto_statement) @jump\n (#set! "kind" "control"))\n' > "$rule"

	run bash -c '"$0" --verbose --rules "c:$1" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$rule" "$OUT" "$TREE"
	assert_success

	# Two, by the fixture's own hand count: lines 15 and 30 (rules/README.md).
	run bash -c 'grep -c "directive\.jump" "$0"' "$OUT"
	assert_output "2"
}

@test "LLR-ANL-47: a filter this build cannot apply discards the match" {
	# The opposite direction, and the reason it is that way round. A filter
	# the query author wrote and this build cannot honour is a condition
	# nobody applied; accepting the match would apply the condition's
	# *inverse* and report a finding the author asked not to see. Under-
	# reporting is the direction every capture in the contract errs in.
	local rule="$BATS_TEST_TMPDIR/unknown.scm"

	printf '((goto_statement) @jump\n (#not-a-real-predicate? @jump "x"))\n' \
		> "$rule"

	run bash -c '"$0" --verbose --rules "c:$1" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$rule" "$OUT" "$TREE"
	assert_success

	run bash -c 'grep -c "unknown\.jump" "$0" || true' "$OUT"
	assert_output "0"
}

@test "HLR-031: the section is emitted even with no rule supplied" {
	# An absent section and an empty one are different claims.
	run bash -c '"$0" --verbose -o "$1" "$2" 2>/dev/null' "$ELC" "$OUT" "$TREE"
	assert_success

	run bash -c 'grep -c "^Custom rule matches (0)$" "$0"' "$OUT"
	assert_output "1"
}

@test "HLR-111: a rule match carries no severity" {
	# The absence is the requirement. elc reports what a rule matched and
	# forms no view about whether the rule was worth writing, so there is
	# nothing honest to put in a severity column and there is no column.
	run bash -c '"$0" --rules "c:$1" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$RULE" "$OUT" "$TREE"
	assert_success

	local heading
	heading="$(awk '/^Custom rule matches/ {getline; print; exit}' "$OUT")"
	case "$heading" in
	*Severity*|*Source*)
		echo "a rule match was given a severity or an attribution" >&2
		false
		;;
	esac
}

# ------------------------------------------------------------ the binding --

@test "HLR-107: a rule in the runtime location is used without being named" {
	local dir
	dir="$(runtime_with_rule "$RULE")"

	run bash -c 'ELC_RUNTIME_DIR="$1" "$0" --verbose -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$dir" "$OUT" "$TREE"
	assert_success

	assert_equal "$(matches "$OUT")" "located.allocation 12-12
located.jump 15-15
located.jump 30-30"
}

@test "HLR-107: what a rule does does not depend on how it arrived" {
	# The same query from the two provenances produces the same matches;
	# only the identity's first half differs, being the file's name.
	local dir
	dir="$(runtime_with_rule "$RULE" "house-style.scm")"

	run bash -c 'ELC_RUNTIME_DIR="$1" "$0" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$dir" "$OUT" "$TREE"
	assert_success
	local located
	located="$(matches "$OUT")"

	run bash -c '"$0" --rules "c:$1" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$RULE" "$OUT" "$TREE"
	assert_success
	assert_equal "$(matches "$OUT")" "$located"
}

@test "HLR-108: adding a rule requires no rebuild" {
	# The binary is not touched between the two runs above and below; the
	# only change is a file. Asserted by mtime, since "no rebuild" is a
	# claim about the executable rather than about the output.
	local before after dir
	before="$(stat -c %Y "$ELC")"

	dir="$(runtime_with_rule "$RULE")"
	run bash -c 'ELC_RUNTIME_DIR="$1" "$0" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$dir" "$OUT" "$TREE"
	assert_success

	after="$(stat -c %Y "$ELC")"
	assert_equal "$before" "$after"
}

# ---------------------------------------------------------- the provenance --

@test "HLR-116: a broken rule in the runtime location is skipped, not fatal" {
	local dir
	dir="$(runtime_with_rule "$BROKEN" "broken.scm")"

	run bash -c 'ELC_RUNTIME_DIR="$1" "$0" -o "$2" "$3" 2>&1 >/dev/null' \
		"$ELC" "$dir" "$OUT" "$TREE"
	assert_success                      # a malformed component is survived
	assert_output --partial "broken.scm"
	[ -f "$OUT" ]                       # and the report was still produced
}

@test "HLR-116: the same file named on the command line is fatal" {
	# The same bytes, the other provenance, the other outcome. Two
	# different broken files could not tell a provenance rule from a
	# file-contents one.
	run bash -c '"$0" --rules "c:$1" -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$BROKEN" "$OUT" "$TREE"
	assert_equal "$status" 2
	[ ! -e "$OUT" ] || {
		echo "a report was written despite a fatal rule error" >&2
		false
	}
}

@test "HLR-116: a named rule that does not exist is fatal and names itself" {
	run bash -c '"$0" --rules "c:$1/absent.scm" "$2" 2>&1 >/dev/null' \
		"$ELC" "$BATS_TEST_TMPDIR" "$TREE"
	assert_equal "$status" 2
	assert_output --partial "absent.scm"
}

@test "LLR-RLR-03: a rule naming an unavailable language is skipped" {
	# What is missing is the module, not the rule, so this is the absence
	# that makes a source file a skip rather than a failure.
	run bash -c '"$0" --rules "cobol:$1" -o "$2" "$3" 2>&1 >/dev/null' \
		"$ELC" "$RULE" "$OUT" "$TREE"
	assert_success
	assert_output --partial "no language module for 'cobol'"
}

@test "HLR-063: --rules without a language is a usage error" {
	run bash -c '"$0" --rules "$1" "$2" 2>&1 >/dev/null' \
		"$ELC" "$RULE" "$TREE"
	assert_equal "$status" 2
	assert_output --partial "lang:path"
}

# ------------------------------------------------------------- discovery --

@test "HLR-110: no rule is discovered from the working directory or target" {
	# Decoys in both places, and in a dotfile directory. Two users running
	# the same command on the same tree must get the same result, so a rule
	# that arrived by being *nearby* would break the guarantee.
	local work="$BATS_TEST_TMPDIR/work"

	mkdir -p "$work/.elc" "$BATS_TEST_TMPDIR/target"
	cp "$BATS_TEST_DIRNAME/rules/tree/subject.c" "$BATS_TEST_TMPDIR/target/"
	cp "$RULE" "$work/decoy.scm"
	cp "$RULE" "$work/.elc/decoy.scm"
	cp "$RULE" "$BATS_TEST_TMPDIR/target/decoy.scm"

	run bash -c 'cd "$1" && "$0" --verbose -o "$2" "$3" 2>/dev/null' \
		"$ELC" "$work" "$OUT" "$BATS_TEST_TMPDIR/target"
	assert_success

	run bash -c 'grep -c "^Custom rule matches (0)$" "$0"' "$OUT"
	assert_output "1"
}

# ------------------------------------------------------------ the record --

@test "HLR-056: rule matches survive a record round trip byte-identically" {
	# Every new report section is a new thing for regeneration to lose.
	local record="$BATS_TEST_TMPDIR/record.xml"
	local direct="$BATS_TEST_TMPDIR/direct.md"
	local again="$BATS_TEST_TMPDIR/again.md"

	run bash -c '"$0" --rules "c:$1" -f md "$2" 2>/dev/null' \
		"$ELC" "$RULE" "$TREE"
	assert_success
	printf '%s\n' "$output" > "$direct"

	run bash -c '"$0" --rules "c:$1" -f xml "$2" 2>/dev/null' \
		"$ELC" "$RULE" "$TREE"
	assert_success
	printf '%s\n' "$output" > "$record"

	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	printf '%s\n' "$output" > "$again"

	run diff -u "$direct" "$again"
	assert_success
}

@test "HLR-054: the record carries each match with its identity" {
	run bash -c '"$0" --rules "c:$1" -f xml "$2" 2>/dev/null' \
		"$ELC" "$RULE" "$TREE"
	assert_success

	local record="$BATS_TEST_TMPDIR/record.xml"
	printf '%s\n' "$output" > "$record"

	run bash -c 'grep -c "<match " "$0"' "$record"
	assert_output "3"
	run bash -c 'grep -c "rule=\"house-style.allocation\"" "$0"' "$record"
	assert_output "1"
}

@test "HLR-032: two runs with the same rule produce identical output" {
	run bash -c '"$0" --rules "c:$1" -f md "$2" 2>/dev/null' \
		"$ELC" "$RULE" "$TREE"
	assert_success
	local first="$output"

	run bash -c '"$0" --rules "c:$1" -f md "$2" 2>/dev/null' \
		"$ELC" "$RULE" "$TREE"
	assert_success
	assert_equal "$output" "$first"
}
