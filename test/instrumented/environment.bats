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
	# libtree-sitter and libexpat are parsing libraries, not language
	# runtimes: neither executes user code or starts an interpreter. Both
	# are on the list because both are deliberate, documented dependencies
	# (SDD §18), and the list exists to catch the ones that are neither.
	#
	# libexpat is linked for the XML *read* path alone. The write path is
	# hand-rolled emission, which is why there is no second XML library
	# here — and why adding one would be a change worth arguing about
	# rather than a detail.
	#
	# The grammars elc loads are dlopen'd at run time and so never appear
	# in ldd output; HLR-009 is what makes that the right place for them.
	#
	# libgit2 is the one entry on this list that can open a socket: it
	# speaks the smart-HTTP and SSH transports. elc uses only its local
	# object-database side — open a repository, resolve HEAD, walk a tree,
	# read a blob — and never calls a remote-bearing entry point. That is a
	# claim about our code, not about the library, so it is not this test
	# that holds it: the no-network-syscall test below does, by observing
	# that a real run makes no connect(2) at all. Linking a library that
	# *could* reach the network is precisely why that test earns its keep.
	#
	# libz arrives with libgit2 rather than by our choice; git object
	# storage is deflate-compressed, so reading a blob at all requires it.
	#
	# igraph holds the dependence graph's topology so that the traversals
	# of Phases 9 to 11 are a mature library's algorithms rather than ours
	# (HLR-113). It brings libstdc++ and libgcc_s with it — parts of it are
	# C++ internally — which is why those are on the list without elc
	# containing a line of C++.
	#
	# libelf parses the linked image --elf names, and is a container reader
	# rather than a runtime: it reads a symbol table and executes nothing.
	# elc invokes no nm, objdump, readelf, compiler, or linker, which is a
	# claim about our code rather than about the library — held by the
	# execve test below, which observes that a filtered run starts no
	# process at all (HLR-141).
	#
	# libzstd arrives with libelf rather than by our choice, as libz
	# arrives with libgit2: elfutils supports compressed sections, so
	# opening any ELF at all links its decompressor.
	#
	# libdw reads the debug line information an image carries, so that
	# pruning reaches line granularity (HLR-153). Same elfutils tree as
	# libelf and taken on the same terms; it brings libbz2 and liblzma
	# with it, elfutils having a decompressor per compression scheme.
	#
	# What this list cannot see is the distinction that matters most about
	# it: elc uses libdw's low-level dwarf_* interface and never the Dwfl
	# layer, which would resolve separate debug information by
	# .gnu_debuglink and build-id and so open a file under /usr/lib/debug
	# the user never named (HLR-141). Both live in the same library, so
	# ldd is identical either way. The open-counting test below is what
	# holds it.
	#
	# libstdc++ was on this list from Phase 8 as something igraph brought
	# in. From Phase 16 elc references a symbol in it deliberately —
	# __cxa_demangle, which decodes the Itanium ABI and with it C++ and
	# Rust's legacy mangling (HLR-142). The entry did not change; what it
	# means did, which is why it is said here rather than left to a reader
	# to infer from an unchanged line.
	#
	# Two libraries are deliberately *absent*, and this list is what
	# noticed both.
	#
	# libgomp — igraph's default build links OpenMP, whose runtime
	# allocates a thread pool during the dynamic linker's init, before
	# main is entered. elc is single-threaded by requirement (HLR-041),
	# so igraph is built with -DIGRAPH_OPENMP_SUPPORT=OFF. If libgomp
	# appears here again, that flag was lost.
	#
	# libgmp — igraph's IGRAPH_USE_INTERNAL_GMP defaults to AUTO, which
	# takes system GMP when its headers are present and a bundled Mini-GMP
	# otherwise. That made the link line depend on what was installed on
	# the build machine: a developer box without gmp.h produced one binary
	# and CI produced another, and this test failed only in CI. A fixed
	# allowlist cannot accept "it depends", so the choice is pinned to the
	# bundled copy. igraph uses GMP only for graph-isomorphism automorphism
	# counts, which no elc analysis performs.
	#
	# The sanitizer runtimes are allowed because `make asan` re-runs this
	# very suite against an instrumented build, and libasan is test
	# instrumentation rather than a product dependency: it is absent from
	# the binary `make all` produces and `make install` ships. Excluding
	# them here would make the sanitized pass fail on its own scaffolding.
	local allowed='^(linux-vdso|libc|libm|libdl|libgcc_s|libstdc\+\+|libtree-sitter|libexpat|libgit2|libz|libzstd|libbz2|liblzma|libelf|libdw|libigraph|libasan|libubsan|ld-linux|/lib64/ld-linux)'
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

	# Named `.txt` rather than `.fifo`: the extension of an output path
	# names the report format, and `.fifo` names none (HLR-148). A FIFO is
	# what the file *is*; the extension states what elc writes into it.
	local fifo="$BATS_TEST_TMPDIR/report.txt"
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

@test "LLR-DOT-02: elc links no Graphviz library" {
	# Graphviz renders the output; elc writes it. The `.dot` file is plain
	# text emission for exactly this reason — it keeps Graphviz a tool the
	# user may run on the result rather than a dependency of producing it
	# (HLR-102).
	require_tool ldd "LLR-DOT-02 no Graphviz dependency"
	run ldd "$ELC"
	assert_success
	refute_output --partial "libgvc"
	refute_output --partial "libcgraph"
	refute_output --partial "libcdt"
}

@test "LLR-DOT-02: writing the call tree spawns no process" {
	# The other half of the same claim, and the half a link check cannot
	# make: elc does not shell out to `dot` either.
	require_tool strace "LLR-DOT-02 no Graphviz invocation"
	local log="$BATS_TEST_TMPDIR/exec.log"

	# execve alone: a fork that never execs cannot have run Graphviz, and
	# clone is already asserted at zero by the single-thread test above.
	strace_elc "$log" "execve,execveat" \
		-o "$BATS_TEST_TMPDIR/report.md" "$REPO_ROOT/src"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"
	[ -f "$BATS_TEST_TMPDIR/report.dot" ] || {
		echo "no call tree was written, so nothing was observed" >&2
		false
	}

	# strace -f prefixes each line with a pid, so the match is unanchored.
	# The one execve is the kernel starting elc itself.
	run bash -c 'grep -cE "execve(at)?\(" "$0" || true' "$log"
	assert_output "1"
}

@test "HLR-135: deciding a conditional region spawns no preprocessor" {
	# elc runs no preprocessor, and this is the half a reading of the source
	# cannot establish: a run over conditionally compiled source with
	# definitions supplied issues one execve, the kernel's own. No cpp, no
	# compiler, no build system — a result that depended on which toolchain
	# was installed would not be a property of the source.
	require_tool strace "HLR-135 no external preprocessor"
	local log="$BATS_TEST_TMPDIR/exec.log"

	strace_elc "$log" "execve,execveat" -DFEATURE -DLEAN \
		"$REPO_ROOT/test/fixtures/conditional/tree"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"

	run bash -c 'grep -cE "execve(at)?\(" "$0" || true' "$log"
	assert_output "1"
}

@test "HLR-135: deciding a conditional region reads no other file" {
	# The other half: no file the source refers to is opened. The three
	# fixture sources are opened once each and nothing else is read from
	# the tree — an implementation resolving #include to decide a condition
	# would show the header here.
	require_tool strace "HLR-135 no external preprocessor"
	local log="$BATS_TEST_TMPDIR/open.log"

	strace_elc "$log" "openat" -DFEATURE \
		"$REPO_ROOT/test/fixtures/conditional/tree"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"

	for f in config.c nested.c cfg.rs; do
		run bash -c 'grep -c "openat(.*/conditional/tree/'"$f"'\"" "$0" || true' \
			"$log"
		assert_output "1"
	done
}

@test "HLR-141: filtering by an image spawns no toolchain utility" {
	# The half a reading of the source cannot establish: a filtered run
	# issues one execve, the kernel's own. No nm, no objdump, no readelf, no
	# compiler and no linker — a result that depended on which toolchain was
	# installed would not be a property of the image.
	require_tool strace "HLR-141 no toolchain"
	require_tool cc "HLR-141 no toolchain"
	local log="$BATS_TEST_TMPDIR/exec.log"
	local tree="$REPO_ROOT/test/fixtures/elf/tree"
	local image="$BATS_TEST_TMPDIR/libkept.so"

	cc -O0 -fPIC -shared -o "$image" "$tree/kept.c" 2>/dev/null || \
		skip "cc cannot link here: HLR-141 unverified"

	strace_elc "$log" "execve,execveat" --elf "$image" "$tree"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"

	run bash -c 'grep -cE "execve(at)?\(" "$0" || true' "$log"
	assert_output "1"
}

@test "HLR-141: the image is opened once and nothing beside it" {
	# The other half. The image is read for its symbol table alone, so it is
	# opened once; no second image is searched for, and no debugging
	# information is fetched from anywhere else. An implementation shelling
	# out to a toolchain would show that tool's own reads here.
	require_tool strace "HLR-141 no toolchain"
	require_tool cc "HLR-141 no toolchain"
	local log="$BATS_TEST_TMPDIR/open.log"
	local tree="$REPO_ROOT/test/fixtures/elf/tree"
	local image="$BATS_TEST_TMPDIR/libkept.so"

	cc -O0 -fPIC -shared -o "$image" "$tree/kept.c" 2>/dev/null || \
		skip "cc cannot link here: HLR-141 unverified"

	strace_elc "$log" "openat,open" --elf "$image" "$tree"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"

	run bash -c 'grep -c "libkept.so\"" "$0" || true' "$log"
	assert_output "1"
}

@test "HLR-141: an image carrying debug information is still opened once" {
	# The same claim against the case that can break it. Reading debug line
	# information is what a DWARF library offers to do *elsewhere*: given
	# the chance it resolves a .gnu_debuglink or a build-id and opens a
	# file under /usr/lib/debug that the user never named. elc uses the
	# low-level dwarf_* interface, which reads the descriptor it is handed
	# and nothing else — a distinction one API call deep, invisible in ldd,
	# and observable only here (HLR-141, HLR-153).
	require_tool strace "HLR-141 no toolchain"
	require_tool cc "HLR-141 no toolchain"
	local log="$BATS_TEST_TMPDIR/opendbg.log"
	local tree="$REPO_ROOT/test/fixtures/elf/tree"
	local image="$BATS_TEST_TMPDIR/libkept-g.so"

	cc -g -O0 -fPIC -shared -o "$image" "$tree/kept.c" 2>/dev/null || \
		skip "cc cannot link with -g here: HLR-141 unverified"

	strace_elc "$log" "openat,open" --elf "$image" "$tree"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"

	run bash -c 'grep -c "libkept-g.so\"" "$0" || true' "$log"
	assert_output "1"

	# And nothing from a separate-debug directory, whatever it was called.
	run bash -c 'grep -cE "/usr/lib/debug|\.debug\"|debuglink" "$0" || true' \
		"$log"
	assert_output "0"
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

# Verifies LLR-BLD-19: the C++ runtime is named on the link line.
@test "the link line names the runtime the demangler lives in" {
	# libstdc++ was already loaded — igraph is partly C++ inside, and it has
	# been in ldd output since Phase 8. That is not enough to *reference* a
	# symbol in it: a current ld will not resolve an undefined symbol from
	# an indirect DT_NEEDED, so __cxa_demangle fails to link unless the
	# library is named. Nothing in the suite would catch its removal, since
	# ldd would still show the library.
	run make -C "$REPO_ROOT" -Bn all
	assert_success
	assert_output --partial "-lstdc++"

	require_tool nm "LLR-BLD-19 the demangler is linked"
	run bash -c 'nm -D --undefined-only "$0" 2>/dev/null || true' "$ELC"
	assert_output --partial "__cxa_demangle"
}

@test "HLR-041: the build passes no threading flag" {
	# -pthread must never appear: elc is single-threaded by decision, and
	# the flag would silently license a future thread.
	run grep -c -- "-pthread" "$REPO_ROOT/Makefile"
	assert_output "0"
}

# Verifies LLR-BLD-13: `make help` and the file's own header are one text.
#
# The help block is hand-maintained, which the mechanism it replaced was not:
# the old one reconstructed the list by pattern-matching the target
# declarations, so it could not drift. This buys that guarantee back — the
# text is written once, for both readers, and cannot silently disagree with
# the targets that exist.
@test "the help block and the real target set agree" {
	local listed declared

	# Names in the help block, which `make help` prints verbatim.
	listed="$(sed -n 's/^#>  \([a-z][a-z0-9-]*\)  *[A-Z].*/\1/p' \
		"$REPO_ROOT/Makefile" | sort -u)"

	# Every .PHONY target, less the internal helpers, which are named with
	# a leading underscore precisely so they can be excluded here.
	declared="$(grep -oE '^\.PHONY: .*' "$REPO_ROOT/Makefile" |
		sed 's/^\.PHONY: //' | tr ' ' '\n' |
		grep -vE '^(_|$)' | sort -u)"

	assert_equal "$listed" "$declared"
}

@test "make help prints the block from the file's header" {
	run make -C "$REPO_ROOT" help
	assert_success
	assert_output --partial "Usage:"
	assert_output --partial "make <target>"

	# Every listed target must appear in what is printed, and the marker
	# must not.
	while read -r target; do
		[ -n "$target" ] || continue
		assert_output --partial "$target"
	done < <(sed -n 's/^#>  \([a-z][a-z0-9-]*\)  *[A-Z].*/\1/p' "$REPO_ROOT/Makefile")

	refute_output --partial "#>"
}

# --- the grammar build (HLR-009, HLR-011) ----------------------------------

# Verifies LLR-BLD-16: a grammar is linked with the external scanner it needs.
#
# This exists because it did not. The rule found an optional `scanner.c` with
# `$(wildcard)`, which make expands *before* running the recipe — so it looked
# for a file the fetch on the line above had not yet unpacked, and silently
# found nothing. C has no external scanner, so nothing showed until three
# grammars that have one arrived; and a grammar linked without its scanner
# fails at load, not at build.
@test "every grammar is linked with the scanner it requires" {
	require_tool nm "HLR-009 runtime-loaded language support"

	local broken=()
	for parser in "$REPO_ROOT"/runtime/parsers/*.so; do
		[ -e "$parser" ] || continue
		nm -D --undefined-only "$parser" 2>/dev/null |
			grep -q external_scanner && broken+=("$parser")
	done

	[ "${#broken[@]}" -eq 0 ] || {
		echo "linked without an external scanner: ${broken[*]}" >&2
		false
	}
}

# Verifies LLR-BLD-15: the upstream owner and the archive reference are
# parameters, so a grammar hosted elsewhere, or one pinned by commit because
# its upstream cuts no releases, needs no change to any source module.
#
# Every grammar shipped today comes from the parsing library's own
# organisation and is fetched by tag, so no single rule demonstrates the
# parameters by being different. The claim is checked at its source instead:
# the reference is shown to be a parameter by overriding it and seeing the
# override in the command, and the owner is shown to be one by reading the
# macro that builds every grammar. The project has shipped a grammar from
# another owner, pinned by commit, and this is what keeps that possible.
@test "the grammar build takes its owner and reference as parameters" {
	run make -C "$REPO_ROOT" -Bn runtime/parsers/c.so GRAMMAR_C_VER=9.9.9
	assert_success
	assert_output --partial "v9.9.9"

	# The owner reaches the fetch from the call site rather than from a
	# constant inside it. A macro that hardcoded the organisation would
	# read as though a grammar from elsewhere could not be added as data.
	run grep -A20 "^define build_grammar" "$REPO_ROOT/Makefile"
	assert_success
	assert_output --partial '$(2)'
	refute_output --partial "github.com/tree-sitter/tree-sitter-$"
}

@test "every grammar the build declares is one the build can produce" {
	# A name in GRAMMARS with no rule behind it fails only on a clean tree,
	# which is the tree nobody builds on.
	run make -C "$REPO_ROOT" -n grammars
	assert_success
}

# Verifies LLR-BLD-17: an immutable pin is not thereby an invisible one.
@test "check-prereqs reports every grammar against upstream" {
	run make -C "$REPO_ROOT" check-prereqs
	assert_success
	assert_output --partial "== grammars =="

	# One line per delivered language, whatever the network did.
	# Extracted with awk rather than matched with --regexp: bats-assert
	# applies a regexp to the whole output as one string, so `^` anchors
	# the output and not a line.
	local listed
	listed="$(awk '/^== grammars ==/ { g = 1; next } g && /^== / { g = 0 }
	               g && /^  [a-z]/ { print $1 }' <<<"$output" | sort | tr '\n' ' ')"
	assert_equal "$listed" "c cpp python rust "
}

@test "check-prereqs survives an unreachable upstream" {
	# It is a diagnostic a person runs, not a gate: no network must mean
	# "unknown" rather than a failure or a hang.
	run timeout 30 make -C "$REPO_ROOT" _check-grammar LANG=probe \
		REPO=nonexistent-org-xyzzy/nope KIND=tag PIN=1.0
	assert_success
	assert_output --partial "unknown"
}

# --- HLR-060, HLR-121: the runtime data actually ships ---------------------

# This exists because it did not. `.gitignore` carries `*.map` for linker map
# files, which also matches runtime/extensions.map — so the extension table
# worked locally, was absent from the clone CI made, and every parsing test
# failed naming the missing file rather than the rule that hid it.
#
# An ignore rule that swallows a data file is silent by construction. The
# only reliable check is to ask git what it is tracking.
@test "every runtime data file the build does not produce is tracked" {
	require_tool git "HLR-060 runtime data ships with the product"
	[ -d "$REPO_ROOT/.git" ] || skip "not a git checkout; nothing to ask"

	local untracked=()
	while read -r file; do
		[ -n "$file" ] || continue
		# Grammars are build products, deliberately ignored: the build
		# fetches and compiles them from pinned upstream releases.
		case "$file" in *.so) continue ;; esac
		git -C "$REPO_ROOT" ls-files --error-unmatch "$file" \
			>/dev/null 2>&1 || untracked+=("$file")
	done < <(cd "$REPO_ROOT" && find runtime -type f)

	[ "${#untracked[@]}" -eq 0 ] || {
		echo "runtime data missing from the repository: ${untracked[*]}" >&2
		echo "run: git check-ignore -v <file>" >&2
		false
	}
}

@test "HLR-125: the local sanitizer gate is as strong as the pipeline's" {
	# `abort_on_error=1` is what makes a leak reported inside a forked
	# Criterion child reach the parent's exit status. Without it a leaking
	# unit test is reported and `make asan` still succeeds — which is worse
	# than no local gate, because the local gate is trusted. Phase 11
	# shipped two leaking tests past a green local run and was caught only
	# by CI; this keeps the two definitions from drifting apart again.
	local makefile="$REPO_ROOT/Makefile"
	local workflow="$REPO_ROOT/.github/workflows/ci.yml"

	require_path "$workflow" "LLR-BLD-18 sanitizer option parity"

	local from_make from_ci
	from_make="$(awk -F'?= ' '/^ASAN_OPTIONS/ { print $2; exit }' "$makefile")"
	from_ci="$(awk -F': ' '/ASAN_OPTIONS:/ { print $2; exit }' "$workflow")"

	assert_equal "$from_make" "$from_ci"
	[[ "$from_make" == *abort_on_error=1* ]] || {
		echo "the sanitizer gate must abort on error: $from_make" >&2
		false
	}
}

@test "every linked library the Makefile takes from the distribution is installed by CI" {
	# The same drift the sanitizer-option test above guards, in the other
	# place the two definitions meet. `PKGS_BUILD` says which libraries elc
	# links and takes from the distribution rather than building from
	# source; the workflow installs them on the runner. Nothing connected
	# the two, so Phase 20 added libdw to one and not the other and every
	# compiling job failed at once — with `make test` green locally,
	# because the developer machine had the package.
	#
	# Scoped to `lib*-dev`, which is exactly the set of linked libraries.
	# The rest of PKGS_BUILD is the toolchain, and the runner image
	# supplies that already.
	local makefile="$REPO_ROOT/Makefile"
	local workflow="$REPO_ROOT/.github/workflows/ci.yml"

	require_path "$workflow" "LLR-BLD-20 dependency parity"

	# PKGS_BUILD continues across lines with a backslash, so the value is
	# joined before it is split into packages.
	local declared
	declared="$(awk '/^PKGS_BUILD/ { v = $0; sub(/^[^=]*= */, "", v);
	                                 while (v ~ /\\$/) {
	                                         sub(/\\$/, "", v);
	                                         getline nx; sub(/^ */, "", nx);
	                                         v = v nx;
	                                 }
	                                 print v; exit }' "$makefile")"
	[ -n "$declared" ] || {
		echo "PKGS_BUILD not found in the Makefile" >&2
		false
	}

	local missing=()
	local pkg
	for pkg in $declared; do
		case "$pkg" in
		lib*-dev) ;;
		*) continue ;;
		esac
		grep -q "apt-get install .*[ =]*$pkg\([ ]\|$\)" "$workflow" || \
			missing+=("$pkg")
	done

	[ "${#missing[@]}" -eq 0 ] || {
		echo "declared in PKGS_BUILD but never installed by CI: ${missing[*]}" >&2
		echo "every compiling job will fail on a runner without them" >&2
		false
	}
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

# --- HLR-076: one parse per file -------------------------------------------

@test "HLR-076: each source file is opened exactly once" {
	require_tool strace "HLR-076 the single parse"
	local tree="$BATS_TEST_TMPDIR/single" log="$BATS_TEST_TMPDIR/open-once.log"
	mkdir -p "$tree"
	printf 'int a(void) { return 0; }\n' > "$tree/one.c"
	printf 'int b(void) { return 0; }\n' > "$tree/two.c"

	strace_elc "$log" "openat,open" "$tree"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"

	# The single-parse rule is what makes the graph and the metrics come
	# from the same tree (HLR-076). A second open of a source file would
	# mean some stage re-read it, which is the thing the rule forbids —
	# and counting opens is the only way to see that from outside.
	run grep -c '/one\.c' "$log"
	assert_output "1"
	run grep -c '/two\.c' "$log"
	assert_output "1"
}

# --- HLR-009: language support is loaded at run time ------------------------

@test "HLR-009: the grammar is loaded from the runtime location, not linked" {
	require_tool ldd "HLR-009 runtime-loaded language support"
	local tree="$BATS_TEST_TMPDIR/grammar" log="$BATS_TEST_TMPDIR/grammar.log"
	mkdir -p "$tree"
	printf 'int a(void) { return 0; }\n' > "$tree/one.c"

	# Not linked: no grammar appears among the binary's dependencies.
	run ldd "$ELC"
	refute_output --partial "parsers/"

	# Loaded: the grammar file is opened during the run.
	require_tool strace "HLR-009 runtime-loaded language support"
	strace_elc "$log" "openat,open" "$tree"
	[ -f "$log" ] || skip "strace produced no log; cannot observe syscalls"
	run grep -c "parsers/c.so" "$log"
	refute_output "0"
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
