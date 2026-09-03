# Product Vision Document: elocker (elc)

**Version:** 0.7
**Date:** 2026-08-14
**Author(s):** John Anderson

## 1. Purpose

This Product Vision Document (PVD) defines *why* `elc`
exists, *who* it is for, *what* problem it solves, and the
*measurable outcomes* that determine whether it is succeeding. It
sits above the [Software Design Document](SDD.md), [High-Level
Requirements](HLRs.md), [Low-Level Requirements](LLRs.md), and
[Software Test Plan](STP.md), and is the document the rest of the
specification stack must remain aligned with.

When in doubt about a feature, scope decision, or trade-off, this
document is the reference.

## 2. Vision Statement

> **A developer can point one command at any codebase — a file, a
> directory, or a Git repository — and immediately see both which
> *functions* carry the code and the complexity, and how the system
> as a whole hangs together: what depends on what, where the cycles
> and bottlenecks are, how deep the call chains run, and what is
> provably dead — with numbers solid enough to estimate effort from
> and to compare, codebase to codebase or version to version.**

The user is a developer or maintainer deciding where to spend
refactoring effort, an architect checking whether the layering they
designed is the layering that actually exists, a program manager
estimating the cost of certifying a codebase, or a reviewer comparing
two codebases — or two versions of the same one — to see how they
diverge. Today each of these people either settles for file-level
line counts that hide the problem functions inside large files, or
reaches for a different, heavier tool for every language in the
repository, and ends up with output that cannot be trusted to mean
the same thing twice, let alone be diffed against another run. Worse,
the questions that matter most at the system level — does anything
still call this, do these modules depend on each other in a circle,
how deep can this call stack actually get — usually have no answer at
all short of reading the code. `elc` removes those compromises: one
binary, per-function numbers *and* a whole-project call graph, every
language handled the same way, and output deterministic enough to add
up, estimate from, and compare.

## 3. Problem Statement

Existing line-counting tools report per *file* and per *language*;
existing complexity tools are typically per *language*, and often
carry a runtime, a server, or a plugin ecosystem with them. A
polyglot repository therefore needs several tools that disagree with
each other, or one large platform.

*   **File-level counts hide the problem.** A 2,000-line file tells
    you nothing about which of its forty functions is the one nobody
    wants to touch.
*   **Regex and brace-matching counters are wrong at the edges.**
    Nested block comments, comment syntax inside string literals, and
    language-specific comment forms all produce silently incorrect
    counts.
*   **Every language means another tool.** Each has its own
    definition of a "line", its own complexity formula, and its own
    output format, so results cannot be compared across a repository.
*   **Adding a language means waiting.** Support for a new or niche
    language requires an upstream release, a plugin ecosystem, or a
    local rebuild of the tool.
*   **Output is not composable.** Human-formatted output has to be
    scraped before it can go into CI, a spreadsheet, or a document.
*   **Repository hygiene is manual.** Tools that are not
    Git-aware re-analyse vendored code, build output, and generated
    files unless the user maintains an exclusion list by hand.
*   **Architecture is invisible to line counters.** Per-function
    metrics say nothing about how the system fits together. Whether
    two modules depend on each other in a circle, whether application
    code reaches past an abstraction layer, how deep the call stack
    can actually get, and whether a function is still reachable at
    all are questions that usually have no answer short of reading
    the code — and regex-based linters answer the last one wrongly
    whenever dead functions call each other.

The cumulative effect is that code health is measured rarely,
inconsistently, and only for the languages that happen to be well
served — while architectural decay accumulates unmeasured entirely.
Both kinds of decision get made on intuition instead of evidence.

## 4. Target Users

| Persona | Needs from `elc` |
| ------- | ------------------------- |
| **Project / Program Manager** | Generate ELOC and complexity numbers that can be used to estimate the cost of certifying a given code base. |
| **Maintainer / tech lead** | Rank the functions in a repository by complexity so refactoring effort goes where it pays. Also, track code growth to recognize bloat before it's too late. |
| **CI / build engineer** | Machine-readable per-function metrics from a single static binary, with a meaningful exit status. |
| **Developer in a polyglot repository** | One consistent definition of ELOC and complexity across every language in the tree. |
| **Architect / systems engineer** | Verify that the layering, coupling, and call-depth of the delivered system match the design — and see the call graph rather than infer it. |
| **Embedded / resource-constrained developer** | Bound the maximum call-chain depth to reason about stack exhaustion, and prove which code is unreachable before pruning it. |
| **Toolsmith / platform engineer** | Add support for a new or in-house language by dropping in a grammar and its query files, and express the team's own coding standard as custom queries — no rebuild, no upstream dependency. |
| **Reviewer / auditor** | A Markdown table of the functions in a change, suitable for pasting into a review or a report. |

`elc` is **not** aimed at: developers wanting real-time in-editor
feedback, teams looking for a full static-analysis or lint platform,
or organisations wanting a hosted quality dashboard with historical
trends.

## 5. Value Proposition

`elc` reports per-function code size and complexity, and the
architecture that connects them, for any codebase by doing ten
things, in order:

1.  **Accepts any target** — a single file, a plain directory, or a
    Git repository, and any number of them in any combination — so
    the same command works everywhere without the user explaining
    what they pointed it at.
2.  **Detects language automatically, per file.** A mixed-language
    repository is analyzed in one pass; the user never tells `elc`
    which files are which language, and never invokes it once per
    language.
3.  **Skips what does not count.** In a Git repository, `.gitignore`
    is honoured and binary files are skipped automatically; outside
    one, binary extensions and hidden directories are filtered.
4.  **Parses, rather than guesses.** Every metric comes from a real
    syntax tree, so nested comments, strings containing comment
    syntax, and unusual comment forms are counted correctly.
5.  **Reports per function, not per file** — name, line range,
    effective lines, and cyclomatic complexity — so the user sees the
    unit they can actually act on.
6.  **Deducts comments accurately.** Overlapping and nested comment
    spans are merged before subtraction, so effective lines never
    double-count.
7.  **Stitches the files into one graph.** Per-file syntax trees are
    resolved against each other — calls, global reads and writes —
    into a project-wide **System Dependence Graph**, so the unit of
    analysis becomes the system, not the file.
8.  **Answers the architectural questions from that graph** —
    layering violations, coupling bottlenecks, dependency cycles,
    call-chain depth, hidden coupling through global state, and
    provably unreachable code — by graph mathematics rather than
    pattern matching.
9.  **Emits in the shape you need** — an aligned ASCII table for
    reading, CSV for spreadsheets and pipelines, XML as a complete
    unfiltered record of a run, Markdown for reviews and
    documentation, and Graphviz `.dot` for seeing the call tree
    rather than reading about it.
10. **Regenerates a report from a saved run.** A previously generated
    XML file can be converted to a Markdown report at any time,
    against a freshly chosen complexity threshold, without
    re-analyzing the original source — so a dataset captured today can
    be re-rendered, re-thresholded, or handed to someone else's
    toolchain later.

The unifying design choice: **everything language-specific lives in
data, never in the binary.** A language is a shared object plus a set
of Scheme query files in a runtime directory, which is what makes a
single, small, dependency-light tool able to speak every language the
user cares about — at the function level and at the graph level
alike. The same mechanism is open to the user: a team's own coding
standard is expressed as `.scm` queries and checked by the same
engine, so extending what `elc` looks for never means changing what
`elc` is.

## 6. Product Principles

These principles are the tie-breakers when requirements conflict.

1.  **Syntax trees or nothing.** Every metric is derived from a
    Tree-sitter parse and a `.scm` query. This rules out regular
    expressions, brace matching, indentation heuristics, and any
    other textual approximation, even where one would be faster or
    easier for a single language.

2.  **No language knowledge in the binary.** Language names, file
    extensions, grammar symbols, and AST node types live in the
    runtime directory and its query files. A language may never be
    added, fixed, or special-cased by editing C and recompiling.

3.  **Zero configuration.** Behaviour is determined by command-line
    arguments and the contents of the runtime directory. There is no
    configuration file, no dotfile discovery, and no implicit
    per-project state; two users running the same command on the same
    tree get the same answer. The declarations the graph analyses
    need — architectural strata, and the entry points reachability is
    measured from — are command-line arguments like any other. A
    project that finds them too unwieldy to pass explicitly is asking
    for a configuration file, and that is a vision-level decision,
    not a convenience to be added quietly.

4.  **`stdout` is data, `stderr` is conversation.** Results go to
    standard output and nothing else does. Identical input produces
    byte-identical output regardless of traversal order or
    filesystem, so the output can be diffed, piped, and asserted on.

5.  **Dependencies stay countable.** No interpreter, no virtual
    machine, no network access at any point, and no build-time code
    generation. Graphviz is a tool the user may run on `elc`'s
    output, never something `elc` links against. *Taking on* a
    dependency at all is a vision-level decision, not an
    implementation convenience — but *which* library fills a given
    role is a design decision. Every library this document names —
    `libtree-sitter` for parsing, `libgit2` for repository-aware
    discovery, `Expat` for XML, `igraph` for graph mathematics — is
    a **suggested candidate, not a commitment**; design is free to
    substitute a better fit. One caveat: Tree-sitter's query language
    and grammar format are visible to the user in the `.scm` files
    and runtime grammars they author, so replacing *it* would change
    the product, not merely the implementation.

6.  **Fail soft per file, fail hard on setup.** A file that cannot be
    parsed produces a diagnostic and the run continues; a missing or
    malformed runtime directory is fatal. Any file-level failure is
    reflected in a non-zero exit status — degraded results are never
    reported as success.

7.  **One parse, no copies.** Source is memory-mapped and read once;
    parser and query-cursor contexts are allocated once and reused
    for the whole run. Correctness may never be bought by re-reading
    or re-parsing a file — cross-file resolution and every graph
    analysis operate on what the single parse produced, never by
    going back to the source.

8.  **Verifiable.** Every behaviour worth describing is captured as
    an HLR/LLR with at least one bound test. The
    [Traceability Matrix](Traceability.md) is the contract.

## 7. Scope

### 7.1 In Scope

#### Per-function and per-file metrics

*   Effective Lines of Code (ELOC) per function, with comment lines
    deducted via merged comment spans.
*   Cyclomatic complexity per function, computed as one plus the
    decision points found within the function body.
*   File-level totals: physical lines and effective lines, plus the
    list of functions whose cyclomatic complexity meets or exceeds a
    user-supplied threshold (default: 15).
*   Project-level totals: physical lines and effective lines, both
    combined and broken down per source language.
*   Function identity: name, start line, and end line.
*   Three target types — regular file, plain directory, and Git
    repository — with the traversal strategy selected automatically
    per target. A single invocation accepts any number of targets in
    any combination of files and directories; a file reached through
    more than one of them is analysed and counted exactly once.
*   `.gitignore` awareness and binary-file skipping in Git
    repositories; binary-extension and hidden-directory filtering
    when falling back to filesystem traversal.
*   Automatic per-file language detection in mixed-language
    repositories, by file extension — the user never denotes which
    source files are which language.
*   Initial language support: **C, C++, Rust, and Python.**
*   Runtime-loaded language grammars and queries, discovered relative
    to the binary or via an environment variable.

#### Custom rules

*   **User-supplied `.scm` rule files.** A team can express its own
    coding standard as Tree-sitter queries and have `elc` check the
    codebase against them, using the very mechanism that defines the
    built-in metrics. A custom rule is *data* — a `.scm` file placed
    in the runtime location or named on the command line — never a
    code change and never a rebuild, exactly as a new language is.
*   Matches against user-supplied rules are reported alongside the
    built-in findings, identified by the rule that matched and by the
    file and line range of each occurrence. Consistent with §7.3,
    `elc` reports what the user's own rule matched and forms no
    opinion of its own about whether that rule is a good one.

#### Macro-architectural analysis

Per-file syntax trees are resolved against one another — calls,
memory references, and variable mutations across file boundaries —
into a project-wide **System Dependence Graph (SDG)**. Every item
below is an analysis over that graph.

*   **SDG construction.** Cross-file resolution of function calls and
    global-state reads and writes into a single directed graph
    spanning the whole target.
*   **Architectural layering validation.** Users declare
    architectural strata (for example Application Logic, Hardware
    Abstraction, Driver); `elc` flags "skip-level" calls that bypass
    a layer, such as application code reaching directly into driver
    logic.
*   **Coupling metrics.** Fan-in (afferent) and fan-out (efferent)
    per component, with components exhibiting both high fan-in and
    high fan-out flagged as architectural bottlenecks.
*   **Circular dependency identification.** Topological analysis of
    the graph to surface cycles (A → B → C → A) that prevent
    independent testing and deployment.
*   **Call-tree dimensionality.** Maximum *width* — nodes with
    extreme fan-out, indicating monolithic dispatchers and "god
    functions" that fail to delegate — and maximum *height*, the
    deepest call chain, which bounds stack consumption on
    resource-constrained targets and predicts latency spikes.
*   **The deepest call stack, reported in full.** Not merely its
    depth as a number, but the ordered sequence of functions from
    entry point to deepest leaf, so the specific path driving
    worst-case stack usage can be inspected and shortened rather than
    only measured. The depth is worst-case *within the resolved call
    graph*: a chain continuing through an unresolved indirect call is
    not followed, so the figure is reported alongside the count of
    unresolved calls.
*   **Hidden state and memory coupling.** Mapping every function that
    writes a given global or memory address against every function
    that reads it, exposing modules implicitly coupled through shared
    state; and, for architectures with overlapping memory maps and
    symbol tables, verifying that execution scopes remain isolated.
*   **Deterministic dead-code detection.** From user-declared entry
    points (`main()`, interrupt vectors, exported API boundaries) —
    and from any function whose address is taken, since it may be
    invoked indirectly — graph reachability proves which functions and
    data structures are never visited: unreachable by mathematics
    rather than by pattern-matching guesswork, so pruning can proceed
    confidently.
*   **Evaluation against published thresholds.** Every graph metric
    above is computed for the project as a whole — application or
    library — and reported against the accepted academic and
    safety-critical industry ranges for that metric, so a number
    arrives with the context needed to act on it rather than as a
    bare figure. The thresholds are set out in
    [Appendix A](#appendix-a-industry-standards--architectural-thresholds).

#### Output

*   Four report formats: aligned ASCII table (default), CSV, XML, and
    GitHub-Flavored Markdown, with an optional output file — plus a
    companion Graphviz `.dot` artefact, written *alongside* a report
    rather than selected instead of one. CSV and XML are both complete
    and unfiltered — the complexity threshold above only governs what
    is *listed* in the table and Markdown formats.
*   Call-tree emission in Graphviz `.dot` for visual inspection,
    optionally annotated with the graph findings above (high fan-in
    and fan-out nodes, the deepest chain, cycles, unreachable
    subgraphs). Generation is **enabled by default** and can be
    switched off from the command line for runs that do not want the
    extra artefact. The `.dot` file is written alongside a named
    output file; when the report goes to standard output instead, no
    `.dot` file is produced whether or not it was disabled, since
    there is no output path to derive one from and graph markup must
    never enter the result stream.
*   Graph export in a standard graph serialisation schema
    (**GraphML**), so the SDG can be ingested by other tools rather
    than only rendered.
*   Converting a previously generated XML output file into a Markdown
    report, without re-analyzing the original source files, against a
    complexity threshold chosen independently at conversion time.

### 7.2 Out of Scope

*   Per-function metrics measuring a function *in isolation*, beyond
    ELOC and cyclomatic complexity — Halstead measures, cognitive
    complexity, maintainability index, and duplication detection.
    (Coupling *is* in scope, but as a property of the graph rather
    than of a function; see §7.1. A figure derived from a function's
    place in that graph — the Weighted Test Burden Index, and the
    mocking cost and weighted degree it is built from — is therefore
    in scope and is reported per function: what it measures is the
    entanglement, and the function is only where it is attributed.)
*   Aggregation of line and complexity totals between the file and
    whole-project levels: no per-directory, per-module, per-class, or
    per-package ELOC rollups. (The project-wide total in §7.1 is the
    only level of line-count rollup above a single file. Graph
    metrics such as fan-in and fan-out are reported per component by
    their nature and are unaffected by this exclusion.)
*   Comparison across commits, branches, or runs; no history, trends,
    or baselines.
*   Policy gating on thresholds — for example, failing the build or
    returning a non-zero exit status when a function exceeds a
    complexity limit. (The complexity threshold in §7.1 controls what
    is *listed*, not the exit status. The same holds for the
    Appendix A thresholds: a finding reported as "critical" is a
    severity label within the report, not an exit status. Acting on
    it is the caller's decision — see the CI integration theme in
    §9.)
*   JSON, SARIF, or other structured machine formats beyond CSV and
    XML.
*   General-purpose sorting, ranking, or filtering of results — for
    example, top-N selection or ordering by an arbitrary field. (The
    fixed complexity-threshold list in §7.1 is the one built-in
    exception.)
*   A configuration file or per-project settings that alter how `elc`
    itself behaves, and any automatic discovery of one. (A
    user-supplied `.scm` rule file is query *data* — explicitly named
    or placed in the runtime location, carried by the same mechanism
    as the built-in queries — not a setting that changes `elc`'s
    behaviour, and never auto-discovered.)
*   Bundled grammars beyond the initial supported language set
    (§7.1); building grammars is the user's responsibility.
*   Concurrency. `elc` is single-threaded by choice — the throughput
    is not currently worth the work queue, the shared-state locking,
    and the class of defects that come with them.

### 7.3 Non-Goals

*   Being a linter, style checker, formatter, or static-analysis
    platform. `elc` measures and reports — including where a
    measurement falls outside a published threshold (Appendix A), and
    where it violates criteria the *user* supplied, such as declared
    architectural strata or entry points. Every threshold it cites is
    external and attributed to its source; `elc` invents no style
    opinion of its own, ranks no design as better than another beyond
    what the cited standard already states, proposes no fix, and
    leaves what to do about a finding to the reader.
*   Modifying, rewriting, or generating source code. `elc` opens
    files read-only, always.
*   Running as a server, daemon, or hosted service, or shipping a web
    dashboard.
*   Editor and IDE integration, language-server behaviour, or
    real-time analysis.
*   Native support for non-POSIX platforms.
*   Reimplementing language parsing. If Tree-sitter cannot express a
    metric, the answer is a better query or a better grammar, never
    a hand-written parser.
*   Reimplementing graph mathematics. Cycle detection, topological
    ordering, reachability, and centrality come from an established
    graph library — `igraph` being the suggested candidate (Principle
    5) — rather than from hand-written adjacency lists and traversal
    algorithms, which are not this project's value-add.

## 8. Success Metrics

`elc` is succeeding when:

| Metric | Target |
| ------ | ------ |
| **Per-function granularity** | A user can identify the most complex functions and determine the ELOC of each in an unfamiliar repository with a single command and no configuration. |
| **Per-file reporting** | A user can identify the ELOC per file in an unfamiliar repository with a single command and no configuration. |
| **Project Summary** | A user can quickly scan a summary that highlights the total ELOC in the project — broken down by source language as well as combined — and the most complex files and functions. |
| **Extensibility without rebuild** | A new language is added by adding one shared object and its query files. Zero lines of C change, and no recompilation of `elc`. |
| **Correctness against hand counts** | Every fixture's ELOC and per-function complexity match values counted by hand, including files with nested comments and comment syntax inside string literals. The count being verified is effective lines of code, not raw source lines of code. |
| **Architectural visibility** | A user can identify every dependency cycle, the deepest call chain, the highest fan-in/fan-out components, and every provably unreachable function in an unfamiliar repository, with a single command. |
| **Provable dead code** | Functions reported as unreachable are unreachable by graph reachability from the declared entry points and from every address-taken function — never a heuristic guess, never fooled by dead functions that call one another, and never claiming a callback or interrupt handler is dead merely because nothing calls it directly. |
| **Determinism** | Repeated runs over the same tree produce byte-identical output. |
| **Coverage** | Every requirement in [HLRs.md](HLRs.md) and [LLRs.md](LLRs.md) is bound to at least one test in [STP.md](STP.md), per [Traceability.md](Traceability.md). Coverage gaps are documented, not silent. |
| **Self-quality** | `elc` run against its own source reports no function exceeding a cyclomatic complexity of 15, and no critical finding from a band other than recursion and fan-out. One debt is recorded rather than gated: some sixty functions sit in §A.3's warning band between 11 and 15, and seventy-odd sit in the warning band of the Testing Burden Index. No function is in that index's critical band, and that figure is ratcheted at zero — it may not rise. A warning band meeting old code is cleared by a refactor of the source tree rather than by a step in the phase that drew it. |

## 9. Roadmap Themes

These are *themes* — not committed features — that frame future
investment. Specific work items live in HLRs/LLRs as they are
adopted.

*   **Language breadth.** Grow the set of languages with maintained
    grammars and query files, beyond the initial set in §7.1.
    Candidates under consideration: Zig, Carbon, Odin, Java,
    JavaScript, and TypeScript. Gated by demand and by the
    availability of a usable Tree-sitter grammar for each language.

*   **CI integration.** Structured output (JSON or SARIF) and
    threshold-based exit statuses so `elc` can gate a build directly
    rather than through a wrapper script. Gated by the output-format
    design settling and by real CI use.

*   **Presentation and ranking.** General-purpose sorting, top-N
    selection, and arbitrary filtering, beyond the fixed
    complexity-threshold list already in scope (§7.1) — so a large
    repository's results are usable without piping through other
    tools. Gated by evidence that users are routinely post-processing
    the output the same way.

*   **Change-scoped analysis.** Using `libgit2` to report metrics for
    the functions touched by a commit or a diff, rather than the
    whole tree. Gated by the per-function results being trustworthy
    first.

*   **Metric depth.** Additional per-function metrics beyond ELOC and
    cyclomatic complexity, if and only if they can be expressed as
    Tree-sitter queries under the existing extensibility model.

*   **Throughput.** Parallel file processing, gated strictly by
    measurement: only if a real repository demonstrably runs too
    slowly, single-threaded, to be usable in practice. There is no
    committed performance target; a 100,000-line repository analyzed
    in a few seconds on a single core is an informal best guess, not
    a measured success criterion, and gates nothing on its own.

*   **Graph analysis depth.** Further analyses over the System
    Dependence Graph beyond those already in scope (§7.1) — data-flow
    slicing, change-impact prediction, or architectural-drift
    comparison between two runs — where each can be expressed as a
    query over the existing graph rather than as new parsing. Gated
    by the in-scope graph analyses proving trustworthy first.

Anything not listed here is not on the roadmap and would require an
explicit vision update.

## 10. Relationship to the Rest of the Spec Stack

| Document | Question it answers |
| -------- | ------------------- |
| **PVD** (this document) | *Why* does this product exist, and how do we know it is succeeding? |
| [HLRs.md](HLRs.md) | *What* must the product do to deliver on the vision? |
| [LLRs.md](LLRs.md) | *How* does each function in the implementation contribute to an HLR? |
| [SDD.md](SDD.md) | *How is the implementation structured* to satisfy the LLRs? |
| [STP.md](STP.md) | *How do we verify* that each LLR (and therefore each HLR, and therefore the vision) is actually delivered? |
| [Traceability.md](Traceability.md) | *Where are the gaps*, end-to-end? |

A change to this PVD should propagate downward (some HLRs may be
added, retired, or reworded). A change discovered during
implementation that conflicts with this PVD is a signal to update
this document — not to silently diverge.

## Appendix A: Industry Standards & Architectural Thresholds

To make its System Dependence Graph analysis actionable rather than
merely descriptive, `elc` evaluates the whole project — application
or library — against established academic benchmarks and
safety-critical industry standards, notably **MISRA C**, and reports
where each measurement falls relative to the accepted range.

Every threshold below is external and attributed to its source.
`elc` reports position against them; it does not invent them, and a
severity label here is a statement about the measurement, not about
the exit status (§7.2).

### A.1 Component Coupling and Instability

Following Robert C. Martin's Instability metric, `elc` evaluates the
ratio of efferent coupling (`Ce`, fan-out) to afferent coupling
(`Ca`, fan-in):

```text
I = Ce / (Ce + Ca)
```

There is no universal "safe" value. The standard is that a module's
instability must align with its level of abstraction:

*   **`I` ≈ 0 — maximum stability.** High fan-in, low fan-out: many
    modules depend on this one and it depends on little (for example
    a core hardware abstraction layer). Changes here are dangerous
    and propagate widely.
*   **`I` ≈ 1 — maximum instability.** High fan-out, low fan-in:
    this module depends on much and nothing depends on it (for
    example top-level application logic). It can be changed freely
    with no downstream impact.

A module whose measured instability is mismatched to its intended
abstraction level — a stable-by-design layer drifting toward `I` ≈ 1,
or leaf logic that has accumulated dependents — is the finding this
metric exists to surface.

### A.2 Call Tree Dimensionality

Applying the Henry–Kafura information-flow metrics together with
strict embedded constraints, `elc` evaluates the geometric shape of
the call tree.

**Tree width (function fan-out)**

| Range | Interpretation |
| ----- | -------------- |
| 0–2 | Below the healthy band. No finding. |
| 3–7 unique subroutine calls | Healthy delegation. |
| 8–10 | Acceptable. No finding. |
| 11–15 | **Warning** — indicates weak abstraction and poor delegation. |
| > 15 | **Critical** — a "god function" or monolithic dispatcher. Violates the Single Responsibility Principle and is nearly impossible to isolate for unit testing. |

The bands are exhaustive: every fan-out value falls in exactly one.

**Tree width the other way (function fan-in)**

| Range | Interpretation |
| ----- | -------------- |
| 0–25 | No finding. |
| > 25 | **Warning** — a function this widely called is an interface, and can no longer be changed without changing its callers. |

**This band is `elc`'s own and is labelled as such wherever it is
reported.** No published source divides fan-in into accepted and
unaccepted ranges; 25 is this project's judgement, and it sits beside
the bottleneck heuristic of §A.1 under the same words — *elc
heuristic — not a published standard*. There is no critical band,
because `elc` has no published basis for a first line and none
whatever for a second.

*The Henry–Kafura structural-complexity metric,
`HK = Length × (Fan-In × Fan-Out)²`, was reported here per function
and summed across the project until Phase 24 withdrew it. No
published source bands it, so `elc` reported it with no severity —
and in practice readers took an unbanded four-order-of-magnitude
figure for a score anyway. The two degrees it was formed from are
reported as they are measured, side by side with each function's
ELOC and complexity in one table, and each is now banded on a stated
authority.*

**Tree height (call-chain depth)**

*   **Embedded constraint.** Call depth should be strictly
    minimised. In 8-bit environments where stack space is heavily
    constrained (for example under 2 KB of SRAM), depths beyond
    **8 to 12 layers** risk the stack colliding with the heap.
*   **Safety standard.** `elc` checks **MISRA C Rule 17.2**,
    statically establishing the absence of recursion so that the call
    tree is a measurable Directed Acyclic Graph and maximum stack
    usage is predictable rather than unbounded.
*   **Reported in full.** `elc` reports the deepest call stack
    itself — the ordered chain of functions from entry point to
    deepest leaf — alongside its measured depth, so the path
    responsible for worst-case stack consumption is identified, not
    just counted. The measurement is a lower bound within the resolved
    call graph, and is presented with the unresolved-call count so its
    completeness can be judged. Where recursion breaks the DAG
    property above, that cycle is reported in place of an unbounded
    depth.

### A.3 Cyclomatic Complexity

McCabe's measure counts the linearly independent paths through a
function, and is the figure `elc` computes per function alongside its
effective lines.

| Range | Interpretation |
| ----- | -------------- |
| 1–10 | No finding. McCabe's own recommended limit. |
| 11–15 | **Warning.** NIST SP 500-235 records limits as high as 15 as having been used successfully — but only where an organisation has the design, review, and test practices to justify going past 10. |
| > 15 | **Critical.** Beyond the range any published source reports as workable; the function has more independent paths than a test suite is likely to cover. |

The bands are exhaustive, and both numbers are somebody else's.

They are deliberately **independent of the
`--complexity-threshold` option**,
which controls which functions are *listed* and carries no severity
(§7.1). A user who moves that value is choosing what they want to
see; if moving it also moved a severity, the number would be theirs
rather than McCabe's and the attribution above would be false.

### A.4 Strict DAG Validation (Circular Dependencies)

Graph reachability and topological sorting enforce a unidirectional
flow of dependencies.

*   **Acceptable range: strictly 0.**
*   **Rationale.** A cyclic dependency (Module A → B → C → A) fuses
    the participating components into a single strongly connected
    unit, destroying the ability to unit-test them in isolation or to
    link them incrementally.
*   Any cycle is therefore reported at **critical** severity.

### A.5 Global State and Temporal Coupling

Tracking global-variable mutations across the SDG enforces safe data
flow and encapsulation, following **MISRA C Rule 8.9** — *an object
should be defined at block scope if its identifier appears in only a
single function*.

*   **Single-function access.** Where the graph shows read and write
    edges for a global originating from only one function, it is
    flagged for **scope reduction** — the object should be local.
*   **Multi-domain access.** Where edges originate from multiple
    disconnected domains, it is flagged as a **hidden channel**:
    dangerous temporal coupling in which the order of function
    execution silently dictates system stability.
