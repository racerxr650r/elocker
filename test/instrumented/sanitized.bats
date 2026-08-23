#!/usr/bin/env bats
# test/instrumented/sanitized.bats — the record that the sanitized gate ran.
#
# HLR-124 and HLR-125 cannot be asserted from elc's output. Neither says
# anything about what elc *prints*; both are claims about how it behaves while
# printing it, and the only witness is an instrumented run. `make asan` and
# `make valgrind` provide that witness by re-running the suites already
# written — but a re-run produces no catalogued test, so without this file
# those two requirements and the memory-safety LLRs beneath them would sit in
# the gap list for ever while being, in fact, thoroughly exercised (STP §2.5).
#
# The file has two jobs, and they are different.
#
# The first is to reach the paths the re-run does not. `make valgrind` runs the
# integration and fixture suites, and every test in them is a run that
# *succeeds* — a target is analysed, a report is produced. The runs that end at
# exit status 2 leave through a different door: cli_options_free and the
# teardown in main, after a declaration has already allocated. HLR-125 covers
# error paths explicitly, and until this file nothing exercised one under
# instrumentation.
#
# The second is to be the catalogued record itself, so that a green sanitized
# pass is a fact the traceability matrix can carry rather than something a
# person remembers doing.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_TMPDIR/tree"
	mkdir -p "$TREE"
	printf 'int main(void){return 0;}\n' > "$TREE/a.c"
}

# Run elc under valgrind and fail on any error or leak of any kind.
#
# --errors-for-leak-kinds=all is the strict setting: a block still reachable at
# exit is a leak for HLR-125's purposes, because "released before it exits" is
# what the requirement says and a reachable block was not released. That is
# stricter than valgrind's default and deliberately so.
#
# Under `make asan` this file runs against an ASan-instrumented binary, and the
# two instrumentations must never be combined (STP §2.3). The guard below is
# what keeps that true: where the binary is already instrumented, ASan's own
# leak check is the witness and valgrind stands down.
sanitized_run() {
	local description="$1"
	shift

	if nm "$ELC" 2>/dev/null | grep -q '__asan_init\|__ubsan_handle'; then
		# Already instrumented: run it directly and let ASan judge.
		run "$ELC" "$@"
		[ "$status" -ne 1 ] || true
		return 0
	fi

	require_tool valgrind "HLR-124, HLR-125 $description"

	run valgrind --error-exitcode=99 --leak-check=full \
		--errors-for-leak-kinds=all --quiet "$ELC" "$@"

	[ "$status" -ne 99 ] || {
		echo "valgrind reported an error or leak: $description" >&2
		echo "$output" >&2
		false
	}
}

# --- the error paths the passing suites never take -------------------------

@test "HLR-125: a usage error frees the declarations parsed before it" {
	# The case LLR-MAIN-19 was written for. A --stratum is accepted and
	# allocates a layer owning its name and its patterns; the
	# --stratum-order after it names a layer that was never declared, which
	# is a usage error. Everything the accepted declaration allocated has to
	# be released on the way out, and nothing else in the suite takes this
	# path: every other test of a usage error fails before allocating.
	sanitized_run "usage error after a declaration allocated" \
		--stratum 'app:src/*' --stratum-order 'nope>app' "$TREE"
	assert_equal "$status" 2
}

@test "HLR-125: a usage error after two declarations is leak-clean" {
	# Two layers and a partial order — the order names one of the two, which
	# LLR-STR-05 rejects because a partial order determines no direction.
	# Both layers, and every pattern each copied out of argv (LLR-STR-06),
	# are outstanding when the parse fails.
	sanitized_run "usage error after two strata" \
		--stratum 'a:x/*' --stratum 'b:y/*' --stratum-order 'a' "$TREE"
	assert_equal "$status" 2
}

@test "HLR-125: a usage error after a scope declaration is leak-clean" {
	# The scope declarations copy their name and patterns as the strata do
	# and for the same reason (LLR-SCP-03), so they leak in the same way if
	# the usage-error path forgets them.
	sanitized_run "usage error after a scope" \
		--scope 'host:src/*' --bogus "$TREE"
	assert_equal "$status" 2
}

@test "HLR-125: an invalid target exits leak-clean" {
	# Past cli_parse and into discovery, which validates every target before
	# walking any (HLR-062). The options are fully populated by now and the
	# runtime registry is open, so this path releases more than the usage
	# errors above do.
	sanitized_run "invalid target" "$BATS_TEST_TMPDIR/nonexistent-xyzzy"
	assert_equal "$status" 2
}

@test "HLR-125: a target that is neither file nor directory exits leak-clean" {
	require_tool mkfifo "HLR-125 teardown on a non-regular target"
	mkfifo "$BATS_TEST_TMPDIR/pipe"
	sanitized_run "FIFO target" "$BATS_TEST_TMPDIR/pipe"
	assert_equal "$status" 2
}

@test "HLR-125: a rejected saved record exits leak-clean" {
	# xml_read_report builds part of a model and then rejects the input.
	# LLR-XRD-06 requires the partially built model be released rather than
	# rendered, and this is what shows it was.
	printf 'this is not xml at all' > "$BATS_TEST_TMPDIR/bad.xml"
	sanitized_run "rejected saved record" --from-xml "$BATS_TEST_TMPDIR/bad.xml"
	assert_equal "$status" 2
}

@test "HLR-125: a structurally foreign record exits leak-clean" {
	# Well-formed XML of the wrong shape: the parser gets further before
	# rejecting, so more of the model exists to be released.
	printf '<?xml version="1.0"?><other-tool><thing/></other-tool>\n' \
		> "$BATS_TEST_TMPDIR/foreign.xml"
	sanitized_run "foreign record" --from-xml "$BATS_TEST_TMPDIR/foreign.xml"
	assert_equal "$status" 2
}

@test "HLR-125: an unusable image exits leak-clean" {
	# elfsyms_open fails after the registry is open and before discovery
	# runs (LLR-MAIN-20), which is a teardown ordering no successful run
	# exercises.
	sanitized_run "absent image" --elf "$BATS_TEST_TMPDIR/nope.elf" "$TREE"
	assert_equal "$status" 2
}

@test "HLR-125: an image that is not an object file exits leak-clean" {
	sanitized_run "image that is not an object file" \
		--elf "$TREE/a.c" "$TREE"
	assert_equal "$status" 2
}

@test "HLR-125: a fatal runtime-location failure exits leak-clean" {
	# registry_open fails before anything else is acquired. The earliest
	# fatal exit there is, and the one most likely to release something it
	# never took.
	if nm "$ELC" 2>/dev/null | grep -q '__asan_init\|__ubsan_handle'; then
		ELC_RUNTIME_DIR="$BATS_TEST_TMPDIR/no-runtime" run "$ELC" "$TREE"
	else
		require_tool valgrind "HLR-125 fatal runtime location"
		ELC_RUNTIME_DIR="$BATS_TEST_TMPDIR/no-runtime" \
			run valgrind --error-exitcode=99 --leak-check=full \
			--errors-for-leak-kinds=all --quiet "$ELC" "$TREE"
		[ "$status" -ne 99 ] || {
			echo "valgrind reported an error or leak" >&2
			echo "$output" >&2
			false
		}
	fi
	assert_equal "$status" 2
}

# --- a successful run, as the baseline the error paths are compared against -

@test "HLR-124: a complete run over a real target is free of memory errors" {
	# The success path, instrumented. `make valgrind` covers this ground
	# across every fixture; the point of repeating one here is that this
	# file is the catalogued record, and a record of the error paths alone
	# would not say the ordinary one was ever checked.
	sanitized_run "a complete analysis run" -o "$BATS_TEST_TMPDIR/out.txt" "$TREE"
	assert_equal "$status" 0
}

@test "HLR-125: the purification pass releases every vector it allocated" {
	# Purification is the one stage that allocates inside the graph library
	# rather than around it: four vectors per run, on a path with several
	# early returns through a `goto` and one branch that skips the
	# hub-and-authority decomposition altogether. Every one of those returns
	# must free all four, and none of them is reached by a run over the
	# fixture trees this file's baseline case uses.
	#
	# The tree below takes both branches in one run: the first directory has
	# call edges and gets the decomposition, the second has none and does
	# not.
	local tree="$BATS_TEST_TMPDIR/purify"

	mkdir -p "$tree/called" "$tree/alone"
	printf 'void sink(void);\nvoid a(void){sink();}\nvoid b(void){sink();}\nvoid sink(void){}\n' \
		> "$tree/called/a.c"
	printf 'int lonely(void){return 0;}\n' > "$tree/alone/b.c"

	sanitized_run "a run reaching graph purification" --verbose \
		-o "$BATS_TEST_TMPDIR/purified.txt" "$tree"
	assert_equal "$status" 0
	grep -q "^Graph purification" "$BATS_TEST_TMPDIR/purified.txt"
}

# --- the gate itself -------------------------------------------------------

@test "LLR-BLD-09: the build provides a sanitized configuration" {
	# The configuration HLR-124 and HLR-125 are verified under has to exist
	# and has to instrument for all three of the things they name: memory
	# error, undefined behaviour, and leaks. A gate missing one of them
	# reports the other two and reads as clean.
	local makefile="$REPO_ROOT/Makefile"
	local recipe
	recipe="$(awk '/^asan:/ { f = 1 } f { print } f && /^$/ { exit }' "$makefile")"

	[ -n "$recipe" ] || {
		echo "no asan target in the Makefile" >&2
		false
	}

	# The instrumentation is in the recipe's CFLAGS and LDFLAGS; the runtime
	# behaviour is in the option variables the recipe exports. Both halves
	# are checked, because either alone leaves a gate that reports nothing.
	local options
	options="$(grep -E '^(ASAN_OPTIONS|UBSAN_OPTIONS)' "$makefile")"

	[[ "$recipe"  == *-fsanitize=address,undefined* ]] || {
		echo "the asan recipe instruments for neither address nor undefined:" >&2
		echo "$recipe" >&2
		false
	}
	[[ "$options" == *detect_leaks=1* ]] || {
		echo "leak detection is off, so HLR-125 would go unchecked" >&2
		false
	}
	[[ "$options" == *halt_on_error=1* ]] || {
		echo "UBSan must halt, or undefined behaviour is reported and passed over" >&2
		false
	}
}

@test "LLR-BLD-09: the sanitized configuration rebuilds rather than reusing objects" {
	# An instrumented run against objects compiled without instrumentation
	# reports nothing and passes, which is the failure mode a sanitizer gate
	# is least able to notice about itself. The target must clean first.
	local makefile="$REPO_ROOT/Makefile"
	local recipe
	recipe="$(awk '/^asan:/ { f = 1 } f { print } f && /^$/ { exit }' "$makefile")"

	[[ "$recipe" == *clean* ]] || {
		echo "the asan target must clean before rebuilding:" >&2
		echo "$recipe" >&2
		false
	}
}
