# elocker (`elc`)

**Got a codebase and want to determine the scale of it, the quality of the code,
and where it can be improved?** `elc` parses your source instead of guessing at
it, and reports both: per-function metrics and whole-project architecture
analysis, for many languages.

**Contents:** [Status](#status) ·
[What it does](#what-it-does) ·
[Why another metrics tool](#why-another-metrics-tool) ·
[Adding a language](#adding-a-language-costs-no-rebuild) ·
[Example output](#what-the-output-looks-like) ·
[How it works](#how-it-works) ·
[Documentation](#documentation) ·
[Building](#building) ·
[What it's not](#what-elc-is-not) ·
[Contributing](#contributing)

## Status

**The specification is complete; implementation has not begun.**

**Progress: 0 of 16 phases complete.**

<details>
<summary><strong>Phase-by-phase status</strong> (click to expand)</summary>

| Phase | Description | Status |
| ----- | ----------- | ------ |
| [0](doc/SDP.md#phase-0--foundation-and-continuous-integration) | Build system, CI pipeline, test harness, skeleton binary | 🔲 Not started |
| [1](doc/SDP.md#phase-1--target-discovery-and-the-walking-skeleton) | Target discovery, ordering, table output — end to end | 🔲 Not started |
| [2](doc/SDP.md#phase-2--language-runtime-and-function-discovery) | Runtime loading, Tree-sitter parse, function identity | 🔲 Not started |
| [3](doc/SDP.md#phase-3--effective-lines-of-code) | ELOC, comment merging, file and project totals | 🔲 Not started |
| [4](doc/SDP.md#phase-4--cyclomatic-complexity) | Complexity, threshold listing, most-complex callouts | 🔲 Not started |
| [5](doc/SDP.md#phase-5--output-formats-and-the-saved-record) | CSV, XML, Markdown, escaping, regeneration mode | 🔲 Not started |
| [6](doc/SDP.md#phase-6--language-breadth) | C++, Rust, Python, Ada — data only, no C change | 🔲 Not started |
| [7](doc/SDP.md#phase-7--git-aware-discovery) | Repository detection, applicability, scoping, routes | 🔲 Not started |
| [8](doc/SDP.md#phase-8--system-dependence-graph) | Cross-file resolution, the SDG, GraphML export | 🔲 Not started |
| [9](doc/SDP.md#phase-9--call-tree-analyses) | Fan-out, depth, deepest stack, recursion | 🔲 Not started |
| [10](doc/SDP.md#phase-10--reachability-and-global-state) | Dead code, global coupling, hidden channels, scopes | 🔲 Not started |
| [11](doc/SDP.md#phase-11--coupling-layering-and-cycles) | Strata, skip-level, Ca/Ce, instability, cycles | 🔲 Not started |
| [12](doc/SDP.md#phase-12--thresholds-severity-and-attribution) | The Appendix A catalogue, severity, attribution | 🔲 Not started |
| [13](doc/SDP.md#phase-13--graph-visualisation) | Annotated Graphviz `.dot` companion | 🔲 Not started |
| [14](doc/SDP.md#phase-14--custom-rules) | User-supplied `.scm` rules, binding, matching | 🔲 Not started |
| [15](doc/SDP.md#phase-15--hardening-and-release-readiness) | Full sanitizer sweep, self-analysis, coverage closure | 🔲 Not started |

</details>

**Metrics land in Phases 3–4, the call graph in Phase 8, and the architectural
analyses in Phases 9–13.** If you only want ELOC and complexity, Phase 4 is the
one to watch.

---

## What it does

`elc` points at a file, a directory, or a Git repository and tells you two
kinds of thing:

**Which functions carry the code and the complexity.** Effective Lines of Code
and cyclomatic complexity, reported **per function** — name, line range, ELOC,
complexity — rather than aggregated per file where the problem function hides
inside a large one.

**How the system hangs together.** By stitching the per-file syntax trees into
a project-wide **System Dependence Graph**, it answers the questions that
line counters cannot: what depends on what, where the dependency cycles are,
which components are architectural bottlenecks, how deep the call stack can
actually get, and which functions are provably unreachable.

## Why another metrics tool

Most tools in this space report per *file* and per *language*, so a polyglot
repository needs several of them that disagree with each other, or one large
platform. `elc` is built on a few decisions that follow from that:

| Decision | Why |
| -------- | --- |
| **Per function, not per file** | A 2,000-line file tells you nothing about which of its forty functions nobody wants to touch. |
| **Parses, never guesses** | Every metric comes from a real syntax tree. Nested block comments, comment syntax inside string literals, and unusual comment forms are counted correctly — the cases where regex counters are silently wrong. |
| **One definition across every language** | The same notion of ELOC and complexity applies to C and to Python, so numbers from different parts of a repository are comparable. |
| **Deterministic output** | Identical input produces byte-identical output regardless of traversal order or filesystem. The results can be diffed, piped, estimated from, and compared between codebases or between versions of one. |
| **Architecture, not just size** | Dead code proven by graph reachability rather than guessed at by pattern matching — including the case that fools textual linters, where unused functions call one another. |
| **Measures, never lectures** | Findings are reported against *published* thresholds (MISRA C, Robert C. Martin's Instability metric, Henry–Kafura), each attributed to its source. `elc` proposes no fixes and holds no style opinions of its own. |
| **Small and self-contained** | One C11 binary, four libraries, a POSIX libc. No interpreter, no virtual machine, no network access, no plugin ecosystem, no server. |

## Adding a language costs no rebuild

Everything language-specific lives in data, never in the binary. A language is
a Tree-sitter grammar shared object plus a set of Scheme query files in a
runtime directory:

```text
runtime/
├── extensions.map          # "<ext> <lang>" per line
├── parsers/<lang>.so       # exports tree_sitter_<lang>
└── queries/<lang>/
    ├── comments.scm  functions.scm  complexity.scm
    ├── eloc.scm      calls.scm      globals.scm
    └── rules/*.scm         # your own coding standard, optional
```

No language name, file extension, or grammar node type appears anywhere in the
C source. Adding a language means adding a directory — and the same mechanism
is open to you: a team's own coding standard is expressed as `.scm` queries and
checked by the same engine that produces the built-in metrics.

**Planned initial support:** C, C++, Rust, Python, and Ada.

## What the output looks like

> *Illustrative only — this shows the specified output shape, not a real run.
> See [PVD.md](doc/PVD.md) §7.1 for the authoritative scope.*

```console
$ elc src/
Project: 4,182 physical lines, 2,317 ELOC  (C 1,904 · Python 413)

  Route: src/ — filesystem traversal

  src/analyze.c            612 lines   381 ELOC
    ⚠ merge_spans          142–198      41 ELOC   complexity 17

  src/graph.c              498 lines   309 ELOC

  Architecture
    ✖ cycle          graph.c → arch.c → graph.c
    ⚠ bottleneck     report.c   Ca 9  Ce 6   (elc heuristic, not a standard)
    ⚠ fan-out        dispatch()  12 callees        (Henry–Kafura: >10)
      deepest chain  main → run → analyze → parse → visit   (4 layers)
    ✖ unreachable    legacy_dump(), legacy_fmt()   [util.c]
```

Reports render as an aligned table (default), CSV, XML, or GitHub-Flavored
Markdown, with a Graphviz `.dot` call tree written alongside. The XML form is a
complete record of a run, so a report can be regenerated later against a
different complexity threshold without re-analysing the source.

## How it works

A one-way pipeline of fifteen translation units. Each stage consumes the
previous stage's output; no stage reaches backwards, and **no source file is
read twice**.

```text
cli → discover → registry → analyze ─┬─→ report → format
                                     │      ↑
                                     └→ graph → arch / calltree / state → thresholds
```

`analyze` is the only module that touches source text, and it extracts the
per-function metrics *and* the graph facts in a single traversal. That is what
makes the single-parse guarantee structural rather than a discipline the code
has to remember.

The design is documented in full in [SDD.md](doc/SDD.md).

## Documentation

This project is specified before it is built, and the specification is the
current deliverable. It is managed with [TraceR](https://github.com/racerxr650r/TraceR):
[`doc/Project.xml`](doc/Project.xml) is the single source of truth, and the
documents below are generated from it.

| Document | Answers |
| -------- | ------- |
| [PVD.md](doc/PVD.md) | *Why* does this exist, who is it for, how do we know it is succeeding? |
| [HLRs.md](doc/HLRs.md) | *What* must it do — 129 high-level requirements |
| [SDD.md](doc/SDD.md) | *How is it structured* — modules, data, algorithms, dependency selection |
| [LLRs.md](doc/LLRs.md) | *How does each function contribute* — 249 low-level requirements |
| [STP.md](doc/STP.md) | *How is it verified* — test levels, fixtures, the sanitizer gate |
| [Traceability.md](doc/Traceability.md) | *Where are the gaps*, end to end |
| [SDP.md](doc/SDP.md) | *How is it built* — the 16 phases below |

## Building

*Not yet available — Phase 0 delivers the build system.* When it does:

```sh
make            # prints the target list
make all        # builds build/elc
make test       # unit, integration, fixture, and instrumented suites
make asan       # the whole suite under AddressSanitizer and UBSan
make install    # binary, runtime/, man page, and user manual
```

### Dependencies

| Library | Used for |
| ------- | -------- |
| [`libtree-sitter`](https://tree-sitter.github.io/tree-sitter/) | Parsing and query execution |
| [`libgit2`](https://libgit2.org/) | Repository-aware file discovery |
| [`igraph`](https://igraph.org/c/) | Graph algorithms — cycles, reachability, centrality |
| [Expat](https://libexpat.github.io/) | Streaming XML read for report regeneration |
| POSIX libc | `mmap`, `fts`, `dlopen` |

Build tooling: GNU make, a C11 compiler, GNU ld or lld (for `--wrap`),
[Criterion](https://github.com/Snaipe/Criterion) and
[Bats](https://github.com/bats-core/bats-core) for tests, and Python 3 for the
documentation toolchain.

## What `elc` is not

*   Not a linter, style checker, or formatter. It measures and reports; it
    never proposes a fix.
*   Not a static-analysis platform. Two metrics and a dependency graph, done
    carefully.
*   Not a server, daemon, or hosted dashboard, and not an editor plugin.
*   Not a source of historical trends — it reports one run, deterministically
    enough that you can diff two of them yourself.

## Contributing

The project is in its implementation phases; see [SDP.md](doc/SDP.md) for what
is being built and in what order. Because it is specified before it is built,
a behaviour change is a change to [`doc/Project.xml`](doc/Project.xml) as well
as to the source — a pull request that alters behaviour without a requirement
and a test tracing to it is incomplete by design.

## License

[MIT](LICENSE)
