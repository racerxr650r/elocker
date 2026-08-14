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
