#!/usr/bin/env bats
# test/instrumented/environment.bats — requirements verified by observing the
# process and the binary rather than its output (doc/STP.md §2.1).
#
# These depend on Linux facilities. Where one is unavailable the test skips
# *explicitly*, naming the requirement that thereby went unverified; a silent
# non-run is a suite failure, not a pass.

setup() {
	load "../helpers/common"
}

# --- HLR-040: no interpreter, no virtual machine, no network --------------

@test "HLR-040: the binary links no interpreter or virtual machine" {
	require_tool ldd "HLR-040 dependency allowlist"
	run ldd "$ELC"
	assert_success

	# Everything elc links must be on this list. A language runtime
	# appearing here is exactly what HLR-040 forbids.
	local allowed='^(linux-vdso|libc|libm|libgcc_s|ld-linux|/lib64/ld-linux)'
	while read -r line; do
		[ -n "$line" ] || continue
		local lib
		lib="$(awk '{print $1}' <<<"$line")"
		[[ "$lib" =~ $allowed ]] || {
			echo "unexpected shared library: $lib" >&2
			false
		}
	done <<<"$output"
}

@test "HLR-040: elc runs identically with no network available" {
	require_tool unshare "HLR-040 no network access"
	run unshare -rn "$ELC" --help
	[ "$status" -eq 0 ] || skip "unprivileged namespaces unavailable here"
	local isolated="$output"

	run "$ELC" --help
	assert_equal "$output" "$isolated"
}

# --- HLR-041: single-threaded execution ------------------------------------

# elc is short-lived enough that sampling /proc/<pid>/status races its exit,
# and a test that skips every time verifies nothing. Until a phase gives elc
# enough work to observe while running, HLR-041 is checked statically: a
# binary that links no threading library and calls no thread-creation symbol
# cannot create a thread. The runtime sample is added when there is something
# long enough to sample.

@test "HLR-041: elc links no threading library" {
	require_tool ldd "HLR-041 single-threaded execution"
	run ldd "$ELC"
	assert_success
	refute_output --partial "libpthread"
}

@test "HLR-041: elc references no thread-creation symbol" {
	require_tool nm "HLR-041 single-threaded execution"
	run bash -c 'nm -D --undefined-only "$0" 2>/dev/null || true' "$ELC"
	refute_output --partial "pthread_create"
	refute_output --partial "thrd_create"
}

@test "HLR-041: the build passes no threading flag" {
	# -pthread must never appear: elc is single-threaded by decision, and
	# the flag would silently license a future thread.
	run grep -c -- "-pthread" "$REPO_ROOT/Makefile"
	assert_output "0"
}

# --- HLR-043: read-only operation ------------------------------------------

@test "HLR-043: elc does not modify the tree it analyses" {
	require_tool sha256sum "HLR-043 read-only operation"
	local tree="$BATS_TEST_TMPDIR/tree"
	mkdir -p "$tree"
	printf 'int main(void){return 0;}\n' > "$tree/a.c"
	printf 'void f(void){}\n'            > "$tree/b.c"

	local before after
	before="$(find "$tree" -type f -exec sha256sum {} + | sort)"
	run "$ELC" "$tree"
	after="$(find "$tree" -type f -exec sha256sum {} + | sort)"

	assert_equal "$after" "$before"
}

@test "HLR-043: elc runs against a read-only directory" {
	local tree="$BATS_TEST_TMPDIR/ro"
	mkdir -p "$tree"
	printf 'int main(void){return 0;}\n' > "$tree/a.c"
	chmod -R a-w "$tree"

	run "$ELC" "$tree"
	local rc=$status
	chmod -R u+w "$tree"        # so the harness can clean up
	[ "$rc" -eq 0 ]
}
