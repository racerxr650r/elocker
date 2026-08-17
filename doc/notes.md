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
*   **igraph's GMP choice is pinned to the bundled copy**
    (`-DIGRAPH_USE_INTERNAL_GMP=ON`), added in Phase 8. The option
    defaults to `AUTO`, which links system GMP when its headers are
    present and a bundled Mini-GMP otherwise — so elc's link line
    depended on what happened to be installed on the machine that built
    igraph. A developer box without `gmp.h` and a CI runner with
    `libgmp-dev` produced *different binaries from the same commit*, and
    the instrumented allowlist failed in CI alone. A fixed allowlist
    cannot accept "it depends". Pinning the bundled copy also keeps the
    dependency inside the pinned-source story above rather than taking a
    distribution library; igraph uses GMP only in bliss, for the
    automorphism-group counts of graph isomorphism, which no elc
    analysis performs.
*   **igraph's OpenMP support is switched off too**
    (`-DIGRAPH_OPENMP_SUPPORT=OFF`), added in Phase 8. The default build
    links `libgomp`, which allocates its thread pool during the dynamic
    linker's init — before `main` runs — putting 104 bytes of
    still-reachable state in every process and a thread runtime in the
    link line of a binary that promises one thread (HLR-041). The
    single-thread tests happened to pass, because nothing elc calls
    enters a parallel region *yet*; the instrumented allowlist is what
    actually caught it, and is now the guard against its return.

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

**Update, Phase 8 (2026-08-16).** The libraries have since been built
from source and `check-prereqs` reports tree-sitter 0.26.2, expat 2.8.3,
libgit2 1.9.0 and igraph 1.0.1, all conforming. One outstanding action
on this machine: the installed igraph predates the
`-DIGRAPH_OPENMP_SUPPORT=OFF` flag added in Phase 8, so it still links
`libgomp` and `make instrumented` will fail on the dependency allowlist
until `make prereqs-igraph` is re-run. `check-prereqs` now warns about
exactly this condition, so it does not have to be remembered.

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

**What Phase 8 found, which is better than this predicted.** An array
index is captured as a call site, its name is then resolved against the
project symbol table, and — because an array is not a subprogram — it
resolves to *nothing*. So the ambiguity lands in the **unresolved
count**, which is reported, rather than becoming an edge, which would
not be. The visible cost is an inflated unresolved figure, not a
corrupted graph.

A spurious *edge* survives in one case only: an array whose name is also
a subprogram's somewhere in the project. That is rarer than "every
indexing expression", and it is the case the fan-out and cycle warnings
above actually apply to. Pinned by the Ada case in
`test/fixtures/graph/`, so a regression either way is a diff rather than
a discovery.

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
*   **The comment exclusion is byte-granular, and the line-granular
    version was written first** (Phase 3). Comment spans are byte
    ranges, and asking whether a *statement's line* touches one is a
    different question with a wrong answer: on
    `int n = 0;   /* note */` the line touches a comment and the
    statement does not, so the first implementation silently deleted a
    line of code. The fixture pins the case, because it is the kind of
    thing a later change to the exclusion would break without noticing.
*   **HLR-016's merge is a guard, not a subtraction** (Phase 3). ELOC
    counts lines carrying statements, statements come from the syntax
    tree, and a parser does not produce a statement inside a comment —
    so the merged comment set and the ELOC line set are disjoint by
    construction, and the exclusion never fires for a correct query
    file. It is implemented anyway, because it is what the requirement
    asks for and because it is the guard that catches a *wrong* query
    file. The merge itself is load-bearing regardless: its returned
    line count is a reported quantity in its own right, and the
    coalescing is what a nesting language (Rust, Ada) will need. If a
    later phase finds a real subtraction case, this note is the record
    that the current answer was reasoned about rather than assumed.
*   **`merge_comment_spans` counted a shared line twice**, and a unit
    test caught it (Phase 3). Two comments on one line are two disjoint
    byte ranges — neither contains the other, so they do not coalesce —
    and summing per-span line counts reported that one line as two.
    LLR-MRG-03 says no line is excluded more than once; the return value
    now counts distinct lines.
*   **HLR-048 (exception handling counts toward ELOC) cannot be
    verified in C**, which has no such construct. It is the one ELOC
    category with no test, and it stays uncovered until C++ arrives in
    Phase 6. Recorded here so the gap is a known one rather than an
    oversight found later.
*   **`complexity.scm` is run over the whole tree, not against each
    `@function.body`** (Phase 4). The SDD said the latter and the
    published contract still describes the *scope* that way, which is
    the obvious reading and the wrong mechanism: running per body gives
    an enclosing function every decision point its nested functions
    contain, and needs a subtraction to undo. Running once and
    attributing by `innermost_enclosing` makes HLR-018 and HLR-068 fall
    out of the same pass — a nested *named* function is reported, so it
    is its own innermost; an anonymous callable is not, so its decision
    points land on the named function around it. Amended in the SDD.
*   **HLR-018 (anonymous-scope attribution) has no observable in C**,
    which has no lambdas or closures. The *mechanism* it constrains is
    unit-tested — an offset inside an unreported scope resolving to the
    named function containing it — but the language-level fixture waits
    for C++ in Phase 6. Different from HLR-048 above, where nothing at
    all can be checked today.
*   **`&&` and `||` each count as a decision point** (Phase 4). McCabe's
    original measure counts edges in the control-flow graph, and a
    short-circuit operator adds one: `a && b` can be decided two ways.
    The alternative reading — count only statements — makes a function
    built from one long compound condition score the same as a function
    with no condition at all, which is the opposite of informative.
    `goto` is *not* counted, on the same reasoning read the other way:
    it moves control without choosing.
*   **Code inside `#if 0` counted toward ELOC** (Phase 3) — *until
    Phase 15, which reversed it*. `elc` parses; it does not run the
    preprocessor, so
    `tree-sitter-c` yields the statements inside a disabled block and
    they are counted like any other. The reasoning at the time: a
    preprocessing pass would be a second parse of the same file, which
    HLR-076 forbids, and a heuristic over `#if` conditions would be the
    textual approximation HLR-013 forbids. The note ended "if it proves
    to matter, the honest fix is a query-level exclusion, not C."

    **It proved to matter, and that is the fix** — HLR-131 through
    HLR-136, delivered in Phase 15, and the query-level exclusion is
    exactly the shape the original note predicted. Worth being precise
    about what did
    and did not change, because the two look alike:

    *   `elc` still runs **no preprocessor** (HLR-135). Not `cpp`, not
        `rustc --cfg`, not a build system, and it reads no file the
        source includes. A result that depended on which toolchain was
        installed would not be a property of the source.
    *   HLR-013 is untouched, because the condition is evaluated from
        the *parsed condition nodes* rather than matched in the text.
        That distinction is the whole of why this is admissible: `#if`
        is a construct `tree-sitter-c` already parses, and reading a
        tree it produced is not a textual approximation.
    *   HLR-076 is untouched: pruning happens on the one tree, after
        parsing, and reads no file twice.
    *   Which constructs are conditional, and where their conditions
        sit, is runtime data — a seventh, *optional* query file
        (HLR-134). A C `#if` and a Rust `#[cfg]` are one mechanism, and
        the required six are unchanged, so no shipped module breaks.

    The cost is a genuine scope limit, stated in HLR-133 rather than
    discovered later: only a literal condition or a definedness test
    over symbols the user named is decided. `#if VERSION > 2` is
    **undecidable, not false** — both branches stay counted and the
    region is reported undecided. Treating it as false would silently
    delete code and produce a report indistinguishable from a correct
    one, which is the failure mode this whole design is arranged
    against.

    Two things the implementation settled that the specification left
    open, both recorded in the SDD:

    *   **A symbol no `-D` mentions is undecidable, not undefined.** A
        build may define it in a header or on a command line `elc` never
        sees, and `-D` can only assert definedness — there is no `-U`.
        That single rule is also what delivers HLR-131's "with no
        definitions, nothing changes": with an empty set every
        definedness test is undecidable, so nothing prunes, and no
        special case says so. A constant condition is different in kind
        and prunes whatever the definitions are, because `#if 0` means
        the same thing in every configuration.
    *   **The query decides truth; `elc` decides bytes.** A `.scm`
        captures `@conditional.true` or `@conditional.false` on a
        condition it recognises, and `elc` works out which bytes that
        excludes. Pointing a query at a span instead would make it
        responsible for knowing that a `#if` with an `#else` keeps half
        of itself — arithmetic, not a fact about C.

    One consequence for the gap baseline, since it is the one place the
    protocol's arithmetic runs backwards: specifying these eighteen
    requirements ahead of the phase that delivers them **raised** the
    count from 242 to 260, and the baseline was raised to match. Step 8
    reads a rise as "requirements were added without tests", which is
    exactly what happened and is exactly what was intended — a
    requirement recorded and not yet built is honest, and the
    alternative is not recording it. The count falls again when Phase 15
    lands. This is the only circumstance in which raising the baseline
    is not a regression, and it is worth being suspicious of any other.
*   **Four grammars were added without touching `src/`** (Phase 6),
    which is HLR-010's claim demonstrated rather than asserted. Four
    things the exercise taught, worth knowing before a fifth:
    a capture name is a contract and a node type is not — every
    disagreement between the languages lives in a `.scm` file;
    anonymous callables need *no* pattern, because not capturing them is
    what makes HLR-018 work; `(node (_))` is not `(node (specific))` —
    Ada's `(object_declaration (_))` matches every declaration, since
    every one has a name and a subtype as named children, and the first
    version of that pattern was exactly that mistake; and an anchor does
    work a field cannot, since Rust's tail expression has no field name
    and `(block (_) @x .)` is the only way to reach it.
*   **`$(wildcard)` in a recipe is expanded before the recipe runs**
    (Phase 6). The grammar rule used it to find an optional
    `scanner.c`, which meant looking for a file the `fetch` on the line
    above had not yet unpacked — it silently found nothing. C has no
    external scanner, so this was invisible until C++, Rust, and Python
    arrived with one each; a grammar linked without its scanner fails at
    load, not at build. A shell glob in the recipe is evaluated at the
    right time and is what the rule uses now.
*   **Ada's `when others` is counted as a decision and C's `default:`
    is not** (Phase 6). Ada writes the catch-all as an alternative like
    any other and the grammar does not mark it, so telling them apart
    would mean matching the text `others` — the textual approximation
    HLR-013 forbids. An exhaustive Ada case therefore scores one higher
    than the equivalent C switch. If the grammar ever distinguishes
    them, this is a one-line change in `ada/complexity.scm`.
*   **A Python module docstring counts as one line of ELOC** (Phase 6).
    It is an expression statement, not a comment, and the interpreter
    keeps it as `__doc__`. Treating it as documentary is defensible and
    is not what the grammar says; the `eloc/` fixture makes the choice
    visible rather than incidental.
*   **tree-sitter-c cannot parse two ordinary C constructs** (found in
    Phase 5, by `elc` failing to read its own source):
    `va_arg(ap, T)` for any *multi-token* type — `char *`,
    `const char *`, `unsigned long` all fail, while `va_arg(ap, int)`
    parses — and a macro standing between a function's return type and
    its name, as in Expat's `static void XMLCALL handler(...)`. Both
    yield an error node, and HLR-035 then skips the whole file and
    degrades the run to exit 1, which is correct behaviour over a
    grammar that cannot read the input.
    Both are common in real C, so this is a product limitation and not
    only a self-analysis nuisance. The project's own source avoids them
    — a single-token typedef for the `va_arg` type, and no `XMLCALL`,
    which names a calling convention only on Windows and `elc` is
    POSIX-only. Worth reporting upstream, and worth re-testing when the
    grammar pin moves; LLR-BLD-14 records the constraint on our source.
*   **The XML reader recomputes rather than trusts** (Phase 5). It
    rebuilds only the per-file and per-function *measurements* from a
    record and calls the same `report_assemble` a live run calls, so
    the totals, the callouts, the ordering, and the threshold listing
    are derived once by one function on both paths. HLR-056's
    byte-identical guarantee is then structural. Rebuilding the derived
    values from the record — which the record does carry, for other
    consumers — would have made the requirement a claim that two
    implementations agree, which they would eventually stop doing.
*   **The threshold is deliberately absent from the record.** It stores
    what was measured; a threshold is what somebody decided about it.
    Keeping them apart is the whole of HLR-057 and is why one record
    answers any number of threshold questions. If a later phase is
    tempted to record it "for reference", note that a recorded
    threshold and a supplied one would then disagree, and something
    would have to choose.
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
*   **The Makefile's help text moved into its header** (Phase 3). It
    used to be reconstructed by `awk`-ing a `## <description>` off each
    target declaration, which could not drift but gave a reader who
    *opened* the file nothing at all. It is now a `#>`-marked block at
    the top that `make help` prints verbatim. The trade is that a
    hand-maintained block can go stale, which the old one could not, so
    an instrumented test compares it against the `.PHONY` declarations
    and fails on either kind of drift. The `##` annotations are gone
    rather than kept alongside: two mechanisms for one text is how the
    two come to disagree.
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
*   **`make clean` destroys the prerequisite source builds** (Phase 8).
    `SRC_WORK` defaults to `$(BUILD)/prereq-src`, so a `make clean`
    between building a library and installing it throws away the build
    — which is minutes of `cmake` for igraph. It was found the obvious
    way. The fix is to move `SRC_WORK` outside `$(BUILD)`, or to have
    `clean` spare it; neither has been done, because the prereq targets
    are normally run once on a new machine and the coupling has not
    otherwise bitten. Worth doing the next time the prereq recipes are
    touched.
*   **The SDG has two views, and using the wrong one is a silent
    error** (Phase 9). `graph.c` holds the full graph and a call-only
    view; every call-tree analysis reads the second. A global-state
    edge joins a writer to a reader, and two functions that share two
    objects in opposite directions form a cycle in the *full* graph —
    which, measured there, is reported as mutual recursion and a
    critical MISRA C Rule 17.2 violation against perfectly ordinary
    code. Depth has the same shape of bug: a chain following a global
    edge counts a stack frame for a call that never happens. Neither
    would look wrong in the output. Phase 10's reachability analysis
    faces the same choice and should reach for `call_graph` as well —
    with the nuance that a function reachable only through a global is
    still not *called*, so reachability is a call question too.
*   **Depth counts the entry point as a layer** (Phase 9). An entry
    point calling nothing is depth 1. The alternative — counting edges
    rather than functions — reads more naturally as "how many calls
    deep", but makes a leaf entry point depth 0, which is
    indistinguishable from "not measured" in a report where omission is
    a real outcome. Recorded because the choice is arbitrary and a
    later reader will wonder.
*   **Calls resolve by name alone** (Phase 8). There is no type
    resolution, so a method call resolves on the method name and a name
    defined twice resolves to its first definition in sorted file order
    (with a diagnostic). For C this is nearly exact — two `static`
    helpers sharing a name is the main case. For C++ and Rust it is a
    real approximation: two classes with a `size()` method are one node
    as far as resolution is concerned. Doing better needs a symbol table
    with scope and type information, which is a different kind of tool
    from a query-driven one; the honest position is that the graph is
    name-resolved and the manual says so. Revisit if the C++ numbers
    turn out to be useless in practice rather than merely approximate.
*   **The over-broad captures are load-bearing, not sloppy** (Phase 8).
    `@call.address_taken`, `@global.read` and `@global.write` capture
    identifiers in value position, most of which are variables. This
    looks wrong in a query file until you notice that neither question
    — is this a function? is this a global? — can be answered from one
    file's syntax, which is all a query sees. Resolution against the
    project tables is what filters them, in `graph.c`. Anyone tightening
    these patterns to "only capture real globals" will be encoding scope
    rules the grammar cannot express, and will silently lose the
    cross-file cases that motivate the analysis.
*   **Repository applicability is decided by the result of the walk,
    not by a question asked first** (Phase 7). `walk_git_tree` returns
    the number of files it appended, and a negative return means
    *inapplicable*; the caller falls back on that. The alternative —
    asking libgit2 whether the target is tracked, then walking — is two
    traversals that can disagree, and the disagreement would show as an
    empty report rather than as an error.

    The subtlety, which was got wrong first: the walk counts blobs at
    or beneath the target *before* the exclusions, and applicability
    turns on that count rather than on how many files survived. Deciding
    it on the survivors makes a tracked directory holding only excluded
    files look untracked, so the run falls back to the filesystem and
    analyses the untracked files this route exists to exclude. An
    applicable repository can therefore legitimately return 0, and the
    two zeroes — "tracked, nothing to analyse" and "not tracked" — are
    what the sign distinguishes. The cost of the choice is
    that "the repository tracks nothing under this target" and "the
    repository has no commits" are indistinguishable to the caller.
    That is acceptable because both mean the same thing to the user and
    both take the same action, but a future need to tell them apart is
    a change to this return convention, not an addition to it.
*   **`libgit2` is the only linked library that can open a socket**
    (Phase 7). It speaks smart-HTTP and SSH. `elc` calls only its local
    object-database entry points, and the instrumented allowlist in
    `test/instrumented/environment.bats` says so — but that is a claim
    about our code, which the allowlist cannot check. What holds it is
    the no-network-syscall test observing a real run. Before Phase 7
    that test guarded against a dependency nobody had; now it guards
    against a dependency we have. Note also that the library is built
    with its transports disabled (`Makefile`), so the claim has a
    second, independent guard — and if that build flag is ever dropped
    for convenience, the syscall test is the one that will notice.
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
