# Software Development Plan: elocker (elc)

**Version:** 1.3
**Date:** 2026-08-14
**Author(s):** John Anderson

**Status:** Phase 12 complete. Every measurement `elc` makes is now evaluated
against the published catalogue of PVD Appendix A, and **one module does all
of it** — which is what makes the project's central claim checkable rather
than merely asserted. A reviewer can read one table to confirm that every line
`elc` draws comes from MISRA C, Martin, or Henry–Kafura, and that the single
exception, the bottleneck heuristic, says so in the text a reader sees. The
fan-out bands are exhaustive, three of the five producing no finding at all;
severity is a closed ordered set with the highest applicable band winning; and
no severity touches the exit status, because deciding what a critical finding
warrants belongs to the caller. 645 catalogued tests verify 320 of 497
requirements and the coverage baseline falls from 191 to 177. The
conditional-compilation set HLR-131 to HLR-136 remains the only
specified-and-unbuilt group, and Phase 15 builds it. Phase 13 — graph
visualisation — is ready to start.

## Status

| Phase | Description | Status |
| ----- | ----------- | ------ |
| [0](#phase-0--foundation-and-continuous-integration) | Build system, CI pipeline, test harness, skeleton binary | ✅ Complete |
| [1](#phase-1--target-discovery-and-the-walking-skeleton) | Target discovery, ordering, table output — end to end | ✅ Complete |
| [2](#phase-2--language-runtime-and-function-discovery) | Runtime loading, Tree-sitter parse, function identity | ✅ Complete |
| [3](#phase-3--effective-lines-of-code) | ELOC, comment merging, file and project totals | ✅ Complete |
| [4](#phase-4--cyclomatic-complexity) | Complexity, threshold listing, most-complex callouts | ✅ Complete |
| [5](#phase-5--output-formats-and-the-saved-record) | CSV, XML, Markdown, escaping, regeneration mode | ✅ Complete |
| [6](#phase-6--language-breadth) | C++, Rust, Python, Ada — data only, no C change | ✅ Complete |
| [7](#phase-7--git-aware-discovery) | Repository detection, applicability, scoping, routes | ✅ Complete |
| [8](#phase-8--system-dependence-graph) | Cross-file resolution, the SDG, GraphML export | ✅ Complete |
| [9](#phase-9--call-tree-analyses) | Fan-out, depth, deepest stack, recursion | ✅ Complete |
| [10](#phase-10--dead-code-reachability-and-global-state) | Dead code within and between functions, global coupling, scopes | ✅ Complete |
| [11](#phase-11--coupling-layering-and-cycles) | Strata, skip-level, Ca/Ce, instability, cycles | ✅ Complete |
| [12](#phase-12--thresholds-severity-and-attribution) | The Appendix A catalogue, severity, attribution | ✅ Complete |
| [13](#phase-13--graph-visualisation) | Annotated Graphviz `.dot` companion | 🔲 Not started |
| [14](#phase-14--custom-rules) | User-supplied `.scm` rules, binding, matching | 🔲 Not started |
| [15](#phase-15--conditional-compilation) | `-D` definitions, inactive-region pruning | 🔲 Not started |
| [16](#phase-16--hardening-and-release-readiness) | Full sanitizer sweep, self-analysis, coverage closure | 🔲 Not started |

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
framework, and the inspection tools from the distribution, then builds all
four libraries elc links from pinned upstream releases.

**The libraries are built from source deliberately.** When a security
advisory lands against one of them, the fix is to bump a version in the
Makefile and rebuild — the same day, if needed. Waiting for a distribution
to rebuild and ship is not a control this project has. `make prereqs-<lib>`
rebuilds one; `make check-prereqs` reports what is installed and flags
anything below the minimums above.

Two configure-time decisions fall out of building them ourselves, and both
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
3. Ship runtime language support for C, C++, Rust, Python, and Ada, added as
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
5.  **Test** — `make test`, then `make asan`, then `make valgrind`. All three
    must be clean before the phase is considered done (§6).
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
2. The release is tagged `vMAJOR.MINOR.PATCH`.
3. `make install` under a `DESTDIR`/`PREFIX` staging root produces the
   deliverable: the `elc` binary plus the `runtime/` tree.
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
| Sanitized | The whole suite re-run instrumented | ASan, UBSan, LSan, valgrind | HLR-124, HLR-125 |

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
| Additional grammars | Phase 6 | Ada's is community-maintained but vetted and accepted ([notes.md](notes.md) §2.2) |
| `libgit2` | Phase 7 | Isolated to one discovery route |
| `igraph` | Phase 8 | With GraphML and OpenMP off, and its GMP choice pinned |

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

1. Grammars and six query files each for C++, Rust, Python, and Ada.
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

Add C++, Rust, Python, and Ada: a grammar, six query files, `extensions.map`
entries, and hand-counted fixtures for each.

**The acceptance criterion for this phase is that `git diff` shows no change
under `src/`.** That is HLR-010's extensibility claim demonstrated rather than
asserted. If a language cannot be supported without touching C, stop — the
defect is in the design, not the grammar, and the fix belongs in `src/` as a
separate issue before this phase proceeds.

Watch for:
* **The Ada grammar is vetted and accepted** — `briot/tree-sitter-ada`,
  MIT, actively maintained, authored by an AdaCore developer
  (`doc/notes.md` §2.2). Use it; no further vetting is needed. The caveat
  recorded there about call-site ambiguity lands in Phase 8, not here.
* Ada nested subprograms and Rust closures are the interesting cases for
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
* **Ada `calls.scm` needs care.** `Foo (X)` is ambiguous between a function
  call and an array index; the grammar manages this with precedence rules
  rather than resolving it, since resolution needs semantic analysis. Expect
  false-positive call edges in Ada. Do not disambiguate heuristically in C —
  that would put language knowledge in the binary. Add an Ada case to the
  `graph/` fixtures pinning whatever behaviour you settle on, so it is a
  recorded decision rather than a later surprise (`doc/notes.md` §2.2).
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
before you push. Close by opening the issue for Phase 16
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

### Phase 16 — Hardening and Release Readiness

1. Full sanitizer sweep across every fixture and target type, including every
   error path.
2. Teardown completeness — every `*_free`, every error path leak-clean.
3. `elc` analysed by `elc`: the self-quality check.
4. Coverage-gap closure; the review-verified residue recorded.
5. `test/instrumented/sanitized.bats` — the catalogued tests that record the
   sanitized gate having been applied and come back clean. Without these,
   HLR-124, HLR-125, and the memory-safety LLRs can never leave the gap list
   however diligently the sanitized build is run ([STP](STP.md) §2.5).
6. `main` created from `develop` for the first release, per §5.4.
7. `make install` verified against a staging root.

**Requirements:** HLR-124, HLR-125, plus any requirement still lacking a
bound test.

**Acceptance:** `make asan` and `make valgrind` both clean across the whole
suite, including runs ending in usage errors, invalid targets, and rejected
records. `Traceability.md` §6 lists nothing but the review-verified items the
STP names. `elc` on its own source reports no function exceeding complexity
15 and no dependency cycle. `make install` under a staging root produces a
working binary and runtime tree.

## 9. Risks & Open Questions

*   **~~The Ada grammar is community-maintained.~~ Discharged 2026-08-14.**
    `briot/tree-sitter-ada` was vetted and accepted: MIT, last commit July
    2026, authored by an AdaCore developer, adopted by Zed
    ([notes.md](notes.md) §2.2). HLR-011 stands unamended. *Residual risk,
    now carried by Phase 8 instead:* Ada's `Foo (X)` is ambiguous between a
    call and an array index, and the grammar manages rather than resolves it,
    so Ada call edges may carry false positives. The direction is safe — extra
    edges only shrink the unreachable set — but Ada's coupling figures will be
    noisier than C's.
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
    C++/Rust/Ada where several classes or packages share a file
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

*   **The `valgrind` job will become the long pole.** It re-runs the
    integration and fixture levels under instrumentation, and the fixture
    corpus grows fivefold at Phase 6 and again at Phase 8. No performance
    target is committed, so a slow gate is acceptable rather than a defect —
    but expect PR turnaround to lengthen noticeably from Phase 6, and decide
    then whether to keep `valgrind` on every PR or move it to a merge-queue
    or nightly job. *Mitigation:* the choice is a CI configuration change, not
    a plan change; ASan remains on every PR either way.

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
| 16 | M | Sweeping and closing, plus whatever Phase 16 uncovers |

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
*   **Language-defined components** — Rust modules, C++ namespaces, Ada
    packages — should Phase 11 show file-granularity coupling to be
    uninformative.
*   **A configuration file** for stratum and entry-point declarations, should
    passing them on the command line prove unwieldy at scale. This is a
    vision-level decision, not a convenience ([PVD](PVD.md) §6 Principle 3).

**AI prompt.** Run after issue #<N> exists; `<N>` is its number.

```text
Implement **Phase 16 — Hardening and Release Readiness**, tracked by
issue #<N>. The final phase.

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
  cycle.
* Closure of every remaining coverage gap, and an explicit record of the
  review-verified residue the STP names (HLR-101, HLR-111, HLR-121's
  cross-release clause).
* `main` created from `develop`, and `make install` verified against a
  staging root.

There is no next phase. In place of step 12 of the protocol, open the release
PR from `develop` to `main` and attach the `Traceability.md` at that commit as
the evidence of verification. Confirm before doing so that the manual and man
page describe the whole delivered product, not merely the last phase's
additions.

When the work is done, follow the Phase Execution Protocol in §5.4 —
including step 6 (updating `doc/Project.xml` with everything this phase
discovered), step 7 (the manual and man page), step 8's gap-baseline
update, and step 9's Status update in both `doc/SDP.md` and `README.md`,
before you push. Close as that phase's prompt directs.
```
