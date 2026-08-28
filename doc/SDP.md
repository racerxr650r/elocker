# Software Development Plan: elocker (elc)

**Version:** 1.4
**Date:** 2026-08-17
**Author(s):** John Anderson

**Status:** Phase 16 complete, and it answers the question Phase 15 answers a
different way. Conditional compilation *re-decides* the conditions a build
resolved, from definitions the user restates; `elc --elf build/app.elf src/`
*observes what the build did*, restricting every measurement to the functions
the linked image defines. Where both are available the image is the stronger
evidence, and neither replaces the other: the image says which functions
survived and nothing about which lines inside one were compiled out. `elc`
invokes no toolchain to read it — an instrumented test observes a filtered run
issuing one `execve`, the kernel's own, and opening the image exactly once.

Three decisions carried the phase. **The filter is applied once, in
`analyze.c`**, by adding an omitted function's whole extent to the excluded set
the collectors already consult — not by dropping it from the reported set
alone, which would leave its statements attributed to no function and so
counted as the file-scope figure HLR-145 keeps separate. **A symbol must be a
*defined* function**: without the `SHN_UNDEF` test every function the image
calls into a shared library counts as one it contains, and the filter then
retains source the build never compiled. And **an empty function set is fatal,
not an empty filter** — a stripped image would otherwise report a project
containing no functions at all, which is confidently wrong and
indistinguishable from a correct result.

Both directions of mismatch are reported and they are different claims: the
linkage names `elc` could not decode state how complete the filter is, and the
source functions the image lacks are dead code established by what the linker
did rather than inferred from the call graph. Implementation added four LLRs —
the ordering the three exclusions are gathered in, the separate pass that makes
it independent of query order, the split between assembling a filter's effects
and recording its provenance, and naming `libstdc++` on the link line now that
`elc` references `__cxa_demangle` in it. 783 catalogued tests verify the spec
and the coverage baseline falls from 184 to 151. Phase 17 — hardening and
release readiness — is ready to start, and is the last.

## Status

| Phase | Description | Status |
| ----- | ----------- | ------ |
| [0](#phase-0--foundation-and-continuous-integration) | Build system, CI pipeline, test harness, skeleton binary | ✅ Complete |
| [1](#phase-1--target-discovery-and-the-walking-skeleton) | Target discovery, ordering, table output — end to end | ✅ Complete |
| [2](#phase-2--language-runtime-and-function-discovery) | Runtime loading, Tree-sitter parse, function identity | ✅ Complete |
| [3](#phase-3--effective-lines-of-code) | ELOC, comment merging, file and project totals | ✅ Complete |
| [4](#phase-4--cyclomatic-complexity) | Complexity, threshold listing, most-complex callouts | ✅ Complete |
| [5](#phase-5--output-formats-and-the-saved-record) | CSV, XML, Markdown, escaping, regeneration mode | ✅ Complete |
| [6](#phase-6--language-breadth) | C++, Rust, Python — data only, no C change | ✅ Complete |
| [7](#phase-7--git-aware-discovery) | Repository detection, applicability, scoping, routes | ✅ Complete |
| [8](#phase-8--system-dependence-graph) | Cross-file resolution, the SDG, GraphML export | ✅ Complete |
| [9](#phase-9--call-tree-analyses) | Fan-out, depth, deepest stack, recursion | ✅ Complete |
| [10](#phase-10--dead-code-reachability-and-global-state) | Dead code within and between functions, global coupling, scopes | ✅ Complete |
| [11](#phase-11--coupling-layering-and-cycles) | Strata, skip-level, Ca/Ce, instability, cycles | ✅ Complete |
| [12](#phase-12--thresholds-severity-and-attribution) | The Appendix A catalogue, severity, attribution | ✅ Complete |
| [13](#phase-13--graph-visualisation) | Annotated Graphviz `.dot` companion | ✅ Complete |
| [14](#phase-14--custom-rules) | User-supplied `.scm` rules, binding, matching | ✅ Complete |
| [15](#phase-15--conditional-compilation) | `-D` definitions, inactive-region pruning | ✅ Complete |
| [16](#phase-16--elf-filtered-analysis) | `--elf` image filter, linkage-name resolution, unmatched reporting | ✅ Complete |
| [17](#phase-17--hardening-and-release-readiness) | Full sanitizer sweep, self-analysis, coverage closure | ✅ Complete |
| [18](#phase-18--output-format-selection-and-report-verbosity) | Format from filename extension, summary default, `--verbose` | ✅ Complete |
| [19](#phase-19--information-flow-complexity) | Per-function fan-in, Henry–Kafura complexity, project total | ✅ Complete |
| [20](#phase-20--debug-line-pruning) | DWARF line pruning of code the build did not compile | ✅ Complete |
| [21](#phase-21--architecture-conformance-measurement) | Conformance indices, the Dependency Structure Matrix | ✅ Complete |
| [22](#phase-22--graph-purification) | Centrality-based classification, the masked recovery view | ✅ Complete |
| [23](#phase-23--architecture-recovery-and-the-manifest) | Recovered layering, the purification manifest, visual diffing | ✅ Complete |
| [24](#phase-24--report-composition-and-the-banded-function-table) | Report order, the combined function table, the maintainability index, DWARF-placed image symbols, the debug companion | ✅ Complete |
| [25](#phase-25--repairing-what-the-grammar-could-not-follow) | In-buffer repair of unparsable macro shapes | ✅ Complete |
| [26](#phase-26--placing-templated-names-by-debug-information) | DWARF names reduced the way image symbols are; a diagnostic that states what it observed | ✅ Complete |
| [27](#phase-27--preprocessor-macro-expansion--ast-sanitization) | Macro expansion through `gcc -E`, filtered to project source and reported per file | ✅ Complete |
| [28](#phase-28--repair-where-expansion-cannot-reach) | Repair restored as the fallback beneath expansion | ✅ Complete |
| [29](#phase-29--function-visibility-and-editor-navigable-locations) | Public/private visibility, `path:line` locations, and a line count | ✅ Complete |
| [30](#phase-30--deciding-conditionals-from-the-build-and-recovering-macro-generated-functions) | Conditional regions decided from the image, functions recovered from its debug information, CSV columns matched to the table | ✅ Complete |
| [31](#phase-31--interactive-html-reporting--semantic-zooming) | The `.html` report format: layers containing files containing functions, opened collapsed | ✅ Complete |

## 0. Required Tools for Development

### Required

| Tool | Version | Purpose |
| ---- | ------- | ------- |
| GCC or Clang | C11 support | Building `elc`; `--wrap` support required for unit mocking |
| GNU make | ≥ 4.0 | The only build entry point |
| GNU ld or LLVM lld | — | `--wrap` link-time interception (STP §2.2) |
| `libtree-sitter` | ≥ 0.25 | Parsing and query execution. **Built from source** — see below |
| `libgit2` | ≥ 1.7 | Repository-aware discovery (Phase 7) |
| `igraph` | ≥ 1.0 | Graph algorithms (Phase 8). **Built from source** with `-DIGRAPH_GRAPHML_SUPPORT=OFF -DIGRAPH_OPENMP_SUPPORT=OFF -DIGRAPH_USE_INTERNAL_GMP=ON` |
| Expat | ≥ 2.6 | Streaming XML read for regeneration mode (Phase 5) |
| `libelf` | ≥ 0.18 | Reading the symbol table of the image `--elf` names (Phase 16). **From the distribution** — see below |
| `libdw` | ≥ 0.18 | Reading the debug line information that image carries, where it carries any (Phase 20). **From the distribution**, with `libelf` and for the same reason — see below |
| Jansson | ≥ 2.14 | Generating and parsing the purification manifest (Phase 23). **Taken from the distribution** (`libjansson-dev`), unlike every other linked library: GNU `ld` links libjansson itself, so a copy installed under `/usr/local` shadows the system linker's and — the version nodes being named differently upstream and downstream — stops `ld` linking anything at all. The third exception to the build-from-source rule, and the only one where building is the hazard ([SDD](SDD.md) §23) |
| Criterion | ≥ 2.4 | Unit test framework |
| Bats | ≥ 1.10 | Integration, fixture, and instrumented levels |
| `bats-support`, `bats-assert` | — | Assertion helpers; vendored under `test/helpers/` |
| `git` | ≥ 2.20 | Building the repository fixtures the discovery tests run against (Phase 7). `elc` itself needs only `libgit2` at run time |
| Python 3 | ≥ 3.9 | `tools/render_doc.py`, `tools/lint_project.py` |
| `pkg-config` | — | Library discovery in the Makefile |

### Optional

| Tool | Purpose |
| ---- | ------- |
| `valgrind` | The third pass of the test gate; Linux only |
| `strace` | HLR-076 single-parse verification |
| `graphviz` | Rendering and validating `.dot` output; never linked |
| `xmllint` | Validating emitted XML and GraphML well-formedness |
| `clang-format` | Consistent formatting; not enforced by CI |

### Installing them

`make prereqs` installs everything. It takes the toolchain, the test
framework, and the inspection tools from the distribution, then builds the
libraries elc links from pinned upstream releases.

**The libraries are built from source deliberately.** When a security
advisory lands against one of them, the fix is to bump a version in the
Makefile and rebuild — the same day, if needed. Waiting for a distribution
to rebuild and ship is not a control this project has. `make prereqs-<lib>`
rebuilds one; `make check-prereqs` reports what is installed and flags
anything below the minimums above.

Three configure-time decisions fall out of building them ourselves, and all
serve requirements rather than merely convenience:

*   **libgit2 is built without network transports.** `elc` reads local
    repositories and never contacts a remote, so HTTPS and SSH support is
    attack surface with no matching capability. Compiling it out also drops
    the OpenSSL and libssh2 dependencies, and makes HLR-040's no-network
    rule a property of the link line rather than a promise.
*   **igraph is built with OpenMP support off.** Its default build links
    `libgomp`, whose runtime allocates a thread pool during the dynamic
    linker's initialisation — before `main` is entered. `elc` is
    single-threaded by requirement (HLR-041), and a thread runtime in the
    link line is a standing invitation for that to stop being true the
    first time a parallel algorithm is called. The instrumented
    dependency allowlist is what caught it, and now guards against its
    return.
*   **igraph is built with GraphML support off.** `elc` writes GraphML
    itself, so igraph's reader and writer are unused, and enabling them
    links a second XML library the project has no other need for.

It also resolves a version problem: Debian and Ubuntu currently carry
`libtree-sitter` at 0.22 and `igraph` at 0.10, both below the minimums, and
no backport carries a newer one. Those minimums are not arbitrary — a
tree-sitter runtime refuses a grammar generated for a newer language ABI,
which is what Phase 6 will produce, and igraph 1.0 is API-breaking against
0.10.

**Criterion is the exception**, taken from the distribution: it is a test
framework, never linked into the shipped binary, so a vulnerability in it
reaches no user of `elc`.

**`libelf` is the second exception, and unlike Criterion it is linked**, so it
is worth stating rather than assuming. elfutils does not ship `libelf` as a
library that builds on its own: configuring the project pulls in bison, flex,
gettext, and three compression libraries, every one of them a distribution
package. Building it from source would therefore import *more* distribution
packages than taking `libelf` from the distribution does, which inverts the
reason the rule exists. `make check-prereqs` reports its version alongside the
rest, so a build too old to read a class of image is visible before it is a
mystery.

**`libstdc++` was already linked and is now referenced deliberately.** It
arrives with `igraph`, which is partly C++ inside, and from Phase 16 `elc`
calls `__cxa_demangle` in it to decode the Itanium ABI (HLR-142). That is not a
new dependency, but it does mean naming the library on the link line: a symbol
reached through an indirect `DT_NEEDED` is not resolved by a modern `ld`.

## 1. Motivation

`elc` exists because per-function code health and architectural decay are
measured rarely, inconsistently, and only for the languages that happen to
be well served — so both kinds of decision get made on intuition instead of
evidence. The full argument is in the [PVD](PVD.md); this plan is concerned
only with how the product gets built.

The approach — Tree-sitter grammars loaded at runtime, everything
language-specific held as data, one parse feeding both the per-function
metrics and a project-wide graph — is what makes a single small binary able
to speak every language a polyglot repository contains without a plugin
ecosystem behind it.

## 2. Goals

1. Deliver an `elc` binary satisfying every requirement in [HLRs.md](HLRs.md),
   each bound to at least one passing test.
2. Reach and hold an empty coverage-gap list in [Traceability.md](Traceability.md),
   excepting the review-verified items the [STP](STP.md) §7 names explicitly.
3. Ship runtime language support for C, C++, Rust, and Python, added as
   data with no C change — the extensibility claim demonstrated rather than
   asserted.
4. Pass the three-pass test gate — ordinary, `make asan`, `make valgrind` —
   on every merge, not merely at release.
5. Produce byte-identical output across repeated runs, target orderings, and
   discovery routes, so that the numbers can be diffed, estimated from, and
   compared.
6. Keep the dependency set to four libraries and a POSIX libc, with library
   choice reviewable rather than incidental.
7. Ship a user manual and a man page that describe the delivered version,
   created in Phase 0 and extended by every phase that changes what a user
   can see.

## 3. Non-Goals

*   **Performance optimisation.** No performance target is committed
    ([PVD](PVD.md) §9). Correctness and determinism come first; if a real
    repository proves unusably slow, that opens a new plan, not a phase here.
*   **Concurrency.** `elc` is single-threaded by decision (HLR-041). No phase
    introduces a thread.
*   **Packaging and distribution.** Building distro packages, containers, or
    release binaries is out of scope for this plan; `make install` is the
    delivery surface.
*   **Grammar authorship.** Tree-sitter grammars are consumed, not written.
    Where a grammar is inadequate the answer is a better query or an upstream
    fix.
*   **The roadmap themes.** JSON/SARIF output, change-scoped analysis,
    ranking and sorting, and further graph analyses are named in
    [PVD](PVD.md) §9 and are not planned here.

## 4. Design — see the SDD

The detailed architecture is documented in the
[Software Design Document](SDD.md). `elc` is a one-way pipeline of fifteen
translation units; each stage consumes the previous stage's output and no
stage reaches backwards.

*   `cli.c` — the only reader of `argv`; produces the immutable options
*   `discover.c` — targets → a de-duplicated, stably ordered file list
*   `registry.c` — runtime location, grammar loading, query compilation
*   `analyze.c` — **the single parse**: per-function metrics *and* graph facts
*   `graph.c` — cross-file resolution into the System Dependence Graph
*   `arch.c`, `calltree.c`, `state.c` — the analyses over that graph
*   `thresholds.c` — the Appendix A catalogue, severity, attribution
*   `report.c` — the model, and **every sort** (the determinism audit point)
*   `format_*.c` — table, Markdown, CSV, XML, `.dot`, GraphML

The phase order below follows this pipeline outward: discovery before
parsing, parsing before metrics, metrics before the graph, the graph before
anything derived from it.

## 5. Development Process

Each phase is one unit of work travelling a fixed path: **Issue → branch →
implementation → PR → CI → review → merge**. Nothing is merged that has not
been through all of it.

### 5.1 Branching Strategy

Trunk-based, with short-lived phase branches off `develop`.

*   One branch per phase, named `phase/NN-slug` — e.g. `phase/03-eloc`.
*   A phase branch is opened from current `develop` and rebased onto it
    before the PR is raised, so that CI tests the merge result.
*   No branch outlives its phase. A phase that grows too large to review is
    split into two issues and two branches, not carried.
*   `develop` is always green: every commit on it has passed the full gate.

### 5.2 Code Review

*   Every phase is delivered as a Pull Request referencing its issue with a
    closing keyword, so merging the PR closes the issue.
*   The PR description restates the phase's acceptance criteria as a
    checklist, each item ticked with the evidence — a test name, a CI job, a
    fixture.
*   Review checks three things beyond correctness: that new behaviour has an
    HLR and an LLR, that the LLR has a bound test, and that no test seam
    entered `src/` (STP §7).
*   A PR that changes behaviour without changing `doc/Project.xml` is
    incomplete by definition and is sent back.

### 5.3 Continuous Integration

GitHub Actions, triggered on every push to a phase branch and on every PR
against `develop`. The workflow runs these jobs; **all must pass** before a
PR is mergeable:

| Job | Runs | Gates |
| --- | ---- | ----- |
| `spec` | `python3 tools/lint_project.py --no-warnings` | Spec integrity — broken traces, bad IDs, duplicate IDs, XSD violations |
| `coverage` | Gap count vs. `test/gap-baseline.txt` | Verification coverage never regresses |
| `build` | `make all` with `-Werror` | No warnings, no build regressions |
| `unit` | `make unit` | Criterion binaries, TAP output |
| `integration` | `make integration` | Bats over `build/elc` |
| `fixtures` | `make fixtures` | Hand-counted conformance |
| `instrumented` | `make instrumented` | `/proc`, `strace`, `ldd`, `unshare` checks |
| `asan` | `make asan` | ASan + UBSan + LeakSanitizer, whole suite |
| `valgrind` | `make valgrind` | Separate pass; never combined with ASan |

Notes that matter:

*   **`spec` runs first and fails fast.** A drifted `Project.xml` invalidates
    everything downstream, so there is no point testing against it.
*   **`--no-warnings` suppresses noise, not failures.** `lint_project.py`
    exits non-zero only on *errors* — a broken trace, a malformed ID, a
    duplicate, an XSD violation. Coverage gaps are warnings and never affect
    its exit status, so the flag changes nothing about what fails; it only
    keeps 378 lines of "no test verifying it" out of the log while every
    requirement is still unimplemented. The gaps are tracked by the next job
    instead, where they can be tracked *usefully*.
*   **The `coverage` job is what makes §6's completion rule enforceable.**
    That rule says a phase is not done until `Traceability.md` shows no new
    gap — an honour-system claim until something checks it. The job counts
    unverified requirements and compares against a committed baseline:

    ```sh
    GAPS=$(python3 tools/lint_project.py 2>&1 | grep -c 'has no test verifying it')
    BASE=$(cat test/gap-baseline.txt)
    if [ "$GAPS" -gt "$BASE" ]; then
        echo "coverage regressed: $GAPS gaps against a baseline of $BASE"
        exit 1
    fi
    ```

    Each phase lowers the baseline as part of its own commit, so the number
    falls monotonically and can only fall. A phase that adds requirements
    without tests raises the count and fails the job — which is precisely the
    incompleteness §6 describes. The baseline starts at **378** and must reach
    the review-verified residue by Phase 16.
*   **The runner is Linux.** `/proc`, `strace`, and `unshare` are Linux
    facilities; the instrumented level skips explicitly elsewhere and a
    silent skip is a failure (STP §2.2).
*   **Tree-sitter grammars are built and cached** by the workflow. A cache
    miss is slow, not fatal.
*   The generated Markdown documents are re-rendered in CI and compared; a
    stale `doc/*.md` fails the `spec` job, so the rendered documents can
    never drift from `Project.xml`.

### 5.4 Phase Execution Protocol

Every phase travels the same path, and the per-phase prompts in §8 assume it
rather than repeating it. The GitHub CLI (`gh`) is available locally, so the
whole cycle runs from the working copy.

1.  **Read the issue.** `gh issue view <N>` — its body carries the
    deliverables, its checklist the acceptance criteria.
2.  **Read the specification** before writing code: the HLRs and LLRs the
    issue names, the [SDD](SDD.md) sections for the modules being built, and
    the [STP](STP.md) fixture groups the phase must satisfy.
3.  **Create the branch** — `git switch -c phase/NN-slug develop`.
4.  **Implement**, following the [`elocker-dev`](../.github/skills/elocker-dev/SKILL.md)
    skill's conventions. New behaviour needs an HLR, an LLR, and a test; no
    test seam enters `src/`.
5.  **Test** — `make test`, then `make asan`. Both must be clean before the
    phase is considered done (§6).

    **Do not run `make valgrind` locally.** It re-runs the integration and
    fixture levels under instrumentation and takes the better part of an hour,
    which is an unreasonable price for a small change and a price paid on every
    iteration of one. It runs in CI on the pull request instead, where it costs
    waiting rather than working, and where it runs against the merge result
    rather than the working copy — which is the thing that has to be clean.

    **What that gives up, stated rather than glossed.** ASan and LSan catch
    invalid accesses and leaks, so those still fail locally and fail fast.
    `valgrind`'s memcheck additionally catches **reads of uninitialised
    memory**, which ASan does not detect at all; a defect of that one class now
    surfaces on the PR rather than before the push. That is the trade, it is
    accepted deliberately, and it is not a licence to push untested work — the
    two local gates above are not optional.

    Run it locally only when CI has reported a `valgrind` failure and you are
    reproducing it, or when a change is specifically about memory handling and
    you would rather know now. `ELC_VALGRIND=1` on a single suite is the cheap
    form; the full target is rarely what you want.
6.  **Feed discoveries back into the specification — before pushing.** This is
    the step that keeps the documents true, and it is not optional. During
    implementation you will find requirements that were ambiguous, designs
    that did not survive contact, and behaviours nobody wrote down. Every one
    of those is a change to [`doc/Project.xml`](Project.xml):
    *   An **ambiguous or wrong requirement** → amend the HLR or LLR text.
    *   A **behaviour with no requirement** → add one, and an LLR beneath it.
        Allocate the next free ID; never reuse or renumber.
    *   A **design that changed** → amend the SDD module, its data dictionary
        entry, or its algorithm prose.
    *   A **test approach that changed** → amend the STP.
    *   **Every test written** → a `<test>` entry under `<tests>`, with
        `<traces>` naming the LLRs or HLRs it verifies. *Without this the
        traceability matrix stays empty and the phase cannot close*, since a
        requirement is only verified when a catalogued test traces to it.
    *   Update `<counts>` and bump the affected `<document>` versions.

    Use the [`tracer`](../.github/skills/tracer/SKILL.md) skill for the
    mechanics.
7.  **Update the user documentation.** `doc/elc.1` and
    `doc/User_Manual.md` describe the version they ship with (HLR-129,
    HLR-130), so any option, output format, companion artefact, or category
    of finding this phase added, removed, or altered is documented **in this
    same change**. The man page carries the reference form; the manual adds
    at least one worked example per new capability. The documentation test
    checks the option lists against `elc --help`, but it cannot check that a
    new *format* or *finding* was described — that part is yours.
8.  **Re-render, validate, and lower the gap baseline.**
    ```sh
    python3 tools/lint_project.py --no-warnings          # must report 0 errors
    for d in SDD HLRs LLRs STP Traceability; do
        python3 tools/render_doc.py tools/templates/$d.md.j2 $d --out doc/$d.md
    done

    # the gap count must have fallen; commit the new figure
    python3 tools/lint_project.py 2>&1 | grep -c 'has no test verifying it' \
        > test/gap-baseline.txt
    ```
    **Lowering the baseline is part of the phase, not an afterthought.** If
    the count did not fall, requirements were delivered without tests
    tracing to them and the phase is not complete (§6). If it rose,
    requirements were added without tests — same conclusion. Drop the full
    warning list (`lint_project.py` without the flag) to see which
    requirements are still uncovered and whether they are this phase's.

9.  **Update the Status sections — in both `doc/SDP.md` and `README.md` —
    before pushing.** Two places track the same fact and both go stale the
    moment a phase lands if this step is skipped:
    *   In **this document**, mark the completed phase's row ✅ in the §8
        Status table (🔄 if the phase is genuinely partial — that should be
        rare, since a phase is not done until §6's rule is satisfied).
    *   In **`README.md`**, make the identical change to its own copy of the
        table, and update the `**Progress: N of M phases complete.**` line
        to match, where `M` is the number of rows in the table — inserting a
        phase changes it, and the instruction naming a fixed total is how it
        gets missed.
    The two tables must read the same after this step. A phase that changes
    what a user can see but leaves either Status section unstated as
    "Not started" is misleading anyone who reads the README instead of the
    SDP — which is most readers.
10. **Commit and push.** One commit per logical change, the last of which
    carries the spec, documentation, and Status updates. `git push -u origin
    phase/NN-slug`.
11. **Open the PR** — `gh pr create --base develop --fill`, with a body that
    restates the acceptance criteria as a ticked checklist naming the evidence
    for each, and `Closes #<N>` so the merge closes the issue.
12. **Open the next phase's issue** — `gh issue create` with the next phase's
    title, deliverables, and acceptance criteria taken from §8, so the next
    phase can begin the moment this PR merges.

### 5.5 Release Process

No release is cut before Phase 16. From then on:

1. `develop` merges to `main` when all phases through the release's scope are
   complete and the gap list is empty but for review-verified items. `main`
   does not exist before the first release and is created at that point;
   `develop` is the default branch until then.
2. The release is tagged `vMAJOR.MINOR.PATCH`. **The first release is
   `v0.1.0`.** Feature completeness is not the thing the major version
   promises — all 30 phases have shipped and the gap list is empty, and the
   number still says the *command-line interface* is not yet fixed. Two
   contracts are already stable regardless, and are stable because a
   requirement says so rather than because a version number implies it:
   HLR-121's cross-release clause fixes the six query files and their capture
   names, so a language module written against this release keeps working,
   and HLR-061 versions the saved record independently, so a record is
   rejected by a build that cannot read it rather than half-understood. A
   `1.0.0` would add to those only a promise about option spelling, which is
   the one thing feedback from real use is most likely to change.
3. `make install` under a `DESTDIR`/`PREFIX` staging root produces the
   deliverable: the `elc` binary plus the `runtime/` tree, and the man page
   and user manual that HLR-128 ships with them. Verified by
   `test/fixtures/runtime.bats`, which installs into a staging root and runs
   the result — "the files are present" and "the installed binary works" are
   different claims, and the build tree flatters the first by putting a
   `runtime` symlink beside `build/elc` that no installed layout has.
4. The [Traceability Matrix](Traceability.md) at the tagged commit is the
   evidence of verification and is published with the release.

## 6. Testing Strategy

The strategy is defined in full by the [Software Test Plan](STP.md); this is
the SDP-local summary of what each phase must satisfy.

| Level | Scope | Tools | Coverage Target |
| ----- | ----- | ----- | --------------- |
| Unit | One binary per `src/` module, calling internal functions directly | Criterion, `--wrap` mocking | Every LLR the phase delivers |
| Integration | `build/elc` over its command line | Bats, `bats-assert` | Every HLR the phase delivers |
| Fixture conformance | Hand-counted expected values per language | Bats | Every metric the phase computes |
| Instrumented | Process, syscall, and link observation | Bats + `strace`/`ldd`/`unshare` | The non-functional HLRs |
| Sanitized | The whole suite re-run instrumented | ASan, UBSan, LSan locally and in CI; valgrind in CI only (§5.4 step 5) | HLR-124, HLR-125 |

Tests are traced to Low-Level Requirements in [Project.xml](Project.xml) and
reported in the [STP](STP.md) and the [Traceability Matrix](Traceability.md).

**The phase-completion rule:** a phase is not done when its code works. It is
done when every HLR and LLR it claims has a passing test tracing to it, and
`Traceability.md` shows no new gap. This is what keeps the gap list falling
monotonically rather than being deferred to Phase 16.

**Progressively-satisfied requirements — the one sanctioned exception.** Four
requirements are assigned to the phase that introduces their mechanism, but
their text spans report content that later phases add:

| Requirement | Assigned | Completed by |
| ----------- | -------- | ------------ |
| HLR-031 — uniform tier list | Phase 5 | Its tiers name routes (P7) and the architectural findings of Phases 8–14 |
| HLR-054 — the complete XML record | Phase 5 | Likewise: the record grows as the report grows |
| HLR-056 — byte-identical regeneration | Phase 5 | Re-verified whenever the record gains a tier |
| HLR-115 — omitted analyses | Phase 9 | Also covers strata (P11) and execution scopes (P10) |

Each is verified at its assigned phase **against the report content that exists
at that phase**, re-verified automatically as each later phase extends the
report — the fixture suites re-run on every PR regardless — and finally closed
at Phase 16. Read strictly, the completion rule would make these four phases
uncloseable; this paragraph is the exception, and it extends to these four
requirements and no others.

## 7. Dependencies & Prerequisites

| Dependency | Required By | Notes |
| ---------- | ----------- | ----- |
| Criterion, Bats | Phase 0 | The harness must exist before any testable phase |
| GitHub Actions runner | Phase 0 | The gate is meaningless if it is not enforced |
| `libtree-sitter` + a C grammar | Phase 2 | Nothing can be parsed before this |
| Expat | Phase 5 | Only the regeneration read path needs it |
| Additional grammars | Phase 6 | Each is fetched from its pinned upstream release and built from source |
| `libgit2` | Phase 7 | Isolated to one discovery route |
| `igraph` | Phase 8 | With GraphML and OpenMP off, and its GMP choice pinned |
| `libelf` | Phase 16 | The image container only; the demangler it needs is already linked, the C++ runtime arriving with `igraph` |
| `jansson` | Phase 23 | The purification manifest, and from Phase 31 the HTML companion's payload. One JSON library, not two |
| Cytoscape.js + `expand-collapse` | Phase 31 | Fetched by the *browser* at view time, not linked and not required by the run (HLR-040) |

**Ordering constraints beyond the obvious:**

*   **Phase 8 must ship GraphML export**, not Phase 13. The [STP](STP.md) §5
    establishes GraphML as the only channel exposing graph *topology* — the
    findings report conclusions, not edges — so without it Phases 8 through
    11 have no way to assert that the graph was built correctly. Moving it
    later would leave four phases untestable.
*   **Phase 5 before Phase 6.** Adding four languages against a single output
    format tests less than adding them against all of them.
*   **Phase 2 must settle the query-file contract.** The six per-language
    queries and their capture names (HLR-121) are what Phase 6 codes against;
    changing them later invalidates every grammar shipped.
*   **Phase 16 is independent of Phase 15**, though the two answer one
    question. Conditional compilation re-decides the conditions a build
    resolved; a linked image observes what the build did. Either may ship
    first, and a project with a build to hand gets a sharper answer from
    Phase 16 — but only about which *functions* survived, so neither
    subsumes the other.
*   **Phase 11 is independent of Phases 9 and 10.** It consumes only the
    Phase 8 graph, so it may be worked in parallel with either.
*   **Phase 10 depends on Phase 9**, however — the two are not
    interchangeable. Reachability is measured from the entry points that
    Phase 9 declares (HLR-095), and the root set of HLR-096 is those entry
    points together with the address-taken functions. Phase 10 cannot begin
    until that declaration exists. Hoisting the entry-point option into
    Phase 8 would restore full independence, at the cost of a deliverable
    that nothing in Phase 8 consumes.

## 8. Phased Delivery

Each phase below becomes one GitHub Issue. The deliverables are the issue
body; the acceptance criteria are the PR checklist.

### Phase 0 — Foundation and Continuous Integration

1. Non-recursive `Makefile` with the self-documenting `help` default goal and
   every target from §5.3, `-MMD -MP` auto-dependencies, and `pkg-config`
   library discovery.
2. `build/runtime` symlink so `build/elc` resolves grammars without install.
3. GitHub Actions workflow implementing every CI job, including the
   re-render-and-diff check that prevents document drift.
4. Criterion and Bats harnesses wired, with `bats-support`/`bats-assert`
   vendored and one trivial test per level proving each level runs.
5. `src/main.c` and `src/cli.c` far enough for option parsing, the usage
   summary, and the exit-status scheme — no analysis.
6. `--wrap` plumbing demonstrated on one symbol, so the mocking approach is
   proven before it is depended upon.
7. `doc/elc.1` and `doc/User_Manual.md` created, documenting what exists at
   the close of this phase — the options, the usage summary, the exit-status
   scheme — and nothing more. Both installed by `make install`.
8. The documentation test: every option `elc --help` prints appears in both
   documents, and every option they document is accepted.
9. `test/gap-baseline.txt` seeded with the current gap count (378), and the
   `coverage` CI job that enforces it never rising.

**Requirements:** HLR-040, HLR-063, HLR-112, HLR-113, HLR-117, HLR-120,
HLR-128, HLR-129, HLR-130.

The two documents are created here, while there is almost nothing in them, so
that every later phase extends a document that already exists rather than
deferring one that does not. The documentation test exists from this phase for
the same reason: it is the mechanism that makes HLR-130 enforceable rather
than aspirational.

**Acceptance:** CI green on all nine jobs. `elc --help` exits 0 with the
summary on stdout; `elc --bogus` exits 2 with usage on stderr; `elc` with no
target exits 2. `ldd` shows no interpreter or VM. `make asan` and
`make valgrind` both pass on the skeleton. A deliberately broken
`Project.xml` fails the `spec` job. `man -l doc/elc.1` renders without
diagnostic; adding an option without documenting it fails the documentation
test.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 0 — Foundation and Continuous Integration** of
`doc/SDP.md`, tracked by issue #<N>.

Read first: `doc/SDP.md` §0 and §5, and the `elocker-dev` skill's Build
section — the Makefile conventions there are binding.

Build the scaffolding only. No analysis logic: `elc` parses its options,
prints usage, and exits. The point of this phase is that every later phase
has a gate to pass.

Watch for:
* `help` is the **default goal**; every target carries a `## description` or
  it vanishes from `make help`.
* `-pthread` must NOT appear — `elc` is single-threaded (HLR-041).
* The `spec` CI job runs **first** and fails fast; a drifted `Project.xml`
  invalidates every job after it.
* Add the re-render-and-diff check now, while there is nothing to break: CI
  re-renders the five generated documents and fails if any differs from what
  is committed.
* Prove `--wrap` on one symbol before anything depends on it. If the
  toolchain cannot do it, stop and raise it — the whole unit level rests on
  this.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 1
from §8.
```

### Phase 1 — Target Discovery and the Walking Skeleton

1. `discover.c`: target validation, classification, filesystem traversal with
   `fts(3)`, binary-extension and hidden-directory exclusion, symlink policy.
2. Canonicalisation and de-duplication; stable sort into byte order.
3. `report.c` skeleton and the aligned table renderer, reporting physical
   line counts only.
4. Output-file redirection; the stdout/stderr split.
5. `traversal/` and `determinism/` fixture groups.

**Requirements:** HLR-001, HLR-004, HLR-005, HLR-006, HLR-027, HLR-030,
HLR-032, HLR-033, HLR-038, HLR-039, HLR-041, HLR-043, HLR-062, HLR-066,
HLR-069, HLR-071, HLR-072.

**Acceptance:** `elc src/` prints a stable table of files and physical line
counts. Repeat runs are byte-identical; so are runs with targets given in a
different order. `elc a.c src/` counts `a.c` once. A cyclic directory symlink
does not hang the walk. An invalid target exits 2 with no partial report. An
empty target produces a zero-total report and exits 0. `/proc` shows one
thread; the fixture tree checksums identically before and after. A decoy
`.elcrc` planted in the working directory, the target, and an ancestor changes
nothing about the output.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 1 — Target Discovery and the Walking Skeleton**,
tracked by issue #<N>.

Read first: `doc/SDD.md` §5 (`discover.c`) and §13 (`report.c`), and LLR
groups `LLR-DSC`, `LLR-FTS`, `LLR-EXT`, `LLR-TBL`.

End to end by the close of this phase: `elc src/` prints an aligned table of
files with physical line counts.

Watch for:
* `FTS_PHYSICAL`, not `FTS_LOGICAL` — a cyclic directory symlink otherwise
  walks forever. But targets are classified with `stat(2)`, which *does*
  follow links, because a symlink named explicitly identifies its referent.
  The pairing is deliberate; changing one to match the other breaks HLR-069.
* Canonicalise with `realpath()` **before** de-duplicating, or `elc a.c src/`
  counts `a.c` twice.
* Every sort lives in `report.c`. No renderer sorts. This is the audit point
  for HLR-032/033 and it is easier to hold from the start than to retrofit.
* Validate **all** targets before walking any of them — a report that
  silently covers fewer targets than were named is worse than no report.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 2
from §8.
```

### Phase 2 — Language Runtime and Function Discovery

1. `registry.c`: runtime-location resolution with environment precedence,
   `extensions.map` loading, lazy `dlopen`, query compilation, teardown
   ordering. (`binary.exts` is already loaded by `discover.c`, which needed
   it in Phase 1 and is its only consumer — see the note below.)
2. The C grammar and its six query files, with `functions.scm` complete.
3. `analyze.c`: `mmap`, single parse, function discovery, nested named
   functions, identifier extraction, line-number conversion.
4. Per-file read and parse failure tolerance; unusable-module tolerance.
5. `runtime/` fixture group; the documented query-file and capture-name
   contract published.

**Requirements:** HLR-007, HLR-008, HLR-009, HLR-010, HLR-012, HLR-014,
HLR-035, HLR-036, HLR-037, HLR-059, HLR-060, HLR-067, HLR-070, HLR-121.

**Acceptance:** `elc file.c` lists every function with its name and line
range, nested subprograms included. An absent runtime directory is fatal
before any file is read; a single broken module degrades that language only
and still exits 0. A file with an unmappable extension is skipped, listed as
skipped, and reported on stderr — and a skip, unlike a failure, leaves the
exit status at 0. A run in which one file fails to parse still produces a
report for the rest and exits 1. `strace` shows each source file opened
exactly once.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 2 — Language Runtime and Function Discovery**,
tracked by issue #<N>.

Read first: `doc/SDD.md` §6 (`registry.c`) and §7 (`analyze.c`), and LLR
groups `LLR-ROP`, `LLR-RFP`, `LLR-RCL`, `LLR-ANL`.

Ship the C grammar and all six query files, with `functions.scm` complete.
The other five may be stubs that capture nothing — later phases fill them.

Watch for:
* `discover.c` resolves the runtime location for itself today, because
  Phase 1 needed `binary.exts` and no registry existed. Resolve it once in
  `registry.c` per LLR-ROP-01/02 and have `discover.c` ask for it, rather
  than leaving two copies of the same precedence rule.
* **Teardown order is load-bearing**: delete every `TSQuery`, then the parser
  and cursor, then `dlclose`. A query holds pointers into the grammar the
  handle unmaps; the wrong order crashes at exit with a useless backtrace.
* ISO C forbids casting `void *` to a function pointer. Use
  `*(void **)&fn = dlsym(...)`, clearing `dlerror()` first and checking it
  after — `NULL` is a legal result for a NULL symbol.
* `mmap` of a zero-length file fails `EINVAL`. Short-circuit empty files.
* `ts_parser_parse_string` takes an explicit length; the mapping is not
  NUL-terminated. Copy identifiers out before unmapping.
* The six query filenames and their capture names become a **published
  contract** (HLR-121) that Phase 6 codes against. Settle them here; changing
  them later invalidates every grammar shipped.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 3
from §8.
```

### Phase 3 — Effective Lines of Code

1. `comments.scm` and `eloc.scm` for C; span collection.
2. `merge_comment_spans`: sort by start byte, coalesce overlapping and nested
   spans, exclude once.
3. `innermost_enclosing` attribution — each statement to exactly one reported
   function.
4. The nine ELOC categories; multi-line statements counted once.
5. File-level and project-level totals, combined and per language.
6. `eloc/`, `comments/`, and `nesting/` fixture groups with hand-counted
   values and stated reasoning.

**Requirements:** HLR-013, HLR-015, HLR-016, HLR-019, HLR-020, HLR-024,
HLR-025, HLR-034, HLR-044 – HLR-053, HLR-068.

HLR-013 (AST-based extraction only) lands here rather than in Phase 2 because
it has no direct observable: the [STP](STP.md) §5 binds its verification to the
`comments/` adversarial fixtures, which defeat any textual approach and which
arrive with this phase.

**Acceptance:** Every fixture's ELOC matches its hand-counted header,
including nested block comments, comment syntax inside string literals, and
string delimiters inside comments — the last of which is what verifies
HLR-013, since no regex approach survives it. A file of only comments and
declarations reports zero. A statement split across three lines counts as one. No
statement in a nested named function is counted twice.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 3 — Effective Lines of Code**, tracked by issue #<N>.

Read first: `doc/SDD.md` §7 (`analyze.c`, especially its Algorithm section),
LLR groups `LLR-ANL`, `LLR-MRG`, `LLR-INN`, and HLR-044 through HLR-053 for
the category definitions.

Watch for — this phase carries more correctness weight than any other before
Phase 8:
* **Comment spans must be sorted and coalesced before anything is
  subtracted.** Subtracting per capture double-counts a block comment
  containing inline comment syntax and can drive a file's ELOC negative. This
  is the canonical bug in this class of tool.
* **Each statement contributes to exactly one function** — the innermost
  reported one. Without that, a nested named function's lines count twice.
* A statement spanning several physical lines counts **once**, at its start
  line. Style must not move the number.
* Comment syntax inside a string literal is not a comment, and a string
  delimiter inside a comment does not open a string. Both fall out of the
  syntax tree — and passing the `comments/` fixtures is what verifies
  HLR-013, which has no observable of its own.
* Fixture expected values are hand-counted with their reasoning in the header.
  **Never** generate them from `elc`'s output; a fixture that agrees with the
  implementation by construction asserts nothing.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 4
from §8.
```

### Phase 4 — Cyclomatic Complexity

1. `complexity.scm` for C; decision-point counting as one plus captures.
2. Anonymous-scope attribution to the nearest enclosing named function.
3. Per-file threshold listing with the configurable threshold, default 15.
4. Project-wide most-complex callouts with the deterministic tie-break.
5. Complexity added to the fixture expected values.

**Requirements:** HLR-017, HLR-018, HLR-021, HLR-022, HLR-023, HLR-026.

**Acceptance:** Complexity matches hand counts on every fixture. A lambda's
decision points land on its enclosing function. `-c 5` changes what is listed
and nothing else — in particular not the exit status. Two functions tied for
most-complex resolve identically on every run.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 4 — Cyclomatic Complexity**, tracked by issue #<N>.

Read first: `doc/SDD.md` §7 and §13, LLR groups `LLR-ANL` (complexity
clauses) and `LLR-RPT`.

Mostly a second query over machinery Phase 3 built.

Watch for:
* Complexity is **one plus** the decision points in the function body — the
  base is added in C, so `complexity.scm` must not capture the function
  itself.
* A decision point inside an anonymous lambda belongs to the nearest
  enclosing **named** function, because the lambda is not itself reported. A
  nested *named* function is different: it owns its own decision points
  (HLR-018 vs HLR-067/068).
* The threshold changes **what is listed and nothing else** — in particular
  not the exit status (HLR-023).
* Ties for the most-complex callout resolve by the stable presentation order,
  or repeat runs disagree and HLR-032 fails.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 5
from §8.
```

### Phase 5 — Output Formats and the Saved Record

1. CSV writer with RFC 4180 quoting through a single field function.
2. XML writer: the complete record, format-version identifier, escaping
   through a single function.
3. Markdown writer; the shared traversal that guarantees uniform composition.
4. Expat-based streaming reader and the regeneration mode, with its
   independently supplied threshold.
5. Rejection of malformed, foreign, and unsupported-version records.
6. `escaping/` and `regeneration/` fixture groups.

**Requirements:** HLR-028, HLR-029, HLR-031, HLR-054, HLR-055, HLR-056,
HLR-057, HLR-058, HLR-061, HLR-064, HLR-065, HLR-122.

**Acceptance:** A C++ template signature containing a comma survives a CSV
round-trip with its field count intact. Emitted XML passes `xmllint`.
Markdown regenerated from a saved record is byte-identical to Markdown
produced directly at the same threshold. A record with a bumped version is
rejected with exit 2 and no partial output.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 5 — Output Formats and the Saved Record**, tracked by
issue #<N>.

Read first: `doc/SDD.md` §14–§16, LLR groups `LLR-CSV`, `LLR-FLD`,
`LLR-XWR`, `LLR-ESC`, `LLR-XRD`, `LLR-MKD`, `LLR-SUM`.

Watch for:
* **Every** CSV field goes through the one quoting function, and **every**
  XML value through the one escaping function. A C++ template signature
  `foo<int, long>` contains a comma and an angle bracket; one unescaped path
  corrupts the document silently.
* Writing is hand-rolled text emission — no XML writer library. Only the
  *read* path uses a parser, because that input is user-supplied and may not
  be something `elc` wrote.
* The XML record must carry **everything a report can present**, so the
  regenerated Markdown can be byte-identical. Anything omitted here is a
  Phase 16 defect.
* Reject a malformed, foreign, or unsupported-version record outright. No
  best-effort partial conversion — a partly reconstructed report is
  indistinguishable from a complete one once rendered.
* HLR-031, HLR-054, and HLR-056 are progressively satisfied (§6): verify them
  against the report content that exists *now*.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 6
from §8.
```

### Phase 6 — Language Breadth

1. Grammars and six query files each for C++, Rust, and Python.
2. Per-language fixtures under `eloc/` and `nesting/` with hand-counted
   values.
3. `extensions.map` entries for every shipped extension.

**Requirements:** HLR-011.

**Acceptance:** All five languages produce correct ELOC and complexity
against hand-counted fixtures. **The diff for this phase contains no change
under `src/`** — that is the phase's real acceptance criterion, and it is the
extensibility claim of HLR-010 demonstrated rather than asserted. A
mixed-language target is analysed in one invocation.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 6 — Language Breadth**, tracked by issue #<N>.

Read first: the query-file contract published in Phase 2, and `doc/STP.md` §5
for the fixture conventions.

Add C++, Rust, and Python: a grammar, six query files, `extensions.map`
entries, and hand-counted fixtures for each.

**The acceptance criterion for this phase is that `git diff` shows no change
under `src/`.** That is HLR-010's extensibility claim demonstrated rather than
asserted. If a language cannot be supported without touching C, stop — the
defect is in the design, not the grammar, and the fix belongs in `src/` as a
separate issue before this phase proceeds.

Watch for:
* **Vet every grammar before adopting it** — licence, maintenance, and
  authorship — and record the verdict in `doc/notes.md`. A grammar is a
  dependency like any other.
* Nested named functions and Rust closures are the interesting cases for
  HLR-067/068 — give each a `nesting/` fixture.
* Fixture authoring is the bulk of this phase. Budget for it.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 7
from §8.
```

### Phase 7 — Git-Aware Discovery

1. `libgit2` integration; repository detection via ancestor search.
2. The applicability test — a repository is used only if it tracks the target.
3. Enumeration scoped to paths at or beneath the target.
4. Binary-blob exclusion by content, not extension.
5. Discovery-route recording and reporting.
6. `repo/` fixture group, including the ignored-target and enclosing-repo
   cases.

**Requirements:** HLR-002, HLR-003, HLR-126, HLR-127.

**Acceptance:** A repository target analyses tracked, non-binary files only,
with no exclusion list. `elc src/` inside a repository analyses `src/` and
nothing above it. A `.gitignore`d target directory falls back to filesystem
traversal rather than reporting zero files. A target beneath an unrelated
enclosing repository does the same. The route taken appears in the report.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 7 — Git-Aware Discovery**, tracked by issue #<N>.

Git support is **confirmed in scope** (§9) — implement it. Note the fallback
recorded there: if the dependency weight ever proves unacceptable, the answer
is a build-time option degrading to filesystem traversal, not removal of the
capability.

Read first: `doc/SDD.md` §5 (`discover.c`, its Algorithm section especially),
LLR groups `LLR-GIT`, `LLR-DSC`.

Watch for:
* `git_repository_open_ext()` **searches ancestors**. Finding a repository is
  not sufficient reason to use it: test that it actually tracks the target
  first. Two ordinary situations break without this — analysing a
  `.gitignore`d build directory, and analysing anything beneath a
  version-controlled home directory. Both must fall back to filesystem
  traversal.
* **Scope enumeration to the target.** The tree at `HEAD` is the whole
  repository; without scoping, `elc src/` analyses the entire project.
* A directory target must denote the same file set whichever route reaches it.
* Report the route taken, or an unexpectedly empty result is undiagnosable.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 8
from §8.
```

### Phase 8 — System Dependence Graph

1. `calls.scm` and `globals.scm` for all five languages; address-taken
   capture.
2. `graph.c`: symbol table, cross-file call resolution, global-state edges,
   simple-graph collapse with call-site counts, component projection.
3. Unresolved-call recording and reporting.
4. **GraphML export** — required here, not later, as the test observability
   channel for this phase and the three that follow.
5. `graph/` fixture group asserting against `expected.graphml`.

**Requirements:** HLR-073, HLR-074, HLR-075, HLR-076, HLR-077, HLR-106,
HLR-114.

**Acceptance:** The exported GraphML matches the fixture's expected topology
node for node and edge for edge, with call and global edges distinguishable.
A call into an external library is counted as unresolved and reported, not
silently dropped. The graph is built without reopening any source file —
`strace` still shows one open per file.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 8 — System Dependence Graph**, tracked by issue #<N>.

The largest phase. If it proves oversized, split symbol resolution and graph
construction from the GraphML writer — but note that leaves the first half
untestable until the second lands, which is why they are planned together.

Read first: `doc/SDD.md` §8 (`graph.c`) and §17 (`format_graph.c`, its
GraphML Content Model interface), LLR group `LLR-SDG`.

Watch for:
* **GraphML ships in this phase, not Phase 13.** It is the only channel that
  exposes graph *topology* — findings report conclusions, not edges — so
  without it this phase and the three after it have nothing to assert
  against. Follow the content model in the SDD exactly; the fixtures'
  `expected.graphml` depends on it being stable.
* The SDG is a **simple** digraph: repeated calls collapse to one edge with a
  call-site count. A multigraph inflates fan-out for a function calling one
  helper in a loop and again in an error path.
* An unresolved call — external library, syscall, function pointer — is
  **counted and reported, never fatal**. Do not guess at a destination;
  over-claiming edges would make Phase 10's dead-code proof unsound.
* Capture address-taken functions here even though Phase 10 consumes them.
* **A `calls.scm` for an ambiguous grammar needs care.** Where a language
  writes an array index exactly like a call, the grammar manages the
  ambiguity with precedence rules rather than resolving it, and the query
  cannot separate the two. Expect false-positive call edges. Do not
  disambiguate heuristically in C — that would put language knowledge in the
  binary. Pin whatever behaviour you settle on in the `graph/` fixtures, so
  it is a recorded decision rather than a later surprise.
* Build the graph from the facts the single parse produced. `strace` must
  still show one open per file (HLR-076).

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 9
from §8.
```

### Phase 9 — Call Tree Analyses

1. Fan-out per function; the exhaustive band classification.
2. Recursion detection, direct and mutual.
3. Entry-point declaration; maximum depth by longest path over the DAG.
4. The deepest call stack reported in full, with predecessor reconstruction.
5. Recursion reported in place of depth where the graph is cyclic.
6. Omission handling when no entry points are declared.
7. `calltree/` fixture group covering every band boundary.

**Requirements:** HLR-085, HLR-087, HLR-088, HLR-089, HLR-090, HLR-095,
HLR-115.

This phase *measures*; it does not classify. Fan-out banding (HLR-086) and the
depth bands of HLR-087 are `thresholds.c`'s work and land in Phase 12, so the
fixtures here assert raw values and the band assertions are added when that
phase arrives.

**Acceptance:** Fan-out is reported correctly for functions with 2, 3, 7, 8,
10, 11, 15, and 16 distinct callees — the boundary values Phase 12 will band.
A recursive fixture reports its cycle and no depth figure, and terminates. The deepest chain is reported as an ordered
sequence, not a number. With no entry points declared, depth analysis is
omitted with a stated reason rather than producing an empty or misleading
result.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 9 — Call Tree Analyses**, tracked by issue #<N>.

Read first: `doc/SDD.md` §10 (`calltree.c`), LLR groups `LLR-CTR`, `LLR-LPD`.

This phase **measures**; it does not classify. Fan-out banding is
`thresholds.c`'s work in Phase 12 — report raw values here.

Watch for:
* **Establish acyclicity before measuring depth.** On a cyclic graph the
  longest path has no finite answer and a naive traversal will not terminate.
  That is precisely why MISRA C Rule 17.2 exists.
* Where recursion is present, report the **cycle in place of a depth
  figure** — not a misleading finite number.
* Report the deepest **chain**, the ordered sequence of functions, not just
  its length. The embedded user needs to know which path to shorten.
* Depth is a lower bound: chains through unresolved indirect calls are not
  followed. Report it alongside the unresolved count.
* With no entry points declared, **omit** the analysis with a stated reason.
  Never report an empty or misleading result.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 10
from §8.
```

### Phase 10 — Dead Code, Reachability, and Global State

1. Root-set construction — declared entry points ∪ address-taken functions.
2. Forward reachability; unreachable functions and unreachable globals.
3. **Intra-procedural dead code** — `deadcode.scm` per language, the
   terminator/re-entry sibling walk, and literal-condition branches.
4. Global access mapping; scope-reduction and hidden-channel classification.
5. Execution-scope declaration and cross-scope access reporting.

**Requirements:** HLR-091, HLR-092, HLR-093, HLR-094, HLR-096, HLR-097,
HLR-137, HLR-138, HLR-139.

**Why the two dead-code analyses land together.** They share nothing
mechanically — one is a graph traversal needing the whole project, the other
a syntax query needing one function — and deliverable 3 could have been built
any time since Phase 4. They are here because they share a *report section*
and a user question: "what code does not run?" Splitting them across phases
would ship half an answer, then change the shape of the section that carries
it. If this phase proves oversized, deliverable 3 is the clean split: it
depends on nothing else here, and its fixtures stand alone.

**Acceptance:** A clique of unused functions calling one another is reported
unreachable. **A function reachable only through an address-taken pointer is
not** — the case that makes the dead-code claim sound rather than merely
plausible. A global touched by one function is flagged for scope reduction; a
global spanning disconnected regions is flagged as a hidden channel. With no
entry points declared, nothing is reported unreachable.

For the intra-procedural half: statements after a `return`, `break`, or
`continue` are reported; **a `goto` label following a `return` is not**, and
neither is a branch guarded by a variable holding a constant — the two
false-positive cases that decide whether this analysis can be trusted. `if
(0)` and the `else` of `if (1)` are reported. A file whose language supplies
no `deadcode.scm` is reported as *not analysed*, never as clean.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 10 — Reachability and Global State**, tracked by
issue #<N>. Depends on Phase 9's entry-point declaration.

Read first: `doc/SDD.md` §11 (`state.c`) and §7 (`collect_dead_code` in
`analyze.c`), LLR groups `LLR-RTS`, `LLR-RCH`, `LLR-UGL`, `LLR-GLB`,
`LLR-ISO`, `LLR-DED`.

Watch for — one item here decides whether the product's headline claim is
sound:
* **The root set is declared entry points ∪ address-taken functions.** A
  function invoked only through an interrupt vector or a callback table has
  no resolved caller; without this it is reported as provably dead, which is
  a correctness failure. The asymmetry is deliberate: an extra root only
  shrinks the unreachable set, whereas a missing root produces a false claim
  of death. Err toward reachable.
* Unreachability is established **solely by graph traversal**. A clique of
  unused functions calling one another must still be reported dead — that is
  the case textual linters get wrong, and the reason this analysis exists.
* Globals accessed only by unreachable functions are themselves unreachable.
* With no entry points declared, report **nothing** unreachable.

And for the intra-procedural half, where the danger is the opposite one:
* **A false claim of dead code is worse than a missed one.** A missed
  statement costs a cleanup opportunity; a false claim invites deleting code
  that runs. Where the two cannot both be had, report nothing (HLR-138).
* **A label following a `return` is reachable.** It is a *sibling* of the
  return in the tree and looks exactly like dead code to a naive sibling
  walk. The `@dead.reentry` capture exists for it; a language module omitting
  that pattern produces false claims, which is why the fixture group pins it.
* **Evaluate nothing.** `if (0)` is dead because the source writes a literal.
  `x = 0; if (x)` is not detected and must not be — that needs data flow, and
  data flow is how this analysis would start being wrong.
* The terminator and re-entry constructs, and what counts as a false literal,
  are **language knowledge and belong in `deadcode.scm`**. The sibling walk
  is structural and belongs in C. If you find yourself typing a node type
  into a `.c` file, the split is in the wrong place.
* `deadcode.scm` is **optional**, unlike the six required query files —
  making it required would invalidate every language module already shipped,
  which is precisely what HLR-121 protects against. A language without it is
  reported as unanalysed, not as clean (HLR-139).

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 11
from §8.
```

### Phase 11 — Coupling, Layering, and Cycles

1. Stratum and dependency-direction declaration.
2. Skip-level and direction-inverted call detection as distinct findings.
3. Afferent and efferent coupling per component.
4. Instability, with the undefined case handled.
5. Bottleneck identification against the configurable threshold.
6. Component-level cycle detection, distinct from function-level recursion.
7. `arch/` fixture group with a hand-computed coupling table.

**Requirements:** HLR-078, HLR-079, HLR-080, HLR-081, HLR-082, HLR-083,
HLR-118.

As in Phase 9, detection is separated from severity: cycles are found here,
but reporting them at critical severity (HLR-084) is `thresholds.c`'s work in
Phase 12.

**Acceptance:** Ca/Ce and instability match the hand-computed table. A
component with both couplings zero reports instability undefined, not a
division error. Mutual recursion within one file is a recursion finding and
**not** a component cycle; across two files it is legitimately both. A call
inverting the declared direction without skipping a layer is reported, and
vice versa.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 11 — Coupling, Layering, and Cycles**, tracked by
issue #<N>. Independent of Phases 9 and 10; may proceed in parallel.

Read first: `doc/SDD.md` §9 (`arch.c`), LLR groups `LLR-CPL`, `LLR-INS`,
`LLR-CYC`, `LLR-LAY`, and HLR-114's definition of a component.

Detection only — reporting cycles at critical severity is Phase 12's work.

Watch for:
* **A component is a source file** (HLR-114). Cycles are found over the
  *component* projection, not the function graph. Mutual recursion between
  two functions in one file is a recursion finding (Phase 9) and **not** a
  component cycle; across two files it is legitimately both, because they are
  different facts.
* Instability is undefined when both couplings are zero. Report it as such —
  do not divide.
* Skip-level and direction-inverted are **distinct findings** from one
  ordinal comparison: a call may descend two layers without inverting, or
  ascend one layer without skipping.
* Strata come from the command line only — never inferred from the
  filesystem. With none declared, omit the analysis with a stated reason.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 12
from §8.
```

### Phase 12 — Thresholds, Severity, and Attribution

1. The Appendix A catalogue as a static table.
2. Band evaluation for every measurement kind.
3. The closed severity vocabulary, highest-applicable-band rule.
4. Source attribution, with `elc`'s own heuristics labelled as such.
5. Measurements carried into the report whether or not a band was crossed.

**Requirements:** HLR-084, HLR-086, HLR-098, HLR-099, HLR-100, HLR-101,
HLR-111, HLR-123.

HLR-084 and HLR-086 arrive here rather than with the analyses that produce
their inputs, because both are classification rather than measurement, and the
[SDD](SDD.md) places all banding and severity in `thresholds.c`. Splitting them
out avoids Phase 9 and Phase 11 each building a fragment of this module early
and then reworking it.

**Acceptance:** Fan-outs of 2, 3, 7, 8, 10, 11, 15, and 16 each classify into
exactly one band, and every dependency cycle is reported at critical severity.
Every reported finding carries exactly one severity from the closed set and an
attribution naming its source. The bottleneck threshold is
visibly marked as `elc`'s own rather than published. A measurement inside its
band still appears in the report. No finding contains imperative remediation
text. Severity does not move the exit status.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 12 — Thresholds, Severity, and Attribution**, tracked
by issue #<N>.

Read first: `doc/SDD.md` §12 (`thresholds.c`, its Threshold Catalogue
interface), `doc/PVD.md` Appendix A for the published values, LLR group
`LLR-THR`.

This phase owns **all** banding and severity. Phases 9 and 11 deliberately
deferred HLR-086 and HLR-084 here so that no fragment of this module was
built early and reworked.

Watch for:
* The fan-out bands are **exhaustive**: every value from 0 upward classifies
  exactly once. 8–10 is acceptable and yields no finding — the gap that used
  to sit there is why the bands were rewritten.
* Severity is a **closed set**, `info` < `warning` < `critical`, exactly one
  per finding, highest applicable band wins.
* Every threshold names its source. The bottleneck threshold is **`elc`'s own
  heuristic, not a published standard**, and must be labelled as such
  wherever it appears (HLR-099) — that label is what keeps the "no built-in
  opinion" non-goal honest while shipping MISRA and Martin values.
* A measurement inside its band is still **reported** (HLR-031). Severity
  never touches the exit status (HLR-100).

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 13
from §8.
```

### Phase 13 — Graph Visualisation

1. `.dot` writer with deterministic node and edge ordering.
2. Annotation of every applicable finding, degrading gracefully.
3. Default-on generation with a command-line disable.
4. Filename derivation by extension substitution; suppression on stdout.

**Requirements:** HLR-102, HLR-103, HLR-104, HLR-105, HLR-119.

**Acceptance:** The emitted `.dot` parses under `dot -Tsvg -o /dev/null`.
Cycles, unreachable functions, the deepest chain, and threshold-exceeding
nodes are visibly annotated. Output to a named file produces the companion;
output to stdout produces none, disabled or not. A renderer ignoring the
annotation attributes still draws a valid tree.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 13 — Graph Visualisation**, tracked by issue #<N>.

Read first: `doc/SDD.md` §17 (`format_graph.c`), LLR groups `LLR-DOT`,
`LLR-STY`, `LLR-WAR`.

Watch for:
* This is the one renderer that walks the `Sdg` rather than the sorted report
  model, so it must **impose its own order**: ascending node id, and each
  node's adjacency by ascending target id. Otherwise the graph library's
  internal enumeration leaks into the output and HLR-032 fails.
* Generation is **on by default**, disabled by an option — but produces
  nothing when the report goes to stdout, disabled or not, since there is no
  output path to derive a filename from.
* Annotations use attributes a renderer may ignore while still producing a
  valid tree.
* Graphviz renders the output; `elc` never links it.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 14
from §8.
```

### Phase 14 — Custom Rules

1. Rule loading from the runtime location and the command line, with language
   binding in both forms.
2. Compilation against the bound grammar; rule identity as basename plus
   capture.
3. Match reporting alongside the built-in findings.
4. Invalid-rule handling split by provenance — fatal when named, skipped when
   located.
5. `rules/` fixture group.

**Requirements:** HLR-107, HLR-108, HLR-109, HLR-110, HLR-116.

**Acceptance:** A valid rule matches and reports with file, line range, and
identity. Adding a rule requires no rebuild. An invalid rule named on the
command line is fatal; the same file found in the runtime location is skipped
with a diagnostic and the run continues. No rule file is discovered from the
working directory or the target.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 14 — Custom Rules**, tracked by issue #<N>.

Read first: `doc/SDD.md` §6 (`registry.c`, its Custom Rule Binding
interface), LLR groups `LLR-RLR`, and HLR-107 through HLR-111, HLR-116.

Watch for:
* **A query compiles against one specific grammar**, so every rule must name
  its language: bound by directory in the runtime location, bound by the
  `lang:path` argument form on the command line.
* Error handling splits by **provenance**: a rule named on the command line
  that is invalid is a user error and fatal; the same file found in the
  runtime location is a malformed component — diagnose, skip, continue.
* Rule identity is the file's basename plus the capture name, so one file can
  express several named rules.
* No rule file is ever discovered from the working directory, the target, or
  a dotfile (HLR-110) — that would break the zero-configuration guarantee.
* `elc` reports what a rule matched and forms **no opinion** about whether the
  rule is a good one.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 15
from §8.
```

### Phase 15 — Conditional Compilation

1. `cli.c`: the `-D` option, its definition list, and its rejection alongside
   regeneration mode.
2. `conditionals.scm` for C and Rust — the optional seventh query file — and
   the contract entry describing it.
3. `registry.c`: loading that file where a module supplies one, and treating
   its absence as "this language has none".
4. `analyze.c`: evaluating each region's condition from the parsed condition
   nodes, and joining the inactive byte ranges to the exclusion the merged
   comment set already uses.
5. The undecided-region count, and the definitions, carried into the report
   model, every format, and the saved record.
6. The `conditional/` fixture group, with hand-counted values per
   configuration.

**Requirements:** HLR-131 – HLR-136.

**Acceptance:** `elc -DFOO src/` reports the metrics of the configuration in
which `FOO` is defined, and `elc src/` reports byte-identically to a build
made before the option existed. `#if 0` prunes with no definitions supplied.
A condition naming a symbol no `-D` mentions leaves both branches counted and
raises the undecided count. The same source under a Rust `cfg` attribute
prunes by the same mechanism, with no change to `src/` beyond what C needed.
`-D` with `--from-xml` is a usage error.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 15 — Conditional Compilation**, tracked by issue #<N>.

Read first: `doc/SDD.md` §7 (`analyze.c`, its Algorithm section), §4
(`cli.c`), and §6 (`registry.c`); HLR-131 through HLR-136; and the
`conditionals.scm` entry in `runtime/queries/README.md`.

This reverses a judgement recorded in `doc/notes.md` §3 — that code inside
`#if 0` counts, because elc does not preprocess. It still does not
preprocess. What changed is that a condition can be *decided from the parsed
tree* for the cases that matter, which is a different thing from running a
preprocessor over the text.

Watch for:
* **No toolchain, ever** (HLR-135). No `cpp`, no `rustc --cfg`, no build
  system, and no reading a file the source `#include`s. The result must not
  depend on which compiler is installed.
* **Undecidable is not false** (HLR-133). A condition elc cannot decide
  leaves *both* branches counted and increments the undecided count.
  Treating it as false silently deletes code and produces a report that is
  confidently wrong and indistinguishable from a right one.
* **With no `-D`, nothing changes.** Every existing fixture must report the
  same numbers it does today; that is the cheapest regression test available
  and it is already written.
* **The exclusion already exists.** Inactive ranges join the merged comment
  set rather than becoming a second mechanism — one thing to understand, and
  neither can remove a range twice.
* **`conditionals.scm` is optional, and the required six are unchanged.** A
  module without one has no conditional compilation. Do not make it a
  seventh required file; that would break every module already shipped.
* The definitions belong in the report *and* in the saved record, or a
  regenerated report describes a configuration it does not name (HLR-136).

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 16
from §8.
```

### Phase 16 — ELF-Filtered Analysis

1. `cli.c`: the `--elf` option, and its rejection alongside regeneration mode.
2. `elfsyms.c`: the image reader, the function-symbol set, and its ordering.
3. Linkage-name resolution — the published schemes, and the reduction of a
   decoded name to the identifier the report presents.
4. `analyze.c`: omitting a function the image does not define, and retaining
   file-scope ELOC as a figure of its own.
5. Both directions of mismatch carried into the report model, every format,
   and the saved record.
6. The `elf/` fixture group, with images the test builds rather than commits.

**Requirements:** HLR-140 – HLR-147.

**Acceptance:** `elc --elf build/app.elf src/` reports the metrics of the
functions that image defines and lists the source functions it does not, with
file-scope ELOC reported separately. `elc src/` reports byte-identically to a
build made before the option existed. A C++ image matches through Itanium
demangling. A stripped image, a file that is not an object file, and an absent
path each exit 2 with no report and their own diagnostic. `--elf` with
`--from-xml` is a usage error.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 16 — ELF-Filtered Analysis**, tracked by issue #<N>.

Read first: `doc/SDD.md` §18 (`elfsyms.c`), §4 (`cli.c`), §7 (`analyze.c`);
HLR-140 through HLR-147; LLR groups `LLR-ELF` and `LLR-SYM`.

This answers the question Phase 15 answers a different way. Conditional
compilation *re-decides* the conditions a build resolved; a linked image
*observes what the build did*. Where both are available the image is the
stronger evidence, and neither replaces the other — the image says which
functions survived and says nothing about which lines inside one were
compiled out.

Watch for:
* **The filter is applied once, in `analyze.c`.** A function the image does
  not define is never recorded, so the graph, the analyses, the thresholds
  and every renderer see a smaller set of functions and nothing else.
  Filtering at each consumer would put the same test in eleven places and
  let them disagree.
* **A symbol must be a defined function**, not merely a function: without
  the `SHN_UNDEF` test every function the image *calls* into a shared
  library counts as one the image contains, and the filter then retains
  source the build never compiled.
* **Raw name matching retains nothing outside C.** C++ and Rust both encode
  the source name. `__cxa_demangle` costs no new dependency — the C++
  runtime is already linked because igraph is partly C++ — and covers
  Itanium and Rust's legacy scheme. A language whose compiler uses a
  mangling this build cannot decode reports a large unresolved count and
  says so.
* **A decoded name is not yet a match.** Itanium yields
  `ns::C::f(int) const` and the report presents the identifier. Reduce both
  sides to one form, or every qualified name is a mismatch.
* **An empty function set is fatal, not an empty filter.** A stripped image
  would otherwise filter every function away and report a project with
  none — confidently wrong and indistinguishable from a correct result.
* **Both directions of mismatch are reported and they are different
  claims.** Unresolved linkage names state the completeness of the filter;
  source functions the image lacks are the finding the option exists to
  produce, and are dead code established by what the linker did rather than
  inferred from the call graph.
* **With no `--elf`, nothing changes.** Every existing fixture must report
  the numbers it does today; that is the cheapest regression test available
  and it is already written.
* **Never commit an image.** A binary in the repository is a fixture nobody
  can review. Build it in `$BATS_TEST_TMPDIR`, and skip explicitly where a
  compiler is unavailable.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 17
from §8.
```

### Phase 17 — Hardening and Release Readiness

**Status: complete.** Delivered in two halves, eight months apart in the
commit log and one issue apart in the tracker, which is itself the phase's
most useful finding.

The **engineering half** landed in PR #45 on 2026-08-21:
`test/instrumented/sanitized.bats`, the nine error paths a re-run of the
ordinary suites never reaches, and the decomposition pass that running `elc`
over `src/` called for. The Status row said "not started" throughout, which
is how Phase 23 came to open an issue describing the phase as untouched.

The **release half** closed it, and had to repair the drift the intervening
six phases produced.

1. Full sanitizer sweep across every fixture and target type, including every
   error path.
2. Teardown completeness — every `*_free`, every error path leak-clean.
3. `elc` analysed by `elc`: the self-quality check.
4. Coverage-gap closure; the review-verified residue recorded.
5. `test/instrumented/sanitized.bats` — the catalogued tests that record the
   sanitized gate having been applied and come back clean. Without these,
   HLR-124, HLR-125, and the memory-safety LLRs can never leave the gap list
   however diligently the sanitized build is run ([STP](STP.md) §2.5).
6. `main` created from `develop` for the first release, per §5.5.
7. `make install` verified against a staging root.

**Requirements:** HLR-124, HLR-125, HLR-181, plus any requirement still
lacking a bound test.

**Acceptance:** `make asan` and `make valgrind` both clean across the whole
suite, including runs ending in usage errors, invalid targets, and rejected
records. `Traceability.md` §6 lists nothing but the review-verified items the
STP names. Every requirement covering behaviour this release *ships* has a
bound test. `elc` on its own source reports no function exceeding complexity
15 and no dependency cycle. `make install` under a staging root produces a
working binary and runtime tree.

#### What the self-quality check cost, and why it is now a test

PR #45 brought `elc`'s own source under complexity 15. Phases 18 through 23
put **43 functions** back over it, eight from Phase 23 alone —
`manifest_write` and `build_proposal` at 30, twice the threshold this project
holds other people's code to. Nothing said so, because nothing was watching.

All 43 are decomposed, and **none survives over the threshold**: the most
complex function in `src/` is `collect_calls` at 14. The bar the phase set
itself was that a split must name a step the enclosing function was
sequencing, not merely reduce a number — this project would rather carry an
honest 16 than a dishonest 14. In the event no function needed the
concession, because the seams were already there and mostly already marked
by comments: `manifest_write` ordered the rows, built the document, and wrote
it out, and now calls three functions that each do one.

Two findings fell out of the pass that were not decomposition at all:

*   **A dependency cycle between `analyze.c` and `report.c`**, which `elc`
    reported at critical severity in its own source. `analyze.c` included
    `report.h` for one function, `component_directory`, and `report.c`
    already depends on `analyze.h`. The derivation moved to the module that
    builds the `FileMetrics` carrying it, and the dependency now runs one
    way.
*   **Five file-local statics sharing a name across modules** — two
    `by_line`, three `by_path`, one each of `render`, `rule` and
    `markdown_cell`. `elc` resolves a call by name, so each collision put an
    edge in its own graph pointing at the wrong module: a false claim in the
    very analysis the acyclicity result rests on.

The check is now `test/instrumented/self.bats`, and the reason it is a
catalogued test rather than a step someone performs is the reason
`sanitized.bats` exists (STP §2.5): a person running `elc src/` and finding
it clean proves nothing the traceability matrix can carry, and the evidence
decays the moment a phase adds a function. HLR-181 is the requirement it
verifies, added by this phase because the behaviour had none.

#### The gap baseline, from 128 to 0

The original text deferred the Phases 18–23 requirements to those phases. All
six have shipped, so what remained was sorted into the review-verified
residue the STP names and everything genuinely uncovered.

Almost every entry turned out to be the second kind only by construction. The
convention says black-box tests trace to HLRs and unit tests to LLRs — read
as a rule rather than a default, that leaves every LLR whose only
verification is at the fixture level in the gap list *because the thing that
checks it was forbidden to say so*. LLR-ANL-15 is "a `return` counts toward
ELOC"; the fixture test named `HLR-047: a return counts, with or without a
value` runs the delivered binary over a hand-counted fixture and asserts
exactly that.

STP §2.5 already carried the clause that resolves it — *a test that genuinely
verifies both may declare both* — and §2.5 now states the rule that governs
its use: **a test declares every requirement it genuinely verifies, and
nothing it merely passes through.** A fixture asserting a project total does
not verify the twenty LLRs that contributed to it, and adding those traces to
reach a number would empty the gap list while emptying it of meaning. Writing
a unit test that duplicates a fixture's assertion purely so an LLR may be
traced from the preferred level is the same evasion wearing a lab coat.

Eleven requirements had nothing checking them at all and now have tests.
**Two of those tests found defects on their first run**, which is the
argument for writing them:

*   An empty final table cell still emitted the column separator that would
    have preceded it, so every row with a blank Finding column ended in
    trailing whitespace — the exact thing leaving that column unpadded was
    for (LLR-SUM-04).
*   LLR-THR-11 read as forbidding any module from naming a published source,
    which HLR-159 *requires* of the renderer presenting Henry-Kafura. The
    requirement now distinguishes citing an authority for a line drawn from
    naming the authority a number is computed after.

One requirement was found stale rather than uncovered: LLR-ANL-29 said an
error node anywhere made the whole file a parse failure, which LLR-ANL-48
reversed several phases ago without amending it. It now states what survived
of it and records the reversal.

`Traceability.md` §6 gains a third section for the three items only review can
settle — HLR-101, HLR-111, and HLR-121's cross-release clause. They were never
gaps, and were never written down either, which left the distinction between
"verified by review" and "not verified" resting on nobody confusing the two.

#### On the Status row

The phase was complete in its engineering half for the whole of Phases 18
through 23 and its row said "not started". Two consequences followed: an issue
was opened describing work that existed, and the self-quality bar the phase
had met decayed unnoticed for six phases. §5.4 step 9 is the step that was
skipped, and it is worth stating plainly why it is a step at all — the Status
table is the only place a reader learns what has been done, and a reader
includes the person deciding what to do next.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 17 — Hardening and Release Readiness**, tracked by
issue #<N>. The last phase before the first release.

Read first: `doc/STP.md` §2 and §7, `doc/Traceability.md` §6 for the
outstanding gaps.

Deliver:
* `test/instrumented/sanitized.bats` — the catalogued tests recording that the
  sanitized gate ran clean. **Without these, HLR-124, HLR-125 and the
  memory-safety LLRs can never leave the gap list**, however diligently the
  sanitized build is run, because a re-run produces no catalogued test.
* A full ASan/UBSan/LeakSanitizer and valgrind sweep across every fixture,
  every target type, and **every error path** — usage errors, invalid
  targets, rejected records. HLR-125 covers error paths, so teardown cannot
  live only at the bottom of a successful pipeline.
* `elc` analysed by `elc`: no function above complexity 15, no dependency
  cycle — **as a catalogued test**, for the reason `sanitized.bats` is one.
  A check nothing runs decays silently, and this one did.
* Closure of every remaining coverage gap, and an explicit record of the
  review-verified residue the STP names (HLR-101, HLR-111, HLR-121's
  cross-release clause).
* `main` created from `develop`, and `make install` verified against a
  staging root — the binary, the runtime tree, and both user documents.

Decompose because a function is doing several things, not because a number is
too high. A function chopped to land under a threshold reads worse than the
one it replaced, and this project would rather carry an honest 16 than a
dishonest 14 — if any survive, say so in the phase's spec update and why.

This phase closes the **first release**, not the project. In place of step 12
of the protocol, open the release PR from `develop` to `main` and attach the
`Traceability.md` at that commit as the evidence of verification. Confirm
before doing so that the manual and man page describe the whole delivered
product, not merely the last phase's additions.
```

### Phase 18 — Output Format Selection and Report Verbosity

Specified 2026-08-20, after the first release. The three phases from here are
the first whose requirements were captured ahead of any design, so each opens
with more open questions than a phase written alongside its implementation —
step 6 of the protocol matters correspondingly more.

1. `cli.c`: the report format derived from the output filename's extension,
   the format option narrowed to standard output, and the usage errors for an
   unrecognised extension and for an extension contradicting an explicit
   format.
2. `report.c` / `format_text.c`: the summary and verbose compositions, and the
   one traversal that emits both — a tier must not be able to appear in one
   verbosity and be forgotten in the other, exactly as it must not across
   formats (LLR-SUM-02).
3. The verbosity carried into regeneration, so a record renders at either
   verbosity byte-identically to a direct run.
4. XML and CSV left complete whatever the verbosity, and a verbose request
   against them accepted rather than rejected.
5. The manual and man page: the format table restated around extensions, and
   the summary/detail partition published as HLR-150 requires.

**Requirements:** HLR-148 – HLR-152, and the amendments to HLR-027, HLR-031
and HLR-056.

**Acceptance:** `elc -o report.csv src/` writes CSV with no format option
given. `elc -o report.json src/` is a usage error naming the recognised
extensions. `elc -f csv -o report.md src/` is a usage error naming both.
`elc src/` prints the summary tiers alone; `elc --verbose src/` prints what
`elc src/` printed before this phase, byte for byte. `elc -f xml src/` and
`elc --verbose -f xml src/` produce identical bytes. A record regenerated at
either verbosity matches a direct run at that verbosity.

**Open questions for step 6.** Whether the format option survives at all is
settled here by HLR-149 — it does, scoped to standard output — but the
spelling of the verbosity option, and the exact assignment of each existing
report section to the summary or detail tier, are design decisions this phase
makes and records. HLR-150 fixes the partition *rule* and requires the result
be published; it deliberately does not enumerate the sections.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 18 — Output Format Selection and Report Verbosity**,
tracked by issue #<N>.

Read first: `doc/SDD.md` §4 (`cli.c`, and its "Option List as Single
Reference" section), §13 (`report.c`), and §14 (`format_text.c`, its
Algorithm section); HLR-148 through HLR-152; and the amendments those
requirements made to HLR-027, HLR-031 and HLR-056.

This phase changes a default that most of the suite depends on, and that is
the whole of its difficulty. HLR-151 defines the verbose report as *exactly*
what elc printed before this phase, which turns the existing suite into the
regression gate: capture every fixture's output before changing anything, and
require a `--verbose` run to reproduce it byte for byte afterwards. Do that
first rather than last — it is the only cheap proof that a presentation
change did not become a measurement change.

Watch for:
* **The fixtures assert on detail tiers the new default omits.** Most will
  need `--verbose`. Adding it mechanically is right where a test is about a
  *measurement*, and wrong where a test is about *composition* — those need
  a summary-mode counterpart instead, or HLR-150 ends the phase with no test
  bound to it and the gap list will say so.
* **Two invocations in the suite already break on the extension rule, and
  they are the shape to search for.** `test/integration/formats.bats` writes
  `out.table` for the table format: `.table` is not a recognised extension
  (HLR-148), and it does not agree with the `-f table` beside it (HLR-149).
  Audit every `-o` in the suite for both faults.
* **`-o report.md` now selects Markdown.** Several graph and dot fixtures
  name `report.md` while relying on the default table. Those change format
  silently rather than failing, which is worse than breaking — read what
  each one asserts before assuming it still holds.
* **Regeneration now has two format rules that can contradict.** HLR-055
  gives Markdown alone, and LLR-CLI-10 rejects only a format *explicitly*
  selected and other than Markdown — but an output path's extension is not
  an explicit selection. Decide what `--from-xml rec.xml -o out.txt` means,
  record it as an amendment to HLR-055 or LLR-CLI-10 under step 6, and do
  not leave it to fall out of whatever the parser happens to do.
* **One traversal, still.** LLR-SUM-02 and LLR-SUM-03 make the table and
  Markdown renderers share a single walk, so that a tier cannot be present
  in one and forgotten in the other. Verbosity must be a property *of* that
  walk rather than a second walk beside it, or a guarantee that currently
  holds by construction becomes two things that have to be kept agreeing.
* **`--verbose` with `-f xml` or `-f csv` is accepted, not rejected**
  (HLR-152). Every other option pairing this project defines is a usage
  error, so the analogy pulls the wrong way here: there is nothing
  contradictory about asking for detail from a format that is already
  complete, and the request simply has no effect.
* **The summary keeps the findings.** HLR-150 lists them among the summary
  tiers deliberately. A summary that dropped the one section a reader acts
  on would be shorter and useless.
* The verbosity option belongs in `cli_usage`, which is the reference the
  documentation test checks both documents against (LLR-DOC-04).

The gap baseline was raised to 175 when these requirements were specified
ahead of any design, and Phase 17 brought it to 166. The five HLRs this phase
closes must bring it below that; step 8 is not satisfied by a baseline that
merely holds.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 19 from §8.
```

### Phase 19 — Information-Flow Complexity

1. `graph.c` / `calltree.c`: per-function fan-in over the call view, counted
   distinctly and excluding global-state edges, as fan-out already is
   (LLR-CTR-07 applies unchanged to its converse).
2. The Henry–Kafura value per function, and its project-level sum, in an
   integer width no run can overflow.
3. `report.c`: both carried into the model, the per-function value in the
   detail tier and the project total among the summary totals of HLR-024.
4. `thresholds.c`: the metric reported as a bare measurement with no severity
   — the path LLR-THR-08 already provides, exercised here for the first time
   by a measurement that will never have a catalogue entry.
5. The `graph/` and `calltree/` fixture groups extended with hand-computed
   fan-in and Henry–Kafura values, including the zero cases at both ends of
   the call graph.
6. The manual and man page: the metric, its formula, and the two properties
   HLR-159 requires be stated — the zero at either end, and the ordinal
   rather than absolute reading.

**Requirements:** HLR-156 – HLR-159.

**Acceptance:** every function reports a fan-in, and a hand-computed
Henry–Kafura value matches the fixture for each. An entry point and a leaf
each report zero, and the report says why rather than leaving it to be read as
an absence of code. The project total equals the sum of the per-function
values. No Henry–Kafura row carries a severity, and the threshold-catalogue
test still finds exactly one row marked as `elc`'s own heuristic.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 19 — Information-Flow Complexity**, tracked by issue #<N>.

Read first: `doc/SDD.md` §8 (`graph.c`), §10 (`calltree.c`), §12
(`thresholds.c`) and §13 (`report.c`); HLR-156 through HLR-159; and PVD
Appendix A.2, which carries the formula and the two properties HLR-159
requires be stated wherever the metric is documented.

Fan-in is the only new *measurement* here; everything after it is arithmetic
over figures the graph already holds. The care goes into which edges it
counts and how wide the arithmetic is.

Watch for:
* **Fan-in is over the call view alone**, exactly as fan-out is
  (LLR-CTR-07). A global-state edge joins a function that writes an object
  to one that later reads it, and that is coupling rather than invocation.
  Taking in-degree over the whole SDG would inflate every fan-in — and the
  squared term would then inflate the Henry–Kafura value by the square of
  that error.
* **Widen before you multiply.** `(fan_in * fan_out)` evaluated in 32 bits
  and squared afterwards overflows long before the result needs a wider
  type. The widening has to happen before the multiplication, not at the
  assignment. HLR-158 requires that no run overflow, and a wrapped total
  renders as a perfectly ordinary number.
* **Zero is a value, not an absence.** A function with no callers or no
  callees scores zero and must print `0`. Instability — the metric this one
  will sit beside — prints `undefined` where its inputs are zero (HLR-082).
  Copying that pattern here would be wrong, and the two appearing in
  adjacent columns is what makes it an easy mistake to make.
* **No severity, and the catalogue stays honest.** The metric takes the path
  LLR-THR-08 already provides for a measurement the catalogue holds no entry
  for. Do not add a `Threshold` row for it. The existing test asserting that
  exactly one catalogue row is marked as elc's own heuristic is what catches
  an invented band, and it should keep passing untouched.
* **The duplicate-name artefact reaches this metric raised to a power.**
  Calls resolve by name, so a `static` helper defined in several files gives
  the winning definition every caller's fan-in and the losers none
  (SDD §8.5). That imprecision is already documented for reachability and
  for coupling; here the squared term magnifies it. Document it rather than
  leaving a reader to discover a four-order-of-magnitude figure built on it.
* **Tier placement was decided in Phase 18.** The per-function value is a
  detail tier and the project total belongs among the summary totals of
  HLR-024. That is an assignment the verbosity partition has to accommodate,
  not a fresh decision to make here.
* **The record carries both or regeneration loses them.** Neither figure can
  be recomputed from a record, for the reason the call-tree measurements
  cannot (LLR-XWR-08): regeneration has no graph, and no source to build one
  from.

Phase 18 left the gap baseline at 156; the four HLRs this phase closes must
bring it to 152 or below. *(It closed at 151.)*

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 20 from §8.
```

### Phase 20 — Debug-Line Pruning

1. `elfsyms.c`, or a module beside it: the image's debug line information read
   directly from the image, with no toolchain utility invoked (HLR-141).
2. The set of source lines a covered translation unit compiled, per file.
3. `analyze.c`: those lines joined to the excluded set the comment spans, the
   inactive regions and the absent functions already share, so that one
   mechanism governs every exclusion and none can remove a range twice
   (LLR-ANL-43, LLR-ANL-55, LLR-ANL-58).
4. Coverage established per file, and pruning confined to files the image's
   line information demonstrably covers.
5. Both counts of HLR-155 into the report model, every format, and the record.
6. The `elf/` fixture group extended with images built at more than one
   optimisation level and one translation unit compiled without debug
   information, so that the uncovered-file path is exercised rather than
   assumed.

**Requirements:** HLR-153 – HLR-155, and the amendment to HLR-141.

**Acceptance:** an image built with debug information prunes the lines a
`#ifdef` excluded from that build, and `elc` reports how many. An image built
without it reports the **same metrics** as the same image before this phase —
metrics rather than bytes, because HLR-155 adds two counts to the filter
section whether or not the image carried line information, and a section that
omitted the question would be a different claim from one answering it with two
zeroes. A translation unit compiled without debug information has no line
pruned and is counted among those whose coverage could not be established. No
file the image's line information does not cover loses a line.

**Open questions for step 6.** *(Both settled by the phase.)* `libdw` was
taken, beside the `libelf` already linked and from the same elfutils tree, so
it extends that documented distribution exception rather than opening a new
one; the dependency allowlist, `PKGS_BUILD` and `make check-prereqs` were
updated with it, and the low-level `dwarf_*` interface is used rather than the
`Dwfl` layer that would open a file the user never named — a distinction the
allowlist cannot see and an instrumented test holds. The optimiser's
line-folding (HLR-154) proves **material rather than marginal**: at `-O2` a
call folded to a constant leaves the whole callee body without line entries and
it is pruned entire. That is a true statement about what shipped, it is not
recoverable from the image, and the counts of HLR-155 are what make it
measurable — which is the outcome the requirement anticipated.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 20 — Debug-Line Pruning**, tracked by issue #<N>.

Read first: `doc/SDD.md` §18 (`elfsyms.c`) and §7 (`analyze.c`, its "One
Excluded Set, Not Two" section); HLR-153 through HLR-155; the amendment to
HLR-141; and the Section 19 introduction, which sets out the two
granularities an image answers at and which of them debug information adds.

This is the most dangerous phase specified so far, and the reason is that its
failure mode is silent. Over-pruning deletes code the build did compile and
leaves a report that is smaller, internally consistent, and wrong — the exact
outcome HLR-133 and HLR-138 are written to prevent in the two analyses that
came before it.

Watch for:
* **The DWARF reader must not open a file the user did not name.** HLR-141
  forbids it, and `test/instrumented/environment.bats` already asserts it —
  "the image is opened once and nothing beside it". A DWARF library will
  happily follow `.gnu_debuglink` into `/usr/lib/debug` given the chance.
  Configure it not to, or the requirement is breached by the library rather
  than by anything you wrote, and that test is what will tell you.
* **Absence of a line proves nothing where coverage was never established**
  (HLR-154). A translation unit compiled without `-g` contributes no line
  entries at all, so a rule keyed on absence alone would delete the entire
  file. Coverage is established per compilation unit, and must be
  established before a single line inside it is excluded.
* **The optimiser folds lines and the mapping does not record that it did.**
  A line whose instructions were merged into a neighbour's entry is
  indistinguishable, in the mapping alone, from one that produced none.
  HLR-154 accepts that limit and requires it be documented. Do not attempt
  to recover the difference, and do not let a fixture that happens to look
  right at `-O2` be read as evidence the case does not arise.
* **Join the exclusion set that already exists**, in the order LLR-ANL-58
  fixes: comments, then inactive regions, then the functions the image does
  not define — and these lines after all three. A line inside a function the
  image never defined must be reported as a function the linker dropped, not
  as a line the compiler did not emit. The two answers cite different
  evidence and imply different remedies.
* **Line granularity meets a byte-granular exclusion.** `byte_is_excluded`
  works in byte ranges and DWARF speaks in line numbers, so the conversion
  between them is yours to define — and a line's byte extent has to come
  from the mapped source rather than be assumed.
* **Code outside a function has to survive.** HLR-145 keeps file-scope ELOC
  and reports it separately, and file-scope data has few line entries to its
  name. A rule that pruned uncovered lines everywhere would delete precisely
  the figure HLR-145 exists to protect, which is why HLR-154 confines
  pruning to within functions the image defines.
* **Both counts, or the result is unfalsifiable.** HLR-155's pruned-line and
  uncovered-file counts are what let a reader tell a thoroughly pruned
  report from one where the evidence was mostly absent. They are the
  equivalent of the unresolved-call count and are read the same way.
* The `elf/` fixtures build their images rather than committing them
  (STP §6). Extend that to more than one optimisation level, and to a
  translation unit deliberately compiled without `-g`, or the
  uncovered-file path of HLR-154 ships untested.

Taking a DWARF library is the one dependency these three phases may add, and
it is a design decision under HLR-112. If `libdw` is chosen — beside the
`libelf` already linked — the instrumented dependency allowlist and
`make check-prereqs` both need it, and step 6 records the choice in the SDD's
dependency table.

Phase 19 left the gap baseline at 151; the three HLRs this phase closes must
bring it to 148 or below.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 21 from §8.
```

### Phase 21 — Architecture Conformance Measurement

Section 11 already detects both kinds of layering breach. This phase adds the
two things detection alone does not give a reader: an aggregate saying how
much of the code base conforms, and a matrix showing where the breaches sit.

1. `analyze.c` / `report.c`: the component directory recorded once and read
   everywhere, rather than re-derived from a path at each use (HLR-160).
2. `arch.c`: the Back-Call and Skip-Call Violation Indices, counted from the
   layering violations already recorded — not re-derived from the graph.
3. The undefined case for both, where no inter-layer call edge exists.
4. `format_dsm.c`: the matrix over declared layers, or over directories where
   none were declared, and its CSV and Markdown renderings.
5. The `arch/` fixture group extended with a hand-computed index and a
   hand-drawn matrix for a tree whose violations are known.

**Requirements:** HLR-160 – HLR-166, and the `arch.c` and `format_dsm.c`
sections of the SDD.

**Acceptance:** a tree with a known layering reports indices matching the
hand-computed values, and the matrix's below-diagonal cells account for
exactly the back-calls the layering section lists. A project with no
inter-layer call reports both indices `undefined` rather than 0 or 100%. A
run with no strata declared still produces a matrix, over directories. The
diagonal convention is printed with every rendering.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 21 — Architecture Conformance Measurement**, tracked by
issue #<N>.

Read first: `doc/SDD.md` §9 (`arch.c`) and §22 (`format_dsm.c`); HLR-160
through HLR-166; and HLR-079 and HLR-118, which are the violations these
indices count.

Nothing here is a new *detection*. Both indices count findings `arch.c`
already produces, and the matrix arranges edges the component projection
already holds. The work is aggregation and presentation, and the hazards are
all in the denominators and the ordering.

Watch for:
* **Count the findings, do not re-derive them** (HLR-164). A second code
  path deciding what a back-call is will eventually disagree with the first
  — over a call touching an unpartitioned component, over a call that both
  skips and inverts, over an edge collapsed from several call sites — and a
  percentage contradicted by the table printed beside it is worse than no
  percentage at all.
* **The denominator is inter-layer call edges, and it excludes three things
  people forget.** Intra-layer edges have no direction to invert; edges
  touching a component outside every declared stratum are excluded by
  HLR-161 and LLR-LAY-05; and global-state edges are not calls (LLR-CTR-07).
* **Zero denominator is `undefined`, not zero** (HLR-162). A project with no
  inter-layer calls has demonstrated nothing either way, and reporting 100%
  conformance for it would be the same error HLR-082 avoids by reporting an
  undefined Instability rather than a stable 0.00.
* **One call can be both a skip and an inversion** and is counted once in
  each index (LLR-LAY-04). Do not sum the two indices into a single score —
  it would double-count exactly the calls that most need attention.
* **The matrix orientation is load-bearing and must be printed.** Rows are
  callers, columns callees, both in ascending layer order, so back-calls
  gather below the diagonal (HLR-166). A reader who has to infer which way
  round it is gets the opposite answer half the time.
* **Escaping is not this renderer's own.** The CSV cells go through the same
  `write_field` the per-function renderer uses, and the Markdown rendering
  must escape the cell separator — a directory containing a comma or a pipe
  would otherwise corrupt the grid (HLR-064).
* **The matrix appears without strata.** Falling back to directories is what
  makes it useful to the reader who has declared nothing, which is most
  readers on a first run.

The gap baseline is re-derived from the figure this phase inherits rather
than projected forward from 175: read `test/gap-baseline.txt`, and the seven
HLRs this phase closes must bring it seven lower. Phase 20 left it at 148.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 22 from §8.
```

### Phase 22 — Graph Purification

A raw call graph rarely sorts into layers: a logger everything calls and a
dispatcher that calls everything each fuse regions of a program that have
nothing to do with one another. This phase builds the masked view that lets
the structure underneath be seen, and reports every assumption it made in
doing so. It recovers nothing — that is Phase 23.

1. `purify.c`: hub-and-authority, betweenness, and coreness over the call
   view, via `igraph`.
2. Classification of utility sinks, god objects, and peripheral nodes
   against configurable thresholds, compared by rank rather than by raw
   score.
3. The recovery view built as a **copy**, with the graph every other stage
   reads left untouched (HLR-167).
4. The transparency report: node, class, triggering metric and value, and
   action taken — as a report section, not as raw `stdout`.
5. Deterministic classification: a stated floating-point comparison, and
   ties broken by stable node identifier.
6. A fixture tree built around a known utility sink and a known dispatcher,
   with the classification hand-worked in its header.

**Requirements:** HLR-167 – HLR-171, HLR-174, HLR-179, and SDD §20.

**Acceptance:** every metric `elc` reported before this phase is byte-identical
after it, on every fixture — masking reaches nothing outside the recovery
view. The fixture's planted utility sink and dispatcher are classified as
such, and the report names the metric and value that triggered each. Two runs
over the same tree classify identically. No classification carries a severity.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 22 — Graph Purification**, tracked by issue #<N>.

Read first: `doc/SDD.md` §20 (`purify.c`, its interface sections and its
Algorithm section) and §8 (`graph.c`); HLR-167 through HLR-171, HLR-174 and
HLR-179; and HLR-101, which bounds what this phase is allowed to claim.

This is the first place elc forms a view of its own about a code base, and
the requirements are shaped almost entirely around containing that. Read
HLR-167 before writing anything: it is the constraint the rest of the phase
is built on.

Watch for:
* **The recovery view is a copy, and that is structural rather than
  disciplinary** (HLR-167). Mask into a second graph; never mask the `Sdg`
  and unmask afterwards. The in-place version is one early return away from
  reporting a fan-out that omits real calls, and every existing fixture is
  the regression test — if any reported number moves, the phase is wrong.
* **Rank, don't threshold on the raw score.** Betweenness scales with graph
  size, so a fixed cut-off classifies everything in a large project and
  nothing in a small one. Compare against position in the ordered
  distribution, which is what makes one default serviceable for both.
* **HITS is iterative and its scores are approximate.** A comparison at the
  boundary must be made to a stated tolerance, or the same source classifies
  differently on two machines and HLR-032 fails in a way no fixture reliably
  catches. Ties break by node identifier, never by igraph's enumeration
  order (HLR-179).
* **A utility sink loses its incoming edges only; a god object loses both
  directions** (HLR-168, HLR-169). The asymmetry is the point: a sink's
  fusion is between its callers, so its outgoing edges harm nothing.
* **A peripheral node is excluded, not placed** (HLR-170). It gets no
  recovered layer at all — a function elc did not consider is not a function
  elc put at the bottom, and conflating the two would drop every leaf into
  the lowest layer.
* **No severity, no finding, no advice** (HLR-171, HLR-101). "God object" is
  an observation about a graph's shape, not a measurement banded against a
  published range. The existing test asserting exactly one catalogue row is
  marked as elc's own heuristic should still pass untouched.
* **The transparency report goes to the results destination, not to
  `stdout` directly** (HLR-174). HLR-038 reserves that stream: a run
  redirecting its report to a file must not have a second report appear on
  the terminal.
* `igraph`'s error handler is already installed non-aborting (LLR-SDG-15);
  every new call into it needs its return checked in the same way.

The gap baseline is re-derived from the figure this phase inherits rather
than projected forward from 149: read `test/gap-baseline.txt`, and the seven
HLRs this phase closes must bring it seven lower. Phase 21 left it at 141.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 23 from §8.
```

### Phase 23 — Architecture Recovery and the Manifest

The payoff phase: a layering read off the purified view, a manifest by which
a user overrules a classification they disagree with, and the two drawings
that let them see what purification did before they trust it.

1. `recover.c`: topological ordering of the recovery view, folded into
   per-directory layers by edge density, with cycles reported where no
   ordering exists.
2. The proposal emitted in the form `--stratum` and `--stratum-order`
   accept, so adopting it needs no transcription (HLR-173).
3. `purify.c`: the manifest written on request and read only when named,
   with a manual classification overruling a computed one and the report
   distinguishing the two. Jansson joins the build here — the only
   third-party library `elc` uses to *write* a format, and the reason is
   argued in [SDD](SDD.md) §22.
4. `format_graph.c`: the raw and purified `.dot` exports, masked nodes drawn
   detached rather than deleted, named by the companion rule of HLR-119.
5. A fixture asserting that a manifest overriding a classification changes
   the recovered layering, and that a manifest naming an unknown function is
   reported and ignored.

**Requirements:** HLR-172, HLR-173, HLR-175 – HLR-178, and SDD §20 – §21.

**Acceptance:** a fixture tree with a plain layered structure recovers that
structure. A tree whose recovery view is cyclic reports the cycles instead of
a layering. The emitted proposal, passed back as arguments, is accepted by
`cli_parse` unmodified. A manifest marking the planted dispatcher
`mask = false` changes the recovered layering, and the report attributes that
classification to the manifest. A manifest that is not named is not read,
including one sitting in the working directory.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 23 — Architecture Recovery and the Manifest**, tracked by
issue #<N>.

Read first: `doc/SDD.md` §21 (`recover.c`) and §20 (`purify.c`, its manifest
interface); HLR-172, HLR-173, HLR-175 through HLR-178; and the amendments
those requirements made to HLR-078, HLR-101 and HLR-119.

The whole phase turns on one boundary, drawn by HLR-173: what this produces
is a *proposal*, and a proposal is never the baseline it is measured against.
Everything else follows from keeping that true.

Watch for:
* **`recover.c` must not be reachable from `arch.c`.** The conformance
  analyses take their baseline from the declared strata and from nothing
  else (HLR-173). Enforce it with the dependency direction rather than with
  care: if the recovery results are not visible to the module that measures
  conformance, elc cannot mark its own homework even by mistake.
* **With no strata declared, conformance stays omitted** however confidently
  a layering was recovered (HLR-115, HLR-173). Recovery is what that user
  gets *instead*, not a substitute baseline.
* **A topological order is not a layering.** It orders functions; an
  architecture orders directories. Fold by directory using where the bulk of
  a directory's edges point — one function reaching far down the order must
  not drag its whole directory with it.
* **Emit the proposal as arguments, not as prose** (HLR-173). Rendering it
  in the form `--stratum` and `--stratum-order` accept is what makes
  adoption a copy rather than a transcription, and it is the boundary made
  visible: elc produces an argument list, and it takes effect only when the
  user passes it back.
* **The manifest is read only when named** (HLR-176). Never from the working
  directory, the target, an ancestor, or a dotfile — HLR-039 is unchanged by
  this phase, and the `determinism/` fixture group already plants decoy
  dotfiles that will catch a violation.
* **A malformed manifest is fatal; an unknown function in a valid one is
  not** (HLR-176, HLR-177). The first is a file the user named and can fix;
  the second is ordinary use when analysing one directory of a larger
  project, exactly as a declared entry point that matches nothing is
  (LLR-CTR-08).
* **The report must say which classifications came from the manifest**
  (HLR-177), or a reader cannot tell the tool's assumptions from their own
  team's.
* **Jansson is a new linked dependency and three places record that.**
  `make prereqs` builds it from a pinned release like the rest, `make
  check-prereqs` reports its version against the ≥ 2.14 minimum, and the
  instrumented dependency allowlist — a fixed list by design (LLR-BLD-05) —
  gains exactly one entry. The allowlist test fails until it does, which is
  the intended order.
* **Well-formed is not valid.** Jansson tells you the file is JSON; whether
  it is a *manifest* — every class name known, the version one this build
  reads — is this module's judgement, and both failures are rejected the
  same way (HLR-176).
* **Both drawings follow the companion rule** (HLR-119, HLR-178): names
  derived from the output path, no path of their own, and nothing written
  when the report goes to standard output. Masked nodes are drawn *detached
  or greyed*, never deleted — a drawing that removed them could not show
  what purification did, which is the entire reason there are two.

The gap baseline is re-derived from the figure this phase inherits rather
than projected forward: read `test/gap-baseline.txt`, and the six HLRs this
phase closes must bring it six lower. Phase 22 left it at 134.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close by opening the issue for Phase 24 from §8.
```

### Phase 24 — Report Composition and the Banded Function Table

The report grew one section per analysis and reads in the order it was built.
This phase re-orders it for the reader, folds the three per-function tables
into one, withdraws the one metric that was reported without a band, bands the
two measurements that should have had one, and stops printing tables with
nothing in them.

1. `format_text.c`: the findings move to second place, immediately after the
   project summary. Everything after `Files` is re-ordered — component
   coupling, component dependency cycles, the threshold listing, the function
   table, the deepest call chain, recursion — so a reader descends from the
   component to the function rather than climbing back out of it.
2. `Functions`, `Fan-out (distinct callees)` and `Information flow` become one
   table carrying File, Function, Lines, ELOC, Complexity, Fan-in and Fan-out
   (HLR-183). Three tables enumerating the same functions in the same order
   were three chances to disagree about which functions exist.
3. Henry–Kafura is withdrawn per function and per project. HLR-157, HLR-158
   and HLR-159 are retired, `calltree.c` stops computing it, the `hk`
   attribute leaves the XML record, and `ELC_XML_FORMAT_VERSION` becomes 2 —
   an element was removed, which is exactly what that constant counts
   (HLR-061).
4. `thresholds.c` gains two catalogue rows: cyclomatic complexity, warning
   above 10 and critical above 15 on McCabe's authority as NIST SP 500-235
   records it; and fan-in, warning above 25 on `elc`'s own, carrying the
   `ELC_OWN_HEURISTIC` label the bottleneck row carries (HLR-185, HLR-186).
5. The threshold listing becomes the listing of every function a band names —
   complexity, fan-in, or fan-out — united with the functions at or over the
   configured complexity threshold, which `--complexity-threshold` still governs
   (HLR-187).
6. A table with no rows is not printed, and a closing statement names the ones
   that were empty (HLR-188, HLR-189). The statement names them by their full
   heading, so a section omitted for want of a user declaration still carries
   its reason where HLR-115 requires one.
7. Every Markdown table is folded behind an HTML `<details>` whose `<summary>`
   states its row count, beneath a heading that stays a heading so a section
   keeps its anchor (HLR-190).
8. `calltree.c` gains the **Adapted Maintainability Index** — Coleman and
   Oman's, with the information flow through a function substituted for its
   Halstead Volume — reported as a column of the function table and banded
   downwards at 65 and 55 (HLR-191, HLR-192). The bands are `elc`'s own and
   labelled as such: a citation is not transitive, and the Software
   Engineering Institute's published thresholds were calibrated against the
   term this adaptation replaces. Applied unchanged they band four functions
   in five, which is the measurement no longer discriminating.

9. `dwarfline.c` gains a map of which source file the image's debug
   information places each function in, and the `--elf` filter matches on
   name *and* file where it can. Where it cannot, and two analysed files
   define a name the image keeps, the run is refused rather than guessed at
   (HLR-193).

10. `diag.c`: every message to standard error passes through one stream, and
    `--dbg` writes a debug companion recording the run beside the report —
    the invocation, every diagnostic, and the source of every region the
    grammar could not parse (HLR-194, HLR-195). It is the one module holding
    global mutable state, and SDD §25 argues the exception rather than
    assuming it.

Deliverables 7 through 10 landed after the first pull request merged, and are
recorded here rather than as a phase of their own because their subject is
this phase's: what the report presents, and on whose authority.

**Requirements:** HLR-182 – HLR-195, amendments to HLR-021, HLR-031, HLR-061,
HLR-086, HLR-098, HLR-150 and HLR-151, and the retirement of HLR-157 –
HLR-159.

**Acceptance:** `elc src/` prints the summary, then the findings, then the
sections in the order above. No report in any format reports a Henry–Kafura
figure, while the fan-out band keeps its Henry–Kafura attribution. A record
this build writes is rejected by a build reading version 1, and the converse.
A function of complexity 11 warns, one of 16 is critical, one with a fan-in of
26 warns, and each appears in the threshold listing. A fixture with no
recursion prints no `Recursion` table and names it in the closing statement.
`make test`, `make asan` and `make valgrind` are clean, `lint_project.py`
reports 0 errors, and `test/gap-baseline.txt` is still 0.

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 24 — Report Composition and the Banded Function Table**,
tracked by issue #<N>.

Read first: `doc/SDD.md` §13 (`report.c`), §14 (`format_text.c`) and §12
(`thresholds.c`); HLR-182 through HLR-189; and PVD Appendix A, which this
phase edits — the Henry–Kafura subsection goes from A.2, a fan-in band takes
its place, and a new A.3 carries the complexity bands.

Watch for:
* **A retired identifier is never reused.** HLR-157 through HLR-159 leave
  `Project.xml` along with the LLRs and tests that traced to them, and the
  numbers stay gone — `HLR-042` is the precedent.
* **The two new bands are not the same kind of claim.** Complexity is
  published and cited; fan-in is `elc`'s own and must carry
  `ELC_OWN_HEURISTIC` wherever it is reported (HLR-099). Presenting an
  invented band beside MISRA and Martin without saying so is the one thing
  the catalogue exists to prevent — and it is what withdrawing Henry–Kafura
  is *for*, so do not trade one unlabelled opinion for another.
* **Every sort still lives in `report.c`.** The combined function table is
  one table over the model's own file and function order; do not sort in the
  renderer to get the columns to line up.
* **The function table must list every function**, whether or not the graph
  has a node for it. Drive it from the file metrics and attach the flow
  figures to them, rather than driving it from the flow rows — a run whose
  graph was not built would otherwise report no functions at all.
* **Both paths that assemble a model must attach the flow figures**: the live
  run and the regeneration in `format_xml.c`. The threshold listing is
  rebuilt after they are attached, or it is a complexity listing wearing the
  new heading.
* **An omitted section is not an empty one.** A table omitted for want of a
  `--stratum` or `--scope` declaration still has to state its reason
  (HLR-115); naming it by its full heading in the closing statement is what
  keeps that true once the heading itself is no longer printed.
* **The record version is a removal counter.** Bump it, and check that both
  directions are rejected — a new build reading an old record and an old
  build reading a new one.

The gap baseline is 0 and must stay 0: every new HLR needs an LLR and a
catalogued test in this same change, and every test that traced to a retired
requirement is removed with it.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. No further phase is specified: in place of step 12, cut a
release per §5.5, or open the issue for whatever is specified after this one.
```

### Phase 25 — Repairing What the Grammar Could Not Follow

Repairs source the grammar rejected by rewriting the rejected regions in
`elc`'s own buffer — an upper-case token beside a string literal becomes `""`,
one in front of a declaration is blanked, one standing where a declarator
belongs is given a type. On a 44-file AVR project, 64 error regions fell to 8
and unparsed lines 47 to 5, with the file count, function count, physical lines
and ELOC every one unchanged.

**Withdrawn in favour of [Phase 27](#phase-27--preprocessor-macro-expansion--ast-sanitization), and restored beneath it by
[Phase 28](#phase-28--repair-where-expansion-cannot-reach).** The withdrawal
reasoned that a tool which can expand a macro properly has no business guessing
at its shape. That was right about the two mechanisms and wrong about the
choice between them — see Phase 28 for the measurements that settled it.

### Phase 26 — Placing Templated Names by Debug Information

`elc --elf` refuses a run it has everything it needs to complete. Given a C++
image built **with `-g`** and a function-template name defined in two analysed
headers, HLR-193's ambiguity check fails the run and prints:

```text
elc: serialize_seq is defined in .../micro/plugin.hpp and .../pro/plugin.hpp,
and .../app carries no debug information placing it; rebuild the image with -g
so the filter can tell them apart
```

Every clause after the comma is false. Walking the image with the same `libdw`
calls `elc` makes returns **135 compilation units and 2167 usable
subprograms**, and the function comes back complete — `DW_AT_low_pc` set and
`DW_AT_decl_file` naming exactly one of the two headers. DWARF holds precisely
the answer HLR-193 asks for, and the run fails anyway.

The miss is on the **spelling of the name**, not on the absence of data.
`dwarf_diename` returns the instantiated name, `serialize_seq<int>`; the
origin map is keyed on that, while every lookup passes the bare declarator
parsed from the header. The comparison is `strcmp`, so `<int>` is the whole of
the failure.

**`elc` already solves this one layer away.** `elfsyms_defines` reconciles a
source name with an image symbol through `reduce_to_identifier` (HLR-142,
LLR-SYM-03), whose `without_template_args` strips exactly this suffix — and
strips it safely, acting only on a name ending in `>`, so `operator<`,
`operator<<` and `operator<=` pass through untouched. The DWARF origin map is
the one name-matching path in the tool that does not go through that
reduction. This phase is mostly the removal of that inconsistency.

1. **One name, one spelling, on every path that compares names** (HLR-200).
   Key the origin map and the lookups into it through the same reduction that
   already reconciles source names with image symbols. A tool that normalises
   names for one comparison and not for another will disagree with itself, and
   the disagreement surfaces as a confident false statement rather than as an
   error.
2. **A diagnostic states the condition it observed** (HLR-201). *The image
   carries no debug information* and *the debug information has no entry under
   this name* are different conditions with different remedies, and only the
   first is answered by rebuilding with `-g`. Advice that cannot work is worse
   than none: it sends a reader to rebuild an image that was already correct,
   and when that changes nothing there is nowhere left to look.

**Requirements:** HLR-200, HLR-201, and an amendment to HLR-193 stating that
the names it compares are the reduced forms.

**Nothing here loosens the rule.** A name the debug information genuinely
cannot place, in a tree where two files define it, still fails the run and
still produces no report — HLR-193's reasoning is untouched. What changes is
that the tool now reaches that conclusion only when it is true.

**Acceptance:** a C++ image built with `-g`, whose templated function name is
defined in two analysed headers, filters to the definition DWARF places rather
than failing. `operator<`, `operator<<`, `operator>` and `operator>>` survive
the reduction unchanged. An image genuinely built without `-g` still fails and
still names `-g` as the remedy; an image whose debug information simply lacks
the name fails with a diagnostic describing *that* condition. Verified against
a fixture image built by the test, not against a recorded string.

```text
AI prompt — Phase 26

Read issue #68 and HLR-193, HLR-142 and LLR-SYM-03 before touching code.

The bug is a name-spelling mismatch, not a missing-data condition. Confirm
that for yourself first: build a two-header C++ reproduction, compile it with
-g, and watch elc claim the image carries no debug information. Then walk the
image with libdw and find the entry it claims is absent.

Two changes, and resist making a third:

* Reduce DWARF names the same way image symbols are already reduced, so the
  origin map is keyed and searched in one spelling. Reuse
  `reduce_to_identifier` rather than writing a second reduction — a name
  normalised two ways is the defect this phase exists to remove, and adding
  another normaliser reintroduces it in a new place.
* Split the diagnostic so it distinguishes an image with no DWARF from DWARF
  with no entry for the name. Check the condition; do not infer it from the
  failed lookup.

Do not widen HLR-193. A genuinely unplaceable ambiguous name must still fail
the run and produce no report. The `operator<` family is the trap in the
reduction — cover it with a test whatever your reading of the code says.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push.
```

### Phase 27 — Preprocessor Macro Expansion & AST Sanitization

Tree-sitter parses source the preprocessor has not touched, so a macro standing
where the grammar expects a keyword, a type, or a string is a parse error.
[Phase 25](#phase-25--repairing-what-the-grammar-could-not-follow-withdrawn)
guessed at the shape of those failures and repaired them in place. It worked on
the shapes it knew and could never work on the ones it did not, because it
never knew what a macro expanded to.

**This phase asks the compiler.** `gcc -E` (or `g++ -E`) expands the macros and
`elc` parses the result — which is the only way to be right about a macro
rather than lucky.

The reason this was not done years ago is that raw preprocessor output is
unusable. Measured on a 19-line C file: `gcc -E` returned **829 lines**, and the
functions of every header it pulled in — `clamp`, `scale` — appeared as
functions of the file under analysis. Every reported line number moved. A tool
that measured that output would report confident figures about a program nobody
wrote.

**The `#line` markers are what make it affordable.** The preprocessor states,
for every run of lines it emits, which file and which line they came from:

```text
# 1 "app.c"
# 1 "/usr/include/stdio.h" 1 3 4
```

That is enough to discard everything the project did not write and to restore
the line numbering of what it did.

1. `preproc.c`: run the preprocessor on one file, capture `stdout` into memory
   (never to disk), and return the filtered buffer (HLR-202). No intermediate
   file means nothing to clean up, nothing to collide under parallel runs, and
   nothing left behind by a killed process — which HLR-043 requires.
2. **A `#line` state machine over the output** (HLR-203). Each marker switches
   the filter between `APPENDING` and `IGNORING` by the file it names: the
   target file appends, `/usr/include/...`, `<built-in>`, `<command-line>` and
   every path outside the project ignore. This is what keeps `<iostream>`'s
   50,000 lines out of the buffer, and what stops a header's functions being
   attributed to whichever file included it.
3. **Line numbering is restored, not merely preserved** (HLR-204). Where a
   marker says the next line is line 400 of the target and the filter has
   emitted 120, it emits 280 newlines before continuing. Every figure `elc`
   reports is line-based, so without this the whole report points at the wrong
   places — and blank lines cost nothing, since ELOC counts neither them nor
   the comments `-C` preserves.
4. **A failure falls back to the raw file** (HLR-205). No compiler installed, a
   header that cannot be found, a cross-compiled tree the host toolchain cannot
   preprocess at all — each yields the unexpanded source and the parse `elc`
   would have done anyway. The tool gets better where a toolchain is available
   and no worse where it is not.
5. **The report says which run it was** (HLR-206), per file: expanded, or
   fallen back and why. A figure obtained two ways, with no way to tell which,
   is the confidently-wrong result the tool exists to avoid.
6. **C library use outside MISRA's constraints is warned about** (HLR-207).
   MISRA C:2012 §21 names the facilities a compliant program does not use, and
   `elc` reports each call to one — with the file, the line, and the rule
   number. By **function** and never by header: `<stdlib.h>` supplies `abs`,
   which MISRA permits, beside `malloc`, which it does not, so flagging the
   include would be a false claim about code that called neither. The headers
   each file drew on are reported alongside, as the exposure those warnings are
   drawn from.
7. **Conditional regions are answered before expansion** (HLR-208). A
   preprocessor reads an undefined identifier in an `#if` as zero and discards
   the branch it did not take — a silent guess, where HLR-133 requires the
   guess be declared and both branches kept. So the unexpanded text answers
   what the source *says*, the expanded text measures what it *means*, and a
   file `elc` could not fully decide is not expanded at all.

**Requirements:** HLR-202 – HLR-208, and amendments to **HLR-135** and
**HLR-141**, which today forbid invoking a toolchain outright, and to
**HLR-076**, whose single parse becomes two for an expanded file.

**Those two amendments are the real cost of this phase and must be argued, not
slipped through.** `elc` promised that its output depended on the source and
the command line and on nothing else. After this phase, a file that includes a
system header can measure differently on two machines with different libc
versions. The promise narrows to: *the same source, preprocessed by the same
toolchain, yields the same report* — and the per-file provenance of HLR-206 is
what keeps the narrowing visible rather than silent. HLR-039 (zero
configuration) is untouched: no file is discovered, and the compiler is chosen
by the command line or by a fixed default.

**Acceptance:** a macro standing in for a storage class, a string literal, or a
whole declarator parses and measures what the hand-expanded equivalent
measures. A file including `<stdio.h>` or `<iostream>` gains no function,
no line, and no ELOC from it, and reports the dependence. Every function's
reported line range is identical to its range in the unexpanded file. A run
with the preprocessor disabled, absent, or failing produces exactly the report
this build produces today. Two runs over one target agree (HLR-032, HLR-033),
and no intermediate file is written (HLR-043).

```text
AI prompt — Phase 27

Read issue #70, then HLR-135 and HLR-141 before writing a line of code: this
phase contradicts both as they stand, and amending them is part of the work
rather than a consequence of it.

Build the filter before the subprocess. The state machine over `#line` markers
is the whole phase — the `popen` call is a dozen lines and the filter is what
decides whether the output is measurable. Test it against captured
preprocessor output as a string, not by running gcc.

Three things that will be wrong if you do not plan for them:

* **Line numbering.** Filtering alone leaves every line number wrong. Pad with
  newlines to the line the marker names. Assert a function's reported range
  against the unexpanded file, or this ships broken and looks fine.
* **Comments.** `gcc -E` strips them and elc measures them. Pass `-C`.
* **The fallback is the common case, not the error case.** Cross-compiled
  trees cannot be preprocessed by a host compiler at all — the AVR project
  that prompted Phase 25 fails on `avr/io.h` before it reaches a macro. Make
  falling back ordinary, quiet, and recorded per file.

Do not let a header's content reach the report. A function attributed to the
file that included it is worse than the parse error this phase removes.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push.
```

### Phase 28 — Repair Where Expansion Cannot Reach

[Phase 27](#phase-27--preprocessor-macro-expansion--ast-sanitization) replaced
[Phase 25](#phase-25--repairing-what-the-grammar-could-not-follow) on the
reasoning that a tool which can expand a macro properly has no business
guessing at its shape. Measured against the project both were built for, that
reasoning does not survive:

| build | unparsed lines | functions |
|---|---|---|
| Phase 25 — repair only | 9 | 541 |
| Phase 27 — expansion, host `gcc` | **50** | 541 |
| Phase 27 — expansion, `avr-gcc` + every `-I` + `-mmcu` | **50** | 541 |
| Phase 27 — with the undecidable guard lifted | 45 | **531** |
| Phase 28 — both | **8** | 541 |

Expansion is exact and it is *conditional*. It needs a toolchain, the build's
include paths, and conditions `elc` can decide — and on a 49-file AVR project
those hold for **three files**. Of the remaining 46, every one falls back:
under a host compiler because `avr/io.h` cannot be found, and under `avr-gcc`
with every flag supplied because HLR-208 refuses to expand a file whose `#if`
`elc` could not decide. Lifting that guard does not rescue it either — the
preprocessor then resolves the conditions itself and **ten functions
disappear** with the branches it discarded.

So the two are not rivals. Expansion answers exactly and reaches little;
repair guesses and reaches everything. Where the first applies the second is
unnecessary; where it does not, the second is all there is.

1. Restore `repair.c`, its requirements HLR-196 – HLR-199, its design and its
   tests, unchanged but for their scope.
2. **Repair runs only where expansion did not** (HLR-196 as amended). A file
   whose macros were resolved exactly is not then guessed at, and the two
   paths are exclusive per file rather than layered.
3. The report already says which path each file took (HLR-206); the repair
   tally of HLR-199 says what was done about it.

**Requirements:** HLR-196 – HLR-199 restored, HLR-196 amended to name the
condition, and no new requirement. The withdrawal was a mistake about scope,
not about the requirements themselves.

**Acceptance:** on the AVR project, unparsed lines fall from 50 to single
figures with the function count unchanged. A file the preprocessor expanded
carries no repairs. One file measured both ways — expanded, and repaired under
`--no-expand` — reports the same functions with the same figures.

```text
AI prompt — Phase 28

The withdrawal of Phase 25 was a scope error, so this is a restoration and not
a redesign. Take src/repair.c and its tests back from the merge that removed
them and change only what the new position requires.

The one thing to get right is that repair is the *fallback*: it runs where
`preproc_expand` produced nothing, and never on text the preprocessor already
resolved. Guessing at the shape of a failure that no longer exists could only
make it worse.

The repair fixtures must run under --no-expand, and that is the point rather
than a convenience — the fixture tree is ordinary C that a host compiler
expands happily, so a test that let expansion run would be testing expansion.

Assert the two paths agree. One file measured both ways must report the same
functions with the same figures; without that the fallback is merely better
than nothing rather than trustworthy.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push.
```

### Phase 29 — Function Visibility and Editor-Navigable Locations

Three changes to one table, and the third pays for the second.

**A reader of the Functions table cannot tell a file's interface from its
internals.** Every function is listed the same way, so a header's three exported
entry points sit indistinguishable among forty file-local helpers. That
distinction is the first thing anyone asks of an unfamiliar module, and the
source states it plainly — `static`, `pub`, a leading underscore — so `elc`
reads it rather than leaving the reader to.

**And a reader who wants to look at one cannot get there.** The path is in one
column and the line range in another, and neither is something an editor will
act on. `path:line` is, and VS Code's terminal turns it into a jump.

That leaves the line range with nothing to do, since its start has moved into
the location. It becomes the figure a reader actually compares between
functions: **how many lines this one occupies**.

1. **Visibility, from the language's own rule** (HLR-209). An optional
   per-language `visibility.scm`, resolved by the first pattern that matches —
   the rule `collect_inactive_regions` already follows for conditional regions.
   The basis differs by language and is stated rather than assumed uniform: C
   and C++ report linkage, Rust reports its `pub` keyword, Python reports the
   leading-underscore convention. **A language whose module supplies no rule
   reports neither**, in the way HLR-138 already refuses to claim "no dead code"
   for a language it cannot analyse.
2. **The location as an editor-navigable reference** (HLR-210). `path:line` in
   the File column, where `line` is the function's first line. Nothing is lost:
   the range is the start plus the count beside it.
3. **The Lines column as a count** (HLR-014 amended). `end − start + 1`, which
   is the figure a reader compares between two functions — where a range is a
   fact about the file that they had to subtract to use.

**The complete-record formats are unaffected.** CSV and XML keep separate start
and end fields: they are parsed by consumers rather than read, a consumer
cannot subtract a column it was not given, and the XML round-trip of HLR-056
would break. This phase is about the human-readable report (HLR-027, HLR-029)
and about nothing else.

**Acceptance:** a `static` C function reports `private` and a non-static one
`public`; the same for C++ `static` and anonymous namespaces, Rust `pub`, and
Python's leading underscore. A language with no visibility query reports
neither and says so. The File column reads `path:line` with the function's
first line, and the Lines column equals `end − start + 1`, both hand-checked
against a fixture. The XML round-trip is byte-identical to today's.

```text
AI prompt — Phase 29

Read issue #74 and HLR-014 before writing code.

The visibility rule is language-specific and belongs in a query file, not in
C. Put it in runtime/queries/<lang>/visibility.scm and resolve overlapping
matches by the earliest pattern — `collect_inactive_regions` already does
this, for the same reason, and a second convention would be one too many.

Three states, not two. A language with no visibility query must report
neither public nor private: "not analysed" and "public" are different claims,
and HLR-138 already draws that line for dead code.

The table is parsed positionally by about seventeen test helpers. Inserting a
column shifts every field after it. Fix them; do not add the column at the end
to avoid the work, because the requirement is about where a reader's eye
lands.

Leave CSV and XML alone. They are complete records for consumers, not
tables for readers, and the round-trip test will tell you if you forget.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push.
```

### Phase 30 — Deciding Conditionals from the Build, and Recovering Macro-Generated Functions

`elc` already reads the debug line table (HLR-153) and already builds a
subprogram origin map keyed on `DW_AT_decl_file` (HLR-193). It then throws away
two answers that information contains, and both are answers to questions the
report currently declines to answer.

**A conditional region `elc` cannot decide, the image can.** Today an `#ifdef`
whose symbol no `-D` mentions is undecidable (HLR-133): the region is kept
whole, counted, and — since HLR-208 — the file is refused expansion on account
of it. On a filtered run the image says outright what the build compiled.
Measured on `avrOS`, `drv/uart.c`:

| Lines | Instructions |
| ----- | ------------ |
| 105–120, the only ones with any | 108, 119 |
| 111–118, the whole `#ifdef UART_STATS` block | none |

That build did not compile it — `avrOSConfig.h` `#undef`s `UART_STATS` in the
`#else` of `#ifdef CLI`, and `CLI` is not set in this build. `elc` has the
evidence in hand and reports the region undecided anyway.

**A function a macro defines, the grammar cannot see.** `ISR(USART0_DRE_vect)`
expands to a whole function definition. Tree-sitter finds no function there, and
repair cannot help, because repair does not know the macro *defines* one. On the
same image the debug information places **11** `__vector_*` subprograms in the
analysed sources — `__vector_12` at `sys.c:44`, which is
`ISR(SYS_TICK_INT_VECT)` — and `elc` reports none of them, out of 524 functions.

1. **Conditional regions decided from the image** (HLR-211). Where the image's
   line information covers a file, a region none of whose lines produced an
   instruction, and which is bracketed by lines that did, is **inactive** for
   that build; one with instructions is **active**. Everything else stays
   undecidable, and the count says how many regions each of the three
   dispositions claimed — a figure decided from evidence is not the same claim
   as one decided from a `-D`.
2. **Functions the image places and the parse did not reach** (HLR-212). A
   subprogram the debug information places in an analysed file, at a line no
   parsed function covers, is reported in a table of its own: name and location,
   and nothing else. `elc` has no body for it, so it has no ELOC, no
   complexity, and no edges, and it enters neither the metrics nor the call
   graph — reporting a fan-out of 0 for a function whose body was never read
   would be a measurement rather than an absence (HLR-133, HLR-138).
3. **CSV carries the columns the Functions table carries** (HLR-014 amended).
   They had drifted apart: the table gained a visibility, a navigable location
   and the flow degrees in Phase 29 and before, and CSV still wrote
   `file,language,function,start_line,end_line,eloc,complexity`. The two are the
   same view of the same rows and are now spelled the same way. XML is
   untouched — HLR-056's round-trip depends on its fields.

**This reverses a decision Phase 29 wrote down.** HLR-014 said the
complete-record formats keep `start_line` and `end_line` separately "because a
consumer cannot subtract a column it was not given". That reasoning still holds
for XML, which must rebuild a report; it did not survive contact with CSV, whose
whole purpose is to be the Functions table a consumer can load. The requirement
is amended rather than quietly contradicted.

**Debug information is never required** (HLR-141). A run without `--elf`, or
with an image carrying no DWARF, behaves exactly as it does today: both
deliverables are additions to what a filtered run answers, and neither is a
change to what an unfiltered one does. Coverage governs per file, exactly as
HLR-154 requires.

**Acceptance:** the `debugline` fixture's guarded region, undecidable from the
source and decided by no `-D`, is reported inactive from the image, and the
region counts distinguish it from a region a `-D` settled and from one still
undecided. The same file with no image, and the same file in an image built
without `-g` for its unit, is unchanged. A file whose only undecidable region is
one the image decides is expanded, where today HLR-208 refuses it. A macro that
expands to a function definition is reported with its name and line and with no
figures beside it, and does not appear in the call graph, the fan-out column, or
the project's function count. The CSV header equals the Functions table's
columns, and the XML round-trip is byte-identical to today's.

```text
AI prompt — Phase 30

Read issue #<N>, HLR-133, HLR-141, HLR-154 and HLR-208 before writing code.

The image is evidence, not proof, and the requirement has to say so. HLR-154
already warns that an optimiser folds one line into a neighbour; a single
absent line proves nothing, and an entire region absent between two present
neighbours is strong evidence and still not proof. Report it as evidence: the
region counts must let a reader tell "decided from the image" from "decided
from -D" from "not decided".

Coverage governs, per file. A file the line information never described loses
nothing, at any optimisation level — that is the invariant `debugline/`
exists to hold, and it is the one this phase is unsafe without.

A recovered function has no parsed body. Do not give it a fan-out of 0, an
ELOC of 0, or a complexity of 1; do not put it in the graph, where every
degree it acquired would be a measurement of something nobody measured.

The Functions table is parsed positionally by about twenty bats helpers across
twelve files, and the Graph purification and Dead code tables use the same
`$2 == want` idiom over different tables. Do not bulk-edit `$N` fields with a
regex: Phase 29 broke `class_of` in purify.bats that way.

Verify against the real target and not only the fixtures:

    cd ~/Projects/avrOS/app/avrOS_example
    elc --entry main -o report.md --elf build/main.elf . ../../drv ../../sys \
        ../../srv -v --dbg

Today it reports 44 files, 5 unparsed lines, 82 functions and 0 files
expanded. The 11 ISRs are what deliverable 2 should recover.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push.
```

### Phase 31 — Interactive HTML Reporting & Semantic Zooming

`elc` already writes the graph twice: as `.dot` for Graphviz to lay out
(HLR-103) and as GraphML for a tool to load (HLR-106). Both are exports.
Neither is something a reader can *open*, and on a real target neither is
something a reader can read: `avrOS` renders as 82 nodes and the projects this
tool exists for render as thousands. A drawing that cannot be navigated is a
drawing nobody looks at twice, and the two existing companions have that
failure in common — they scale by getting denser.

**The graph is already hierarchical; only the drawing is flat.** A function
belongs to a file (`SdgNode.component`, HLR-114) and a file belongs to a
declared layer (`stratum_of_components`, HLR-078). That is a three-level
containment `elc` computes on every run and then discards at the point of
emission, drawing every function at one altitude as though the project had no
structure. This phase emits the containment it already knows.

1.  **The hierarchy as data** (HLR-213). One JSON document in Cytoscape's
    compound-node form: a node per declared layer, a node per source file
    carrying its layer as `parent`, and a node per function carrying its file
    as `parent`. Serialised with the JSON library already linked for the
    purification manifest (HLR-175) — a second JSON library would be the
    `igraph`-and-GraphML risk in §9 arriving through a different door, and
    HLR-112 makes the choice a design one rather than a requirement.
2.  **Edges stay function-to-function** (HLR-214). Only the call edges the SDG
    holds are emitted. A meta-edge between two collapsed files is *derived* by
    the viewer from the edges it contains, so emitting one would put a second
    opinion about coupling in the artefact — and it would be a worse one, since
    it could not be reconciled with the Ca/Ce figures the report states beside
    it (HLR-081).
3.  **An output format, selected by its extension** (HLR-215). Not a
    companion and not an option: `-o report.html` writes the page, exactly as
    `-o report.md` writes Markdown (HLR-148). A flag asking for it would make
    this the one artefact chosen by a means nothing else uses, and would be a
    second spelling of what the filename already said — the disagreement
    HLR-149 exists to prevent. It renders when double-clicked: no build step,
    no bundler, and **no local web server** — the constraint that decides the
    shape, because a reader who has to start a server to look at a report does
    not look at it.
4.  **It opens collapsed** (HLR-216). The view loads at the layer level and the
    reader descends. A view that opens at function level and offers collapsing
    has answered the wrong question first: the reason the flat drawings fail is
    that they begin at maximum density, and a default is not a preference.

**The viewing libraries are fetched, and that is not an HLR-040 violation.**
Cytoscape.js and its `expand-collapse` extension are loaded from a CDN by the
browser, at view time. HLR-040 governs *`elc`'s execution* — it forbids the
analysis needing an interpreter, a VM, or the network, and this phase adds
nothing to what the run does: `elc` writes text and exits. What the requirement
does oblige is honesty about the word: the artefact is **standalone in the sense
that it needs no server or build step**, not in the sense that it needs nothing.
A reader on a disconnected machine gets the page and no diagram. That is stated
in the manual rather than discovered, and vendoring the libraries into the file
instead is a change to HLR-215 alone should the offline case ever be the one
that matters.

**A component in no declared layer gets no parent.** `stratum_of_components`
returns `SIZE_MAX` for a file matching no stratum, and the reason is argued
where it is computed: the user said nothing about that file, and placing it
would report a structure nobody drew. The drawing follows the analysis — such a
file is a root-level container, and a run with no `--stratum` at all produces a
two-tier file/function hierarchy rather than an invented top. Inferring layers
from the directory tree here would be the filesystem-derived architecture
HLR-078 refuses.

**HLR-031's uniformity rule is softened, and the softening is the honest
reading rather than an exception carved for this phase.** That requirement said
every human-readable format but CSV, the record and `.dot` presents the same
tiers in the same order. Those three were never awkward cases: CSV is one table
to load, the record is every element for a machine to read back, and `.dot` is
a drawing — no reading of the requirement ever asked them to match the aligned
table. HTML joins them on the same ground. It presents its information **in the
context of the drawing** — a figure reached by opening the box that holds it —
rather than as tables beside it, and holding it to the tier list would be
requiring it to be the Markdown report with a diagram attached. The exemption
is about *arrangement* and never about content: wherever this format shows a
measurement, it is the report's.

**Acceptance:** a target with two declared strata produces exactly one node per
stratum, one per file carrying the correct `parent`, and one per function
carrying its file as `parent`, hand-checked against a fixture. Every edge in the
payload joins two function nodes and no edge names a file or a layer. A file
matching no stratum emits a node with no `parent` key. The emitted document
parses as JSON, and the parsed node set equals the SDG's. The page contains both
CDN `<script>` tags, one `cytoscape({...})` initialisation, an `expandCollapse`
call carrying `fisheye: true` and `animate: true`, and a `collapseAll()` after
it. `-o report.html` selects the format with no option
asking, `-f html` selects it for a report on standard output, `--format md`
against an `.html` output is refused as a disagreement, and `--from-xml` with
an `.html` output is refused because a record carries no topology. Names containing `<`, `&`, `"`, and a
Unicode line separator survive into the page without closing the script element
or breaking the parse.

```text
AI prompt — Phase 31

Read issue #81, HLR-112, HLR-119, HLR-040 and HLR-114 before writing code.

Do not add a second JSON library. `libjansson` is already linked for the
purification manifest, and §9's igraph entry is the same objection: a
dependency the project has an existing answer for is a cost with no
purchase. HLR-112 says the library is a design choice, so making it is
allowed; making it twice is not.

The escaping is the correctness core, and it is not JSON escaping. The
payload sits inside an HTML `<script>` element, where a `</script>` in a C++
template signature ends the element and the rest of the graph becomes body
text. Jansson will not escape it for you — it is well-formed JSON. Escape
`<` and `&` after serialising, and escape U+2028 and U+2029, which are
newlines to a JavaScript parser and not to a JSON one.

Emit no meta-edges. The plugin derives them. An edge between two file nodes
in the payload is a coupling figure elc computed twice by two rules, and the
one in the report is the one with a threshold behind it.

`stratum_of_components` returns SIZE_MAX for a file in no declared layer.
That is not an error and not a layer named "other" — omit the `parent` key.
Read the comment above it before deciding otherwise.

This is a *format*, not a companion. It renders to the stream `emit` opened,
is selected by the output filename's extension, and has no option of its own —
a flag would be a second spelling of what `report.html` already says, which is
the disagreement HLR-149 refuses between the two spellings there already are.
Add it to both tables in cli.c: the extension map and the format-name list.
The diagnostics are built from those tables, so a format added to one and not
the other produces an error message naming a set the parser will not accept.

Verify against the real target and not only the fixtures:

    cd ~/Projects/avrOS/app/avrOS_example
    elc --entry main -o report.html --elf build/main.elf . ../../drv ../../sys \
        ../../srv -v \
        --stratum app:'*/app/*' --stratum drv:'*/drv/*' \
        --stratum sys:'*/sys/*' --stratum srv:'*/srv/*'

Open the result. Four layer nodes, 44 file nodes, 82 function nodes, and it
opens showing four boxes.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push.
```

## 9. Risks & Open Questions

*   **~~The Ada grammar is community-maintained.~~ Closed 2026-08-17 by
    withdrawing the language.** It was vetted and accepted in Phase 6 and
    delivered through Phase 15; HLR-011 no longer names it and the module,
    fixtures and grammar are gone ([notes.md](notes.md) §2.2). *What the
    episode leaves behind* is the general risk rather than the particular
    one: a grammar that writes an array index exactly like a call cannot have
    its call sites separated by any query, so such a language's coupling
    figures are noisier than C's. The direction is safe — extra edges only
    shrink the unreachable set — and the `graph/` fixture pins that no
    destination is invented for a call that does not resolve.
*   **`--wrap` is not portable across linkers.** The unit level depends on
    GNU ld or lld. *Mitigation:* Phase 0 proves the mechanism before anything
    depends on it; a toolchain lacking it cannot run the unit suite and must
    be declared unsupported rather than worked around.
*   **`igraph` may arrive with GraphML support enabled**, which links a
    second XML library the project has no need for. *Mitigation:*
    `make check-prereqs` reports the condition, and Phase 8 fails the build
    rather than linking it.
*   **Component granularity may prove too coarse.** HLR-114 defines a
    component as a source file, which is conventional for C but blunt for
    C++ and Rust where several classes or modules share a file
    ([notes.md](notes.md) §3). *Mitigation:* it is a change to HLR-114 alone;
    everything else references it. Revisit after Phase 11 produces real
    numbers.
*   **The whole-file parse-failure rule may be too blunt.** One syntax error
    discards a large file (HLR-035). *Mitigation:* `analyze.c`'s error
    handling is the single place to relax it; do not scatter tolerance.
*   **Fixture authoring is the hidden cost.** Hand-counting ELOC and
    complexity for five languages, and topology for the graph fixtures, is
    slow and exacting work — and it cannot be shortcut by generating expected
    values from `elc`, which would assert nothing. *Mitigation:* budget for
    it explicitly in Phases 3, 6, and 8.

*   **~~The `valgrind` job will become the long pole.~~ It did. Settled
    2026-08-28 by taking it out of the local loop and leaving it on the PR.**
    The prediction was right and the remedy was the other one: the question
    posed here was whether to move the *job* off every PR, and what actually
    hurt was running the same target *locally* before pushing. At roughly an
    hour a pass it was being paid on every iteration of a change, by a
    developer sitting still, to re-verify a working copy that CI would verify
    again as a merge result.

    So `make valgrind` leaves the phase protocol (§5.4 step 5) and stays a
    required CI job. Waiting is cheaper than working, and the PR checks the
    merge result rather than one copy of it. **The cost is one class of defect
    moving later:** memcheck catches reads of uninitialised memory and ASan
    does not, so that class is now found on the PR instead of before the push.
    Everything ASan and LSan cover — invalid accesses and leaks — still fails
    locally and still fails fast.

    *Residual risk:* a developer who ignores a red `valgrind` job merges a
    defect the local gates were never going to catch. That is a review
    obligation now rather than a mechanical one, which is a real weakening and
    is recorded here as such.

**Settled: Git support is kept** (decided 2026-08-14). libgit2 buys
`.gitignore` compliance, which is load-bearing for the estimation and
comparison use cases the [PVD](PVD.md) is built on — counting vendored,
generated, or build-output code inflates ELOC invisibly, and the resulting
number looks plausible while being wrong. Removing it would also make `elc`
an instance of the problem [PVD](PVD.md) §3 names ("tools that are not
Git-aware re-analyse vendored code… unless the user maintains an exclusion
list by hand"), and would retire the change-scoped analysis theme in §9 that
depends on the same library.

The costs are real and stay on the risk register: libgit2 is a large
dependency, two contributors account for more than half its commits, and it
ships HTTP/SSH transports that must be built out to honour HLR-040's
no-network rule. Should the dependency weight prove unacceptable later, the
fallback is a **build-time option** — `elc` without libgit2 degrades to
filesystem traversal — for which HLR-011's "operate over whatever is present"
already sets the precedent. That is a change to Phase 7 alone.

## 10. Estimated Effort

T-shirt sizes; no calendar commitment implied.

| Phase | Effort | Notes |
| ----- | ------ | ----- |
| 0 | M | CI and the harness are fiddly but bounded |
| 1 | M | `fts` and canonicalisation carry the edge cases |
| 2 | L | `dlopen`, teardown ordering, and the query contract |
| 3 | L | Span merging and attribution are the correctness core |
| 4 | S | Largely a second query over machinery Phase 3 built |
| 5 | M | Four writers and one streaming reader |
| 6 | L | Mostly fixture authoring, four times over |
| 7 | M | Isolated, but the applicability rules need care |
| 8 | XL | The largest phase: resolution, the graph, and GraphML |
| 9 | M | Bounded once the graph exists |
| 10 | M | Reachability is simple; the global classification is not |
| 11 | M | Coupling is arithmetic; layering needs the declarations |
| 12 | S | A table and its evaluation |
| 13 | S | Text emission over an ordered model |
| 14 | M | Binding and provenance-split error handling |
| 15 | M | Conditional-region pruning; the evaluator and its fixtures |
| 16 | M | libelf and the demangler are libraries; the name reduction is the work |
| 17 | M | Sweeping and closing, plus whatever Phase 16 uncovers |
| 31 | M | The serialisation is small; the escaping and the fixture are the work |

Phase 8 is the one worth splitting if it proves oversized: symbol resolution
and graph construction could ship separately from the GraphML writer, though
that would leave the first half untestable until the second lands.

## 11. Out-of-Scope Follow-ups

*   **JSON or SARIF output and threshold-based exit statuses**, which would
    let `elc` gate a build directly ([PVD](PVD.md) §9 CI integration).
*   **Change-scoped analysis** — metrics for the functions a commit touched,
    using the `libgit2` already linked.
*   **Sorting, ranking, and top-N selection**, currently deliberately absent.
*   **Parallel file processing**, gated strictly on a real repository proving
    unusably slow.
*   **Language-defined components** — Rust modules, C++ namespaces — should
    Phase 11 show file-granularity coupling to be uninformative.
*   **A configuration file** for stratum and entry-point declarations, should
    passing them on the command line prove unwieldy at scale. This is a
    vision-level decision, not a convenience ([PVD](PVD.md) §6 Principle 3).

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.
