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

**Phase 23 is complete.** `elc src/` reports **effective lines of code** and
**cyclomatic complexity** per function across **C, C++, Rust, and Python**,
in one invocation over a mixed target, as a table, Markdown, CSV, or
XML. Inside a Git repository it analyses **the files tracked at `HEAD`**, and
a directory the repository does not track is traversed instead; every report
names the route it used.

A report that goes to a file takes its format from the filename: `-o
report.csv` writes CSV, and an extension `elc` does not recognise is a usage
error rather than a guess. A report that goes to standard output still takes
`-f`. By default the report is a **summary** — the totals, the findings, and
the provenance, sized to be read in a terminal; **`--verbose`** adds the
per-function and per-entity tables behind it. Verbosity is presentation
only, so `-f csv` and `-f xml` are complete either way.

It also builds the **System Dependence Graph** — every call resolved across
file boundaries, every global linked from its writers to its readers, all
from the same single parse that produced the metrics. `--graphml` exports it.

Reading that graph, `elc` reports each function's **fan-out** and
**fan-in** — callees and callers, both counted distinctly and over calls
alone, so coupling through a shared global counts towards neither — weighs the
two against the function's length as the **Henry–Kafura** information-flow
value, `ELOC × (Fan-In × Fan-Out)²`, and totals that across the project. It
attaches no severity to the figure and no threshold: no published source bands
it, and inventing one for a metric whose name reads as a citation is the last
place to start. It detects **recursion** both direct and mutual, and prints
the **deepest call chain in full** from entry points you declare with
`--entry`. It never guesses at an
entry point, and never invents a number it cannot stand behind: where the
call graph is recursive the depth is unbounded and the cycle is reported
instead, and where a declaration is missing the analysis is omitted with the
reason stated.

It now also answers the question the whole tool is for — **what code does not
run?** — at both scales. Between functions, `elc` traverses the call graph
from a root set that is the entry points you declared **together with every
function whose address is taken**, so a clique of unused functions calling
one another is correctly reported dead while an interrupt handler installed
in a vector table is not; globals every accessor of which is unreachable go
the same way. Within a function, it reports statements after a `return` and
branches a literally written condition excludes — from the syntax tree alone,
with no data flow, so `if (0)` is found and `x = 0; if (x)` deliberately is
not. It reports each global's writers and readers, flagging single-function
objects for **scope reduction** and objects shared across disconnected
regions as **hidden channels**, and with `--scope` it reports every call and
shared variable by which one execution scope reaches another.

At the level of files rather than functions, it reports **afferent and
efferent coupling** per component with Martin's **Instability** beside it —
`undefined`, not zero, where a component has no relationships at all —
flags **bottlenecks** that are both widely depended upon and widely
dependent, and finds **circular dependencies between components**, reporting
each as the entangled group *and* a concrete loop through it. Declare your
layers with `--stratum` and it validates them: a call bypassing a layer is
**skip-level**, a call running against the declared direction is
**inverted**, and the two are independent — a call ascending two layers is
reported as both, because each has its own remedy.

Beside that list it now reports **how much of the code base conforms**: the
**Back-Call** and **Skip-Call Violation Indices**, each the share of the run's
*inter-layer call edges* that breaches the declaration in one of the two ways.
Both are counted from the violations listed above them rather than re-derived,
so the percentage and the table cannot contradict each other, and where there
is no inter-layer call at all both read `undefined` — a project whose layers
never call one another has demonstrated nothing either way, not perfect
conformance. They are never summed: a call that both skips and inverts is
counted once in each, and each names its own remedy.

And it draws the **Dependency Structure Matrix** — rows callers, columns
callees, in ascending layer order, so the back-calls gather below the diagonal
where you can see them at a glance and their total is exactly the inverted
calls the layering table lists. Declare nothing and you still get one, over the
analysed directories; `--dsm` writes it beside the report as CSV.

Every one of those measurements is then evaluated against the published
catalogue of **MISRA C**, **Robert C. Martin** and **Henry–Kafura**, and
reported with a severity and, crucially, **the source that draws the line**.
One module does all the judging, so the claim that `elc` carries no opinion of
its own is something you can check by reading a single table — and the one
threshold that *is* `elc`'s own says `elc heuristic — not a published
standard` wherever it appears. Severity is a label and never the exit status:
a project full of critical findings still exits 0, because deciding what a
finding warrants is your call.

And with a report going to a named file, all of it is **drawn**: `elc` writes
the call tree beside the report as an annotated Graphviz `.dot` file, one
cluster per source file and one node per function, with every finding on an
attribute a renderer is free to ignore. Recursive cycles get a second border,
hidden-channel participants an octagon, unreachable functions a dash, the
deepest chain a thick blue line through it, and each node a tooltip carrying
its findings in full. `elc` writes the file and renders nothing — Graphviz is
yours to run on it, and `elc` neither links it nor invokes it. Generation is on
by default; `--no-dot` declines it.

```sh
elc --entry main -o report.md src/    # writes report.md and report.dot
dot -Tsvg report.dot -o report.svg
```

And you can bring **your own rules**. A custom rule is a Tree-sitter query you
write, checked against your source by the same mechanism that produces `elc`'s
own metrics — during the same parse, with the same predicate handling. Put it
in the runtime location or name it with `--rules lang:path`; either way, adding
one is a file, not a rebuild.

```sh
elc --rules c:house-style.scm src/
```

A rule's identity is the file's basename plus the capture name that matched, so
one file expresses as many named rules as it holds captures. And `elc` reports
what your rule matched and forms **no opinion about it** — no severity, no
citation, because you decided the rule was worth writing, not `elc`. Matches
get a section of their own beside the findings and never appear among them.

And you can say **which configuration** you mean. Conditionally compiled
source describes several programs; measuring it without saying which gives you
the union of them all. `-DFEATURE` measures the build in which `FEATURE` is
defined:

```sh
elc -DFEATURE_X -DTARGET=stm32 src/
```

`elc` runs no preprocessor — no `cpp`, no compiler, no build system, and it
reads no file your source includes. It decides each region from the tree it
already parsed, which means it decides only what it honestly can: a constant
condition like `#if 0`, or a definedness test over a symbol you named. Anything
else is **undecidable rather than false** — both branches stay counted and the
region is reported in an `Undecided regions` figure, because silently deleting
code you did not ask to delete is the one failure this design is arranged
against. A symbol you did not name is undecidable too: it might be defined in a
header `elc` will never see.

Which constructs count as conditional is data, not code, so a C `#if` and a Rust
`#[cfg]` are the same mechanism.

And where you have a **linked image**, you can measure the program your build
actually produced rather than the source it was drawn from:

```sh
elc --elf build/app.elf src/
```

Every measurement is then restricted to the functions that image defines. This
is the same question `-D` answers and a different way of answering it: `-D`
*re-decides* the conditions your build resolved, while an image *observes what
your build did*. Neither replaces the other — the image says which functions
survived and nothing about which lines inside one were compiled out — and you
can give both.

`elc` invokes no toolchain to read it: no `nm`, no `objdump`, no `readelf`, no
compiler, no linker. It opens the file you named, reads the symbol table a
linker writes by default, and closes it. A symbol counts only if it is a
function **and** defined by the image rather than imported by it, so a function
your program merely calls into libc is not mistaken for one it contains. C++
and Rust names arrive mangled and are decoded and reduced to the identifier the
report presents, so `geometry::Rect::area() const` matches a function reported
as `area`.

Two things are then reported, and they are different claims. The **linkage
names `elc` could not decode** state how complete the filter is — the claim the
unresolved-call count makes about the graph. The **source functions the image
does not define** are the finding the option exists to produce: dead code
established by what your linker did, rather than inferred from a traversal.

Where the image also carries **debug line information** — where it was built
with `-g` — `elc` reads that too, and narrows one granularity further: from the
functions the link kept to the **lines inside them the compiler emitted an
instruction for**. This is where an image outreaches `-D`. A region guarded by
a symbol you never restated is one `elc` cannot decide from the source, so it
is left whole and counted undecided; the image settles it, because the build
compiled nothing there and the mapping says so. No option is needed and none
exists: an image without debug information behaves exactly as it did before.

Absence of a line proves nothing where coverage was never established, so
coverage is settled **per file** first — a translation unit compiled without
`-g` loses not one line and is counted instead. Two figures state both halves,
and are read the way the unresolved-call count is: **lines not compiled by this
build**, and **files with no debug coverage**.
Code outside any function is retained and counted on its own, because an
image's function set says nothing about a table of data. And a stripped image
is an error rather than an empty filter, because reporting a project with no
functions in it would be confidently wrong and indistinguishable from a correct
result.

A raw call graph rarely sorts into layers, so `elc` builds a second graph — a
**recovery view** — with the functions that fuse unrelated domains set aside: a
**utility sink** everything calls loses its incoming edges, a **god object**
that dispatches everywhere loses its edges in both directions, and a
**peripheral** function outside the connected centre is left out entirely. The
report names every function it classified, the metric and value that classified
it, and what the masking did.

The view is a **copy**, and that is the point of it: no fan-out, coupling
figure, conformance index, or matrix cell anywhere else in the report is
computed over a masked graph. The five thresholds behind the classifications
are `elc`'s own heuristics rather than published standards, are compared
against a function's rank rather than its raw score so that one default serves
a small project and a large one, and say so wherever a classification appears.
No classification carries a severity: `elc` says where a function sits in a
graph, not that the design is wrong.

From what remains, `elc` reads a **layering** — a description of the
architecture your code already has, for a reader who has declared none. It
orders the purified view and folds the order by directory, placing each
directory where the bulk of its edges point rather than at its outermost
member, so one function reaching far down cannot drag its whole directory with
it. Where the view is still cyclic there is no ordering to have, and the
mutually reachable groups are reported in its place.

**A recovered layering is a proposal and never a baseline.** Nothing is
measured against it: the `--stratum` declarations remain the sole standard the
conformance analyses judge by, and with none declared those analyses stay
omitted with their reason stated — however confidently a layering was
recovered. A tool measuring conformance against its own proposal would find
every code base conformant, because the standard would have been read off the
thing being judged. So the proposal arrives as an **argument list** in the form
`--stratum` and `--stratum-order` accept: read it, and if you agree, paste it
back. The declaring is yours.

The classifications behind all of that are heuristics, and heuristics have
false positives — a state machine's dispatcher looks exactly like a monolith
from inside the graph. `--write-manifest` writes them out as JSON, one
statement per classified function; edit the one you disagree with and hand it
back with `--manifest`. Your statement governs, `elc` does not recompute it,
and the report says which rows came from you and which from the tool. A
manifest is read **only when you name it** — never from the working directory,
the target, an ancestor, or a dotfile. And `--purify-dot` draws the graph twice,
before and after, with the masked functions greyed and detached rather than
deleted, so you can see what was set aside before deciding to trust it.

**Progress: 23 of 24 phases complete.**

<details>
<summary><strong>Phase-by-phase status</strong> (click to expand)</summary>

| Phase | Description | Status |
| ----- | ----------- | ------ |
| [0](doc/SDP.md#phase-0--foundation-and-continuous-integration) | Build system, CI pipeline, test harness, skeleton binary | ✅ Complete |
| [1](doc/SDP.md#phase-1--target-discovery-and-the-walking-skeleton) | Target discovery, ordering, table output — end to end | ✅ Complete |
| [2](doc/SDP.md#phase-2--language-runtime-and-function-discovery) | Runtime loading, Tree-sitter parse, function identity | ✅ Complete |
| [3](doc/SDP.md#phase-3--effective-lines-of-code) | ELOC, comment merging, file and project totals | ✅ Complete |
| [4](doc/SDP.md#phase-4--cyclomatic-complexity) | Complexity, threshold listing, most-complex callouts | ✅ Complete |
| [5](doc/SDP.md#phase-5--output-formats-and-the-saved-record) | CSV, XML, Markdown, escaping, regeneration mode | ✅ Complete |
| [6](doc/SDP.md#phase-6--language-breadth) | C++, Rust, Python — data only, no C change | ✅ Complete |
| [7](doc/SDP.md#phase-7--git-aware-discovery) | Repository detection, applicability, scoping, routes | ✅ Complete |
| [8](doc/SDP.md#phase-8--system-dependence-graph) | Cross-file resolution, the SDG, GraphML export | ✅ Complete |
| [9](doc/SDP.md#phase-9--call-tree-analyses) | Fan-out, depth, deepest stack, recursion | ✅ Complete |
| [10](doc/SDP.md#phase-10--dead-code-reachability-and-global-state) | Dead code within and between functions, global coupling, scopes | ✅ Complete |
| [11](doc/SDP.md#phase-11--coupling-layering-and-cycles) | Strata, skip-level, Ca/Ce, instability, cycles | ✅ Complete |
| [12](doc/SDP.md#phase-12--thresholds-severity-and-attribution) | The Appendix A catalogue, severity, attribution | ✅ Complete |
| [13](doc/SDP.md#phase-13--graph-visualisation) | Annotated Graphviz `.dot` companion | ✅ Complete |
| [14](doc/SDP.md#phase-14--custom-rules) | User-supplied `.scm` rules, binding, matching | ✅ Complete |
| [15](doc/SDP.md#phase-15--conditional-compilation) | `-D` definitions, inactive-region pruning | ✅ Complete |
| [16](doc/SDP.md#phase-16--elf-filtered-analysis) | `--elf` image filter, linkage-name resolution, unmatched reporting | ✅ Complete |
| [17](doc/SDP.md#phase-17--hardening-and-release-readiness) | Full sanitizer sweep, self-analysis, coverage closure | 🔲 Not started |
| [18](doc/SDP.md#phase-18--output-format-selection-and-report-verbosity) | Format from filename extension, summary default, `--verbose` | ✅ Complete |
| [19](doc/SDP.md#phase-19--information-flow-complexity) | Per-function fan-in, Henry–Kafura complexity, project total | ✅ Complete |
| [20](doc/SDP.md#phase-20--debug-line-pruning) | DWARF line pruning of code the build did not compile | ✅ Complete |
| [21](doc/SDP.md#phase-21--architecture-conformance-measurement) | Conformance indices, the Dependency Structure Matrix | ✅ Complete |
| [22](doc/SDP.md#phase-22--graph-purification) | Centrality-based classification, the masked recovery view | ✅ Complete |
| [23](doc/SDP.md#phase-23--architecture-recovery-and-the-manifest) | Recovered layering, the purification manifest, visual diffing | ✅ Complete |

</details>

**Metrics land in Phases 3–4, the call graph in Phase 8, and the architectural
analyses in Phases 9–13.** If you only want ELOC and complexity, everything
through Phase 9 is already in place.

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

**Which of it your build actually keeps.** Name a configuration with `-D` or a
linked image with `--elf`, and the figures describe the program that ships
rather than the source it was drawn from.

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
| **Small and self-contained** | One C11 binary, five libraries, a POSIX libc. No interpreter, no virtual machine, no network access, no plugin ecosystem, no server. |

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

**Planned initial support:** C, C++, Rust, and Python.

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
| [HLRs.md](doc/HLRs.md) | *What* must it do — 135 high-level requirements |
| [SDD.md](doc/SDD.md) | *How is it structured* — modules, data, algorithms, dependency selection |
| [LLRs.md](doc/LLRs.md) | *How does each function contribute* — 296 low-level requirements |
| [STP.md](doc/STP.md) | *How is it verified* — test levels, fixtures, the sanitizer gate |
| [Traceability.md](doc/Traceability.md) | *Where are the gaps*, end to end |
| [SDP.md](doc/SDP.md) | *How is it built* — the 17 phases below |

## Building

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
| [`libelf`](https://sourceware.org/elfutils/) | Reading the symbol table of the image `--elf` names |
| The C++ runtime | `__cxa_demangle`, for C++ and Rust linkage names. Already linked — `igraph` is partly C++ inside |
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
