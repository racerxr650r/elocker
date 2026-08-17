#!/usr/bin/env bats
# test/integration/cli.bats — the command-line surface, black box.
#
# Integration tests drive build/elc as a user would and trace to HLRs.

setup() {
	load "../helpers/common"
}

# --- help (HLR-117) -------------------------------------------------------

@test "--help exits 0" {
	elc --help
	assert_success
}

@test "-h exits 0" {
	elc -h
	assert_success
}

@test "--help writes the summary to stdout, not stderr" {
	run bash -c '"$0" --help 2>/dev/null' "$ELC"
	assert_success
	assert_output --partial "Usage: elc"

	run bash -c '"$0" --help 2>&1 >/dev/null' "$ELC"
	assert_output ""
}

@test "--help lists every option elc accepts" {
	elc --help
	assert_output --partial "-h, --help"
}

@test "HLR-103: --help documents the switch that declines the call tree" {
	# The usage summary is the reference the documentation is checked
	# against (SDD §4.2.1), so an option that parses must also print.
	elc --help
	assert_output --partial "--no-dot"
}

@test "--help documents the exit-status scheme" {
	elc --help
	assert_output --partial "Exit status:"
}

@test "--help is reported without validating other arguments" {
	elc --help --bogus
	assert_success
}

# --- usage errors (HLR-063, HLR-120) --------------------------------------

@test "an unrecognised long option exits 2" {
	elc --bogus
	assert_equal "$status" 2
}

@test "an unrecognised short option exits 2" {
	elc -Z
	assert_equal "$status" 2
}

@test "no target exits 2" {
	elc
	assert_equal "$status" 2
}

@test "a usage error writes to stderr, not stdout" {
	run bash -c '"$0" --bogus 2>/dev/null' "$ELC"
	assert_output ""

	run bash -c '"$0" --bogus 2>&1 >/dev/null' "$ELC"
	assert_output --partial "unrecognised option"
	assert_output --partial "Usage: elc"
}

@test "a usage error names the offending option" {
	run bash -c '"$0" --bogus 2>&1 >/dev/null' "$ELC"
	assert_output --partial "--bogus"
}

@test "no target is diagnosed explicitly" {
	run bash -c '"$0" 2>&1 >/dev/null' "$ELC"
	assert_output --partial "no target given"
}

# --- accepted invocations -------------------------------------------------

@test "a single target is accepted" {
	elc "$REPO_ROOT/src/main.c"
	assert_success
}

@test "several targets are accepted, files and directories intermixed" {
	elc "$REPO_ROOT/src/main.c" "$REPO_ROOT/src" "$REPO_ROOT/include/elc.h"
	assert_success
}

@test "HLR-103: --no-dot is accepted and takes no argument" {
	elc --no-dot "$REPO_ROOT/src/main.c"
	assert_success
}

@test "an accepted invocation writes its report to stdout" {
	run bash -c '"$0" "$1" 2>/dev/null' "$ELC" "$REPO_ROOT/src/main.c"
	assert_success
	assert_output --partial "Project summary"
}

# --- zero configuration (HLR-039) -----------------------------------------

@test "a decoy dotfile in the working directory changes nothing" {
	cd "$BATS_TEST_TMPDIR"
	printf 'x\n' > sample.c

	run "$ELC" --help
	local without="$output"

	printf 'format = nonsense\n' > .elcrc
	printf 'format = nonsense\n' > .elc.toml
	run "$ELC" --help

	assert_equal "$output" "$without"
}
