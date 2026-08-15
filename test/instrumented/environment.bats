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
	# appearing here — libpython, libperl, libmono, libjvm — is exactly
	# what HLR-040 forbids.
	#
	# The sanitizer runtimes are allowed because `make asan` re-runs this
	# very suite against an instrumented build, and libasan is test
	# instrumentation rather than a product dependency: it is absent from
	# the binary `make all` produces and `make install` ships. Excluding
	# them here would make the sanitized pass fail on its own scaffolding.
	local allowed='^(linux-vdso|libc|libm|libdl|libgcc_s|libstdc\+\+|libasan|libubsan|ld-linux|/lib64/ld-linux)'
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

# Observing the syscalls directly is both stronger and more portable than
# running under an isolated network namespace: it proves elc never *attempts*
# network access, rather than proving it survives without it, and it works in
# containers where unprivileged user namespaces are unavailable — which is
# most CI runners, GitHub's included.
@test "HLR-040: elc makes no network syscall" {
	require_tool strace "HLR-040 no network access"
	local log="$BATS_TEST_TMPDIR/net.log"

	strace_elc "$log" "%network" "$REPO_ROOT/src"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"

	# Everything but process bookkeeping lines would be a network call.
	run strace_syscall_count "$log"
	assert_output "0"
}

# --- HLR-041: single-threaded execution ------------------------------------

# Three angles, none of which can be satisfied by a binary that spawns a
# thread: the link, the symbols it references, and the process as it runs.
#
# The runtime sample used to be impossible — elc exits faster than /proc can
# be read, and a test that races is worse than no test. Redirecting the report
# into a FIFO removes the race outright: opening a FIFO for writing blocks
# until a reader arrives, so elc is guaranteed to still be alive when /proc is
# read, and the sample is deterministic rather than lucky.

@test "HLR-041: /proc reports a single thread while elc runs" {
	require_path /proc/self/status "HLR-041 single-threaded execution"
	require_tool timeout "HLR-041 single-threaded execution"

	local fifo="$BATS_TEST_TMPDIR/report.fifo"
	mkfifo "$fifo"

	"$ELC" -o "$fifo" "$REPO_ROOT/src" &
	local pid=$!

	local threads=""
	for _ in $(seq 1 1000); do
		if [ -r "/proc/$pid/status" ]; then
			threads="$(awk '/^Threads:/ { print $2 }' \
				"/proc/$pid/status" 2>/dev/null || true)"
			[ -n "$threads" ] && break
		fi
	done

	timeout 30 cat "$fifo" >/dev/null
	wait "$pid"
	local rc=$?

	assert_equal "$rc" "0"
	assert_equal "$threads" "1"
}

@test "HLR-041: elc issues no thread-creating syscall" {
	require_tool strace "HLR-041 single-threaded execution"
	local log="$BATS_TEST_TMPDIR/clone.log"

	strace_elc "$log" "clone,clone3" "$REPO_ROOT/src"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"

	# Everything but process bookkeeping lines would be a clone.
	run strace_syscall_count "$log"
	assert_output "0"
}

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

# Verifies LLR-BLD-11: the required compiler flags are applied where a
# caller-supplied CFLAGS cannot displace them.
@test "the build's required flags survive an overridden CFLAGS" {
	# A variable set on the command line overrides every assignment in the
	# makefile, `+=` included. The CI build job runs
	# `make all CFLAGS="-O2 -g -Werror"`, so had the standard, the warning
	# set, and the dependency generation been folded into CFLAGS, that job
	# would have compiled with -Werror and almost nothing to fail on.
	run make -C "$REPO_ROOT" -Bn all CFLAGS="-O2 -g -Werror"
	assert_success
	assert_output --partial "-std=c11"
	assert_output --partial "-Wall"
	assert_output --partial "-Wextra"
	assert_output --partial "-Wpedantic"
	assert_output --partial "-MMD"
	assert_output --partial "-Werror"
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

@test "HLR-043: elc opens nothing for writing" {
	require_tool strace "HLR-043 read-only operation"
	local tree="$BATS_TEST_TMPDIR/tree" log="$BATS_TEST_TMPDIR/open.log"
	mkdir -p "$tree"
	printf 'int main(void){return 0;}\n' > "$tree/a.c"

	strace_elc "$log" \
		"openat,open,creat,unlink,unlinkat,truncate,ftruncate,rename" \
		"$tree"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"

	# Read-only operation is a structural property here — analyze.c is the
	# only module that opens a source file and it passes O_RDONLY — so the
	# syscall trace is the direct evidence for it, stronger than comparing
	# checksums after the fact.
	run grep -nE 'O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|creat\(|unlink|truncate|rename' "$log"
	assert_output "" "elc must issue no syscall capable of modifying a file"
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
