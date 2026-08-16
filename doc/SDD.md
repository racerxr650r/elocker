# Software Design Document: elocker (elc)

**Version:** 2.5
**Date:** 2026-08-21
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
        8.  Open the output destination — the file named by the options, or standard output when none was named — and on a failure to open it emit a diagnostic and return 2 without writing a partial report (HLR-030).
        9.  Dispatch to the selected renderer, then to `graph_write_dot()` when the companion artefact is warranted.
        10.  Tear down in reverse order and return the computed status.
    *   Notes: `main()` contains no analysis logic; its cyclomatic complexity is bounded by the number of stages and their failure branches, keeping it well inside the self-quality target of PVD §8.

### 3.4 Dependencies

*   Every other module in `src/`. `main.c` is depended upon by nothing, giving it an Instability of 1 — the correct value for top-level orchestration (HLR-082).

### 3.5 Error Handling and Logging

*   **Stage returns fatal** Tear down what has been acquired, ensure the diagnostic has reached `stderr`, and return 2. No partial report is written.
*   **Output destination cannot be opened or written** Diagnostic to `stderr` naming the destination, and return 2. A truncated report is never reported as success, and the diagnostic never enters the results stream (HLR-030, HLR-038).
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
| `-D`, `--define` | `name[=value]` | none | HLR-131 |
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
*   **Malformed numeric argument** A threshold must be a plain decimal number and nothing else. `strtoul` alone is not that test: it accepts a leading sign, leading whitespace, a hexadecimal prefix, and a trailing tail, each of which would silently become a threshold the user did not write. The argument is rejected unless it begins with a digit, converts without overflow, and is consumed entirely.

## 5. Detailed Design for [src/discover.c](../src/discover.c)

### 5.1 Purpose and Responsibilities
[src/discover.c](../src/discover.c) turns the target arguments into the de-duplicated, stably ordered list of files the rest of the pipeline will analyse. It selects a traversal strategy per target and applies the exclusion rules appropriate to that strategy.

*   Validate every target argument before any traversal begins.
*   Classify each target with `stat(2)` and route it to direct handling, Git enumeration, or filesystem traversal.
*   Establish whether a discovered repository is *applicable* to the target — that it tracks something at or beneath it — and fall back to filesystem traversal when it is not.
*   Enumerate tracked, non-binary blobs at or beneath the target for an applicable Git target; walk the tree with `fts(3)` otherwise, excluding binary extensions, hidden entries, and anything reached through a symbolic link.
*   Load the binary-extension exclusion list from the runtime location and pass it into the walk, so that no extension is compiled into the executable.
*   Record which route each directory target took, for reporting.
*   De-duplicate by resolved absolute path and sort the result into a stable order.

### 5.2 External Interfaces
#### 5.2.1 File System

Reads directory structure and file metadata only; never opens a source file for content. Opening is `analyze.c`'s responsibility.

#### 5.2.2 Symbolic Links

Target arguments are classified with `stat(2)`, which follows links, so a symlink named directly on the command line is resolved and analysed. Traversal uses `FTS_PHYSICAL`, which does not, so a link encountered during a walk is never followed — a linked directory is not descended into, and a linked file is not appended. The pairing is deliberate and implements both halves of HLR-069; changing either call to match the other breaks one half.

Skipping linked *files* as well as linked directories follows from the same reasoning as the cycle case: a link to a file already inside the tree would otherwise contribute it twice, and a link out of the tree would silently widen what the target denotes. A link that the user means to analyse is named, and naming it resolves it.

#### 5.2.3 Repository

Only libgit2's local object-database entry points are used: open a repository, resolve `HEAD`, walk a tree, read a blob. No remote-bearing call is made, and the library is built with its HTTPS and SSH transports disabled. The claim that `elc` reaches no network is not held by either of those facts on its own — it is held by the instrumented test that observes a real run making no `connect(2)` (HLR-040).

Every handle the route acquires is acquired and released inside `walk_git_tree`, including the repository itself. The alternative — opening in the caller and passing the handle down — spreads the teardown across two functions and makes the fallback path, which is the common one, the path most likely to leak.

#### 5.2.4 Runtime Data

The binary-extension exclusion list is read from `binary.exts` in the runtime location. `discover.c` does not resolve that location: `registry_open()` does, and hands it over. The precedence rule of HLR-059 therefore exists once, in one module, and a change to it cannot leave a second copy behind.


### 5.3 Internal Structure
#### 5.3.1 Key Functions

*   **`int discover_targets(const ElcOptions *opts, const char *runtime_dir, FileList *out, RouteList *routes, size_t *failures)`**
    *   Purpose: Produce the complete, ordered, de-duplicated analysis file list.
    *   Pre-condition: `opts` has been validated by `cli_parse()`.
    *   Post-condition: `out` holds absolute paths in ascending byte order, each appearing exactly once, and `routes` holds one record per directory target, naming its canonical path and the route applied to it.
    *   Return Value: 0 on success, with `*failures` holding the number of per-file failures the traversal encountered; non-zero if any target argument was invalid, in which case `out` is empty. The two are distinct outcomes: an invalid target is fatal and yields no report (HLR-062), whereas a per-file failure degrades the exit status to 1 and the report is still produced (HLR-035).
    *   Logic:
        1.  Loop over every target argument calling `stat(2)` and `access(2)`; on the first that does not exist, cannot be read, or is neither regular file nor directory, emit a diagnostic naming it and return non-zero (HLR-062). The classification each target received is recorded, so the traversal pass does not re-`stat` a path already resolved.
        2.  Load the binary-extension exclusion list from the runtime location, once for the whole run.
        3.  For each validated target: a regular file is appended directly; a directory is offered to `git_repository_open_ext()`, which searches the directory and then its ancestors.
        4.  Offer the directory to `walk_git_tree()`, which opens the repository, resolves `HEAD^{tree}`, and walks it, appending each blob that `git_blob_is_binary()` reports as text **and** whose repository-relative path lies at or beneath the target directory. A negative return means the repository is inapplicable — no repository, none tracking the target, or no commit to resolve — and is not a failure.
        5.  On a negative return, walk the target at the filesystem level instead, and record which of the two routes was used.
        6.  Otherwise walk with `fts_open()`/`fts_read()` in `FTS_PHYSICAL` mode, skipping hidden entries below the target, known binary extensions, and anything reached through a symbolic link.
        7.  Canonicalise each accumulated path with `realpath()`, insert into a set keyed on the canonical path, and finally sort the set into byte order.
    *   Notes: Canonicalising before de-duplication is what makes `elc src/main.c src/` count `main.c` once (HLR-072). Sorting here, rather than relying on `fts` or `libgit2` ordering, is what makes the output independent of filesystem enumeration order (HLR-033).

*   **`bool is_excluded_extension(const char *path, const ExtensionList *exts)`** — Test a path against the binary-extension exclusion list, which is runtime data (runtime/binary.exts), not a compiled-in table. The list is a parameter rather than a global, so every function that consults it receives it through its arguments.
*   **`int binary_exts_load(const char *runtime_dir, ExtensionList *out)`** — Read the exclusion list from the runtime location the registry resolved. An absent or unreadable file is a diagnostic and an empty list, not a fatal error: discovery still runs, and the user is told why nothing was excluded.
*   **`void binary_exts_free(ExtensionList *list)`** — Release the exclusion list and every extension it owns.
*   **`long walk_git_tree(const char *target, const ExtensionList *exts, FileList *out, size_t *failures)`** — Open the repository enclosing target, resolve HEAD^{tree}, and enumerate the tracked text blobs at or beneath target, applying the same hidden-entry and binary-extension exclusions the filesystem route applies. Returns the number of files appended, or a negative value when the repository is inapplicable — which is not a failure, and is what sends the caller to the filesystem walk. Opening the repository here rather than in the caller keeps every libgit2 handle inside one function, so the teardown path is one `goto cleanup` and cannot be got wrong by a caller. The target and the reported working directory are both canonicalised before the repository-relative prefix is derived from them, and the prefix test checks the component boundary: a string prefix is not a path prefix, and a working tree at `/src/proj` would otherwise claim a target at `/src/project`. On any inapplicable outcome the function leaves `out` exactly as it found it, including when the tree walk is abandoned part-way — the caller is about to traverse the same directory, and the union of a partial enumeration with a full traversal is a file set neither route would produce.
*   **`int routelist_add(RouteList *list, const char *target, DiscoveryRoute route)`** — Record the route applied to one directory target, owning a copy of its canonical path. A target already recorded is recorded once.
*   **`void routelist_free(RouteList *list)`** — Release the route list and every target it owns, leaving it usable rather than stale.
*   **`int walk_filesystem(const char *root, const ExtensionList *exts, FileList *out, size_t *failures)`** — fts(3) traversal with hidden-entry, binary-extension, and symbolic-link filtering. Returns non-zero only when the traversal could not be started; an entry that cannot be read increments the failure count and the walk continues.
*   **`void filelist_free(FileList *list)`** — Release the list and every path it owns.

#### 5.3.2 Parsing Strategy / Algorithm

Git tree traversal is preferred over filesystem traversal whenever an *applicable* repository is found, because tracked-file enumeration yields `.gitignore` compliance for free: an ignored or untracked path is simply absent from the tree, with no exclusion list to maintain (HLR-003).

Two constraints on that preference exist because `git_repository_open_ext()` searches upward, and both correct failure modes that the permissive default would otherwise produce.

**Applicability.** Finding a repository is not sufficient reason to use it. A repository that does not track the target directory would enumerate nothing beneath it, and the run would report zero files for a directory full of source — technically consistent with HLR-066, and thoroughly baffling. Two ordinary situations produce this: analysing a `.gitignore`d build directory, and analysing anything at all beneath a version-controlled home directory. The applicability test converts both into a filesystem traversal, which is what the user meant.

**Scoping.** Enumeration is restricted to paths at or beneath the target. Without this, `elc src/` inside a repository analyses the entire project, because the tree at `HEAD` is the whole repository regardless of which subdirectory was named. That would also make the two routes disagree about what a directory target denotes — filesystem traversal walks only the target — and HLR-126 requires them to agree.

`FTS_PHYSICAL` rather than `FTS_LOGICAL` is what prevents a cyclic directory symlink from causing unbounded traversal (HLR-069).

### 5.4 Dependencies

*   `libgit2` — `git_repository_open_ext()`, `git_revparse_single()`, `git_tree_walk()`, `git_blob_is_binary()`.
*   POSIX — `stat(2)`, `access(2)`, `fts(3)`, `realpath(3)`, `getline(3)`.

### 5.5 Error Handling and Logging

*   **Invalid target argument** Diagnostic to `stderr` naming the target; the whole run aborts before any analysis, so no report can silently cover fewer targets than the user named (HLR-062).
*   **Unreadable subdirectory during traversal** Diagnostic to `stderr`, skip that subtree, continue; recorded as a per-file failure so the exit status reflects it.
*   **Binary-extension list absent or unreadable** Diagnostic to `stderr` naming the file, and an empty exclusion list; discovery proceeds and nothing is excluded. Not fatal: HLR-036's fatality concerns a runtime location that yields no language module at all, which is a state in which no analysis is possible; an unfiltered walk is a degraded run, not an impossible one, and the diagnostic makes it visible.
*   **Path that cannot be canonicalised** Diagnostic to `stderr`, the path is dropped from the list, and a per-file failure is recorded; the run continues over the rest.
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
The six per-language queries are required; `eloc.scm`, `calls.scm`, and `globals.scm` supply the ELOC statement classification and the graph facts. `extensions.map` is plain text so that associating a new extension with a language is a data edit, never a rebuild (HLR-060). `binary.exts` lives here too and is read by `discover.c`, which is given this location rather than resolving it (HLR-005).

`conditionals.scm` is a **seventh, optional** file. A module that supplies one gains conditional-region pruning; a module that omits one has no conditional compilation, which is the truth for a language that has none. The required set stays at six, so adding this breaks no module that already exists (HLR-121, HLR-134).

A query file that compiles and captures nothing is valid, and is how an unimplemented query is expressed. The registry reads captures; it never asks whether a file is "filled in". That is what lets a phase ship a language with one query complete and the rest as documented stubs, without either a special case in the loader or a module that fails to load.

The contract this directory embodies — the filenames, the capture names, and what each means — is published with the runtime as `runtime/queries/README.md`. That document, not this section, is what a third party codes against (HLR-121).

#### 6.2.2 Custom Rule Binding

A Tree-sitter query compiles against one specific `TSLanguage`, so every custom rule must name the language it applies to. Rules found under `runtime/queries/<lang>/rules/` are bound by their location; rules named on the command line are bound by the `lang:path` argument form. A rule naming a language with no available module is a diagnostic, not a compile attempt.

#### 6.2.3 Environment

`ELC_RUNTIME_DIR`, when set, takes precedence over the path adjacent to the executable (HLR-059). The adjacent path is derived from the executable itself rather than from `argv[0]`, which a caller controls.

This resolution happens once per run and is the module's alone. `discover.c` also needs the runtime location, for `binary.exts`, and asks for it through `registry_runtime_dir()` rather than repeating the rule — one precedence rule, one implementation.


### 6.3 Internal Structure
#### 6.3.1 Key Data Structures

`LanguageModule` (see the Data Dictionary) is the cached unit. The `Registry` holds a dynamic array of them plus the resolved runtime location, the extension map, and the custom rule list.

It also holds **the run's only `TSParser` and its only `TSQueryCursor`**. Allocating either is expensive and reuse costs nothing — only `ts_parser_set_language()` per file — so both live for the whole run and are destroyed at teardown. They belong to the registry rather than to `analyze.c` because this is where the teardown ordering of LLR-RCL-01 is enforced, and the parser must be released before the `dlclose` that unmaps the grammar it was last set to.


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

*   **`const char *registry_runtime_dir(const Registry *reg)`** — The resolved runtime location, so that another module needing runtime data asks for it rather than repeating HLR-059's precedence rule.
*   **`int registry_load_rules(Registry *reg, const ElcOptions *opts)`** — Load and compile custom rule queries against their bound language; fatal for CLI-named files, diagnostic-and-skip for runtime-located ones (HLR-116).
*   **`void registry_close(Registry *reg)`** — Delete every query, then every parser context, then dlclose every handle.

#### 6.3.3 Parsing Strategy / Algorithm

Teardown order is load-bearing. A `TSQuery` holds pointers into the `TSLanguage` that `dlclose` unmaps, so `registry_close()` deletes all queries first, then releases the parser and cursor, and only then closes the `dl` handles. The reverse order produces a crash at exit with a backtrace pointing at unmapped memory.

### 6.4 Dependencies

*   `libtree-sitter` — `ts_query_new()`, `ts_query_delete()`.
*   POSIX — `dlopen()`, `dlsym()`, `dlerror()`, `dlclose()`, `open()`, `fstat()`, `read()`.

### 6.5 Error Handling and Logging

*   **Runtime location absent or empty** Fatal. `registry_open()` returns non-zero and the run stops before any file is processed, since no analysis is possible (HLR-036). An extension map that names no language is the same state reached differently, and is treated the same way.
*   **Query file will not compile** Diagnostic naming the language, the query file, and the reason in words — `ts_query_new()`'s numeric code alone tells the author of a query file nothing, and that author is who acts on this message. The language is excluded and the run continues (HLR-070).
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
        1.  Obtain the language module; a `NULL` return means skip this file and report it skipped, distinct from any failure (HLR-012).
        2.  `open`, `fstat`, and short-circuit a zero-length file to zero metrics, since `mmap` of an empty file fails with `EINVAL` (HLR-020).
        3.  `mmap` the file and count `\n` occurrences for the physical line total, counting a final line that carries no terminating newline, since it is a line the reader sees. Every scan is bounded by the length from `fstat(2)`; the mapping is not NUL-terminated.
        4.  `ts_parser_set_language()` and `ts_parser_parse_string()` with the explicit `st_size` length — the mapping is not NUL-terminated.
        5.  Run `comments.scm`, collect spans, sort by start byte, and merge overlaps into a canonical excluded-line set (HLR-016).
        6.  Run `functions.scm` to establish the reported function set, including nested named functions, and build an innermost-enclosing lookup over their byte ranges (HLR-067). A match is a function only when it supplies **both** `@function.name` and `@function.body`; a match carrying one without the other is discarded rather than reported as a function with no line range or a line range with no name.
        7.  Run `eloc.scm` and `complexity.scm`, attributing each capture to its innermost enclosing reported function, or to no function when it lies outside every one of them — file-scope code, which contributes to the file's ELOC alone (HLR-019). Record the line each capture starts on, discard any capture falling inside the merged comment set, and count distinct lines (HLR-053, HLR-068).
        8.  Run `calls.scm` and `globals.scm`, recording each call site, each global access, and each address-taken function with its enclosing function, into `FileFacts`.
        9.  Run every loaded custom rule query and record its matches (HLR-109). A match's identity is the rule file's basename plus the capture name that matched, so one `.scm` file may express several distinct named rules.
        10.  `ts_tree_delete()` and `munmap()` on every exit path.
    *   Notes: Identifier text is `memcpy`'d out of the mapping into NUL-terminated allocations before the mapping is released, since every name outlives it.

        The signature above is the complete one. The `FileFacts` output arrives with the graph in Phase 8; until then the function is `int analyze_file(Registry *reg, const char *path, FileMetrics **metrics)`. That is the same function at an earlier stage of its construction, not a second entry point.

        **A skip and a failure are different outcomes, and the return value distinguishes them.** A file whose extension maps to no usable language was never attempted: it is reported skipped and leaves the exit status at 0 (HLR-012, HLR-037). A file that was attempted and could not be read or parsed makes it 1 (HLR-035). A single non-zero return would collapse the two and make every unsupported file look like a failure.

        **The reported line span runs from `@function.name` to the end of `@function.body`**, not from the body's opening brace. A reader asked where a function starts points at its signature, so a span beginning at the brace would be an artefact of how the query is written rather than a property of the code — and a hand-counted fixture would have to encode that artefact. Where a language's query captures the name after the body, the span is the body's alone rather than an inverted one.

*   **`uint32_t merge_comment_spans(SpanList *spans)`** — Sort by start byte and coalesce overlapping and nested spans, in place; returns the number of distinct lines the merged set covers.
*   **`const FnRange *innermost_enclosing(const FnRangeIndex *idx, uint32_t byte)`** — Return the narrowest reported function containing a byte offset, or NULL when the offset lies outside every one of them. Only *reported* functions are in the index, which is what makes an anonymous callable transparent to the lookup: an offset inside one resolves to the named function around it (HLR-018).
*   **`void filemetrics_free(FileMetrics *m)`** — Release a file's metrics and every function name it owns.
*   **`void filefacts_free(FileFacts *f)`** — Release the call sites, global accesses, address-taken records, and rule matches a file produced.

#### 7.3.3 Parsing Strategy / Algorithm

**ELOC is a count of lines, derived from statements.** Each capture from `eloc.scm` contributes the line it *starts* on, and the count is of distinct lines. Both halves matter, for opposite reasons: taking the start line is what makes a statement spread over four lines worth one (HLR-053), and counting distinct lines rather than captures is what stops two statements written on one line being worth two — the same error inverted. What does *not* count is decided by absence from the query file rather than by a rule in C: a blank line, a lone brace, a bare declaration, and a preprocessor directive are excluded by never being captured (HLR-049 – HLR-052).

**Comment-span merging is the one place where a naive implementation is silently wrong.** Spans are sorted by start byte and coalesced pairwise before anything is excluded. Subtracting per capture double-counts a block comment that contains inline comment syntax, and can drive a file's ELOC negative.

The merged set is applied as a **byte-granular** exclusion, not a line-granular one, and the difference is not academic. On a line reading `int n = 0;` followed by a trailing comment, the line touches a comment span and the statement does not; excluding by line deletes a line of code. Because statements come from the syntax tree the two sets are then disjoint in practice — a parser does not produce a statement inside a comment — so the exclusion is a guard rather than a subtraction. It is the guard HLR-016 asks for, and merging first is what makes it idempotent.

The merged line count is likewise a count of *distinct* lines. Two comments on one line are two disjoint byte ranges that do not coalesce, and summing their line counts would report that one line twice.

**The innermost-enclosing lookup is the analogous safeguard for nested named functions.** Attributing each statement to exactly one reported function is what prevents a nested subprogram's lines from being counted twice (HLR-068), and *narrowest* rather than *first* is what makes the answer independent of the order the query matched — an implementation returning the first containing range would also break HLR-032.

**Conditional-compilation pruning is the comment exclusion, applied to a different set of ranges.** When definitions are supplied, `conditionals.scm` is run over the same tree and each region's condition evaluated; the byte ranges of the branches the definitions render inactive join the merged comment set as ranges a capture may not lie within. One exclusion mechanism governs both, so neither can remove a range twice and a later reader has one thing to understand rather than two (HLR-132, LLR-ANL-43).

**What can be decided, and what deliberately cannot.** `elc` runs no preprocessor (HLR-135): there is no macro expansion, no include resolution, and no arithmetic over macro values, because each of those needs a toolchain whose presence and configuration `elc` cannot reproduce. A region is therefore decided only when its condition is a literal, or tests the definedness of symbols the user named — possibly negated, possibly combined. Anything else is **undecidable, not false**: both branches stay active and the region is counted as undecided (HLR-133).

That asymmetry is the whole safety argument. Treating an unrecognised condition as false would silently delete code, producing a report that is confidently wrong and looks exactly like a correct one. Treating it as true over-counts, which is visible in the undecided count beside the figures.

With no definitions supplied nothing is inactive and no condition is evaluated, so a run without the option is byte-identical to one made before the option existed (HLR-131, LLR-ANL-45).

**Cyclomatic complexity reuses that lookup rather than scoping the query.** `complexity.scm` is run once over the whole tree and each capture attributed by `innermost_enclosing`, which makes both halves of the requirement fall out of one pass. A nested *named* function is reported, so it is its own innermost enclosing function and owns its decision points (HLR-068). An anonymous callable is *not* reported, so the nearest reported function containing it is the named one, and its decision points land there (HLR-018). Running the query against each `@function.body` separately — the obvious reading — would instead give an enclosing function everything its nested functions branch on, and need a subtraction to undo it.

The `1 +` base is added in C and not in the query, so that a query capturing the function itself cannot make a function that never branches score 2. Every language module would otherwise have to remember not to.

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

*   **`int graph_build(const FactList *facts, const Report *report, Sdg *out)`**
    *   Purpose: Construct the SDG from the accumulated per-file facts.
    *   Pre-condition: `facts` covers every successfully analysed file; `report` has been assembled, so its files are in their final sorted order and carry the per-function metrics. The assembled report rather than the raw file list, for two reasons: the node attributes GraphML exports — name, line range, ELOC, complexity, component — all live in the report model, and the report's file order *is* the sorted order the stable node identifiers depend on (LLR-SDG-09). Facts are matched to files by path.
    *   Post-condition: `out` holds a fully populated graph; no source file has been reopened (HLR-076).
    *   Return Value: 0 on success; non-zero only on allocation failure.
    *   Logic:
        1.  Walk every `FileFacts` in file order, assigning each defined function a node index and inserting it into the symbol table.
        2.  Copy the set of names some file declares at file scope into the graph, de-duplicated. Owned rather than borrowed: the fact list is released the moment `graph_build` returns, and an edge naming a string that has been freed renders as a plausible object name rather than crashing — the worst way for it to be wrong.
        3.  Walk every recorded call site; look its target up in the symbol table and add an edge on a hit.
        4.  On a miss — an external library call, a system call, or an indirect call through a pointer — increment the unresolved tally and record the site for reporting rather than failing (HLR-077).
        5.  Resolve each captured `@global.read` and `@global.write` against the declared set, discarding the names that are not globals. The query files capture identifiers wherever they appear and cannot decide this: a name is global only if *some file in the project* declares it so, which is whole-project knowledge no single file's facts hold.
        6.  For each global object, add an edge from every writing function and to every reading function (HLR-074). Unlike call edges, global edges are per object: two objects shared between one pair of functions are two edges, since merging them would lose which state couples them.
        7.  Resolve each captured `@call.address_taken` name against the symbol table, marking the node when it resolves and discarding the name when it does not. The captures are deliberately over-broad — most identifiers in value position are variables — and this step is what makes that safe, which is why the query files need not decide, and could not.
        8.  Build the component projection: an edge from component X to Y whenever any function in X calls a function in Y, or writes a global that a function in Y reads (HLR-114).
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
*   Accumulate per-file metrics as the analysis stage produces them, growing the collection through a checked reallocation, and take ownership of them at assembly.
*   Carry every computed architectural *measurement* into the model, not only those that crossed a threshold, so that a value lying within its accepted band is still reported.
*   Compute the project summary, including the most-complex callouts with their tie-break rule.
*   Apply the complexity threshold to produce each file's over-threshold function list.
*   Sort every collection in the model by an explicit key before any renderer sees it.
*   Record which analyses were omitted, and why.
*   Record the conditional-compilation definitions in force and the number of regions whose condition could not be decided, so that a figure which depends on a configuration is reported alongside it (HLR-136, HLR-133).
*   Record every file skipped for want of a language module, so the report accounts for each discovered file (HLR-012).
*   Record the discovery route applied to each directory target, so that an unexpectedly empty or oversized result is diagnosable (HLR-127).


### 13.3 Internal Structure
#### 13.3.1 Key Functions

*   **`int report_assemble(const MetricsAccumulator *acc, const ArchResults *a, const TreeResults *t, const StateResults *s, const FindingList *f, const ElcOptions *opts, Report *out)`**
    *   Purpose: Produce the ordered, format-independent report model.
    *   Post-condition: Every collection in `out` is sorted by its defined key; no renderer needs to sort anything. The accumulator's per-file metrics have moved into the model, which owns them thereafter, and the accumulator is left empty — so releasing both, as `main()` does on every path, cannot free the same metrics twice.
    *   Return Value: 0 on success.
    *   Logic:
        1.  Sum physical lines and ELOC across all files, both combined and per language (HLR-024, HLR-025). A language's row is created on first sight of a file written in it; the list holds one entry per language present, so a linear search costs less than the structure that would avoid it.
        2.  Select the highest-ELOC file and highest-complexity function, breaking ties by the stable presentation order (HLR-026). Both are chosen *after* the model is ordered, by scanning it and taking a new candidate only on a strictly greater value: scanning in presentation order and refusing to displace an equal value is what makes the winner whichever sorts first, and what makes the callout the same on every run.
        3.  For each file, filter its functions by the complexity threshold into the over-threshold list, at or above rather than strictly above (HLR-021). The list is built here rather than filtered by a renderer: a renderer is a pure consumer, and a threshold applied at render time would be applied once per format and could differ between them. The threshold reaches nothing else — not a total, not a callout, and never the exit status (HLR-023).
        4.  Attach the architectural findings, custom-rule matches, and omission notices.
        5.  Sort files by path; functions by start line and then by name; per-language totals by language name; skipped files by path; findings by (severity, kind, primary location); cycles by their lowest member; unreachable functions by (file, line) (HLR-033). The name is the tie-break for functions because two can share a start line — a nested function declared on the line its enclosing body opens — and `qsort` is not stable, so a comparator returning 0 there would leave their order to the implementation.
    *   Notes: Centralising every sort here is deliberate: it is the single place a reviewer must check to be satisfied that HLR-032's byte-identical guarantee holds, rather than auditing six renderers and three analysis modules.

        `discover.c` also sorts, and the two are not redundant. Its sort exists so that de-duplication can collapse equal canonical paths, and so that the *analysis* order is not the filesystem's; this one exists so that *presentation* order is a property of the model. A later phase that changes how files are discovered therefore cannot silently change how they are presented.

*   **`int metrics_add(MetricsAccumulator *acc, FileMetrics *m)`** — Append one file's metrics, taking ownership of them. Grows by doubling through a checked reallocation; on failure the accumulator is left intact and the caller still owns the metrics, so nothing is leaked and nothing is freed twice.
*   **`void metrics_free(MetricsAccumulator *acc)`** — Release the accumulator and every FileMetrics it still owns.
*   **`void report_free(Report *r)`** — Release the report model and everything it owns.
### 13.4 Dependencies

*   Every analysis module, for their results. Depended upon by every renderer, giving it high afferent coupling and low efferent coupling — a deliberately stable component.

### 13.5 Error Handling and Logging

*   **Empty run** A run in which no file was analysable still produces a complete model with zero totals, which renders normally and exits zero (HLR-066).

## 14. Detailed Design for [src/format_text.c](../src/format_text.c)

### 14.1 Purpose and Responsibilities
[src/format_text.c](../src/format_text.c) renders the report model in the two human-facing formats: the aligned ASCII table that is the default, and GitHub-Flavored Markdown.

*   Compute column widths from the longest path and function name, and render the aligned table.
*   Render every tier in the fixed order, so that the report has the same shape whatever the type of the target was (HLR-006).
*   Render Markdown with functions grouped under a per-file heading.
*   Present every tier the uniform-composition rule requires, in both formats.


### 14.3 Internal Structure
#### 14.3.1 Key Functions

*   **`int format_table(const Report *r, FILE *out)`** — Render the aligned ASCII table. Returns non-zero when the stream reported a write failure, checked once after the last write rather than at every call.
*   **`int format_markdown(const Report *r, FILE *out)`** — Render GitHub-Flavored Markdown.
*   **`void render_summary(const Report *r, FILE *out, Style style)`** — Shared project-summary rendering for both formats.

#### 14.3.2 Parsing Strategy / Algorithm

**The two renderers are one traversal.** `render_report()` walks the model once and emits every tier in a fixed order; the `Style` decides only how each tier is decorated. A tier added to the traversal appears in both formats, and cannot be added to one and forgotten in the other, because there is nowhere to forget it — which is what makes HLR-031's uniform composition structural rather than maintained (LLR-SUM-02).

Each tier is built into a small grid of already-formatted cells and then rendered. The two passes are what the aligned style needs — a column's width is not known until its last cell is in — and the Markdown style reuses the same widths, so the raw document is readable rather than ragged. Formatting each value once, into a cell, is also what keeps the measuring pass and the writing pass in agreement: measuring a number one way and printing it another is how a column comes out a character short.

A left-aligned final column is not padded in the aligned style. Padding it puts trailing whitespace on every line, which shows up in a diff and is stripped by half the tools that would read it.

The table is laid out in tiers, each introduced by a heading and indented beneath it:

```text
Project summary
  Files             2
  Physical lines   42
  ELOC             18
  Functions         3
  Skipped           0

Callouts
  What          Value  Where
  ------------  -----  -----------------------------
  Largest file     12  /home/u/proj/src/a.c
  Most complex      7  parse in /home/u/proj/src/a.c

Languages
  Language  Files  Lines  ELOC
  --------  -----  -----  ----
  c             2     42    18

Files
  File                  Language  Lines  ELOC  Functions
  --------------------  --------  -----  ----  ---------
  /home/u/proj/src/a.c  c            18    12          2
  /home/u/proj/src/b.c  c            24     6          1
```

Each column is exactly as wide as its widest cell and no wider: the path column from the longest path, the numeric columns from the largest value.

Every section is emitted whether or not it has rows. A heading with an empty body says "nothing here"; an absent heading is indistinguishable from a renderer that forgot, and would make the report's shape vary with its content — which is what HLR-006 forbids across target types and HLR-032 forbids across runs. A run that analysed nothing therefore renders every heading and every column rule, with no rows beneath them (HLR-066).

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

A root `<elc-report format-version="N">` carrying `<summary>`, `<languages>`, `<files>` with nested `<function>` elements, `<skipped>`, `<architecture>` holding every graph finding, `<custom-rules>`, and `<omissions>`. The format version is incremented whenever an element is removed or its meaning changes.

It is **not** incremented when an element is added, because a reader ignores elements it does not recognise. That asymmetry is what makes a later phase's additions compatible with a record written today, and it is why an element that does not yet exist is absent rather than present and empty.

#### 16.2.2 Reading What elc Did Not Write

The read path treats its input as hostile. The root element must be `elc-report`, its `format-version` must be one this build reads, and an attribute that should be numeric and is not makes the record malformed rather than zero — accepting it would produce a report that renders cleanly and is wrong, which is the outcome HLR-058 exists to prevent.

Nothing reconstructed before a rejection survives it. The partially built model is released rather than rendered, since a partly reconstructed report is indistinguishable from a complete one once it reaches a reader (LLR-XRD-06).


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

**The reader reconstructs inputs, not conclusions.** It rebuilds the per-file and per-function facts and hands them to `report_assemble()` — the same function a live run calls. Every derived value (the totals, the per-language breakdown, the callouts and their tie-break, the threshold listing, the ordering) is therefore computed once, by one function, on both paths. That is what makes HLR-056's byte-identical guarantee a structural property rather than one two pieces of code have to keep agreeing about; a reader that rebuilt the derived values from the record would be a second implementation, and would drift.

It follows that **the threshold is not in the record**. The record stores what was measured; the threshold is what somebody decided about it. Keeping them apart is what lets one record answer any number of threshold questions without re-analysis, and is the whole of HLR-057.

The totals *are* written, for a consumer reading the record with something other than `elc` (LLR-XWR-02), and ignored on read.

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
*   **`int graph_write_graphml(const Sdg *g, const char *path)`** — Write the SDG in GraphML, nodes in ascending identifier order and each node's edges by ascending target. Reuses `write_escaped` from `format_xml.c` rather than carrying a second escaper: one implementation of HLR-065 means one place for it to be wrong.
*   **`bool graph_graphml_warranted(const ElcOptions *opts)`** — True only when the export was requested *and* the report goes to a named file. Both halves matter: the export is off by default (HLR-106), and requesting it with the report on standard output writes nothing, because there is no path to derive a name from (HLR-104).
*   **`char *graph_companion_path(const char *output_path, const char *extension)`** — The companion's name, by extension substitution on the report's output path. The extension search is scoped to the last path component, so a dot in a directory name is not mistaken for one.
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
| `definitions` | `DefineList` | Conditional-compilation symbols; empty when none supplied, and an empty set prunes nothing (HLR-131) |
| `targets` | `PathList` | One or more file or directory arguments (HLR-071) |
*   **`FileList`** (defined in [inc/discover.h](../inc/discover.h)) — The discovered files: canonical absolute paths, each appearing exactly once, in ascending byte order. Owns every path it holds.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `paths` | `char **` | Dynamic array, grown by doubling |
| `count` | `size_t` | Populated entries |
| `capacity` | `size_t` | Allocated entries |
*   **`ExtensionList`** (defined in [inc/discover.h](../inc/discover.h)) — The binary-extension exclusion list read from runtime data (HLR-005). Passed explicitly to every function that consults it rather than held in a global.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `exts` | `char **` | Each including its leading dot; matched case-insensitively |
| `count` | `size_t` | Populated entries |
| `capacity` | `size_t` | Allocated entries |
*   **`Registry`** (defined in [inc/registry.h](../inc/registry.h)) — Everything loaded from the runtime location, plus the parser and cursor reused across the whole run.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `dir` | `char *` | The resolved runtime location; handed to any other module needing runtime data, so HLR-059's precedence rule exists once |
| `map` | `ExtensionMapping *` | Extension to language, from runtime data (HLR-060) |
| `modules` | `LanguageModule *` | Loaded languages, cached after first use |
| `parser` | `TSParser *` | One for the whole run; reuse needs only ts_parser_set_language() per file |
| `cursor` | `TSQueryCursor *` | One for the whole run, for the same reason |
*   **`ExtensionMapping`** (defined in [inc/registry.h](../inc/registry.h)) — One extension-to-language association read from runtime data.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `extension` | `char *` | Including its leading period; matched without regard to case |
| `language` | `char *` | Names the parser and query directory to load |
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
*   **`MetricsAccumulator`** (defined in [inc/report.h](../inc/report.h)) — Per-file metrics as they accumulate during the run. Owns every FileMetrics handed to it, until report_assemble takes them.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `files` | `FileMetrics **` | Dynamic array, grown by doubling through a checked reallocation (LLR-RPT-16) |
| `count` | `size_t` | Populated entries |
| `capacity` | `size_t` | Allocated entries |
*   **`ProjectSummary`** (defined in [inc/report.h](../inc/report.h)) — The project-level totals across every analysed file (HLR-024), and the most-complex callouts once there are metrics to compare (HLR-026).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `file_count` | `size_t` | Files analysed in the run |
| `physical_lines` | `uint64_t` | Combined physical line count; wider than the per-file field because it sums over the whole project |
*   **`PathList`** (defined in [inc/report.h](../inc/report.h)) — A sorted, owned list of paths. Used for the files discovered but not analysed, so the report accounts for every discovered file (HLR-012).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `paths` | `char **` | Dynamic array, grown by doubling |
| `count` | `size_t` | Populated entries |
| `capacity` | `size_t` | Allocated entries |
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
| `definitions` | `DefineList` | The configuration the figures describe, sorted by symbol (HLR-136) |
| `undecided_regions` | `size_t` | Conditional regions left active because their condition could not be decided, reported so the completeness of the pruning is visible (HLR-133) |
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
| `registry.c` | *(none)* | Every module failure the requirement names — an absent `.so`, one exporting no entry point, a missing query file, an unparseable one — is a file a fixture can create in two lines. A wrapped `dlopen` would verify the wrapper. The wrap is available if a failure appears that a filesystem cannot produce (HLR-070, LLR-RFP-06). |
| `discover.c` | `realpath` | Canonicalisation failure on a path that exists (LLR-DSC-07). `stat` is deliberately absent: every invalid target class — absent, unreadable, FIFO, device node — can be produced on a real filesystem, and a test that builds one verifies the requirement rather than the mock. `git_repository_open_ext` was listed here through Phase 6 and proved unnecessary for the same reason: every form of repository inapplicability — no repository, one tracking nothing beneath the target, one with no commits, one whose tracked files are all excluded — is a repository `git init` can build in three lines (LLR-GIT-04). |
| `graph.c`, `arch.c`, `calltree.c`, `state.c` | `realloc`, the graph library's allocating entry points | Allocation failure on graph paths (HLR-124, HLR-125) |
| `report.c` | `realloc` | Checked collection growth (LLR-RPT-16) |
| `format_*.c` | `fwrite`, `fprintf`, `fopen` | Write-failure paths, companion file creation failure (LLR-DOT-05) |

Wrapping is confined to the unit level; every other level links and runs the real binary.

The lists are **per module** in the build as well as here. A `--wrap` applies to every object in the binary being linked, and every unit binary links every module, so a symbol wrapped for one module's tests would oblige every other test file to define a `__wrap_` for it whether or not it mocks anything.

Interception and `ptrace` do not compose. LeakSanitizer stops the world at exit through a `clone`d tracer and `ptrace`, which collides with `strace`'s own attachment and aborts the process; an instrumented run observed under `strace` therefore disables leak detection for that run alone. Every other run in the same sanitized pass still has it on, so HLR-125 stays verified — but a test that asserts a syscall never appears would otherwise pass because the process died before reaching the interesting part, which is worse than failing.

**Delivering the grammars.** A grammar is runtime data, not an object: it is `dlopen`'d by name from `runtime/parsers/` and never linked, so it cannot be a build product in the ordinary sense.

The owner and the archive reference are both parameters of the build rule, not constants. Three of the five grammars live under the `tree-sitter` organisation and one does not, and a hardcoded owner would read as though a grammar from elsewhere could not be added as data — which would contradict HLR-010. A repository that cuts releases is fetched by tag; one that does not is fetched by **commit**, because a branch name is not a pin: what it refers to changes without anyone deciding.

A grammar's external scanner must be located by a shell glob rather than by `$(wildcard)`. Make expands a recipe before running any of it, so a `$(wildcard)` would be evaluated before the fetch had unpacked anything and would find nothing — linking a grammar without the scanner it needs. C has no external scanner, which is why that went unnoticed until four grammars that have one arrived. It is nonetheless a project deliverable (HLR-011), and the build produces it: each grammar is pinned to an upstream release, fetched, and compiled from the `parser.c` that release ships. **No code generation is involved** — the generated parser is what upstream publishes, which is what makes HLR-040's no-code-generation rule satisfiable while still shipping a parser.

Two consequences follow. A grammar's ABI must lie inside the range the linked `libtree-sitter` supports, and a mismatch fails at *load* with a version error rather than at build, so the pin is checked against the library rather than assumed. And because grammars are gitignored build products that no `clean` should discard, `make clean` deliberately leaves them: the sanitized pass cleans twice, and refetching an upstream tarball on each is a network round trip for nothing.

**Dependency selection.** HLR-112 defers library choice to this document. The selections below were made after confirming the maintenance status of each candidate named in the PVD:

| Role | Selected | Rationale |
| ---- | -------- | --------- |
| Parsing and queries | **Tree-sitter** | Not a free choice: its query language and grammar format are a user-visible contract (HLR-112). Actively released. |
| Repository access | **libgit2** | Actively maintained, frequent releases; the only mature C option for tracked-file enumeration. |
| Graph algorithms | **igraph** | The only mature C-native graph library; its 1.0 series carries an explicit long-term API stability commitment. Alternatives (Boost.Graph, LEMON, NetworKit) are C++ and would impose a second toolchain. Build with GraphML support disabled — see below. |
| XML reading | **Expat** | Actively maintained, currently funded, streaming, and namespace-aware — everything the read path needs, and nothing it does not. |
| XML and GraphML writing | **none** | Hand-rolled emission with centralised escaping. Removes a dependency rather than adding one. |
| DOT writing | **none** | Plain text. Graphviz renders the output; `elc` never links it. |

**Ownership of the intermediate structures.** HLR-125's leak gate makes this load-bearing rather than merely tidy, and the pipeline's shape leaves it otherwise ambiguous:

*   A **`FileFacts`** is owned by the caller of `graph_build`, never by the graph. `graph_build` copies what it needs into the SDG's own tables, so the fact list is released with `filefacts_free` as soon as `graph_build` returns; it must not be kept alive for the analyses.
*   **`ArchResults`**, **`TreeResults`**, **`StateResults`**, and the **`FindingList`** are owned by `main`. `report_assemble` copies from them into the report model rather than taking ownership, so `main` releases each with its `*_free` once assembly returns.
*   The **`MetricsAccumulator`** is the exception, and is exceptional because its contents are the model rather than an input to it: `report_assemble` *moves* the per-file metrics into the `Report` and leaves the accumulator empty. `main` still calls `metrics_free` afterwards, which is then a no-op — teardown stays unconditional, and no path frees the same `FileMetrics` twice.
*   Every one of these is released on error paths as well as the success path. A run ending in an invalid target or a rejected record must still exit leak-clean, which means teardown cannot live only at the bottom of a successful pipeline.

**Consequence for the igraph build.** `elc` writes GraphML itself, so igraph's own GraphML reader and writer are unused — and enabling them links a second XML library the project has no other need for. igraph must therefore be built with `IGRAPH_GRAPHML_SUPPORT` **off**. A distribution package built with it enabled reintroduces that dependency transitively, so the condition is checked at configure time rather than assumed; `make check-prereqs` reports it.
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
