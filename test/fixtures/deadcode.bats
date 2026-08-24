#!/usr/bin/env bats
# test/fixtures/deadcode.bats — dead code within a function (STP §5).
#
# Expected values are worked out by hand and justified in deadcode/README.md
# beside this file. Never regenerate them from elc's output.
#
# Half these tests assert an *absence*. That is the shape of HLR-138: a missed
# statement costs a cleanup, a false claim invites deleting code that runs, and
# a suite checking only the findings would pass against an implementation that
# reported every `if`.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/deadcode/tree"
}

# The dead-code rows for one function, as "start-end" line ranges.
#
# Scoped to the section and terminated at its blank line: file paths and
# function names appear in half a dozen other sections, and an unterminated
# extractor reads whichever comes next.
dead_lines() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Dead code/ { f = 1; next } f && /^$/ { f = 0 }
		                  f && $2 == want { print $3 }'
}

# The cause column for one function's findings.
dead_causes() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Dead code/ { f = 1; next } f && /^$/ { f = 0 }
		                  f && $2 == want { $1 = ""; $2 = ""; $3 = "";
		                                    sub(/^ +/, ""); print }'
}

# The section heading, which states which languages were not analysed.
dead_heading() { heading_of "Dead code"; }

# --------------------------------------------------- the sibling walk --

@test "HLR-137: statements after a return are reported" {
	elc --verbose "$TREE/terminator.c"
	assert_success
	assert_equal "$(dead_lines after_return)" "9-9
10-10"
}

@test "HLR-138: a label following a return is NOT reported" {
	# The case that decides whether this analysis can be trusted. In
	# tree-sitter-c a labeled_statement is a *sibling* of the return, so a
	# naive walk reports a live goto target and invites deleting it.
	elc "$TREE/terminator.c"
	assert_success

	# Counted against the extracted rows, not against the whole output:
	# bats-assert's line matchers read the last `run`, so a here-string
	# would silently assert nothing.
	run bash -c 'grep -c -e 11-11 -e 12-12 <<<"$0" || true' \
		"$(dead_lines after_return)"
	assert_output "0"
}

@test "HLR-137: statements after a break are reported" {
	elc --verbose "$TREE/terminator.c"
	assert_success
	assert_equal "$(dead_lines after_break)" "19-19"
}

@test "HLR-137: statements after a continue are reported" {
	elc --verbose "$TREE/terminator.c"
	assert_success
	assert_equal "$(dead_lines after_continue)" "28-28"
}

@test "LLR-DED-01: the walk does not leave the terminator's own block" {
	# `return c;` after the loop is live. A walk that climbed to the parent
	# would report it, and would report the rest of every function.
	elc "$TREE/terminator.c"
	assert_success
	run bash -c 'grep -c 21-21 <<<"$0" || true' "$(dead_lines after_break)"
	assert_output "0"
}

@test "HLR-138: a switch arm does not leak into the next" {
	# Verified against this grammar rather than assumed: a case_statement
	# *contains* its statements, so the return in one arm has no sibling to
	# leak into. Reporting `case 2:` would be a false claim.
	elc "$TREE/terminator.c"
	assert_success
	assert_equal "$(dead_lines switch_arms)" ""
}

@test "LLR-DED-01: a statement following two terminators is reported once" {
	# Line 48 follows both returns, so the walk reaches it twice.
	elc --verbose "$TREE/terminator.c"
	assert_success
	assert_equal "$(dead_lines two_terminators)" "47-47
48-48
49-49"
}

# ----------------------------------------------------- literal branches --

@test "HLR-137: the consequence of if (0) and the else of if (1) are reported" {
	elc --verbose "$TREE/literal.c"
	assert_success
	assert_equal "$(dead_lines excluded)" "8-10
13-15"
}

@test "HLR-137: the body of a literally false loop is reported" {
	elc --verbose "$TREE/literal.c"
	assert_success
	assert_equal "$(dead_lines loops)" "20-22"
}

@test "HLR-138: a do-while(0) body is NOT reported" {
	# It runs exactly once, and it is one of the most common idioms in C.
	elc "$TREE/literal.c"
	assert_success
	run bash -c 'grep -c 23- <<<"$0" || true' "$(dead_lines loops)"
	assert_output "0"
}

@test "HLR-138: a branch guarded by a variable is NOT reported" {
	# `x = 0; if (x)` and `const int zero = 0; if (zero)` both need data
	# flow. elc performs none, and must not appear to.
	elc "$TREE/literal.c"
	assert_success
	assert_equal "$(dead_lines needs_data_flow)" ""
}

@test "LLR-DED-03: a zero the source did not write as one is undecided" {
	# `if (0x0)` is false to a reader and not to the query, which matches
	# only a decimal zero. The contrast is the test: the same function
	# writes both spellings, and exactly one of them is judged.
	cat > "$BATS_TEST_TMPDIR/zeroes.c" <<-'EOF'
	int f(void)
	{
		if (0) { return 1; }
		if (0x0) { return 2; }
		if (00) { return 3; }
		return 0;
	}
	EOF

	elc --verbose "$BATS_TEST_TMPDIR/zeroes.c"
	assert_success
	assert_equal "$(dead_lines f)" "3-3"
}

@test "HLR-137: the two causes are distinguished" {
	# The reader's next action differs, so merging them would lose the
	# information the row exists to carry.
	elc --verbose "$TREE/literal.c" "$TREE/terminator.c"
	assert_success
	assert_equal "$(dead_causes excluded)" "literal condition
literal condition"
	assert_equal "$(dead_causes after_return)" "after a terminator
after a terminator"
}

@test "HLR-016: a comment is not dead code" {
	# A comment is a *named* sibling, so the walk sees the trailing note on
	# the terminator's own line. Without the comment exclusion every
	# annotated return reports itself.
	elc --verbose "$TREE/terminator.c"
	assert_success

	local rows
	rows="$(printf '%s\n' "$output" |
		awk '/^Dead code/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { n++ } END { print n + 0 }')"
	assert_equal "$rows" "7"
}

# ------------------------------------------ per-language support (HLR-139) --

@test "HLR-139: a language supplying a dead-code query reports every language analysed" {
	elc --verbose "$TREE/literal.c"
	assert_success
	assert_equal "$(dead_heading)" \
		"Dead code within functions (every language analysed)"
}

@test "HLR-139: a language with no dead-code query is reported not analysed" {
	# Every shipped module supplies one, so the case is built rather than
	# borrowed: a runtime is copied and the file removed. A module omitting
	# an optional query is what the contract permits, whether or not
	# anything shipped today takes the permission up.
	local runtime="$BATS_TEST_TMPDIR/runtime"

	cp -r "$ELC_RUNTIME_DIR" "$runtime"
	rm -f "$runtime/queries/c/deadcode.scm"
	printf 'int p(void)\n{\n\treturn 0;\n}\n' > "$BATS_TEST_TMPDIR/p.c"

	ELC_RUNTIME_DIR="$runtime" elc --verbose "$BATS_TEST_TMPDIR/p.c"
	assert_success
	assert_equal "$(dead_heading)" \
		"Dead code within functions (not analysed for: c)"
}

@test "HLR-139: removing a query file makes that language unanalysed, not clean" {
	# The same distinction reached from the other side, and against a
	# language that does ship one: the file's absence is a choice the
	# report states, never a finding of none.
	local rt="$BATS_TEST_TMPDIR/runtime"

	cp -r "$ELC_RUNTIME_DIR" "$rt"
	rm "$rt/queries/c/deadcode.scm"

	ELC_RUNTIME_DIR="$rt" run "$ELC" --verbose "$TREE/literal.c"
	assert_success
	assert_equal "$(dead_heading)" \
		"Dead code within functions (not analysed for: c)"

	# And the findings are gone rather than reported as none found.
	assert_equal "$(dead_lines excluded)" ""
}

@test "HLR-070: a module missing an optional query is still usable" {
	# The whole point of the file being optional. Every other measurement
	# must survive its absence.
	local rt="$BATS_TEST_TMPDIR/runtime"

	cp -r "$ELC_RUNTIME_DIR" "$rt"
	rm "$rt/queries/c/deadcode.scm"

	ELC_RUNTIME_DIR="$rt" run "$ELC" --verbose "$TREE/literal.c"
	assert_success
	assert_output --partial "excluded"
	refute_output --partial "no usable language module"
}

@test "HLR-070: an optional query that will not compile is a defect, not a choice" {
	# Omitting a file is a decision; writing a broken one is not. The
	# language is excluded, the run survives, and the file is reported
	# skipped.
	local rt="$BATS_TEST_TMPDIR/runtime"

	cp -r "$ELC_RUNTIME_DIR" "$rt"
	printf '(no_such_node) @dead.terminator\n' > "$rt/queries/c/deadcode.scm"

	ELC_RUNTIME_DIR="$rt" run "$ELC" --verbose "$TREE/literal.c"
	assert_success
	assert_output --partial "deadcode.scm"
}

# ------------------------------------- independence of the graph analysis --

@test "HLR-137: dead statements inside an unreachable function are still reported" {
	# Neither dead-code analysis suppresses the other: they answer
	# different questions by different means, and a function may be
	# perfectly reachable and still contain code that is not.
	cat > "$BATS_TEST_TMPDIR/both.c" <<-'EOF'
	int reachable_entry(void)
	{
		return 0;
	}

	static int never_called(void)
	{
		return 1;
		return 2;
	}
	EOF

	elc --verbose --entry reachable_entry "$BATS_TEST_TMPDIR/both.c"
	assert_success

	# Unreachable by traversal...
	local unreachable
	unreachable="$(printf '%s\n' "$output" |
		awk '/^Unreachable functions/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { print $2 }')"
	assert_equal "$unreachable" "never_called"

	# ...and its dead statement reported all the same.
	assert_equal "$(dead_lines never_called)" "9-9"
}

# ------------------------------------------------------ other languages --

@test "HLR-139: dead code is found in every language supplying a query" {
	printf 'def f():\n    return 1\n    n = 2\n' > "$BATS_TEST_TMPDIR/a.py"
	printf 'fn f() -> i32 {\n    return 1;\n    let n = 2;\n}\n' \
		> "$BATS_TEST_TMPDIR/a.rs"
	printf 'int f() {\n\treturn 0;\n\tint n = 1;\n}\n' \
		> "$BATS_TEST_TMPDIR/a.cpp"

	elc --verbose "$BATS_TEST_TMPDIR/a.py"
	assert_success
	assert_equal "$(dead_lines f)" "3-3"

	elc --verbose "$BATS_TEST_TMPDIR/a.rs"
	assert_success
	assert_equal "$(dead_lines f)" "3-3"

	elc --verbose "$BATS_TEST_TMPDIR/a.cpp"
	assert_success
	assert_equal "$(dead_lines f)" "3-3"
}

@test "HLR-138: no language claims a branch guarded by a variable" {
	printf 'def f():\n    x = False\n    if x:\n        pass\n' \
		> "$BATS_TEST_TMPDIR/b.py"
	printf 'fn f() {\n    let x = false;\n    if x { g(); }\n}\n' \
		> "$BATS_TEST_TMPDIR/b.rs"

	elc "$BATS_TEST_TMPDIR/b.py"
	assert_success
	assert_equal "$(dead_lines f)" ""

	elc "$BATS_TEST_TMPDIR/b.rs"
	assert_success
	assert_equal "$(dead_lines f)" ""
}
