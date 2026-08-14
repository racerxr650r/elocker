# Working Notes

Scratch material that does not belong in a specification. Nothing here
is a requirement, nothing traces, and nothing binds.

The PVD, HLRs, SDD, LLRs, STP, and SDP are all written, the traceability
matrix is complete in both directions, and the sections of this file that
fed them have been deleted as their content was absorbed — which is the
rule this file runs on. What remains is the residue: decisions still
open, and findings worth not rediscovering.

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
