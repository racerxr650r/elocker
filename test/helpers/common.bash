# test/helpers/common.bash — shared setup for every Bats suite.
#
# Sourced from each suite's setup(). Loads the assertion helpers, locates the
# binary under test, and points elc at the in-tree runtime so no suite depends
# on an installed copy.

_helpers_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$_helpers_dir/../.." && pwd)"

load "$_helpers_dir/bats-support/load"
load "$_helpers_dir/bats-assert/load"

ELC="$REPO_ROOT/build/elc"
export ELC_RUNTIME_DIR="$REPO_ROOT/runtime"

# Run elc, honouring ELC_VALGRIND=1 so `make valgrind` re-runs the same suites
# under instrumentation without a second copy of every test. Valgrind and
# AddressSanitizer are never combined; they run in separate passes.
elc() {
	if [ "${ELC_VALGRIND:-0}" = "1" ]; then
		run valgrind --error-exitcode=99 --leak-check=full \
			--errors-for-leak-kinds=all --quiet "$ELC" "$@"
		[ "$status" -ne 99 ] || {
			echo "valgrind reported an error or leak" >&2
			return 1
		}
	else
		run "$ELC" "$@"
	fi
}

# strace_elc <log> <trace-expression> [elc argument...]
#
# Trace the named syscalls into <log> while elc runs over the given targets.
#
# Leak detection is switched off for the traced run, and must be: LeakSanitizer
# stops the world at exit through a clone()d tracer and ptrace, which collides
# with strace's own ptrace attachment and aborts the process. Under `make asan`
# that abort truncates the trace, so a test asserting "this syscall never
# appears" would pass because elc died before reaching the interesting part —
# passing for the wrong reason, which is worse than failing.
#
# Nothing is lost: every other run in the same sanitized pass still has leak
# detection on, so HLR-125 stays verified. This concerns the traced run only.
strace_elc() {
	local log="$1" expression="$2"
	shift 2
	ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" \
		strace -f -e "trace=$expression" -o "$log" "$ELC" "$@" \
		>/dev/null 2>&1 || true
}

# Count the lines of a strace log that are syscalls rather than the process
# bookkeeping strace interleaves with them.
strace_syscall_count() {
	grep -cvE 'exited with|\+\+\+|---' "$1" || true
}

# Skip a test that needs a Linux facility the platform does not provide, and
# say which requirement thereby went unverified. A silent non-run is a suite
# failure, not a pass (STP §2.2).
require_tool() {
	local tool="$1" requirement="$2"
	command -v "$tool" >/dev/null 2>&1 || \
		skip "$tool unavailable: $requirement unverified on this platform"
}

require_path() {
	local path="$1" requirement="$2"
	[ -e "$path" ] || \
		skip "$path unavailable: $requirement unverified on this platform"
}

# report_shape <table-format report text>
#
# The set of sections a report *reached*, one name per line and sorted.
#
# Since HLR-188 a table with no rows is not printed, so the sequence of
# printed headings varies with what a run found. What does not vary is the set
# of sections the traversal reached: a section with rows appears as a heading,
# and one without appears in the closing statement of HLR-189. The union of
# the two is the shape HLR-006 and HLR-031 are about, and comparing it is what
# those tests have to compare now.
#
# Only the leading words of a heading are kept — up to the first " (" — so a
# heading carrying a count or a threshold compares equal across runs that
# found different numbers of things.
report_shape() {
	{
		grep -E '^[A-Z]' <<<"$1"
		sed -n '/^Nothing to report$/,$p' <<<"$1" | sed -n 's/^    - //p'
	} | sed 's/ (.*$//' | grep -v '^Nothing to report$' | sort -u
}

# heading_of <heading prefix>
#
# A section's heading, whether the section was printed or — having found no
# rows — named in the closing statement instead (HLR-188, HLR-189).
#
# The heading is what carries the reason an analysis was omitted, and HLR-115
# requires that reason wherever the analysis is not. Reading it from either
# place is what lets one assertion cover both, so a test asserting "the reason
# is stated" does not become a test asserting "the table was printed".
heading_of() {
	printf '%s\n' "$output" | awk -v p="$1" '
		index($0, p) == 1          { print; exit }
		index($0, "## " p) == 1    { sub(/^## /, ""); print; exit }
		index($0, "    - " p) == 1 { sub(/^    - /, ""); print; exit }
		index($0, "- " p) == 1     { sub(/^- /, ""); print; exit }'
}
