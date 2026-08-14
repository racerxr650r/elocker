# Software Design Document: elocker (elc)

**Version:** 1.7
**Date:** 2026-08-14
**Author(s):** John Anderson

## 1. Introduction

### 1.1 Purpose of the Document
This document provides a detailed design for the `elc` (elocker)
POSIX C11 command-line application. It is intended for developers, testers, and maintainers
of the `elc` software.

### 1.2 Scope of the Document
This document describes the design of the source modules that implement the 116 high-level requirements in [HLRs.md](HLRs.md):

*   [src/main.c](../src/main.c): Entry point. Sequences the pipeline, owns the run's top-level state, and computes the process exit status.
*   [src/cli.c](../src/cli.c): Command-line parsing and validation: formats, thresholds, strata, entry points, execution scopes, custom rules, and the help path.
*   [src/discover.c](../src/discover.c): Target classification and file discovery across the three target types, with de-duplication and exclusion filtering.
*   [src/registry.c](../src/registry.c): Runtime location resolution, lazy language-module loading, extension mapping, and custom rule-query loading.
*   [src/analyze.c](../src/analyze.c): Per-file parsing and the single-parse extraction of ELOC, cyclomatic complexity, function identity, and the raw call and global-access facts the graph is later built from.
*   [src/graph.c](../src/graph.c): System Dependence Graph construction: cross-file symbol resolution and population of the graph structure.
*   [src/arch.c](../src/arch.c): Component-level analyses — coupling, instability, bottlenecks, dependency cycles, and architectural layering.
*   [src/calltree.c](../src/calltree.c): Function-level call-tree analyses — fan-out, depth, the deepest call stack, and recursion detection.
*   [src/state.c](../src/state.c): Global-state coupling, execution-scope isolation, and reachability-based dead-code detection.
*   [src/thresholds.c](../src/thresholds.c): Evaluation of every measurement against its published threshold, and assignment of severity and attribution.
*   [src/report.c](../src/report.c): The format-independent report model: assembly of every finding into one structure, in a stable, defined order.
*   [src/format_text.c](../src/format_text.c): The aligned ASCII table and GitHub-Flavored Markdown renderers.
*   [src/format_csv.c](../src/format_csv.c): The RFC 4180 CSV renderer.
*   [src/format_xml.c](../src/format_xml.c): The XML record writer and the reader that drives the report-regeneration mode.
*   [src/format_graph.c](../src/format_graph.c): The Graphviz `.dot` call-tree writer and the GraphML graph-export writer.
*   [doc/elc.1](../doc/elc.1): The section-1 man page: the reference form of every option, format, and finding category.
*   [doc/User_Manual.md](../doc/User_Manual.md): The user manual: the same material in expository form, with worked examples.

It does not describe the language grammars or query files under `runtime/`, which are data rather than code, nor the Criterion and Bats test suites under `test/`, which are covered by the [Software Test Plan](STP.md). The two documentation files are listed because their currency is a delivery obligation (HLR-128 – HLR-130), not because their prose is designed here.

### 1.3 Project Overview
`elc` is a single, statically-linkable POSIX C11 executable that computes Effective Lines of Code and cyclomatic complexity per function, builds a project-wide System Dependence Graph from the same parse, and reports architectural findings derived from that graph against published industry and academic thresholds.

The design is organised as a strict one-way pipeline. Each stage consumes the previous stage's output and produces a value the next stage consumes; no stage reaches backwards, and no stage re-reads a source file. This is what makes the single-parse rule (HLR-076) and the determinism requirements (HLR-032, HLR-033) structural properties of the design rather than disciplines the implementation must remember to observe.

Everything language-specific lives in `runtime/` as data: a Tree-sitter grammar shared object plus its `.scm` query files. No module in `src/` contains a language name, a file extension, or a grammar node type. The same mechanism carries user-supplied custom rules, so a team's coding standard is checked by the identical query engine that produces the built-in metrics.

### 1.4 Definitions, Acronyms, and Abbreviations
*   **ELOC:** Effective Lines of Code — the count of executable statements, excluding blank lines, comments, standalone structural tokens, non-initialising declarations, and preprocessor directives (HLR-015, HLR-044 through HLR-053).
*   **SDG:** System Dependence Graph — the project-wide directed graph whose nodes are functions and whose edges are calls and global-state accesses (HLR-073, HLR-074).
*   **Component:** A single source file (translation unit). The unit of coupling, instability, and dependency-cycle analysis (HLR-114).
*   **Ca / Ce:** Afferent coupling (fan-in) and efferent coupling (fan-out) of a component (HLR-080).
*   **Instability:** `I = Ce / (Ce + Ca)`; 0 denotes maximum stability, 1 maximum instability (HLR-082).
*   **Stratum:** A user-declared architectural layer, together with the components assigned to it and the permitted dependency direction (HLR-078).
*   **Entry point:** A user-declared function from which reachability and call depth are measured (HLR-095).
*   **Hidden channel:** A global object accessed from multiple otherwise-disconnected regions of the SDG, indicating temporal coupling (HLR-093).
*   **Finding:** Any reportable observation — a metric outside its threshold, a cycle, an unreachable function, a custom-rule match — carrying a severity and an attribution.
*   **Runtime location:** The directory holding language grammars and query files, resolved from an environment variable or from a path adjacent to the executable (HLR-059).

### 1.5 References
*   [Product Vision Document](PVD.md) — the vision, scope, and Appendix A threshold catalogue this design implements.
*   [High-Level Requirements](HLRs.md) — the 116 requirements this design satisfies.
*   Tree-sitter — incremental parsing library and `.scm` query language: <https://tree-sitter.github.io/tree-sitter/>
*   libgit2 — Git repository access library: <https://libgit2.org/>
*   igraph — graph algorithm library: <https://igraph.org/c/>
*   Expat — streaming XML parser: <https://libexpat.github.io/>
*   RFC 4180 — Common Format and MIME Type for CSV Files: <https://www.rfc-editor.org/rfc/rfc4180>
*   GraphML Primer: <http://graphml.graphdrawing.org/primer/graphml-primer.html>
*   Graphviz DOT language: <https://graphviz.org/doc/info/lang.html>
*   MISRA C:2012 — Rule 8.9 (object scope) and Rule 17.2 (no recursion).
*   R. C. Martin, *Agile Software Development: Principles, Patterns, and Practices* — the Instability metric.
*   S. Henry and D. Kafura, "Software Structure Metrics Based on Information Flow", *IEEE TSE* SE-7(5), 1981.

### 1.6 Document Overview
*   Section 1: Introduction.
*   Section 2: System Overview.
*   Section 3: Detailed design for [src/main.c](../src/main.c).
*   Section 4: Detailed design for [src/cli.c](../src/cli.c).
*   Section 5: Detailed design for [src/discover.c](../src/discover.c).
*   Section 6: Detailed design for [src/registry.c](../src/registry.c).
*   Section 7: Detailed design for [src/analyze.c](../src/analyze.c).
*   Section 8: Detailed design for [src/graph.c](../src/graph.c).
*   Section 9: Detailed design for [src/arch.c](../src/arch.c).
*   Section 10: Detailed design for [src/calltree.c](../src/calltree.c).
*   Section 11: Detailed design for [src/state.c](../src/state.c).
*   Section 12: Detailed design for [src/thresholds.c](../src/thresholds.c).
*   Section 13: Detailed design for [src/report.c](../src/report.c).
*   Section 14: Detailed design for [src/format_text.c](../src/format_text.c).
*   Section 15: Detailed design for [src/format_csv.c](../src/format_csv.c).
*   Section 16: Detailed design for [src/format_xml.c](../src/format_xml.c).
*   Section 17: Detailed design for [src/format_graph.c](../src/format_graph.c).
*   Section 18: Data Dictionary.
*   Section 19: Traceability.

## 2. System Overview

### 2.1 System Architecture
`elc` is a single executable composed of fifteen translation units arranged as a one-way pipeline. Stages communicate only through the values they return; there is no global mutable state, no callback into an earlier stage, and no second read of any source file.

*   **[src/main.c](../src/main.c)** — Sequences the pipeline and owns the exit status. Contains no analysis logic of its own.
*   **[src/cli.c](../src/cli.c)** — Turns `argv` into a validated, immutable options structure. The only module that reads the command line.
*   **[src/discover.c](../src/discover.c)** — Turns target arguments into a de-duplicated, stably ordered list of files to analyse.
*   **[src/registry.c](../src/registry.c)** — Maps a file extension to a loaded language module, loading grammars and compiling queries on first use and caching thereafter.
*   **[src/analyze.c](../src/analyze.c)** — The single parse. Produces, per file, both the per-function metrics and the raw call/global-access facts that graph construction later resolves.
*   **[src/graph.c](../src/graph.c)** — Resolves those facts across file boundaries into the SDG.
*   **[src/arch.c](../src/arch.c)** — Component-level graph analyses.
*   **[src/calltree.c](../src/calltree.c)** — Function-level call-tree analyses.
*   **[src/state.c](../src/state.c)** — Global-state and reachability analyses.
*   **[src/thresholds.c](../src/thresholds.c)** — Applies the Appendix A catalogue to every measurement, attaching severity and source attribution.
*   **[src/report.c](../src/report.c)** — Assembles every metric and finding into one format-independent, stably ordered model.
*   **[src/format_text.c](../src/format_text.c)** — Renders the report model as an aligned table or as Markdown.
*   **[src/format_csv.c](../src/format_csv.c)** — Renders per-function records as RFC 4180 CSV.
*   **[src/format_xml.c](../src/format_xml.c)** — Writes the complete XML record, and reads one back for regeneration mode.
*   **[src/format_graph.c](../src/format_graph.c)** — Writes the `.dot` call tree and the GraphML export.

The runtime data flow of an analysis run is:

1.  `main()` calls `cli_parse()`. A help request prints usage to `stdout` and exits zero (HLR-117); an invalid invocation prints usage to `stderr` and exits non-zero (HLR-063).
2.  If the options select regeneration mode, `main()` calls `xml_read_report()` and jumps directly to the render step; no source file is touched (HLR-055). A read failure is a setup-class error and exits 2. Because the record carries findings rather than graph topology, `.dot` and GraphML cannot be produced in this mode and are suppressed.
3.  `registry_open()` resolves the runtime location and verifies that at least one language module is loadable; failure here is fatal (HLR-036, HLR-059).
4.  `discover_targets()` validates every target argument up front, classifies each, walks it, and returns a de-duplicated file list in stable order (HLR-062, HLR-071, HLR-072).
5.  For each file, `analyze_file()` maps it, obtains its language module from the registry, parses it once, and emits both `FileMetrics` and a `FileFacts` record of call sites and global accesses (HLR-013, HLR-076).
6.  `graph_build()` resolves the accumulated `FileFacts` into the SDG, recording unresolved call sites rather than failing (HLR-073, HLR-077).
7.  `arch_analyse()`, `calltree_analyse()`, and `state_analyse()` run over the SDG, each skipping any analysis whose user declaration was not supplied (HLR-115).
8.  `thresholds_apply()` evaluates every measurement against the Appendix A catalogue, attaching a severity and a source attribution to each finding (HLR-098, HLR-099).
9.  `report_assemble()` merges metrics, findings, and custom-rule matches into one model and sorts every collection into its defined order (HLR-033).
10.  The selected renderer writes the report; `format_graph.c` additionally writes the `.dot` companion unless disabled or the report is going to `stdout` (HLR-103, HLR-104).
11.  `main()` returns non-zero if any per-file failure was recorded, zero otherwise (HLR-037).

### 2.2 Design Goals and Constraints
*   **Determinism by construction:** Every collection reaching a renderer is sorted by an explicit key before it is emitted, and no renderer iterates a hash container or a library-owned structure directly. This makes HLR-032 and HLR-033 properties of `report.c` rather than obligations spread across fifteen modules.
*   **One parse, one pass:** `analyze.c` is the only module that touches source text. It extracts the per-function metrics *and* the graph facts in the same traversal, so the graph stages never reopen a file (HLR-076, PVD Principle 7).
*   **No language knowledge in the binary:** Language names, extensions, and node types appear only in `runtime/`. `src/` handles opaque query captures by name. A new language is a directory, never a patch (HLR-009, HLR-010, HLR-060).
*   **Fail soft on data, fail hard on setup:** A bad file, a bad language module, or an unresolvable call degrades one result and is reported; a missing runtime location or an invalid command line stops the run before work begins (HLR-035, HLR-036, HLR-062, HLR-070).
*   **Findings are data, not control flow:** Severity is a field on a finding. No analysis stage can influence the exit status, which is reserved for genuine failures (HLR-100, HLR-023).
*   **Countable, maintained dependencies:** Four third-party libraries, each actively maintained, plus a POSIX libc. Writers are hand-rolled text emission rather than library-driven, which removes a dependency class entirely (HLR-040, HLR-112).
*   **Link-level testability:** Every function carrying a low-level requirement has **external linkage** and is declared in a per-module internal header — `src/analyze_internal.h` and its siblings — which is not installed and forms no part of any public interface. `static` is reserved for helpers that carry no requirement of their own.

    This is not a test seam. Nothing is added to `src/` for the benefit of tests: no injected function pointers, no `#ifdef TESTING`, no parameters that exist only to be overridden. The production build links exactly the code that ships. What the convention buys is that the unit level can reach an internal contract at all — a `static` function is invisible outside its translation unit, so a Criterion binary could neither call it nor intercept it, and roughly half the LLRs would bind to functions no test could exercise. It also keeps `--wrap` effective: a wrapped symbol is only interceptable because the call resolves at link time, which an intra-translation-unit call to a `static` function does not.

    Where a name would be ambiguous across modules it is prefixed with the module's own (`thresholds_lookup`, not `lookup`).
*   **Single-threaded throughout:** No work queue, no locking, no thread-local state anywhere in the pipeline (HLR-041).

## 3. Detailed Design for [src/main.c](../src/main.c)

### 3.1 Purpose and Responsibilities
[src/main.c](../src/main.c) is the entry point of the `elc` executable. It sequences the pipeline stages, owns the run-level state that outlives any single stage, and translates the accumulated failure record into a process exit status.

*   Define `main()` and the ordered invocation of every pipeline stage.
*   Own the lifetime of the options, registry, accumulator, graph, and report objects, and release them in reverse order of acquisition.
*   Branch between analysis mode and XML regeneration mode.
*   Compute the exit status from the run's failure record, never from a finding severity.

### 3.2 External Interfaces
#### 3.2.1 Command-Line Arguments

`main()` receives `argc`/`argv` unmodified and passes them directly to `cli_parse()`. No option is interpreted here.

#### 3.2.2 Process Exit Status

`0` when every discovered file was processed without error; `1` when any per-file read or parse failure occurred; `2` for a fatal setup or usage error, including a rejected saved-XML record (HLR-058). Finding severities never contribute (HLR-100).


### 3.3 Internal Structure
#### 3.3.1 Key Functions

*   **`int main(int argc, char *argv[])`**
    *   Purpose: Run one invocation of `elc` end to end.
    *   Post-condition: Every acquired resource has been released; all output has been flushed.
    *   Return Value: The exit status described in the interface section above.
    *   Logic:
        1.  Call `cli_parse()`. On help, print usage to `stdout` and return 0. On error, print usage to `stderr` and return 2.
        2.  If `opts.mode == MODE_REGENERATE`, call `xml_read_report()`, then jump to step 8.
        3.  Call `registry_open()`; on failure return 2 (HLR-036).
        4.  Call `discover_targets()`; on an invalid target return 2 without emitting a report (HLR-062).
        5.  For each discovered file call `analyze_file()`, appending its `FileMetrics` to the accumulator and its `FileFacts` to the fact list; record but do not propagate per-file failures.
        6.  Call `graph_build()` over the fact list.
        7.  Call `arch_analyse()`, `calltree_analyse()`, `state_analyse()`, then `thresholds_apply()`, then `report_assemble()`.
        8.  Dispatch to the selected renderer, then to `graph_write_dot()` when the companion artefact is warranted.
        9.  Tear down in reverse order and return the computed status.
    *   Notes: `main()` contains no analysis logic; its cyclomatic complexity is bounded by the number of stages and their failure branches, keeping it well inside the self-quality target of PVD §8.

### 3.4 Dependencies

*   Every other module in `src/`. `main.c` is depended upon by nothing, giving it an Instability of 1 — the correct value for top-level orchestration (HLR-082).

### 3.5 Error Handling and Logging

*   **Stage returns fatal** Tear down what has been acquired, ensure the diagnostic has reached `stderr`, and return 2. No partial report is written.
*   **Per-file failure recorded** Continue the run; the report is complete for the files that succeeded, and the exit status becomes 1 (HLR-035, HLR-037).

## 4. Detailed Design for [src/cli.c](../src/cli.c)

### 4.1 Purpose and Responsibilities
[src/cli.c](../src/cli.c) parses and validates the command line, producing an immutable `ElcOptions` value. It is the only module that reads `argv`, and the only source of user-supplied configuration in the entire program.

*   Parse short and long options with `getopt_long()`, including the report format, output path, thresholds, custom-rule paths, and the `.dot` and GraphML switches.
*   Parse the structured declarations — architectural strata, entry points, and execution scopes — into their in-memory forms.
*   Validate every option value and reject an unknown option, a missing argument, or a missing target before any analysis begins.
*   Emit the usage summary, to `stdout` on request and to `stderr` on error.

### 4.2 External Interfaces
Options are the entirety of `elc`'s configuration surface: there is no configuration file and no dotfile discovery (HLR-039).

#### 4.2.1 Option List as Single Reference

The set of accepted options appears in three places: this module's `getopt_long` table, the man page, and the user manual. Nothing prevents those three drifting apart, so `cli_usage()`'s output is designated the **reference**: it is generated from the same table that parses, and the documentation is checked against it rather than against the source (LLR-DOC-04). An option that parses but does not print, or prints but is not documented, is a defect the documentation test catches.

#### 4.2.2 Command-Line Options


| Option | Argument | Default | Requirement |
| ------ | -------- | ------- | ----------- |
| `-f`, `--format` | `table\|csv\|xml\|md` | `table` | HLR-027 – HLR-029 |
| `-o`, `--output` | path | `stdout` | HLR-030 |
| `-c`, `--complexity-threshold` | integer | `15` | HLR-022 |
| `-b`, `--bottleneck-threshold` | integer | `5` | HLR-081 |
| `--no-dot` | — | `.dot` enabled | HLR-103 |
| `--graphml` | — | disabled | HLR-106 |
| `--stratum` | `name:glob[,glob…]` | none | HLR-078 |
| `--stratum-order` | `name>name[>name…]` | none | HLR-078 |
| `--entry` | symbol | none | HLR-095 |
| `--scope` | `name:glob[,glob…]` | none | HLR-094 |
| `--rules` | `lang:path` | none | HLR-107 |
| `--from-xml` | path | — | HLR-055 |
| `-h`, `--help` | — | — | HLR-117 |



### 4.3 Internal Structure
#### 4.3.1 Key Data Structures

`ElcOptions` (see the Data Dictionary) is populated here and thereafter treated as read-only by every other module.


#### 4.3.2 Key Functions

*   **`int cli_parse(int argc, char *argv[], ElcOptions *out)`** — Parse and validate argv into out; returns 0, CLI_HELP, or CLI_ERROR.
*   **`void cli_usage(FILE *stream)`** — Print the option summary and defaults to stream.
*   **`int parse_stratum(const char *arg, ElcOptions *out)`** — Parse one name:glob-list stratum declaration.
*   **`int parse_scope(const char *arg, ElcOptions *out)`** — Parse one name:glob-list execution-scope declaration.
*   **`void cli_options_free(ElcOptions *opts)`** — Release every heap allocation owned by the options structure.
### 4.4 Dependencies

*   POSIX `getopt_long()`, `fnmatch()` for the glob patterns in stratum and scope declarations.

### 4.5 Error Handling and Logging

*   **Unknown option or missing argument** Print the usage summary to `stderr` and return `CLI_ERROR`; `main()` exits 2 without analysing anything (HLR-063).
*   **Help requested** Print the usage summary to `stdout` and return `CLI_HELP`; `main()` exits 0, since a help request is not an error (HLR-117).
*   **Malformed declaration** A stratum, scope, or entry-point argument that cannot be parsed is a usage error, handled as above.

## 5. Detailed Design for [src/discover.c](../src/discover.c)

### 5.1 Purpose and Responsibilities
[src/discover.c](../src/discover.c) turns the target arguments into the de-duplicated, stably ordered list of files the rest of the pipeline will analyse. It selects a traversal strategy per target and applies the exclusion rules appropriate to that strategy.

*   Validate every target argument before any traversal begins.
*   Classify each target with `stat(2)` and route it to direct handling, Git enumeration, or filesystem traversal.
*   Establish whether a discovered repository is *applicable* to the target before using it, and fall back to filesystem traversal when it is not.
*   Enumerate tracked, non-binary blobs at or beneath the target for an applicable Git target; walk the tree with `fts(3)` otherwise, excluding binary extensions, hidden directories, and symlinked directories.
*   Record which route each directory target took, for reporting.
*   De-duplicate by resolved absolute path and sort the result into a stable order.

### 5.2 External Interfaces
#### 5.2.1 File System

Reads directory structure and file metadata only; never opens a source file for content. Opening is `analyze.c`'s responsibility.

#### 5.2.2 Symbolic Links

Target arguments are classified with `stat(2)`, which follows links, so a symlink named directly on the command line is resolved and analysed. Traversal uses `FTS_PHYSICAL`, which does not, so a symlinked directory encountered during a walk is never descended into. The pairing is deliberate and implements both halves of HLR-069; changing either call to match the other breaks one half.


### 5.3 Internal Structure
#### 5.3.1 Key Functions

*   **`int discover_targets(const ElcOptions *opts, FileList *out)`**
    *   Purpose: Produce the complete, ordered, de-duplicated analysis file list.
    *   Pre-condition: `opts` has been validated by `cli_parse()`.
    *   Post-condition: `out` holds absolute paths in ascending byte order, each appearing exactly once.
    *   Return Value: 0 on success; non-zero if any target argument was invalid, in which case `out` is empty.
    *   Logic:
        1.  Loop over every target argument calling `stat(2)`; on the first that does not exist, cannot be read, or is neither regular file nor directory, emit a diagnostic naming it and return non-zero (HLR-062).
        2.  For each validated target: a regular file is appended directly; a directory is offered to `git_repository_open_ext()`, which searches the directory and then its ancestors.
        3.  On a successful repository open, test applicability: does this repository track the target directory? A repository that does not — because the target is `.gitignore`d, or because the repository found several levels up is an unrelated one such as a version-controlled home directory — is discarded, and the target falls through to filesystem traversal.
        4.  For an applicable repository, resolve `HEAD^{tree}` and walk it, appending each blob that `git_blob_is_binary()` reports as text **and** whose repository-relative path lies at or beneath the target directory.
        5.  Otherwise walk with `fts_open()`/`fts_read()` in `FTS_PHYSICAL` mode, skipping hidden directories, known binary extensions, and any directory reached through a symbolic link.
        6.  Canonicalise each accumulated path with `realpath()`, insert into a set keyed on the canonical path, and finally sort the set into byte order.
    *   Notes: Canonicalising before de-duplication is what makes `elc src/main.c src/` count `main.c` once (HLR-072). Sorting here, rather than relying on `fts` or `libgit2` ordering, is what makes the output independent of filesystem enumeration order (HLR-033).

*   **`bool is_excluded_extension(const char *path)`** — Test a path against the binary-extension exclusion list, which is runtime data (runtime/binary.exts), not a compiled-in table.
*   **`bool repo_tracks_target(git_repository *repo, const char *target)`** — True when the discovered repository tracks the target directory; the applicability test that gates repository enumeration.
*   **`int walk_git_tree(git_repository *repo, const char *target, FileList *out)`** — Enumerate text blobs tracked at HEAD whose path lies at or beneath target.
*   **`int walk_filesystem(const char *root, FileList *out)`** — fts(3) traversal with hidden, binary, and symlink filtering.
*   **`void filelist_free(FileList *list)`** — Release the list and every path it owns.

#### 5.3.2 Parsing Strategy / Algorithm

Git tree traversal is preferred over filesystem traversal whenever an *applicable* repository is found, because tracked-file enumeration yields `.gitignore` compliance for free: an ignored or untracked path is simply absent from the tree, with no exclusion list to maintain (HLR-003).

Two constraints on that preference exist because `git_repository_open_ext()` searches upward, and both correct failure modes that the permissive default would otherwise produce.

**Applicability.** Finding a repository is not sufficient reason to use it. A repository that does not track the target directory would enumerate nothing beneath it, and the run would report zero files for a directory full of source — technically consistent with HLR-066, and thoroughly baffling. Two ordinary situations produce this: analysing a `.gitignore`d build directory, and analysing anything at all beneath a version-controlled home directory. The applicability test converts both into a filesystem traversal, which is what the user meant.

**Scoping.** Enumeration is restricted to paths at or beneath the target. Without this, `elc src/` inside a repository analyses the entire project, because the tree at `HEAD` is the whole repository regardless of which subdirectory was named. That would also make the two routes disagree about what a directory target denotes — filesystem traversal walks only the target — and HLR-126 requires them to agree.

`FTS_PHYSICAL` rather than `FTS_LOGICAL` is what prevents a cyclic directory symlink from causing unbounded traversal (HLR-069).

### 5.4 Dependencies

*   `libgit2` — `git_repository_open_ext()`, `git_revparse_single()`, `git_tree_walk()`, `git_blob_is_binary()`.
*   POSIX — `stat(2)`, `fts(3)`, `realpath(3)`.

### 5.5 Error Handling and Logging

*   **Invalid target argument** Diagnostic to `stderr` naming the target; the whole run aborts before any analysis, so no report can silently cover fewer targets than the user named (HLR-062).
*   **Unreadable subdirectory during traversal** Diagnostic to `stderr`, skip that subtree, continue; recorded as a per-file failure so the exit status reflects it.
*   **Enclosing repository does not track the target** Not an error. The repository is discarded and the target is traversed from the filesystem; the route taken is recorded and reported (HLR-127), so the fallback is visible rather than silent.

## 6. Detailed Design for [src/registry.c](../src/registry.c)

### 6.1 Purpose and Responsibilities
[src/registry.c](../src/registry.c) owns every piece of runtime-loaded data: the location of the `runtime/` directory, the lazily loaded language modules, the compiled queries, and the user-supplied custom rules. It is the boundary that keeps language knowledge out of the binary.

*   Resolve the runtime location from the environment variable, falling back to a path adjacent to the executable.
*   Load the extension-to-language mapping from runtime data rather than from compiled-in tables.
*   Load and cache a language module on first use of its extension: `dlopen` the grammar, resolve its `tree_sitter_<name>` symbol, and compile its `.scm` queries.
*   Load user-supplied custom rule queries, distinguishing explicitly named files from those found in the runtime location.
*   Tear down in an order that never leaves a compiled query pointing into an unloaded grammar.

### 6.2 External Interfaces
#### 6.2.1 Runtime Directory


```text
runtime/
├── extensions.map          # one "<ext> <lang>" pair per line (HLR-060)
├── parsers/<lang>.so       # exports tree_sitter_<lang>
└── queries/<lang>/
    ├── comments.scm  functions.scm  complexity.scm
    ├── eloc.scm  calls.scm  globals.scm
    └── rules/*.scm         # custom rules for this language (HLR-107)
```
The six per-language queries are required; `eloc.scm`, `calls.scm`, and `globals.scm` supply the ELOC statement classification and the graph facts. `extensions.map` is plain text so that associating a new extension with a language is a data edit, never a rebuild (HLR-060).

#### 6.2.2 Custom Rule Binding

A Tree-sitter query compiles against one specific `TSLanguage`, so every custom rule must name the language it applies to. Rules found under `runtime/queries/<lang>/rules/` are bound by their location; rules named on the command line are bound by the `lang:path` argument form. A rule naming a language with no available module is a diagnostic, not a compile attempt.

#### 6.2.3 Environment

`ELC_RUNTIME_DIR`, when set, takes precedence over the path adjacent to the executable (HLR-059).


### 6.3 Internal Structure
#### 6.3.1 Key Data Structures

`LanguageModule` (see the Data Dictionary) is the cached unit. The registry holds a dynamic array of them plus the extension map and the custom rule list.


#### 6.3.2 Key Functions

*   **`int registry_open(const ElcOptions *opts, Registry *out)`** — Resolve the runtime location, load the extension map and custom rules, and verify at least one language module is loadable.
*   **`const LanguageModule *registry_for_path(Registry *reg, const char *path)`**
    *   Purpose: Return the language module governing a file, loading it on first use.
    *   Return Value: The cached module, or `NULL` when the extension maps to no available language — a skip, not an error (HLR-012).
    *   Logic:
        1.  Extract the extension and look it up in the extension map; return `NULL` on a miss.
        2.  Search the loaded-module cache for that language name; return the hit if present.
        3.  Otherwise `snprintf` the parser path, `dlopen(RTLD_LAZY|RTLD_LOCAL)`, and resolve `tree_sitter_<name>` via the POSIX-sanctioned `*(void **)&fn = dlsym(...)` form with `dlerror()` cleared beforehand.
        4.  Read and compile each `.scm` query with `ts_query_new()`.
        5.  On any failure, emit a diagnostic, mark the language unusable so it is not retried, and return `NULL`; the run continues on the remaining languages (HLR-070).

*   **`int registry_load_rules(Registry *reg, const ElcOptions *opts)`** — Load and compile custom rule queries against their bound language; fatal for CLI-named files, diagnostic-and-skip for runtime-located ones (HLR-116).
*   **`void registry_close(Registry *reg)`** — Delete every query, then every parser context, then dlclose every handle.

#### 6.3.3 Parsing Strategy / Algorithm

Teardown order is load-bearing. A `TSQuery` holds pointers into the `TSLanguage` that `dlclose` unmaps, so `registry_close()` deletes all queries first, then releases the parser and cursor, and only then closes the `dl` handles. The reverse order produces a crash at exit with a backtrace pointing at unmapped memory.

### 6.4 Dependencies

*   `libtree-sitter` — `ts_query_new()`, `ts_query_delete()`.
*   POSIX — `dlopen()`, `dlsym()`, `dlerror()`, `dlclose()`, `open()`, `fstat()`, `read()`.

### 6.5 Error Handling and Logging

*   **Runtime location absent or empty** Fatal. `registry_open()` returns non-zero and the run stops before any file is processed, since no analysis is possible (HLR-036).
*   **Single language module unusable** Diagnostic naming the language, exclude it, continue. Does not by itself make the exit status non-zero (HLR-070).
*   **Custom rule file invalid** Fatal when the file was named on the command line; diagnostic-and-skip when it was found in the runtime location (HLR-116).

## 7. Detailed Design for [src/analyze.c](../src/analyze.c)

### 7.1 Purpose and Responsibilities
[src/analyze.c](../src/analyze.c) performs the single parse of each source file and extracts everything any later stage will need from it: per-function identity and metrics, and the raw call and global-access facts from which the SDG is built. No other module reads source text.

*   Map the file read-only, count physical lines, and hand a zero-copy buffer to the parser. Files are opened `O_RDONLY` and mapped `PROT_READ`; no module in `src/` ever opens a source file for writing, which is how HLR-043 is satisfied structurally rather than by convention.
*   Run the comment, function, ELOC, complexity, call, and global queries against the parsed tree.
*   Merge comment spans and classify statements to compute ELOC per function and per file.
*   Attribute each statement and decision point to its innermost enclosing reported function.
*   Emit `FileFacts` — the call sites, global accesses, and address-taken functions — for later cross-file resolution.
*   Evaluate custom rule queries and record their matches.

### 7.2 External Interfaces
#### 7.2.1 Query Capture Contract

The `.scm` files communicate through capture names, which are the contract between `runtime/` and this module: `@function.name`, `@function.body`, `@comment`, `@statement`, `@decision`, `@call`, `@call.name`, `@function.address_taken`, `@global.read`, `@global.write`. A capture name this module does not recognise is ignored, so a query file may carry extra captures for its own purposes. `@function.address_taken` marks a function whose address is taken without being called — the fact that keeps callbacks and interrupt handlers out of the dead-code report (§11).


### 7.3 Internal Structure
#### 7.3.1 Key Data Structures

Produces `FileMetrics` and `FileFacts`. Holds a scratch span list for comment merging, reused across files to avoid per-file allocation.


#### 7.3.2 Key Functions

*   **`int analyze_file(Registry *reg, const char *path, FileMetrics **metrics, FileFacts **facts)`**
    *   Purpose: Parse one file and produce both its metrics and its graph facts.
    *   Pre-condition: `path` names a readable regular file.
    *   Post-condition: The mapping has been released; the returned structures own their own memory.
    *   Return Value: 0 on success; non-zero on a read or parse failure, which the caller records without aborting.
    *   Logic:
        1.  Obtain the language module; a `NULL` return means skip this file and report it skipped (HLR-012).
        2.  `open`, `fstat`, and short-circuit a zero-length file to zero metrics, since `mmap` of an empty file fails with `EINVAL` (HLR-020).
        3.  `mmap` the file and count `\n` occurrences for the physical line total.
        4.  `ts_parser_set_language()` and `ts_parser_parse_string()` with the explicit `st_size` length — the mapping is not NUL-terminated.
        5.  Run `comments.scm`, collect spans, sort by start byte, and merge overlaps into a canonical excluded-line set (HLR-016).
        6.  Run `functions.scm` to establish the reported function set, including nested named functions, and build an innermost-enclosing lookup over their byte ranges (HLR-067).
        7.  Run `eloc.scm` and `complexity.scm`, attributing each capture to its innermost enclosing reported function; count each multi-line statement once at its start line (HLR-053, HLR-068).
        8.  Run `calls.scm` and `globals.scm`, recording each call site, each global access, and each address-taken function with its enclosing function, into `FileFacts`.
        9.  Run every loaded custom rule query and record its matches (HLR-109). A match's identity is the rule file's basename plus the capture name that matched, so one `.scm` file may express several distinct named rules.
        10.  `ts_tree_delete()` and `munmap()` on every exit path.
    *   Notes: Identifier text is `memcpy`'d out of the mapping into NUL-terminated allocations before the mapping is released, since every name outlives it.

*   **`uint32_t merge_comment_spans(SpanList *spans)`** — Sort by start byte and coalesce overlapping and nested spans; returns the merged line count.
*   **`const FnRange *innermost_enclosing(const FnRangeIndex *idx, uint32_t byte)`** — Return the narrowest reported function containing a byte offset.
*   **`void filemetrics_free(FileMetrics *m)`** — Release a file's metrics and every function name it owns.
*   **`void filefacts_free(FileFacts *f)`** — Release the call sites, global accesses, address-taken records, and rule matches a file produced.

#### 7.3.3 Parsing Strategy / Algorithm

Comment-span merging is the one place where a naive implementation is silently wrong. Spans are sorted by start byte and coalesced pairwise; only then is the merged line count excluded. Subtracting per capture double-counts a block comment that contains inline comment syntax, and can drive a file's ELOC negative. The innermost-enclosing lookup is the analogous safeguard for nested named functions: attributing each statement to exactly one reported function is what prevents a nested subprogram's lines from being counted twice (HLR-068).

### 7.4 Dependencies

*   `libtree-sitter` — parser, query, and cursor API.
*   POSIX — `mmap()`, `munmap()`, `open()`, `fstat()`.
*   `src/registry.c` for the language module and its compiled queries.

### 7.5 Error Handling and Logging

*   **File unreadable or undecodable** Diagnostic to `stderr` naming the file; return non-zero; the run continues (HLR-035).
*   **Parse produces an error tree** Tree-sitter always returns a tree, so this means the root reports an ERROR node. `elc` treats **any** error node as a whole-file parse failure: diagnostic, skip, continue. This is deliberately conservative — one syntax error discards a large file — because partial metrics from a damaged tree would be indistinguishable from sound ones once rendered, and a silently undercounted file is worse than a visibly skipped one. If experience shows this to be too blunt, the tolerance belongs in this one place.
*   **Zero-length file** Not an error. Reported with zero ELOC and no functions (HLR-020).

## 8. Detailed Design for [src/graph.c](../src/graph.c)

### 8.1 Purpose and Responsibilities
[src/graph.c](../src/graph.c) resolves the per-file facts produced by `analyze.c` into the project-wide System Dependence Graph, and owns that graph for the remainder of the run.

*   Build the project symbol table from every function definition discovered across all files.
*   Resolve each recorded call site to a defining function, adding a call edge, or record it as unresolved.
*   Add global-state edges from writers and to readers of each global object.
*   Derive the component-level projection of the function graph used by `arch.c`.
*   Own the underlying graph library object and expose a stable node ordering to every analysis.

### 8.2 External Interfaces
#### 8.2.1 Node Identity

Every SDG node carries a stable index assigned in the order functions were discovered from the already-sorted file list, so graph traversal results can be reduced to a deterministic order regardless of the library's internal enumeration (HLR-033).

#### 8.2.2 Edge Multiplicity

The SDG is a **simple** directed graph: repeated calls from one function to the same callee collapse to a single edge carrying a call-site count attribute. This is what makes out-degree equal the count of *distinct* subroutines invoked, as HLR-085 requires; a multigraph would inflate fan-out for a function that calls one helper in a loop body and again in its error path. The retained count is what the `.dot` writer uses for edge weighting.


### 8.3 Internal Structure
#### 8.3.1 Key Data Structures

`Sdg` (see the Data Dictionary) wraps the graph object, the node table, the symbol table, the component projection, and the unresolved-call tally.


#### 8.3.2 Key Functions

*   **`int graph_build(const FactList *facts, const FileList *files, Sdg *out)`**
    *   Purpose: Construct the SDG from the accumulated per-file facts.
    *   Pre-condition: `facts` covers every successfully analysed file; `files` is in its final sorted order.
    *   Post-condition: `out` holds a fully populated graph; no source file has been reopened (HLR-076).
    *   Return Value: 0 on success; non-zero only on allocation failure.
    *   Logic:
        1.  Walk every `FileFacts` in file order, assigning each defined function a node index and inserting it into the symbol table.
        2.  Walk every recorded call site; look its target up in the symbol table and add an edge on a hit.
        3.  On a miss — an external library call, a system call, or an indirect call through a pointer — increment the unresolved tally and record the site for reporting rather than failing (HLR-077).
        4.  For each global object, add an edge from every writing function and to every reading function (HLR-074).
        5.  Build the component projection: an edge from component X to Y whenever any function in X calls a function in Y, or writes a global that a function in Y reads (HLR-114).
    *   Notes: Resolution is name-and-arity based within the analysed target. Indirect calls through a function pointer are not resolved to a destination, but the *address-taken* fact is retained and carried into reachability (§11). The asymmetry matters: a **missing** edge causes a live function to be reported as provably dead, which is a correctness failure against HLR-097, whereas an **extra** root merely shrinks the unreachable set and costs nothing but a missed pruning opportunity. `elc` therefore errs toward reachable — an interrupt vector or callback table entry is never reported as dead merely because no direct call to it exists.

        **Call-edge precision is language-dependent, and over-approximation is not uniformly safe.** The paragraph above concerns *missing* edges; the opposite error also occurs. Some languages make a call syntactically indistinguishable from something else without semantic analysis a grammar does not perform — Ada's `Foo (X)` is a function call or an array index, and the grammar manages the ambiguity with precedence rules rather than resolving it. Where a language's `calls.scm` cannot separate the two, the graph carries edges that do not correspond to calls. `elc` does not attempt to disambiguate in C: doing so would place language knowledge in the binary, which the design forbids outright.

        The consequences differ by analysis, and only one of them is dangerous:

        *   **Reachability (§11, HLR-096)** — safe. A spurious edge can only shrink the unreachable set, the same direction the address-taken rule already errs in. A live function is never called dead because of it.
        *   **Fan-out and coupling (§10, §9)** — inflated. Numbers are noisier for such a language than for one whose calls are unambiguous, and a borderline function may cross a threshold it would not otherwise cross.
        *   **Call depth (§10)** — inflated, on top of already being a lower bound for the unresolved-call reason.
        *   **Dependency cycles (§9, HLR-083/084)** — **the dangerous case.** A spurious edge can close a cycle that does not exist in the program, and HLR-084 reports every cycle at critical severity against an acceptable count of strictly zero. A false critical finding costs more than a noisy metric, because it spends the reader's trust in every other finding.

        This is not resolvable in `src/`; it is a property of what the grammar can express. What the design owes the reader is that the limitation is stated rather than discovered, and that the `graph/` fixture group pins the behaviour for any language where it applies (STP §5).

*   **`size_t graph_unresolved_count(const Sdg *g)`** — Return the number of call sites that could not be resolved.
*   **`void graph_free(Sdg *g)`** — Release the graph, node table, symbol table, and projection.
### 8.4 Dependencies

*   An established graph library for the underlying structure and its algorithms (HLR-113).
*   `src/analyze.c` for the facts; depended upon by `arch.c`, `calltree.c`, and `state.c`.

### 8.5 Error Handling and Logging

*   **Unresolvable call site** Not an error. Counted and reported so the reader can judge the graph's completeness (HLR-077).
*   **Duplicate symbol definition** Recorded once with a diagnostic; the first definition in sorted file order wins, keeping resolution deterministic.

## 9. Detailed Design for [src/arch.c](../src/arch.c)

### 9.1 Purpose and Responsibilities
[src/arch.c](../src/arch.c) implements the component-level analyses: afferent and efferent coupling, the Instability metric, bottleneck identification, dependency-cycle detection, and architectural layering validation.

*   Compute `Ca` and `Ce` for every component from the component projection.
*   Compute Instability, reporting it as undefined where both couplings are zero.
*   Identify bottlenecks against the configurable threshold.
*   Detect every component-level dependency cycle.
*   Validate declared strata, reporting both skip-level calls (HLR-079) and direction-inverted calls (HLR-118), when strata were declared.


### 9.3 Internal Structure
#### 9.3.1 Key Functions

*   **`int arch_analyse(const Sdg *g, const ElcOptions *opts, ArchResults *out)`** — Run every component-level analysis, skipping layering when no strata were declared.
*   **`void arch_results_free(ArchResults *r)`** — Release the coupling table, instability values, cycle list, and violation list.
*   **`void compute_coupling(const Sdg *g, ArchResults *out)`** — Populate Ca and Ce per component.
*   **`double instability(uint32_t ca, uint32_t ce, bool *defined)`** — Ce/(Ce+Ca), with defined set false when both are zero.
*   **`int find_cycles(const Sdg *g, CycleList *out)`** — Strongly connected components of the component projection, excluding trivial single-node components.
*   **`int check_strata(const Sdg *g, const ElcOptions *opts, ViolationList *out)`** — Report calls that bypass declared layers and calls that invert the declared dependency direction.

#### 9.3.2 Parsing Strategy / Algorithm

Cycles are found as the non-trivial strongly connected components of the *component* projection, not of the function graph. This is what keeps mutual recursion between two functions in one file from being reported as a dependency cycle: within a single component there is no inter-component edge to close a loop. Mutual recursion across two files is legitimately both a recursion finding and a component cycle, because the two facts are different (HLR-083, HLR-089). Stratum checking compares the declared ordinal of the caller's stratum with the callee's, and yields two independent findings from that one comparison. A call descending more than one level is *skip-level* (HLR-079); a call ascending at all runs against the declared direction and is *direction-inverted* (HLR-118). The two are orthogonal — a driver calling one layer up inverts without skipping, and an application reaching two layers down skips without inverting — so each is reported in its own right rather than folded into a single "layering violation".

### 9.4 Dependencies

*   The graph library, for strongly-connected-component decomposition.
*   `src/graph.c` for the SDG and its component projection.

### 9.5 Error Handling and Logging

*   **No strata declared** Layering validation is omitted and the omission is stated in the report; it is not an error (HLR-115).
*   **Stratum pattern matches no component** Diagnostic to `stderr`; the declared layer remains in effect and simply contains nothing.

## 10. Detailed Design for [src/calltree.c](../src/calltree.c)

### 10.1 Purpose and Responsibilities
[src/calltree.c](../src/calltree.c) implements the function-level call-tree analyses: fan-out and its threshold classification, maximum call depth, the deepest call stack in full, and recursion detection.

*   Compute per-function fan-out and classify it against the published width thresholds.
*   Detect direct and mutual recursion among functions.
*   Compute the maximum call depth from the declared entry points, and capture the ordered chain that achieves it.


### 10.3 Internal Structure
#### 10.3.1 Key Functions

*   **`int calltree_analyse(const Sdg *g, const ElcOptions *opts, TreeResults *out)`**
    *   Purpose: Produce every call-tree measurement the report requires.
    *   Post-condition: Either a depth and a deepest chain are present, or the recursion list is non-empty and the depth is marked unbounded.
    *   Return Value: 0 on success.
    *   Logic:
        1.  Compute out-degree per function node; classify against the healthy, warning, and critical bands (HLR-086).
        2.  Decompose the *function* graph into strongly connected components; every non-trivial one, and every self-loop, is a recursive cycle (HLR-089).
        3.  If no entry points were declared, mark depth omitted and stop (HLR-115).
        4.  If any recursion was found, mark the depth unbounded, attach the recursive cycles, and stop — no finite deepest chain exists (HLR-090).
        5.  Otherwise the graph is a DAG: compute the longest path from each entry point by memoised traversal in reverse topological order, retaining the predecessor of each node.
        6.  Walk the retained predecessors back from the deepest leaf to reconstruct the ordered chain, and record it in full (HLR-088).
    *   Notes: Establishing acyclicity before measuring depth is what makes the longest-path computation terminate; on a cyclic graph the question has no finite answer, which is precisely why MISRA C Rule 17.2 exists. The measured depth is a lower bound on true worst-case depth: a chain that continues through an unresolved indirect call is not followed. The report therefore presents the depth alongside the unresolved-call count of HLR-077, so the reader can judge how completely the graph covers the program.

*   **`int longest_path_dag(const Sdg *g, const NodeSet *entries, Chain *out)`** — Memoised longest-path search with predecessor retention.
*   **`void tree_results_free(TreeResults *r)`** — Release the fan-out table, the recursive-cycle list, and the retained deepest chain.
### 10.4 Dependencies

*   The graph library, for topological ordering and strongly connected components.
*   `src/graph.c`.

### 10.5 Error Handling and Logging

*   **No entry points declared** Depth and deepest-chain analysis omitted, with the omission stated in the report (HLR-115).
*   **Recursion present** Not an error. The recursive cycles are reported in place of a depth figure (HLR-090).

## 11. Detailed Design for [src/state.c](../src/state.c)

### 11.1 Purpose and Responsibilities
[src/state.c](../src/state.c) implements the global-state and reachability analyses: the writer/reader map for each global object, scope-reduction and hidden-channel findings, execution-scope isolation, and dead-code detection.

*   Report the writing and reading function sets for every global object.
*   Flag single-function globals for scope reduction, and multi-domain globals as hidden channels.
*   Report cross-scope access paths when execution scopes were declared.
*   Compute reachability from the declared entry points *together with* every address-taken function, and report everything unvisited as unreachable.
*   Report as unreachable any global object accessed solely by functions that are themselves unreachable (HLR-096).


### 11.3 Internal Structure
#### 11.3.1 Key Functions

*   **`int state_analyse(const Sdg *g, const ElcOptions *opts, StateResults *out)`** — Run the global-state, scope-isolation, and reachability analyses, skipping those whose declarations are absent.
*   **`void classify_globals(const Sdg *g, StateResults *out)`** — Apply the scope-reduction and hidden-channel rules to each global.
*   **`int reachability(const Sdg *g, const NodeSet *roots, NodeSet *unreachable)`** — Breadth-first traversal from the root set; the complement is the unreachable set.
*   **`void collect_roots(const Sdg *g, const ElcOptions *opts, NodeSet *roots)`** — Union of the declared entry points and every address-taken function.
*   **`void unreachable_globals(const Sdg *g, const NodeSet *unreachable, GlobalList *out)`** — Globals accessed only by unreachable functions are themselves unreachable.
*   **`int check_scopes(const Sdg *g, const ElcOptions *opts, ViolationList *out)`** — Report every edge crossing a declared execution-scope boundary.
*   **`void state_results_free(StateResults *r)`** — Release the global access map, the hidden-channel and scope-reduction lists, and the unreachable sets.

#### 11.3.2 Parsing Strategy / Algorithm

The hidden-channel test asks whether the functions touching a global fall into more than one weakly connected region of the call graph once that global's own edges are disregarded. A global shared within one call-connected region is ordinary shared state; one shared across regions that never call each other is the temporal coupling MISRA C Rule 8.9 is concerned with. Dead-code detection is plain forward reachability, which is exactly why it is immune to the failure mode of textual linters: a clique of unused functions calling one another is still unvisited, because no path reaches it from any entry point (HLR-097).

### 11.4 Dependencies

*   The graph library, for traversal and connectivity.
*   `src/graph.c`.

### 11.5 Error Handling and Logging

*   **No entry points declared** Reachability analysis omitted with a stated reason. `elc` must never report every function as unreachable merely because nothing was declared (HLR-115).
*   **No execution scopes declared** Scope-isolation analysis omitted with a stated reason (HLR-094, HLR-115).

## 12. Detailed Design for [src/thresholds.c](../src/thresholds.c)

### 12.1 Purpose and Responsibilities
[src/thresholds.c](../src/thresholds.c) applies the published threshold catalogue of PVD Appendix A to every measurement the analyses produced, attaching a severity and a source attribution to each resulting finding.

*   Hold the threshold catalogue as a static table of measurement kind, band boundaries, severity, and citation.
*   Classify each measurement into its band and emit a `Finding` when it falls outside the accepted range.
*   Attribute every threshold to its external source, and mark `elc`'s own heuristics as such.

### 12.2 External Interfaces
#### 12.2.1 Threshold Catalogue


| Measurement | Bands | Attribution |
| ----------- | ----- | ----------- |
| Function fan-out | 0–10 no finding (3–7 healthy); 11–15 warning; >15 critical | Henry–Kafura |
| Call depth | >8 warning; >12 critical, on stack-constrained targets | Embedded practice |
| Recursion present | critical | MISRA C Rule 17.2 |
| Component cycles | any occurrence critical | Martin / acyclic dependencies |
| Single-function global | warning | MISRA C Rule 8.9 |
| Hidden channel | warning | MISRA C Rule 8.9 |
| Instability vs. declared stratum | warning on mismatch | Martin |
| Bottleneck (`Ca` and `Ce` ≥ threshold) | warning | **`elc` heuristic — not a published standard** |



### 12.3 Internal Structure
#### 12.3.1 Key Functions

*   **`int thresholds_apply(const ArchResults *a, const TreeResults *t, const StateResults *s, const ElcOptions *opts, FindingList *out)`** — Evaluate every measurement against the catalogue and emit findings.
*   **`const Threshold *thresholds_lookup(MeasurementKind kind)`** — Return the catalogue entry for a measurement kind.
*   **`void findinglist_free(FindingList *f)`** — Release every finding and the detail string each owns.
### 12.4 Dependencies

*   No third-party dependency. Pure evaluation over the analysis results.

### 12.5 Error Handling and Logging

*   **Measurement with no catalogue entry** Reported as a bare value with no severity, rather than being silently dropped or assigned an invented band.

## 13. Detailed Design for [src/report.c](../src/report.c)

### 13.1 Purpose and Responsibilities
[src/report.c](../src/report.c) assembles every metric, finding, and custom-rule match into a single format-independent report model, and imposes the stable ordering that makes the output deterministic.

*   Merge per-file metrics, project totals, per-language breakdowns, and every analysis result into one structure.
*   Carry every computed architectural *measurement* into the model, not only those that crossed a threshold, so that a value lying within its accepted band is still reported.
*   Compute the project summary, including the most-complex callouts with their tie-break rule.
*   Apply the complexity threshold to produce each file's over-threshold function list.
*   Sort every collection in the model by an explicit key before any renderer sees it.
*   Record which analyses were omitted, and why.
*   Record every file skipped for want of a language module, so the report accounts for each discovered file (HLR-012).
*   Record the discovery route applied to each directory target, so that an unexpectedly empty or oversized result is diagnosable (HLR-127).


### 13.3 Internal Structure
#### 13.3.1 Key Functions

*   **`int report_assemble(const MetricsAccumulator *acc, const ArchResults *a, const TreeResults *t, const StateResults *s, const FindingList *f, const ElcOptions *opts, Report *out)`**
    *   Purpose: Produce the ordered, format-independent report model.
    *   Post-condition: Every collection in `out` is sorted by its defined key; no renderer needs to sort anything.
    *   Return Value: 0 on success.
    *   Logic:
        1.  Sum physical lines and ELOC across all files, both combined and per language (HLR-024, HLR-025).
        2.  Select the highest-ELOC file and highest-complexity function, breaking ties by the stable presentation order (HLR-026).
        3.  For each file, filter its functions by the complexity threshold into the over-threshold list (HLR-021).
        4.  Attach the architectural findings, custom-rule matches, and omission notices.
        5.  Sort files by path; functions by start line; findings by (severity, kind, primary location); cycles by their lowest member; unreachable functions by (file, line) (HLR-033).
    *   Notes: Centralising every sort here is deliberate: it is the single place a reviewer must check to be satisfied that HLR-032's byte-identical guarantee holds, rather than auditing six renderers and three analysis modules.

*   **`void report_free(Report *r)`** — Release the report model and everything it owns.
### 13.4 Dependencies

*   Every analysis module, for their results. Depended upon by every renderer, giving it high afferent coupling and low efferent coupling — a deliberately stable component.

### 13.5 Error Handling and Logging

*   **Empty run** A run in which no file was analysable still produces a complete model with zero totals, which renders normally and exits zero (HLR-066).

## 14. Detailed Design for [src/format_text.c](../src/format_text.c)

### 14.1 Purpose and Responsibilities
[src/format_text.c](../src/format_text.c) renders the report model in the two human-facing formats: the aligned ASCII table that is the default, and GitHub-Flavored Markdown.

*   Compute column widths from the longest path and function name, and render the aligned table.
*   Render Markdown with functions grouped under a per-file heading.
*   Present every tier the uniform-composition rule requires, in both formats.


### 14.3 Internal Structure
#### 14.3.1 Key Functions

*   **`int format_table(const Report *r, FILE *out)`** — Render the aligned ASCII table.
*   **`int format_markdown(const Report *r, FILE *out)`** — Render GitHub-Flavored Markdown.
*   **`void render_summary(const Report *r, FILE *out, Style style)`** — Shared project-summary rendering for both formats.

#### 14.3.2 Parsing Strategy / Algorithm

Both renderers walk the identical model in the identical order and emit the identical tiers — project summary, per-file totals and over-threshold list, per-function detail, architectural measurements and findings, custom-rule matches — differing only in decoration. The traversal also emits the discovery route of each directory target (HLR-127) ahead of the per-file detail, so that a reader sees how a target was enumerated before seeing what it yielded. Sharing the traversal is what keeps HLR-031's uniform-composition guarantee true by construction rather than by parallel maintenance.

### 14.4 Dependencies

*   `src/report.c`. No third-party dependency; `printf`-family formatting only.

### 14.5 Error Handling and Logging

*   **Write failure on the output stream** Diagnostic to `stderr` and a non-zero return; a truncated report is never reported as success.

## 15. Detailed Design for [src/format_csv.c](../src/format_csv.c)

### 15.1 Purpose and Responsibilities
[src/format_csv.c](../src/format_csv.c) renders the per-function dataset as RFC 4180 CSV — the flat, unfiltered, machine-facing view.

*   Emit one record per function, unaffected by the complexity threshold.
*   Quote and escape any field containing a comma, a double quote, or a line break.


### 15.3 Internal Structure
#### 15.3.1 Key Functions

*   **`int format_csv(const Report *r, FILE *out)`** — Write the header row and one record per function.
*   **`void write_field(const char *value, FILE *out)`** — Emit one field, quoting and doubling embedded quotes per RFC 4180.

#### 15.3.2 Parsing Strategy / Algorithm

Every field passes through `write_field()` without exception. A C++ template signature such as `foo<int, long>` contains a comma that would otherwise split one logical field across two columns and silently corrupt every downstream consumer (HLR-064).

### 15.4 Dependencies

*   `src/report.c`. No third-party dependency.

### 15.5 Error Handling and Logging

*   **Write failure** Diagnostic and non-zero return, as for the text renderers.

## 16. Detailed Design for [src/format_xml.c](../src/format_xml.c)

### 16.1 Purpose and Responsibilities
[src/format_xml.c](../src/format_xml.c) writes the complete XML record of a run, and reads such a record back to drive the report-regeneration mode. It is the only module that both produces and consumes a serialised form.

*   Emit every element of the report model, so the record is sufficient to regenerate any report `elc` can produce.
*   Carry a format-version identifier in the document root.
*   Escape every character with structural meaning in XML.
*   Parse a saved record back into a report model, rejecting anything malformed, structurally foreign, or of an unsupported version.

### 16.2 External Interfaces
#### 16.2.1 XML Record Structure

A root `<elc-report format-version="N">` carrying `<summary>`, `<languages>`, `<files>` with nested `<function>` elements, `<architecture>` holding every graph finding, `<custom-rules>`, and `<omissions>`. The format version is incremented whenever an element is removed or its meaning changes.


### 16.3 Internal Structure
#### 16.3.1 Key Functions

*   **`int xml_write_report(const Report *r, FILE *out)`** — Serialise the complete report model, escaping every emitted value.
*   **`int xml_read_report(const char *path, Report *out)`**
    *   Purpose: Reconstruct a report model from a previously written record.
    *   Return Value: 0 on success; non-zero after a diagnostic when the input is malformed, structurally foreign, or of an unsupported format version.
    *   Logic:
        1.  Stream the document through the parser, driving a small element stack rather than materialising a tree.
        2.  Reject immediately unless the root element is `elc-report` and its `format-version` is one this build supports (HLR-058).
        3.  Populate the report model from the recognised elements, ignoring unknown elements from a newer minor revision.
        4.  Re-apply the complexity threshold supplied at conversion time, which may differ from the one in force when the record was written (HLR-057).

*   **`void write_escaped(const char *value, FILE *out)`** — Emit text with &, <, >, and quotation marks escaped.

#### 16.3.2 Parsing Strategy / Algorithm

Writing is hand-rolled text emission; reading is streamed through a parser. This asymmetry is deliberate. Emission needs only correct escaping, which `write_escaped()` provides in one place, so a writer library would add a dependency for no benefit. Ingestion needs a hardened parser, because the input is a file the user supplies and may not be one `elc` wrote.

### 16.4 Dependencies

*   A streaming XML parser for the read path only. The write path has no third-party dependency.
*   `src/report.c`.

### 16.5 Error Handling and Logging

*   **Malformed or foreign input** Diagnostic and non-zero exit. No best-effort partial conversion is attempted, since a partially reconstructed report is indistinguishable from a complete one once rendered (HLR-058).
*   **Unsupported format version** Rejected explicitly, naming the version found and the versions supported.

## 17. Detailed Design for [src/format_graph.c](../src/format_graph.c)

### 17.1 Purpose and Responsibilities
[src/format_graph.c](../src/format_graph.c) writes the two graph-shaped outputs: the Graphviz `.dot` call tree for visual inspection, and the GraphML export for ingestion by other tools.

*   Emit the call tree as `.dot`, annotated with every applicable architectural finding.
*   Decide whether a `.dot` file is warranted, given the default-on setting, the disable switch, and the output destination.
*   Emit the SDG as GraphML when explicitly requested.

### 17.2 External Interfaces
#### 17.2.1 GraphML Content Model

GraphML is the only channel exposing the graph's *topology* — the rendered findings report conclusions, not edges — which makes it the assertion surface for the STP's `graph/` fixture group. Its content model is therefore fixed here rather than left to the writer, so that a fixture's `expected.graphml` stays stable across changes to the writer.

| Scope | Key | Carries |
| ----- | --- | ------- |
| graph | `format-version` | Structure version, incremented on any removal or change of meaning |
| graph | `unresolved-calls` | Count of call sites with no resolvable target (HLR-077) |
| node | `name` | Function name as reported (HLR-014) |
| node | `file`, `line-start`, `line-end` | Definition site |
| node | `component` | The source file that owns it (HLR-114) |
| node | `eloc`, `complexity` | The per-function metrics |
| node | `fan-out` | Distinct callees (HLR-085) |
| node | `address-taken` | Whether the function is a reachability root (HLR-096) |
| edge | `kind` | `call` or `global` — the two edge kinds are distinguishable, never merged |
| edge | `global` | For a `global` edge, the object's name |
| edge | `call-sites` | The collapsed call-site count (HLR-085's simple-graph rule) |

Nodes are emitted in ascending stable node-id order and each node's edges in ascending target-id order (LLR-DOT-04); every value is escaped per HLR-065.

#### 17.2.2 Companion Artefact Naming

Both companion files derive their names from the report's output path by extension substitution: an output of `report.md` yields `report.dot` and `report.graphml`. Neither takes a path of its own. This is precisely why neither is produced when the report goes to standard output — there is no output path to derive a name from, which is the rationale HLR-104 and HLR-106 give (HLR-103, HLR-104, HLR-106).


### 17.3 Internal Structure
#### 17.3.1 Key Functions

*   **`bool graph_dot_warranted(const ElcOptions *opts)`** — True only when .dot generation is enabled and the report goes to a named file.
*   **`int graph_write_dot(const Sdg *g, const Report *r, const char *path)`** — Write the annotated call tree in DOT format.
*   **`int graph_write_graphml(const Sdg *g, const char *path)`** — Write the SDG in GraphML.
*   **`const char *node_style(const Report *r, uint32_t node)`** — Return the Graphviz attributes for a node given the findings that apply to it.

#### 17.3.2 Parsing Strategy / Algorithm

Both writers are plain text emission, which keeps Graphviz a tool the user may run on the output rather than a library `elc` links against (HLR-102), and keeps GraphML generation independent of any XML library. Unlike the report renderers, these two walk the `Sdg` rather than the sorted report model, so they impose their own order explicitly: nodes are emitted in ascending stable node-id order and each node's adjacency in ascending target-id order. Without that the graph library's internal enumeration would leak into the output and break HLR-032. Annotations use colour and shape attributes that a renderer ignoring them will simply drop, leaving a valid call tree (HLR-105).

### 17.4 Dependencies

*   `src/graph.c` and `src/report.c`. No third-party dependency.

### 17.5 Error Handling and Logging

*   **Report written to stdout** No `.dot` and no GraphML file is produced, since no output path exists from which to derive their names (HLR-104, HLR-106).
*   **Companion file cannot be created** Diagnostic to `stderr`; the primary report is still written, and the run is recorded as failed.
## 18. Data Dictionary

*   **`ElcOptions`** (defined in [inc/elc.h](../inc/elc.h)) — The complete, validated configuration of one run. Populated only by cli.c and read-only thereafter.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `mode` | `RunMode` | Analysis or XML regeneration; regeneration requires format md and suppresses both companion artefacts (HLR-055) |
| `format` | `OutputFormat` | table, csv, xml, or md |
| `output_path` | `const char *` | NULL when writing to stdout |
| `complexity_threshold` | `uint32_t` | Default 15 (HLR-022) |
| `bottleneck_threshold` | `uint32_t` | Default 5 (HLR-081) |
| `emit_dot` | `bool` | Default true (HLR-103) |
| `graphml_path` | `const char *` | NULL unless --graphml given |
| `strata` | `StratumList` | Empty when undeclared (HLR-078) |
| `entry_points` | `SymbolList` | Empty when undeclared (HLR-095) |
| `scopes` | `ScopeList` | Empty when undeclared (HLR-094) |
| `rule_paths` | `PathList` | Custom rule query files (HLR-107) |
| `targets` | `PathList` | One or more file or directory arguments (HLR-071) |
*   **`LanguageModule`** (defined in [inc/elc.h](../inc/elc.h)) — One dynamically loaded language, cached by the registry after first use.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `language_name` | `char *` | Resolved from the extension map |
| `dl_handle` | `void *` | Handle from dlopen(); closed last at teardown |
| `ts_lang` | `const TSLanguage *` | Resolved grammar entry point |
| `queries` | `TSQuery *[]` | Compiled comments, functions, complexity, eloc, calls, and globals queries |
| `usable` | `bool` | False once a load failure has been reported, to avoid retrying (HLR-070) |
*   **`FunctionMetric`** (defined in [inc/elc.h](../inc/elc.h)) — The metrics for one reported function, including nested named functions.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `name` | `char *` | Copied out of the mapping before it is released |
| `start_line` | `uint32_t` | 1-based; TSPoint.row is 0-based and converted once |
| `end_line` | `uint32_t` | 1-based |
| `eloc` | `uint32_t` | Executable statements attributed to this function only (HLR-068) |
| `complexity` | `uint32_t` | 1 + decision points |
| `node_id` | `uint32_t` | Index of this function's SDG node |
*   **`FileMetrics`** (defined in [inc/elc.h](../inc/elc.h)) — Per-file totals and the functions the file defines.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `path` | `char *` | Canonical absolute path |
| `language` | `const char *` | Borrowed from the language module |
| `physical_lines` | `uint32_t` | Newline count from the mapping |
| `eloc` | `uint32_t` | File-level ELOC including code outside any function |
| `functions` | `FunctionMetric *` | Dynamic array, grown by doubling |
| `function_count` | `size_t` | Populated entries |
*   **`FileFacts`** (defined in [inc/elc.h](../inc/elc.h)) — The raw graph facts extracted during the same parse that produced FileMetrics.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `calls` | `CallSite *` | Call sites with their enclosing function and callee name |
| `globals` | `GlobalAccess *` | Global reads and writes with their enclosing function |
| `rule_matches` | `RuleMatch *` | Custom rule matches with rule identity and line range |
*   **`Sdg`** (defined in [inc/elc.h](../inc/elc.h)) — The System Dependence Graph and the tables needed to interpret it.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `graph` | `void *` | Opaque handle to the graph library's structure |
| `nodes` | `SdgNode *` | Node table indexed by stable node id |
| `symbols` | `SymbolTable` | Name to node id, for call resolution |
| `components` | `ComponentProjection` | File-level projection used by arch.c (HLR-114) |
| `unresolved` | `size_t` | Call sites with no resolvable target (HLR-077) |
*   **`Finding`** (defined in [inc/elc.h](../inc/elc.h)) — One reportable observation. Severity is data and never influences the exit status (HLR-100).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `kind` | `MeasurementKind` | Which analysis produced it |
| `severity` | `Severity` | info, warning, or critical |
| `attribution` | `const char *` | Citation, or an explicit marker for elc's own heuristics (HLR-099) |
| `location` | `Location` | File, line, and node where applicable |
| `detail` | `char *` | Rendered description, including cycle members or chain steps |
*   **`Report`** (defined in [inc/elc.h](../inc/elc.h)) — The format-independent model every renderer consumes. Every collection is sorted before a renderer sees it.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `summary` | `ProjectSummary` | Combined and per-language totals, and the most-complex callouts |
| `files` | `FileMetrics **` | Sorted by path |
| `measurements` | `MeasurementList` | Per-component coupling and instability, per-function fan-out; reported whether or not a threshold was crossed (HLR-080, HLR-082, HLR-085) |
| `findings` | `FindingList` | Sorted by severity, kind, then location |
| `rule_matches` | `RuleMatchList` | Sorted by file then line |
| `omissions` | `OmissionList` | Analyses skipped for want of a declaration, with reasons (HLR-115) |
| `skipped_files` | `PathList` | Discovered files with no available language module, sorted by path (HLR-012) |
| `routes` | `RouteList` | Per directory target, whether it was enumerated from a repository or traversed from the filesystem (HLR-127) |
| `unresolved_calls` | `size_t` | Call sites with no resolvable target, reported so graph completeness is visible (HLR-077) |
*   **Compile-time constants** (in [inc/elc.h](../inc/elc.h)):

    | Name | Value | Purpose |
    | ---- | ----- | ------- |
| `ELC_DEFAULT_COMPLEXITY_THRESHOLD` | 15 | Per-file over-threshold listing (HLR-022, HLR-057) |
| `ELC_DEFAULT_BOTTLENECK_THRESHOLD` | 5 | Ca and Ce floor for bottleneck flagging; elc's own heuristic (HLR-081) |
| `ELC_FANOUT_HEALTHY_MIN` | 3 | Lower bound of the healthy fan-out band (Appendix A.2) |
| `ELC_FANOUT_HEALTHY_MAX` | 7 | Upper bound of the healthy fan-out band |
| `ELC_FANOUT_WARNING` | 10 | Above this, weak abstraction is reported |
| `ELC_FANOUT_CRITICAL` | 15 | Above this, a god function is reported |
| `ELC_DEPTH_WARNING` | 8 | Call depth beyond which constrained targets are at risk |
| `ELC_DEPTH_CRITICAL` | 12 | Call depth at which stack/heap collision is likely |
| `ELC_XML_FORMAT_VERSION` | 1 | Record format identifier (HLR-061, HLR-058) |
| `ELC_RUNTIME_DIR_ENV` | "ELC_RUNTIME_DIR" | Overrides the runtime location adjacent to the binary (HLR-059) |
**Interception points for unit testing.** The STP mocks dependencies with GNU ld's `--wrap` rather than with a seam in `src/`. The canonical wrap targets per module are recorded here so that the Makefile's `-Wl,--wrap=` lists and the tests' `__wrap_` definitions have one source rather than two:

| Module | Wrapped symbols | Reaches |
| ------ | --------------- | ------- |
| `analyze.c` | `mmap`, `open`, `fstat`, `read`, `realloc` | Read failure, zero-length file, failed array growth (LLR-ANL-04, ANL-28, ANL-34) |
| `registry.c` | `dlopen`, `dlsym`, `dlerror`, `ts_query_new` | Malformed module tolerance without a corrupt `.so` on disk (HLR-070, LLR-RFP-06) |
| `discover.c` | `stat`, `git_repository_open_ext`, `realpath` | Invalid target classes, repository inapplicability, canonicalisation failure (LLR-DSC-02, GIT-04) |
| `graph.c`, `arch.c`, `calltree.c`, `state.c` | `realloc`, the graph library's allocating entry points | Allocation failure on graph paths (HLR-124, HLR-125) |
| `report.c` | `realloc` | Checked collection growth (LLR-RPT-16) |
| `format_*.c` | `fwrite`, `fprintf`, `fopen` | Write-failure paths, companion file creation failure (LLR-DOT-05) |

Wrapping is confined to the unit level; every other level links and runs the real binary.

**Dependency selection.** HLR-112 defers library choice to this document. The selections below were made after confirming the maintenance status of each candidate named in the PVD:

| Role | Selected | Rationale |
| ---- | -------- | --------- |
| Parsing and queries | **Tree-sitter** | Not a free choice: its query language and grammar format are a user-visible contract (HLR-112). Actively released. |
| Repository access | **libgit2** | Actively maintained, frequent releases; the only mature C option for tracked-file enumeration. |
| Graph algorithms | **igraph** | The only mature C-native graph library; its 1.0 series carries an explicit long-term API stability commitment. Alternatives (Boost.Graph, LEMON, NetworKit) are C++ and would impose a second toolchain. Build with GraphML support disabled — see below. |
| XML reading | **Expat** | **Substituted for the PVD's suggested `libxml2`**, which its maintainer declared unmaintained in September 2025 with no successor and known open security issues. Expat is actively maintained, currently funded, streaming, and namespace-aware — everything the read path needs. |
| XML and GraphML writing | **none** | Hand-rolled emission with centralised escaping. Removes a dependency rather than adding one. |
| DOT writing | **none** | Plain text. Graphviz renders the output; `elc` never links it. |

**Ownership of the intermediate structures.** HLR-125's leak gate makes this load-bearing rather than merely tidy, and the pipeline's shape leaves it otherwise ambiguous:

*   A **`FileFacts`** is owned by the caller of `graph_build`, never by the graph. `graph_build` copies what it needs into the SDG's own tables, so the fact list is released with `filefacts_free` as soon as `graph_build` returns; it must not be kept alive for the analyses.
*   **`ArchResults`**, **`TreeResults`**, **`StateResults`**, and the **`FindingList`** are owned by `main`. `report_assemble` copies from them into the report model rather than taking ownership, so `main` releases each with its `*_free` once assembly returns.
*   Every one of these is released on error paths as well as the success path. A run ending in an invalid target or a rejected record must still exit leak-clean, which means teardown cannot live only at the bottom of a successful pipeline.

**Consequence for the igraph build.** igraph's own GraphML support is gated on `IGRAPH_GRAPHML_SUPPORT` and pulls in libxml2 when enabled. Since `elc` writes GraphML itself, igraph must be built with that option **off**, or the unmaintained library re-enters through the back door.
## 19. Traceability

The following table maps the high-level requirements in
[doc/HLRs.md](HLRs.md) and the low-level requirements in
[doc/LLRs.md](LLRs.md) to the design elements above. (Requirement IDs
should be reconciled against the latest revisions of those documents.)

| Requirement Theme | Design Section(s) |
| ----------------- | ----------------- |
| Target discovery and routing (HLR-001 – HLR-006, HLR-062, HLR-069, HLR-071, HLR-072) | §5 |
| Language detection and runtime extensibility (HLR-007 – HLR-012, HLR-059, HLR-060, HLR-070) | §6 |
| ELOC and complexity computation (HLR-013 – HLR-020, HLR-044 – HLR-053, HLR-067, HLR-068) | §7 |
| File and project reporting (HLR-021 – HLR-026, HLR-066) | §13 |
| Report formats (HLR-027 – HLR-031, HLR-064) | §14, §15 |
| XML record and regeneration (HLR-054 – HLR-058, HLR-061, HLR-065) | §16 |
| Determinism (HLR-032, HLR-033) | §5, §13 |
| Failure handling and exit status (HLR-035 – HLR-038, HLR-063, HLR-117) | §3, §4 |
| Non-functional constraints (HLR-039 – HLR-043, HLR-112, HLR-113) | §3, §18 |
| SDG construction (HLR-073 – HLR-077, HLR-115) | §8 |
| Coupling, layering, and cycles (HLR-078 – HLR-084, HLR-114, HLR-118) | §9 |
| Call tree dimensionality (HLR-085 – HLR-090) | §10 |
| Global state and reachability (HLR-091 – HLR-097) | §8, §11 |
| Threshold evaluation and severity (HLR-098 – HLR-101) | §12 |
| Graph outputs (HLR-102 – HLR-106) | §17 |
| Custom rules (HLR-107 – HLR-111, HLR-116) | §6, §7 |
---
