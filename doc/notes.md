# Working Notes

Scratch material that does not belong in a specification. Nothing here
is a requirement, nothing traces, and nothing binds.

The PVD, HLRs, SDD, LLRs, STP, and SDP are all written, the traceability
matrix is complete in both directions, and the sections of this file that
fed them have been deleted as their content was absorbed — which is the
rule this file runs on. What remains is the residue: decisions still
open, and findings worth not rediscovering.

Phase 0 is complete and merged behaviour now lives in the specification,
so what that phase taught is mostly *not* here — the `--wrap` `volatile`
trap and the instrumented-test portability rule both went into
[STP.md](STP.md) §2.2, where the next person writing such a test will
actually be standing. Only what has no home in a specification stayed.

---

## 1. For the development plan / build

Conventions already encoded in
[`.github/skills/elocker-dev/SKILL.md`](../.github/skills/elocker-dev/SKILL.md)
— read that before restating any of it:

*   **GNU make**, non-recursive, single top-level `Makefile`, with the
    `awk`-based self-documenting `help` target as the default goal.
    Every target carries a `## description`.
*   **`-std=c11 -Wall -Wextra -Wpedantic`**, `-MMD -MP` auto-deps,
    objects under `build/`, `asan` target for
    `-fsanitize=address,undefined`.
*   `build/runtime` is a symlink to the source `runtime/`, so
    `build/elc` finds its grammars without an install step. Tests
    override with the runtime-location environment variable.
*   Feature-test macros (`_XOPEN_SOURCE=700`, `_DEFAULT_SOURCE`) in
    `CPPFLAGS` — `fts(3)` needs them on glibc, and `fts` is absent on
    musl, which matters if an Alpine build is ever wanted.

### 1.1 Every linked library is built from source

`make prereqs` takes the toolchain and the test framework from the
distribution, then builds all four libraries elc links —
`libtree-sitter`, `libgit2`, `igraph`, `Expat` — from pinned upstream
releases. Versions are variables at the top of the Makefile's prereqs
section, and `make prereqs-<lib>` rebuilds one.

**The reason is response time to an advisory.** When a CVE lands against
one of these, the fix is to bump a version and rebuild, which can happen
the same day. Waiting for a distribution to rebuild and ship is not a
control this project has.

Two secondary benefits came out of it, both of which happen to serve
requirements:

*   **libgit2's network transports are compiled out** (`-DUSE_HTTPS=OFF
    -DUSE_SSH=OFF`). elc reads local repositories and never speaks to a
    remote, so transport support was attack surface with no matching
    capability — and dropping it removes the OpenSSL and libssh2
    dependencies as well. This is HLR-040's no-network rule enforced at
    the link line rather than trusted.
*   **igraph's GraphML support is switched off** at configure time
    (`-DIGRAPH_GRAPHML_SUPPORT=OFF`), verified to leave no XML library in
    its link line. elc writes GraphML itself, so the feature was unused
    and would have linked a second XML library the project has no need
    for. Debian's `libigraph-dev` has it on, which is why the
    distribution package is not used.

It also settles a version problem: Debian carries `libtree-sitter` at
0.22 and `igraph` at 0.10, both below the SDP §0 minimums, with no
backport. Do not answer that by relaxing the minimums — a tree-sitter
runtime refuses a grammar generated for a newer language ABI, which is
what Phase 6 will produce, and igraph 1.0 is API-breaking against 0.10,
so code written for one does not compile against the other.

**Criterion is the deliberate exception**, taken from the distribution.
It is a test framework that is never linked into the shipped binary, so
a vulnerability in it reaches no user of elc.

**The one gap in this story: nothing verifies what was downloaded.**
`make prereqs-src` fetches four tarballs over HTTPS and builds whatever
arrives. There is no checksum and no signature check, so the pinned
version protects against a *silent upgrade* but not against a tampered
or substituted archive — which is a strange place to stop, given that
supply-chain response is the entire reason the libraries are built from
source. The fix is small and self-contained: record the SHA-256 of each
release beside its version variable and have the `fetch` macro verify
before unpacking. It has not been done. Do it the next time these
versions are bumped, so the checksums are captured from an upstream
release page at the moment the version is chosen rather than computed
from a download already in hand — which would verify nothing.

### 1.2 Bats is vendored, not installed

The SDP §0 lists Bats as a required *tool* and only `bats-support` /
`bats-assert` as vendored. Phase 0 vendored **bats-core as well**, under
`test/helpers/`, pruned to its runtime files (63 files, 476K — its own CI
configs, docs, and test suite removed).

Two reasons. The suite becomes hermetic: no version skew between a
developer's Bats and CI's, and no install step before `make test` works.
And it is what makes the suite runnable in an environment without
package-install rights, which is how Phase 0 was actually developed.

Criterion cannot be vendored the same way — it is a C library needing a
build — so the unit level still requires `libcriterion-dev` and is
verified in CI rather than locally when the package is absent.

### 1.3 Runtime data files the build must ship

Named in the SDD but easy to forget when packaging:

```text
runtime/
├── extensions.map          # "<ext> <lang>" per line   (HLR-060)
├── binary.exts             # excluded extensions       (HLR-005)
├── parsers/<lang>.so
└── queries/<lang>/{comments,functions,complexity,eloc,calls,globals}.scm
    └── rules/*.scm         # optional custom rules     (HLR-107)
```

Six required queries per language, not three — the count grew when
graph analysis and ELOC classification were added.

### 1.4 What the spec toolchain does and does not enforce

Three behaviours of TraceR that Phase 0 established by running into
them. None is documented where you would look for it.

*   **`lint_project.py` does not fail on uncovered requirements.** A
    requirement with no verifying test is reported as a *warning* and
    the tool exits 0. Coverage is therefore enforced entirely outside
    TraceR: `make coverage` counts the warning lines and compares them
    against [`test/gap-baseline.txt`](../test/gap-baseline.txt), and CI
    fails only if the count *rises*. Lowering the baseline is a manual
    step in the phase protocol — nothing detects that you wrote tests
    and forgot to bank them. The number is 361 after Phase 0, down from
    378.
*   **`make spec` re-renders all five documents into a temporary
    directory and diffs them against what is committed.** Editing a
    generated `.md` by hand therefore fails CI rather than sticking.
    Every change goes into [`Project.xml`](Project.xml), and the
    documents are regenerated with
    `python3 tools/render_doc.py tools/templates/<D>.md.j2 <D> --out doc/<D>.md`.
*   **An empty `<traces>` element is schema-invalid.** A catalogued test
    that verifies no requirement — harness self-checks, mostly — must
    omit the element entirely. Emitting it empty to mean "none" fails
    validation with a message that does not obviously say so.

---

### 1.5 What is installed in the development environment, 2026-08-15

Every dependency the build needs is now present, and `make check-prereqs`
reports every tool and every library found. Two things about *how* they
are present matter, and neither is visible from "it builds":

*   **They came from the distribution, not from `make prereqs-src`.**
    Every library reports a prefix of `/usr`; a source build installs
    under `/usr/local`. So the same-day-advisory-response property
    described in §1.1 does not hold in this environment today — a CVE
    against `libgit2` would be answered on the distribution's schedule,
    not by bumping a version at the top of the Makefile. Running
    `make prereqs-src` restores it.
*   **Two are below the SDP §0 minimum**, and `check-prereqs` says so
    rather than failing, because a library is not linked until its phase:

    | Library | Installed | Minimum | Needed from |
    | ------- | --------- | ------- | ----------- |
    | `tree-sitter` | 0.22.6 | 0.25 | **Phase 2** |
    | `igraph` | 0.10.15 | 1.0 | Phase 8 |

    The `tree-sitter` shortfall is not a future problem: Phase 2 links it
    and compiles queries against it. Build it from source before starting
    that phase, or the first grammar load is debugging a version gap
    rather than the code.

    `igraph` 0.10.15 does not report `libxml2` in its link line, so the
    GraphML-support warning of §1.1 has not fired — but the 1.0 rebuild
    that Phase 8 needs anyway must carry
    `-DIGRAPH_GRAPHML_SUPPORT=OFF`, so the condition is worth
    re-checking then rather than assumed settled.

Criterion 2.4.1 is installed and `make unit` runs locally, which the
Phase 0 note in §3 recorded as unavailable. That entry is annotated.

---

## 2. Research findings (August 2026)

### 2.1 Status of the dependencies, as checked

| Library | Status when checked | Note |
| ------- | ------------------- | ---- |
| Tree-sitter | 0.26.2 (Dec 2025), active | Pre-1.0 versioning, but the C API is stable. Not a free choice anyway — HLR-112 makes it a product contract |
| libgit2 | v1.9.4 (May 2026), active 335/365 days | Bus factor worth noting: two contributors are >51% of commits |
| igraph | 1.0 series, explicit long-term API stability commitment | The only mature C-native option; Boost.Graph, LEMON, NetworKit are all C++ and would impose a second toolchain |
| Expat | 2.8.3 (Aug 2026), funded maintenance | Parse-only. Fine — `elc` hand-rolls all writing |

### 2.2 The Ada grammar — vetted 2026-08-14, accepted

C, C++, Rust, and Python have grammars under the official `tree-sitter`
GitHub organisation. Ada does not, so
[`briot/tree-sitter-ada`](https://github.com/briot/tree-sitter-ada) was
vetted before relying on HLR-011's commitment. **It passes.**

| | |
| --- | --- |
| Licence | MIT — matches this project's |
| Last commit | 31 July 2026 (GNAT `finally` extension support) |
| Activity | Sustained through 2025–26: crates.io release Oct 2025, binding work Dec 2025 |
| Health | 97 commits, 27★, 12 forks, 0 open issues |
| Author | Emmanuel Briot, an **AdaCore** developer; derived from Stephen Leak's Emacs `ada-mode` grammar |
| Adoption | Zed's Ada extension, published crate, Neovim integration underway |

**A caution about the source that raised this concern.** The
[tree-sitter parsers wiki](https://github.com/tree-sitter/tree-sitter/wiki/List-of-parsers)
lists the grammar as "last updated 2024-05-23, 14 commits" — stale by
two years and 83 commits. Do not re-derive a maintenance judgement from
that page; check the repository. The only other candidate,
`repa4ok/tree-sitter-ada`, is 5 commits and 0 stars and is not viable.

**One real caveat, for `calls.scm` in Phase 8.** The grammar defines
`function_call`, `procedure_call_statement`, and
`iterator_procedure_call`, so call extraction is possible. But Ada's
`Foo (X)` is genuinely ambiguous between a function call and an array
index, and the grammar *manages* this with precedence rules and a
`_name` / `_name_not_function_call` split rather than resolving it —
resolution needs semantic analysis a grammar does not do.

So Ada call edges in the SDG may include array-indexing false
positives. That is the safe direction: extra edges only shrink the
unreachable set, exactly as HLR-096 reasons about address-taken
functions. But Ada's coupling and fan-out figures will be noisier than
C's, and the `graph/` fixture group needs an Ada case pinning the
behaviour rather than leaving it to be discovered.

---

## 3. Known soft spots and deferred decisions

Places where a defensible choice was made that may want revisiting.
None is a defect; each is a judgement that could go the other way.

*   **Component = source file** (HLR-114). Conventional for Martin's
    metrics in C-family code and language-agnostic, but coarse for
    C++/Rust/Ada where several classes or packages share a file. If
    coupling numbers look uninformative in practice, a language-defined
    component (Rust module, C++ namespace, Ada package) is the
    alternative — it is a change to HLR-114 alone; everything else
    references it.
*   **Any error node skips the whole file** (HLR-035). Deliberately
    conservative: one syntax error discards a 5,000-line file. The SDD
    names `analyze.c`'s error handling as the single place to relax
    this if experience says it is too blunt.
*   **Bottleneck threshold of 5** (HLR-081) is `elc`'s own heuristic,
    not a published standard — no source gives a component-level
    fan-in/fan-out bottleneck threshold. It is labelled as such per
    HLR-099 and is user-configurable. If a citable source turns up,
    prefer it.
*   **Depth is a lower bound** (HLR-087). Chains through unresolved
    indirect calls are not followed. Reported with the unresolved-call
    count so completeness is visible, but an embedded engineer sizing
    a stack from it needs to understand this.
*   **No performance target is committed.** A 100k-line repository in
    a few seconds on one core is an informal best guess only (PVD §9
    Throughput). If it proves unusable, that gates the parallelism
    theme — which would need HLR-041 revisited.
*   **HLR-042 is permanently retired** — the old performance-target
    requirement. Do not reuse the number.
*   **CI runs twice on a phase branch with an open PR.** The workflow
    triggers on pushes to `phase/**` and on pull requests targeting
    `develop`, and a phase branch under review satisfies both, so all
    nine jobs execute twice per push. It was left that way because a
    phase branch is also pushed *before* its PR exists and the feedback
    is wanted then too. Dropping `phase/**` from the push trigger halves
    the minutes at the cost of that early signal; restricting it to
    pushes without an open PR is not expressible in the `on:` syntax and
    would need a job-level `if`.
*   **Unit tests do not run locally in the development environment
    used so far** — `libcriterion-dev` needs package-install rights that
    were unavailable, so the unit level is verified in CI only (§1.2).
    Anything that passes review without a local Criterion run has had
    its unit level checked by a machine and not by a person.
    *Update, Phase 1:* Criterion 2.4.1 was present in the environment
    Phase 1 was written in, and `make unit` ran locally throughout. The
    constraint is environment-specific, not a property of the project.
*   **`discover.c` resolved the runtime location for itself**
    (Phase 1). ~~Resolved in Phase 2~~: `registry_open()` now owns the
    precedence rule and hands the location to `discover_targets()`.
    Kept here as the record of a deliberate temporary duplication that
    was actually undone rather than absorbed.
*   **A function's reported span runs from its name to the end of its
    body** (Phase 2, LLR-ANL-35). The obvious alternative — the body
    node alone — is what the query naturally yields, and it puts the
    start line on the opening brace. A reader asked where a function
    begins points at its signature, and a hand-counted fixture would
    have had to encode the brace convention to agree. The span is
    therefore stitched from two captures. It is not the full definition
    node either: capturing that would need a third capture in every
    `functions.scm`, and the contract is worth more than the handful of
    lines a multi-line return type would add.
*   **The reported span and the complexity scope are the same node in
    C, and need not be in every language.** `complexity.scm` runs
    against `@function.body` (SDD §7); the reported span starts earlier.
    No language shipped so far notices, but a language whose parameter
    list can contain a decision point — a default argument with a
    conditional, in C++ — would be counted differently depending on
    which node the query file chooses to call the body. Worth settling
    in Phase 6 when C++ lands, not before.
*   **A query file that captures nothing is valid** (Phase 2). It is
    how an unimplemented query is expressed, and it is what lets a
    phase ship a language with `functions.scm` complete and the other
    five as documented stubs. The cost is that a genuinely empty query
    file — one someone forgot to write — loads silently and reports
    nothing, which will look like a metric of zero rather than a
    missing file. If that bites, the answer is a manifest in the
    language directory, not a heuristic over file contents.
*   **A symbolic link to a *file* is skipped during traversal**, not
    only a link to a directory (LLR-FTS-05). HLR-069's text is about
    unbounded traversal and double-counting, both of which the
    directory case covers; extending it to files was a judgement, made
    because a link to a file inside the tree double-counts it and a
    link out of the tree silently widens what the target denotes. The
    cost is that a repository which uses file symlinks as its normal
    layout will under-report on the filesystem route. The Git route
    (Phase 7) sees the link as a tracked blob, so the two routes may
    disagree there — worth checking against HLR-126 when Phase 7 lands.
*   **Hidden *files* are excluded from the walk, not just hidden
    directories** (HLR-005, amended in Phase 1). The reason is
    HLR-039: it states observably that a configuration-like file in
    the analysis target must produce output byte-identical to its
    absence, and a walk that yields `.elcrc` as a discovered file
    cannot satisfy that. Excluding hidden entries wholesale is the
    smaller change, and dotfiles are not source. A hidden path named
    *as* the target is still walked; naming it is explicit.
*   **`.gitignore` can swallow product data, silently** (Phase 2). The
    file carries `*.map` for linker map files, and that also matches
    `runtime/extensions.map` — the extension-to-language table, which is
    product data and must ship. It worked locally for a whole phase, was
    absent from the clone CI made, and the failure named the missing file
    rather than the rule that hid it. `!runtime/extensions.map` now
    negates it, and an instrumented test asks git what it is tracking
    under `runtime/` so the next one cannot get through. Anything added
    there is worth a `git check-ignore -v` before it is relied on; the
    patterns that reach in are `*.map` and `*.so`, and only the latter
    is meant to.
*   **Grammars are fetched at build time, not vendored** (Phase 2).
    `make all` builds `runtime/parsers/<lang>.so` from a pinned upstream
    release, downloading it once; the alternative was committing the
    generated `parser.c`, which for five languages is several megabytes
    of machine-written code in the history. The cost is that a first
    build needs network access, and that a CI job without it cannot
    build at all. `make clean` deliberately leaves the grammars, since
    `make asan` cleans twice and would otherwise refetch twice.
*   **The fixture suites are flat, the fixture data is not.**
    `test/fixtures/traversal/` holds the tree; `test/fixtures/traversal.bats`
    sits beside it rather than inside. Bats' recursive discovery
    (`bats -r`) enumerates suites with `find -L`, which follows
    symbolic links — and the traversal fixture contains a deliberate
    self-referential one. GNU find detects the loop and warns rather
    than hanging, but a harness that has to survive a tree built to
    defeat walking is a harness waiting to break. Keeping the suites
    flat means no recursion is needed at all.
*   **LeakSanitizer and `strace` cannot both watch the same run.**
    LSan stops the world at exit through a `clone`d tracer and
    `ptrace`, which collides with `strace`'s attachment and aborts the
    process. The instrumented suite therefore disables leak detection
    for a traced run and for that run only (`strace_elc` in
    `test/helpers/common.bash`). This was found in Phase 1 and was
    already affecting Phase 0's network-syscall test under `make asan`:
    the trace was being truncated by the abort, so the test passed
    because elc had died, not because it made no network call.

---

## 4. Cross-document invariants worth not breaking

Small set of rules that several documents depend on jointly. Breaking
any one of them requires touching all three documents, so they are
worth stating once:

1.  **Severity never affects exit status.** Findings are data
    (HLR-100, HLR-123); exit status is reserved for genuine failures
    (HLR-120). PVD §7.2 keeps policy gating out of scope.
2.  **`stdout` is data, `stderr` is conversation** (HLR-038). Every
    diagnostic decision in the HLRs follows from this.
3.  **Library names in the PVD are suggestions** (Principle 5,
    HLR-112). The SDD may substitute — and has. The one exception is
    Tree-sitter, whose query language and grammar format are visible
    to users authoring `.scm` files and are therefore a product
    contract.
4.  **Every threshold carries attribution** (HLR-099). Published
    standards are cited; `elc`'s own heuristics are labelled as such.
    This is what keeps the "no built-in style opinion" non-goal
    (PVD §7.3) honest while shipping MISRA and Martin thresholds.
5.  **Analyses requiring user declarations are omitted, not empty,
    when the declaration is absent** (HLR-115) — and the omission is
    stated in the report. Never report every function unreachable
    because no entry points were declared.
