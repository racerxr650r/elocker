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
	# --verbose: every assertion in this suite reads the Files, Languages
	# or Skipped tier to say which languages a degraded runtime still
	# served, and all three are detail tiers of the aligned table since
	# HLR-218.
	ELC_RUNTIME_DIR="$dir" run "$ELC" --verbose "$@"
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

# --- HLR-059: the *installed* layout ---------------------------------------
#
# The build tree flatters this: `make all` creates a `runtime` symlink beside
# `build/elc`, so the adjacent path always resolves here and an installed copy
# — where the runtime lives in `share/elc/` and never beside the binary — was
# never exercised by anything. `elc .` after `make install` failed outright.
#
# These run against a real staging root for that reason. Producing the
# deliverable is a release criterion (SDP §5), and "the files are present" is
# not the same claim as "the installed binary runs".

# Install into a staging root under the test's own temporary directory.
staged_install() {
	run make -C "$REPO_ROOT" install DESTDIR="$BATS_TEST_TMPDIR/stage" \
		PREFIX=/usr/local
	assert_success
	STAGED="$BATS_TEST_TMPDIR/stage/usr/local"
}

@test "HLR-059: an installed elc finds the runtime under share/elc" {
	staged_install
	run env -u ELC_RUNTIME_DIR "$STAGED/bin/elc" "$SUBJECT"
	assert_success
	assert_output --partial "only"
}

@test "HLR-059: the install puts the runtime where the binary looks for it" {
	# Asserted as a location rather than only as a working run, so a
	# regression names the disagreement rather than only its symptom.
	staged_install
	assert [ -d "$STAGED/share/elc/runtime" ]
	assert [ ! -e "$STAGED/bin/runtime" ]
}

@test "LLR-DOC-03: the staging root carries the man page and the manual" {
	# The deliverable is not only the binary and the runtime. §5.5 calls the
	# install target's output "the deliverable", and HLR-128 says both
	# documents ship with it — so a release that installed a working elc
	# and neither document would satisfy every test there was and still be
	# short of what was promised.
	staged_install
	assert [ -f "$STAGED/share/man/man1/elc.1" ]
	assert [ -f "$STAGED/share/doc/elc/User_Manual.md" ]
}

@test "LLR-DOC-03: the installed man page is the one that renders" {
	# Asserted against the installed copy rather than the source tree's,
	# since installing a truncated or empty file would pass a check made
	# on the original.
	staged_install
	run man --warnings -E UTF-8 -l "$STAGED/share/man/man1/elc.1"
	assert_success
	assert_output --partial "elc"
}

@test "HLR-059: the installed binary is executable as installed" {
	# `install -m 755` is the claim; a mode the target got wrong would show
	# up only as a permission error at the moment a user first runs it.
	staged_install
	assert [ -x "$STAGED/bin/elc" ]
}

@test "HLR-059: the environment variable still wins over the installed layout" {
	staged_install
	rm "$RT/parsers/c.so"
	ELC_RUNTIME_DIR="$RT" run "$STAGED/bin/elc" --verbose "$SUBJECT"
	assert_degraded_not_failed
}

@test "HLR-036: a binary with no runtime anywhere names every path it tried" {
	# The diagnostic is the whole of the user's next action, and the one
	# this replaced quoted a single path that no layout ever uses — sending
	# the reader to look in a directory of executables.
	mkdir -p "$BATS_TEST_TMPDIR/lonely/bin"
	cp "$ELC" "$BATS_TEST_TMPDIR/lonely/bin/elc"

	run env -u ELC_RUNTIME_DIR "$BATS_TEST_TMPDIR/lonely/bin/elc" "$SUBJECT"
	assert_equal "$status" 2
	assert_output --partial "lonely/bin/runtime"
	assert_output --partial "share/elc/runtime"
	assert_output --partial "ELC_RUNTIME_DIR"
}

# --- an optional query a module does not supply (HLR-209) -------------------

@test "LLR-VIS-03: a module with no visibility query reports neither" {
	# Every module shipped today supplies one, so the state is reached by
	# taking one away. What must not happen is the column reading `public`:
	# a reader scanning for a module's interface would take every function
	# of an unanalysed language for part of it, which is a false claim
	# where the dash is merely an absent one.
	rm -f "$RT/queries/c/visibility.scm"
	elc_with_runtime "$RT" --verbose "$SUBJECT"
	assert_success
	refute_output --regexp "only +public"
	refute_output --regexp "only +private"
	assert_output --regexp "only +—"
}

@test "LLR-VIS-03: losing the visibility query costs nothing else" {
	# Optional means the rest of the analysis is unaffected, in the way
	# HLR-139 requires of a module with no dead-code query.
	rm -f "$RT/queries/c/visibility.scm"
	elc_with_runtime "$RT" --verbose "$SUBJECT"
	assert_success
	assert_output --partial "only"
	assert_output --partial "Functions"
}
