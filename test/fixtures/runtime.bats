#!/usr/bin/env bats
# test/fixtures/runtime.bats — what a broken language module does to a run.
#
# The cases and the reasoning behind each are in runtime/README.md beside
# this file. The line being drawn is HLR-036 (fatal) against HLR-070
# (survivable), and every survivable case asserts the diagnostic, the exit
# status, and the skipped-file entry together: any one of the three alone
# would pass for an implementation that got the other two wrong.

setup() {
	load "../helpers/common"

	SUBJECT="$BATS_TEST_TMPDIR/subject.c"
	printf 'int only(void)\n{\n\treturn 0;\n}\n' > "$SUBJECT"

	# A copy of the shipped runtime, so each case differs from a working
	# run in exactly one respect.
	RT="$BATS_TEST_TMPDIR/runtime"
	cp -r "$ELC_RUNTIME_DIR" "$RT"
}

# Run elc against the case's runtime directory rather than the in-tree one.
elc_with_runtime() {
	local dir="$1"; shift
	ELC_RUNTIME_DIR="$dir" run "$ELC" "$@"
}

# --- the control -----------------------------------------------------------

@test "an intact runtime analyses the subject" {
	elc_with_runtime "$RT" "$SUBJECT"
	assert_success
	assert_output --partial "only"
	assert_output --regexp "Functions +1"
}

# --- fatal: elc can do nothing (HLR-036) -----------------------------------

@test "HLR-036: an absent runtime directory is fatal before any file is read" {
	elc_with_runtime "$BATS_TEST_TMPDIR/not-here" "$SUBJECT"
	assert_equal "$status" 2
	refute_output --partial "Project summary"
}

@test "HLR-036: a runtime location that is a file is fatal" {
	printf 'not a directory\n' > "$BATS_TEST_TMPDIR/a-file"
	elc_with_runtime "$BATS_TEST_TMPDIR/a-file" "$SUBJECT"
	assert_equal "$status" 2
}

@test "HLR-036: an absent extension map is fatal" {
	rm "$RT/extensions.map"
	elc_with_runtime "$RT" "$SUBJECT"
	assert_equal "$status" 2
}

@test "HLR-036: an extension map naming no language is fatal" {
	printf '# nothing\n' > "$RT/extensions.map"
	elc_with_runtime "$RT" "$SUBJECT"
	assert_equal "$status" 2
}

@test "HLR-036: a fatal runtime failure is diagnosed on stderr" {
	ELC_RUNTIME_DIR="$BATS_TEST_TMPDIR/not-here" \
		run bash -c '"$0" "$1" 2>&1 >/dev/null' "$ELC" "$SUBJECT"
	assert_output --partial "not-here"
}

# --- survivable: one language is unusable (HLR-070) ------------------------

# Each of these asserts the same three observables, because the requirement
# is about all three at once: it degrades that language, says so, and does
# not by itself make the exit status non-zero.
assert_degraded_not_failed() {
	assert_success
	assert_output --partial "Skipped files"
	assert_output --partial "subject.c"
}

@test "HLR-070: an absent grammar degrades that language only" {
	rm "$RT/parsers/c.so"
	elc_with_runtime "$RT" "$SUBJECT"
	assert_degraded_not_failed
}

@test "HLR-070: a grammar exposing no entry point degrades that language only" {
	# A valid shared object that simply does not export tree_sitter_c.
	printf 'int unrelated(void) { return 0; }\n' > "$BATS_TEST_TMPDIR/stub.c"
	cc -shared -fPIC -o "$RT/parsers/c.so" "$BATS_TEST_TMPDIR/stub.c"

	elc_with_runtime "$RT" "$SUBJECT"
	assert_degraded_not_failed
}

@test "HLR-070: a grammar that is not a shared object degrades that language only" {
	printf 'this is not an ELF file\n' > "$RT/parsers/c.so"
	elc_with_runtime "$RT" "$SUBJECT"
	assert_degraded_not_failed
}

@test "HLR-121: a module missing a required query file is unusable, not undefined" {
	rm "$RT/queries/c/globals.scm"
	elc_with_runtime "$RT" "$SUBJECT"
	assert_degraded_not_failed
}

@test "HLR-070: an unparseable query degrades that language only" {
	printf '(no_such_node_type) @function.name\n' > "$RT/queries/c/functions.scm"
	elc_with_runtime "$RT" "$SUBJECT"
	assert_degraded_not_failed
}

@test "HLR-070: the diagnostic names the language and the reason" {
	printf '(no_such_node_type) @function.name\n' > "$RT/queries/c/functions.scm"
	ELC_RUNTIME_DIR="$RT" \
		run bash -c '"$0" "$1" 2>&1 >/dev/null' "$ELC" "$SUBJECT"

	assert_output --partial "c:"
	assert_output --partial "functions.scm"
	assert_output --partial "no such node type"
}

@test "HLR-070: an unusable module is reported once, not once per file" {
	rm "$RT/parsers/c.so"
	printf 'int a(void) { return 0; }\n' > "$BATS_TEST_TMPDIR/second.c"

	ELC_RUNTIME_DIR="$RT" run bash -c \
		'"$0" "$1" "$2" 2>&1 >/dev/null' "$ELC" "$SUBJECT" \
		"$BATS_TEST_TMPDIR/second.c"

	# One diagnostic for the language, plus one skip notice per file.
	assert_equal "$(grep -c 'cannot open shared object' <<<"$output")" "1"
}

# --- HLR-059: the environment variable wins --------------------------------

@test "HLR-059: the environment variable takes precedence over the adjacent runtime" {
	# build/elc has a working runtime beside it. Pointing the variable at a
	# broken one must degrade the run, which it can only do if the variable
	# was preferred.
	rm "$RT/parsers/c.so"
	elc_with_runtime "$RT" "$SUBJECT"
	assert_degraded_not_failed
}

@test "HLR-059: the runtime adjacent to the executable is used when the variable is unset" {
	run env -u ELC_RUNTIME_DIR "$ELC" "$SUBJECT"
	assert_success
	assert_output --partial "only"
}
