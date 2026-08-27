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
	elc --verbose "$TREE/pair.c"
	assert_success
	assert_output --regexp "simple +public +4 +1 +1"
	assert_output --regexp "branchy +public +8 +5 +4"
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
	assert_output --partial "At or over a threshold (complexity listed at 15"
}

@test "HLR-021: a function at or over the threshold is listed for its file" {
	elc -c 4 "$TREE/pair.c"
	assert_success

	local listed
	listed="$(awk '/^At or over/ { f = 1; next } f && /^$/ { f = 0 }
	                f && /^  \// { print $2 }' <<<"$output")"
	assert_equal "$listed" "branchy"
}

@test "HLR-021: the listing is at-or-over, not strictly over" {
	# branchy is exactly 4, so -c 4 lists it and -c 5 does not.
	elc -c 5 "$TREE/pair.c"
	assert_success

	local listed
	listed="$(awk '/^At or over/ { f = 1; next } f && /^$/ { f = 0 }
	                f && /^  \// { print $2 }' <<<"$output")"
	assert_equal "$listed" ""
}

@test "HLR-022: a lower threshold lists more" {
	elc -c 1 "$TREE/pair.c"
	assert_success

	local count
	count="$(awk '/^At or over/ { f = 1; next } f && /^$/ { f = 0 }
	              f && /^  \// { n++ } END { print n + 0 }' <<<"$output")"
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
	# Everything above the listing section must be byte-identical. The
	# closing statement below it is not: at -c 100 the listing is empty and
	# is named there, which is the listing changing and nothing else
	# (HLR-189).
	run bash -c '"$0" -c 1 "$1" 2>/dev/null | sed "/^At or over/,\$d"' \
		"$ELC" "$TREE/pair.c"
	local low="$output"
	run bash -c '"$0" -c 100 "$1" 2>/dev/null | sed "/^Nothing to report/,\$d"' \
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

@test "HLR-066: an empty run still renders a well-formed report" {
	mkdir -p "$BATS_TEST_TMPDIR/empty"
	elc "$BATS_TEST_TMPDIR/empty"
	assert_success
	assert_output --partial "Project summary"
	# Every table is empty, so every table is named rather than printed —
	# which is a report that still says what it looked for (HLR-188,
	# HLR-189).
	assert_output --partial "Nothing to report"
	assert_output --partial "- At or over a threshold"
}

# --- the bands (HLR-185, HLR-186, HLR-187) ---------------------------------

@test "HLR-185: complexity above 10 warns and above 15 is critical" {
	# warn() has ten decision points and so a complexity of 11; crit()
	# has fifteen and so 16.
	{
		printf 'int warn(int n)\n{\n'
		for _ in $(seq 1 10); do printf '\tif (n) n++;\n'; done
		printf '\treturn n;\n}\n\nint crit(int n)\n{\n'
		for _ in $(seq 1 15); do printf '\tif (n) n++;\n'; done
		printf '\treturn n;\n}\n'
	} > "$TREE/bands.c"

	elc "$TREE/bands.c"
	assert_success
	assert_output --regexp "warning +complexity +warn +cyclomatic complexity 11"
	assert_output --regexp "critical +complexity +crit +cyclomatic complexity 16"
	assert_output --partial "McCabe"
}

@test "HLR-185: a complexity of exactly 10 is inside the accepted range" {
	{
		printf 'int ten(int n)\n{\n'
		for _ in $(seq 1 9); do printf '\tif (n) n++;\n'; done
		printf '\treturn n;\n}\n'
	} > "$TREE/ten.c"

	elc "$TREE/ten.c"
	assert_success
	refute_output --partial "cyclomatic complexity"
	assert_output --partial "- At or over a threshold"
}

@test "HLR-187: a banded function is listed whatever the listing threshold" {
	{
		printf 'int warn(int n)\n{\n'
		for _ in $(seq 1 10); do printf '\tif (n) n++;\n'; done
		printf '\treturn n;\n}\n'
	} > "$TREE/bands.c"

	# The listing threshold is 100 and warn() is nowhere near it; the band
	# is what puts it in the listing, and the severity says which.
	elc -c 100 "$TREE/bands.c"
	assert_success
	assert_output --regexp "warn +11 +[0-9]+ +[0-9]+ +[0-9]+ +warning"
}

@test "HLR-191: the function table carries a maintainability score" {
	elc --verbose "$TREE/pair.c"
	assert_success
	assert_output --regexp "Function +Visibility +Lines +ELOC +Complexity +Fan-in +Fan-out +MI"
	# simple() is four lines, complexity 1, and joined to nothing: the flow
	# and length terms are small, so it sits at the top of the scale.
	assert_output --regexp "simple +public +4 +1 +1 +0 +0 +100"
}

@test "HLR-192: a low score is banded downwards and says whose line it is" {
	# A long, branchy, widely-joined function. The bands run the other way
	# from every other row in the catalogue: low is the bad one.
	{
		printf 'int leaf%d(void){return %d;}\n' 1 1
		printf 'int sink(int n)\n{\n'
		for _ in $(seq 1 40); do printf '\tif (n) n += leaf1();\n'; done
		printf '\treturn n;\n}\n'
		printf 'int a(void){return sink(1);}\nint b(void){return sink(2);}\n'
	} > "$TREE/sink.c"

	elc "$TREE/sink.c"
	assert_success
	assert_output --regexp "critical +maintainability +sink +maintainability index [0-9]+ of 100"
	assert_output --partial "elc heuristic — not a published standard"
}

@test "HLR-101: a maintainability finding recommends nothing" {
	# The metric's name suggests a verdict, which is the last place to
	# start advising. A finding states the score and stops.
	{
		printf 'int leaf1(void){return 1;}\n'
		printf 'int sink(int n)\n{\n'
		for _ in $(seq 1 40); do printf '\tif (n) n += leaf1();\n'; done
		printf '\treturn n;\n}\n'
		printf 'int a(void){return sink(1);}\nint b(void){return sink(2);}\n'
	} > "$TREE/sink.c"

	elc "$TREE/sink.c"
	assert_success
	assert_output --partial "maintainability index"
	refute_output --partial "recommend"
	refute_output --partial "refactor"
	refute_output --partial "review"
}

@test "HLR-192: a healthy function is not banded at all" {
	# Refuting the finding's own words rather than the measurement's name:
	# the threshold listing's heading names every band it unites, so a
	# report with no maintainability finding still says "maintainability".
	printf 'int tidy(int n)\n{\n\treturn n + 1;\n}\n' > "$TREE/tidy.c"
	elc "$TREE/tidy.c"
	assert_success
	refute_output --partial "maintainability index"
}

@test "HLR-023: a function listed by the configured threshold carries no severity" {
	# branchy is complexity 4 — inside every band — and -c 4 lists it.
	elc -c 4 "$TREE/pair.c"
	assert_success
	refute_output --regexp "branchy +4 +[0-9]+ +[0-9]+ +[0-9]+ +(warning|critical)"
}
