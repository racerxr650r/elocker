# Software Design Document: elocker (elc)

**Version:** 2.16
**Date:** 2026-08-22
**Author(s):** John Anderson

## 1. Introduction

### 1.1 Purpose of the Document
This document provides a detailed design for the `elc` (elocker)
POSIX C11 command-line application. It is intended for developers, testers, and maintainers
of the `elc` software.

### 1.2 Scope of the Document
This document describes the design of the source modules that implement the high-level requirements in [HLRs.md](HLRs.md); [Traceability.md](Traceability.md) carries the current requirement, contract, and test counts, which are generated rather than restated here:

*   [src/main.c](../src/main.c): Entry point. Sequences the pipeline, owns the run's top-level state, and computes the process exit status.
*   [src/cli.c](../src/cli.c): Command-line parsing and validation: formats, thresholds, strata, entry points, execution scopes, custom rules, and the help path.
*   [src/discover.c](../src/discover.c): Target classification and file discovery across the three target types, with de-duplication and exclusion filtering.
*   [src/registry.c](../src/registry.c): Runtime location resolution, lazy language-module loading, extension mapping, and custom rule-query loading.
*   [src/analyze.c](../src/analyze.c): Per-file parsing and the single-parse extraction of ELOC, cyclomatic complexity, function identity, and the raw call and global-access facts the graph is later built from.
*   [src/graph.c](../src/graph.c): System Dependence Graph construction: cross-file symbol resolution and population of the graph structure.
*   [src/arch.c](../src/arch.c): Component-level analyses — coupling, instability, bottlenecks, dependency cycles, and architectural layering.
*   [src/calltree.c](../src/calltree.c): Function-level call-tree analyses — fan-out, fan-in, the Henry-Kafura information-flow value, depth, the deepest call stack, and recursion detection.
*   [src/state.c](../src/state.c): Global-state coupling, execution-scope isolation, and reachability-based dead-code detection.
*   [src/thresholds.c](../src/thresholds.c): Evaluation of every measurement against its published threshold, and assignment of severity and attribution.
*   [src/report.c](../src/report.c): The format-independent report model: assembly of every finding into one structure, in a stable, defined order.
*   [src/format_text.c](../src/format_text.c): The aligned ASCII table and GitHub-Flavored Markdown renderers.
*   [src/format_csv.c](../src/format_csv.c): The RFC 4180 CSV renderer.
*   [src/format_xml.c](../src/format_xml.c): The XML record writer and the reader that drives the report-regeneration mode.
*   [src/format_graph.c](../src/format_graph.c): The Graphviz `.dot` call-tree writer and the GraphML graph-export writer.
*   [src/elfsyms.c](../src/elfsyms.c): The linked-image reader: the function symbols an image defines, and the resolution of a linkage name to the source name the report presents.
*   [src/dwarfline.c](../src/dwarfline.c): The image's debug line information: which source lines this build compiled an instruction for, and which files that mapping covers at all.
*   [src/purify.c](../src/purify.c): The graph purification engine: centrality-based classification of utility sinks, god objects, and peripheral nodes, the masked recovery view built from them, and the manifest by which a user overrules a classification.
*   [src/recover.c](../src/recover.c): Architecture recovery: a proposed layering read off the purified recovery view, emitted in the form the stratum options accept.
*   [src/format_dsm.c](../src/format_dsm.c): The Dependency Structure Matrix and its CSV and Markdown renderings.
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
*   [High-Level Requirements](HLRs.md) — the requirements this design satisfies.
*   [Traceability Matrix](Traceability.md) — the generated requirement, contract, and test counts, and the coverage each has.
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
*   Section 18: Detailed design for [src/elfsyms.c](../src/elfsyms.c).
*   Section 19: Detailed design for [src/dwarfline.c](../src/dwarfline.c).
*   Section 20: Detailed design for [src/purify.c](../src/purify.c).
*   Section 21: Detailed design for [src/recover.c](../src/recover.c).
*   Section 22: Detailed design for [src/format_dsm.c](../src/format_dsm.c).
*   Section 23: Data Dictionary.
*   Section 24: Traceability.

## 2. System Overview

### 2.1 System Architecture
`elc` is a single executable composed of nineteen translation units arranged as a one-way pipeline. Stages communicate only through the values they return; there is no global mutable state, no callback into an earlier stage, and no second read of any source file.

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
*   **[src/elfsyms.c](../src/elfsyms.c)** — Reads the function symbols of a linked image and resolves each linkage name to the source name the report presents, so that a filtered run measures the program the build produced.
*   **[src/dwarfline.c](../src/dwarfline.c)** — Reads the debug line information the same image carries, where it carries any, so that a filtered run can narrow to the lines the build compiled rather than to the functions alone.
*   **[src/purify.c](../src/purify.c)** — Classifies the functions that fuse unrelated domains — utility sinks, god objects, peripheral nodes — and builds the masked recovery view. Reads and writes the manifest that lets a user overrule a classification. Alters no graph any other stage reads.
*   **[src/recover.c](../src/recover.c)** — Proposes a layering from the purified view, for a user who declared none. Depended upon by the report and by nothing in `arch.c`, which is what keeps a proposal from becoming the baseline it would be measured against.
*   **[src/format_dsm.c](../src/format_dsm.c)** — Renders the Dependency Structure Matrix as CSV and as Markdown.

The runtime data flow of an analysis run is:

1.  `main()` calls `cli_parse()`. A help request prints usage to `stdout` and exits zero (HLR-117); an invalid invocation prints usage to `stderr` and exits non-zero (HLR-063).
2.  If the options select regeneration mode, `main()` calls `xml_read_report()` and jumps directly to the render step; no source file is touched (HLR-055). A read failure is a setup-class error and exits 2. Because the record carries findings rather than graph topology, `.dot` and GraphML cannot be produced in this mode and are suppressed.
3.  `registry_open()` resolves the runtime location and verifies that at least one language module is loadable; failure here is fatal (HLR-036, HLR-059).
4.  Where a linked image was named, `elfsyms_open()` reads its function set. This is before discovery, for the reason the registry is: an image the user named and `elc` cannot read is fatal, and it is fatal before any source file is measured rather than after a walk whose results are then thrown away (HLR-140, HLR-146).
5.  `discover_targets()` validates every target argument up front, classifies each, walks it, and returns a de-duplicated file list in stable order (HLR-062, HLR-071, HLR-072).
6.  For each file, `analyze_file()` maps it, obtains its language module from the registry, parses it once, and emits both `FileMetrics` and a `FileFacts` record of call sites and global accesses (HLR-013, HLR-076). Where a function set was read, a function the image does not define is omitted here and nothing downstream learns it existed (HLR-144).
7.  `graph_build()` resolves the accumulated `FileFacts` into the SDG, recording unresolved call sites rather than failing (HLR-073, HLR-077).
8.  `arch_analyse()`, `calltree_analyse()`, and `state_analyse()` run over the SDG, each skipping any analysis whose user declaration was not supplied (HLR-115).
9.  `thresholds_apply()` evaluates every measurement against the Appendix A catalogue, attaching a severity and a source attribution to each finding (HLR-098, HLR-099).
10.  `report_assemble()` merges metrics, findings, and custom-rule matches into one model and sorts every collection into its defined order (HLR-033).
11.  The selected renderer writes the report; `format_graph.c` additionally writes the `.dot` companion unless disabled or the report is going to `stdout` (HLR-103, HLR-104).
12.  `main()` returns non-zero if any per-file failure was recorded, zero otherwise (HLR-037).

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
*   Sequence graph purification after every analysis that measures, so that nothing downstream can take a view `elc` formed of its own for a measurement (HLR-167).

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
        4.  If an image was named, call `elfsyms_open()`; on failure return 2 (HLR-146).
        5.  Call `discover_targets()`; on an invalid target return 2 without emitting a report (HLR-062).
        6.  For each discovered file call `analyze_file()`, appending its `FileMetrics` to the accumulator and its `FileFacts` to the fact list; record but do not propagate per-file failures.
        7.  Call `graph_build()` over the fact list.
        8.  Call `arch_analyse()`, `calltree_analyse()`, `state_analyse()`, then `thresholds_apply()`, then `report_assemble()`.
        9.  Open the output destination — the file named by the options, or standard output when none was named — and on a failure to open it emit a diagnostic and return 2 without writing a partial report (HLR-030).
        10.  Call `dsm_build()` over the graph and the assembled model, so that the dependency matrix is carried on the report every renderer consumes. Here rather than inside the architecture pass, because the matrix is an arrangement of the graph's call edges over the report's components and needs both; and built whether or not strata were declared, since with none its subjects are the analysed directories (HLR-165).
        11.  Dispatch to the selected renderer, passing the verbosity to the two human-facing renderers and to neither complete-record writer, then to `graph_write_dot()` when the companion artefact is warranted (HLR-152, LLR-MAIN-21), and to `format_dsm_csv()` when `dsm_warranted()` says so. The matrix companion is written from the *model* rather than from the graph, which is what makes it the one companion a regenerated report can also produce (HLR-180).
        12.  Tear down in reverse order and return the computed status.
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

*   Parse short and long options with `getopt_long()`, including the report format, output path, thresholds, custom-rule paths, the linked image to filter by, and the `.dot`, GraphML, and dependency-matrix switches.
*   Parse the structured declarations — architectural strata, entry points, and execution scopes — into their in-memory forms.
*   Validate every option value and reject an unknown option, a missing argument, or a missing target before any analysis begins.
*   Emit the usage summary, to `stdout` on request and to `stderr` on error.
*   Record the purification manifest's path, and the two companion requests purification and recovery add, without reading or validating any file: the manifest is read by `purify.c`, which owns the failure, and a companion asked for with the report on standard output writes nothing rather than failing (HLR-104, HLR-119, HLR-175, HLR-176).

### 4.2 External Interfaces
Options are the entirety of `elc`'s configuration surface: there is no configuration file and no dotfile discovery (HLR-039).

#### 4.2.1 Option List as Single Reference

The set of accepted options appears in three places: this module's `getopt_long` table, the man page, and the user manual. Nothing prevents those three drifting apart, so `cli_usage()`'s output is designated the **reference**: it is generated from the same table that parses, and the documentation is checked against it rather than against the source (LLR-DOC-04). An option that parses but does not print, or prints but is not documented, is a defect the documentation test catches.

#### 4.2.2 Command-Line Options


| Option | Argument | Default | Requirement |
| ------ | -------- | ------- | ----------- |
| `-f`, `--format` | `table\|csv\|xml\|md` | `table` | HLR-027 – HLR-029, HLR-149 |
| `-o`, `--output` | path | `stdout` | HLR-030, HLR-148 |
| `-v`, `--verbose` | — | summary | HLR-150, HLR-151 |
| `-c`, `--complexity-threshold` | integer | `15` | HLR-022 |
| `-b`, `--bottleneck-threshold` | integer | `5` | HLR-081 |
| `--no-dot` | — | `.dot` enabled | HLR-103 |
| `--graphml` | — | disabled | HLR-106 |
| `--dsm` | — | disabled | HLR-180 |
| `--stratum` | `name:glob[,glob…]` | none | HLR-078 |
| `--stratum-order` | `name>name[>name…]` | none | HLR-078 |
| `--entry` | symbol | none | HLR-095 |
| `--scope` | `name:glob[,glob…]` | none | HLR-094 |
| `--rules` | `lang:path` | none | HLR-107 |
| `--elf` | path | none | HLR-140 |
| `--from-xml` | path | — | HLR-055 |
| `-D`, `--define` | `name[=value]` | none | HLR-131 |
| `-h`, `--help` | — | — | HLR-117 |


#### 4.2.3 Two Ways to Name One Format

The format can be stated twice, and the two statements are settled against each other rather than by precedence.

Where an `--output` file is named, its extension determines the format (HLR-148) — `.txt`, `.md`, `.csv`, `.xml` — and `--format` is not needed. Standard output has no filename and so no extension, so a report written there takes the option alone, defaulting to the table (HLR-149). Where both are supplied, agreement is accepted and disagreement is a usage error naming both: honouring either would leave the command line disagreeing with the file it produced.

Resolution therefore happens **after** the option loop, in `resolve_format()`, not inside it. Inside the loop the two options would be compared in whichever order they arrived, so `-f csv -o r.md` and `-o r.md -f csv` would need separate handling; after it, one comparison settles both (LLR-CLI-27). It also happens *before* the regeneration checks, because those ask which format was chosen and the filename is one of the two ways of choosing it (LLR-CLI-28).

The extension table is the single statement of the mapping. The diagnostic for an unrecognised extension is generated from it, so a format added there is named in the error message without a second list to keep in step.

#### 4.2.4 Verbosity Is Not a Format

`--verbose` selects how much of the report is presented (HLR-151); `--format` selects how it is decorated. The two are orthogonal, and the parser keeps them so.

It follows that `--verbose` with `-f xml` or `-f csv` is **accepted**, which is worth stating because it is the one option pairing this module decides that is not a usage error (HLR-152, LLR-CLI-30). Every other combination the parser rejects is rejected because the two options make contradictory claims about one run; asking a complete format for more detail claims nothing contradictory. It has no effect, and having no effect is not the same as being wrong.


### 4.3 Internal Structure
#### 4.3.1 Key Data Structures

`ElcOptions` (see the Data Dictionary) is populated here and thereafter treated as read-only by every other module.


#### 4.3.2 Key Functions

*   **`int cli_parse(int argc, char *argv[], ElcOptions *out)`** — Parse and validate argv into out; returns 0, CLI_HELP, or CLI_ERROR.
*   **`void cli_usage(FILE *stream)`** — Print the option summary and defaults to stream.
*   **`static int resolve_format(CliParse *p)`** — Settle the format the option and the output filename can each state, after the option loop so the two may be given in either order.
*   **`static const char *path_extension(const char *path)`** — The extension of a path, or NULL where its basename carries none.
*   **`int parse_stratum(const char *arg, ElcOptions *out)`** — Parse one name:glob-list stratum declaration.
*   **`int parse_scope(const char *arg, ElcOptions *out)`** — Parse one name:glob-list execution-scope declaration.
*   **`void cli_options_free(ElcOptions *opts)`** — Release every heap allocation owned by the options structure.
### 4.4 Dependencies

*   POSIX `getopt_long()`, `fnmatch()` for the glob patterns in stratum and scope declarations.

### 4.5 Error Handling and Logging

*   **Unknown option or missing argument** Print the usage summary to `stderr` and return `CLI_ERROR`; `main()` exits 2 without analysing anything (HLR-063).
*   **Help requested** Print the usage summary to `stdout` and return `CLI_HELP`; `main()` exits 0, since a help request is not an error (HLR-117).
*   **Malformed declaration** A stratum, scope, or entry-point argument that cannot be parsed is a usage error, handled as above.
*   **Unrecognised output extension** An `--output` path whose extension names no format `elc` has, or whose basename carries none, is a usage error naming the extension found and listing those that are recognised (HLR-148). Nothing is written: guessing would put one format under a name promising another, and defaulting to the table would produce a `report.json` holding no JSON.
*   **Format option contradicting the filename** `--format` and an `--output` extension that name different formats is a usage error naming both, rather than a silent preference for either (HLR-149). Where they name the same format the invocation is accepted, since nothing is ambiguous about saying a thing twice.
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
    ├── conditionals.scm    # optional (HLR-134)
    ├── deadcode.scm        # optional (HLR-139)
    └── rules/*.scm         # custom rules for this language (HLR-107)
```
The six per-language queries are required; `eloc.scm`, `calls.scm`, and `globals.scm` supply the ELOC statement classification and the graph facts. `extensions.map` is plain text so that associating a new extension with a language is a data edit, never a rebuild (HLR-060). `binary.exts` lives here too and is read by `discover.c`, which is given this location rather than resolving it (HLR-005).

Installed, that same tree lives at `<prefix>/share/elc/runtime`; in the build tree it is reached through a `runtime` symlink `make all` creates beside the binary. The directory's *contents* are identical in both, which is what lets one resolution rule serve them.

`conditionals.scm` is a **seventh, optional** file. A module that supplies one gains conditional-region pruning; a module that omits one has no conditional compilation, which is the truth for a language that has none. The required set stays at six, so adding this breaks no module that already exists (HLR-121, HLR-134).

`conditionals.scm` and `deadcode.scm` are loaded by the same code path the required six use, and the only difference is what an absence means: a file that is simply not there leaves a NULL for the consumer to notice, while one that is present and will not compile makes the module unusable exactly as a broken required file does.

`deadcode.scm` is the **eighth**, and optional for the same reason. A module supplying one gains dead-code detection within functions; a module omitting one is analysed for every other measurement, and the report states that the analysis was *not performed* for that language rather than that none was found (HLR-139). Every module shipped today supplies one, and the file is optional all the same: a language that writes its false literal as an ordinary identifier the grammar cannot distinguish from one the program declared cannot supply a `deadcode.scm` honestly, since capturing it would assert a resolution nothing performed. Shipping the terminator half alone would report such a language as *analysed* while quietly finding no literal branches, which is the confident-and-wrong outcome the whole design avoids. The project has shipped a module that took that option, and the contract keeps it open.

The distinction between the two ways an optional file can be absent is load-bearing. A file that is **not there** is a choice the contract allows. A file that is there and **will not compile** is a defect, and makes the module unusable exactly as a broken required file does; treating the two alike would let a typo silently disable an analysis.

A query file that compiles and captures nothing is valid, and is how an unimplemented query is expressed. The registry reads captures; it never asks whether a file is "filled in". That is what lets a phase ship a language with one query complete and the rest as documented stubs, without either a special case in the loader or a module that fails to load.

The contract this directory embodies — the filenames, the capture names, and what each means — is published with the runtime as `runtime/queries/README.md`. That document, not this section, is what a third party codes against (HLR-121).

#### 6.2.2 Custom Rule Binding

A Tree-sitter query compiles against one specific `TSLanguage`, so every custom rule must name the language it applies to. Rules found under `runtime/queries/<lang>/rules/` are bound by their location; rules named on the command line are bound by the `lang:path` argument form. A rule naming a language with no available module is a diagnostic, not a compile attempt.

**Both provenances are resolved in `registry_open`, before discovery.** HLR-116 requires a rule named on the command line to fail the run *without analysing any file*, and deciding whether a query compiles requires its grammar — so the language module is loaded then rather than on first use of an extension. The same holds for a located rule, but only for a language that actually has one: the scan reads directories and loads nothing, and a language whose `rules/` directory is absent or empty is never loaded on its account. No module shipped with `elc` carries a rule, so today this loads nothing eagerly at all.

**One rule list, two ways in.** By the time a rule is compiled its provenance is gone, and that is the design: what a rule *does* cannot depend on how it arrived. Only the failure handling differs, and that is settled before the record exists.

The `lang:path` argument splits at the **first** colon, so an absolute path keeps its own — `c:/opt/a:b.scm` is the C language and a path, not a language called `c:/opt/a`.

A language the extension map has never heard of is reported without a load being attempted. Letting the name reach `dlopen` answers a typo with a message about a `.so` the user never mentioned: two diagnostics for one mistake, the louder of them about the wrong thing.

Directory order is not part of the contract. `readdir` yields whatever the filesystem holds and matches within a file are reported in the order the rules loaded, so the filenames are sorted once at discovery (HLR-032).

#### 6.2.3 Environment

`ELC_RUNTIME_DIR`, when set, takes precedence over every path derived from the executable (HLR-059), and is used exactly as given: a variable naming a location that is not there is reported against that path rather than quietly falling back to one the user did not ask for.

With the variable unset, two paths relative to the executable are tried in order, and the first that exists and is a directory wins:

| Path | Layout it serves |
| ---- | ---------------- |
| `<exedir>/runtime` | an unpacked self-contained distribution, and the build tree |
| `<exedir>/../share/elc/runtime` | the installed layout `make install` produces |

**Both, because HLR-059 says "relative to the executable" and not "adjacent to" it, and the difference is the whole of a working installation.** The install target puts the binary in `<prefix>/bin` and the runtime in `<prefix>/share/elc/runtime`, since a tree of grammars and query files does not belong in a directory of executables. A resolver looking only beside the binary fails on every installed copy — while working perfectly in the build tree, where `make all` creates a `runtime` symlink next to `elc`. That asymmetry is why the defect survived: every test level ran against a layout that flattered it, and producing a *working* installation was a release criterion nothing checked. The fixture group now installs into a staging root and runs the result.

Each path derived from the executable is derived from the executable itself rather than from `argv[0]`, which a caller controls.

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
*   **`int registry_load_rules(Registry *reg, const ElcOptions *opts)`** — Load and compile custom rule queries against their bound language; fatal for CLI-named files, diagnostic-and-skip for runtime-located ones (HLR-116). Called from `registry_open`, and therefore before discovery, since "without analyzing any file" is what that fatality means.
*   **`const LanguageModule *module_for_language(Registry *reg, const char *language)`** — The module for a language by name, loading it on first use. Separated out of `registry_for_path` because a rule names its language directly — by the directory holding it, or by the argument form — and never by a file extension. One cache and one load path serve both, so a rule cannot compile against a differently loaded copy of a grammar than the analysis uses.
*   **`int rule_compile(Registry *reg, const LanguageModule *module, const char *path)`** — Compile one rule file against a loaded language and record it. Returns non-zero when the rule was not added, leaving the caller to decide what that means: it is the *provenance* that decides and not the failure, and a function that decided for itself could not serve both (HLR-116).
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
*   Run the comment, function, ELOC, complexity, call, global, and dead-code queries against the parsed tree.
*   Merge comment spans and classify statements to compute ELOC per function and per file.
*   Attribute each statement and decision point to its innermost enclosing reported function.
*   Emit `FileFacts` — the call sites, global accesses, and address-taken functions — for later cross-file resolution.
*   Record on every measured file the directory containing it, derived once through `report.c`'s single definition of that derivation, so that the analyses which group by directory read a recorded value rather than re-deriving one from a path at each point of use (HLR-160).
*   Record the statements within each function that cannot execute, and whether the language supplied the data needed to look (HLR-137, HLR-139).
*   Measure a file the parser only partly understood from the parts it did, and record how many lines it did not, so that a grammar gap costs the lines it touches and not the file (HLR-035).
*   Evaluate custom rule queries and record their matches.
*   Omit every function the linked image does not define, and record which they were, so that no later stage need know a filter was applied (HLR-140, HLR-144).

### 7.2 External Interfaces
#### 7.2.1 Deciding a Conditional Region

Which regions this configuration does not compile, and the exact division of labour that keeps the mechanism language-agnostic (HLR-131 – HLR-135).

**The query decides truth; this module decides bytes.** A `conditionals.scm` settles a condition it recognises with `@conditional.true` or `@conditional.false`, captured on the condition rather than on a span. Given that verdict, `analyze.c` works out what it excludes: the alternative when the condition holds, everything up to the alternative when it does not, and the whole region where there is none. A query pointing at a span would have to know that a `#if` with an `#else` keeps half of itself — arithmetic, not a fact about C — and the same file would then have to be rewritten for a language whose conditional has a different shape.

**Definedness is the one thing only this module can answer**, because only it holds the `-D` set. There the query captures `@conditional.symbol` and stops.

**A symbol no `-D` mentions is undecidable, not undefined.** A build may define it in a header or on a command line `elc` never sees, and `-D` can only assert definedness — there is no `-U`. That single rule also delivers HLR-131's "with no definitions, nothing changes": with an empty set every definedness test is undecidable, so nothing prunes, and no special case says so. A constant condition is different in kind and prunes whatever the definitions are, because `#if 0` means the same thing in every configuration; HLR-131 says so directly, and it is the Phase 3 judgement this phase reverses.

**Where several patterns match one region, the earliest in the query file wins.** A `.scm` writes its specific cases before its catch-all, and the catch-all is what makes an unrecognised condition *undecided* rather than invisible. Without the rule a `#if 0` matched by both a literal pattern and a fallback would be decided by whichever the library reported first.

**A region inside an already-excluded region is neither pruned again nor counted undecided.** Regions are processed outermost first — sorted by start byte, which puts a parent before its children — and one starting inside an exclusion is skipped. A region nobody builds has no condition worth reporting, and counting it would inflate the one figure a reader uses to judge how complete the pruning was.

#### 7.2.2 One Excluded Set, Not Two

Inactive regions join the **merged comment set** rather than becoming a second mechanism (HLR-132). One question — is this byte measured? — answers both, so neither can remove a range twice and there is one thing to understand rather than two that could disagree.

That merge forced the collector order to change. The exclusion must exist before anything consults it, so comments and conditional regions are gathered *first* and the functions after them: a function inside an inactive region must never reach the report, and until this phase `collect_functions` ran before the comment set was built.

It also widened the exclusion's reach. `byte_is_excluded` was consulted only by the ELOC pass, because no code node can begin inside a comment; every collector consults it now, because a node inside an inactive region is an ordinary parsed node and only this test distinguishes it.

**The linked-image filter joins the same set**, which is the decision Phase 16 had to make deliberately: a filter drops whole *functions* where the other two mechanisms drop byte *ranges*, so it could have gated `collect_functions` alone. It does not, and the reason is HLR-145. A function dropped from the reported set with its bytes still measured leaves its statements attributed to no function — which is to say counted as file-scope ELOC, the one figure the filter is required to keep separate and honest. Excluding the extent instead makes HLR-144 fall out of machinery that already exists: no statement, decision point, call site, global access, dead span, or rule match inside an omitted function reaches any later stage, because every collector already asks whether a byte is measured.

**The four exclusions are gathered in one order and it is load-bearing.** Comments, then inactive regions, then the functions the image lacks, then the lines the build compiled no instruction for. Each needs the ones before it settled. The third is third because a function inside a region this configuration does not compile was never built, and reporting it as one the linker discarded would answer a question about the image with a fact about the preprocessor. The fourth is last for the same shape of reason: a line already gone — inside a comment, inside an undecided region that turned out inactive, or inside a function the linker discarded — must not be pruned again, or it is counted twice in a figure a reader is meant to act on (LLR-ANL-58, LLR-ANL-60).

**The fourth exclusion is confined to within functions the image defines**, which is HLR-154's rule and is why the pass above hands back the *kept* extents as well as excluding the absent ones. Code at file scope has few line entries to its name and is the one figure HLR-145 requires be kept separate and honest; a rule that pruned uncovered lines everywhere would delete precisely that. A blank line and a line already excluded are skipped rather than counted, since pruning either removes nothing and counting it would inflate the figure of HLR-155 with lines no measurement rested on.

**The image's ranges are merged in only once its pass is over**, and not as each is found. `byte_is_excluded` exits early on the first span starting past the byte it was asked about, which is correct exactly while the list is ordered; appending to the list being read would leave an unsorted tail behind the merged head, and the answer for one function would then depend on which functions the query happened to report before it.

#### 7.2.3 Query Capture Contract

The `.scm` files communicate through capture names, which are the contract between `runtime/` and this module: `@function.name`, `@function.body`, `@comment`, `@statement`, `@decision`, `@call`, `@call.name`, `@function.address_taken`, `@global.read`, `@global.write`, `@dead.terminator`, `@dead.reentry`, `@dead.branch`. A pattern may also carry **predicates**, and this module evaluates them: the parser library returns a predicate as data rather than applying it, so a stage that never asks accepts every match as though none were written. Five filters are honoured — equality, inequality, membership, and match and its negation against a POSIX extended regular expression — each comparing the text a capture spans against a string the query file wrote. A directive, which carries information rather than filtering, is ignored; a filter this build does not implement discards the match. A capture name this module does not recognise is ignored, so a query file may carry extra captures for its own purposes. `@function.address_taken` marks a function whose address is taken without being called — the fact that keeps callbacks and interrupt handlers out of the dead-code report (§11).


### 7.3 Internal Structure
#### 7.3.1 Key Data Structures

Produces `FileMetrics` and `FileFacts`. Holds a scratch span list for comment merging, reused across files to avoid per-file allocation.


#### 7.3.2 Key Functions

*   **`int analyze_file(Registry *reg, const ElcOptions *opts, const SymbolSet *image, const char *path, FileMetrics **metrics, FileFacts **facts)`**
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
        6.  Where an image was supplied, run `functions.scm` a first time and add to the excluded set the whole extent of every function the image does not define, recording its name and line as the file's absent list (HLR-140, HLR-143).
        7.  Run `functions.scm` to establish the reported function set, including nested named functions, and build an innermost-enclosing lookup over their byte ranges (HLR-067). A match is a function only when it supplies **both** `@function.name` and `@function.body`; a match carrying one without the other is discarded rather than reported as a function with no line range or a line range with no name.
        8.  Run `eloc.scm` and `complexity.scm`, attributing each capture to its innermost enclosing reported function, or to no function when it lies outside every one of them — file-scope code, which contributes to the file's ELOC alone (HLR-019). Record the line each capture starts on, discard any capture falling inside the merged comment set, and count distinct lines (HLR-053, HLR-068).
        9.  Run `calls.scm` and `globals.scm`, recording each call site, each global access, and each address-taken function with its enclosing function, into `FileFacts`.
        10.  Run every loaded custom rule query and record its matches (HLR-109). A match's identity is the rule file's basename plus the capture name that matched, so one `.scm` file may express several distinct named rules.
        11.  `ts_tree_delete()` and `munmap()` on every exit path.
    *   Notes: Identifier text is `memcpy`'d out of the mapping into NUL-terminated allocations before the mapping is released, since every name outlives it.

        The signature above is the complete one. The `FileFacts` output arrived with the graph in Phase 8, the `ElcOptions` with conditional compilation in Phase 15, and the `SymbolSet` with the linked-image filter in Phase 16. Each is a parameter for one reason: measuring a file depends on which program is being measured, so the program is passed in rather than reached for. `image` is NULL where no image was named, and every difference a filtered run makes follows from that one pointer. Earlier phases show the same function at an earlier stage of its construction, not a second entry point.

        **The function query is run twice for a filtered run and once otherwise.** That is a second query execution over a tree already parsed, not a second parse, so HLR-076 is untouched; and it buys order-independence, which a test inside the single pass could not give (LLR-ANL-59).

        **A skip and a failure are different outcomes, and the return value distinguishes them.** A file whose extension maps to no usable language was never attempted: it is reported skipped and leaves the exit status at 0 (HLR-012, HLR-037). A file that was attempted and could not be read or parsed makes it 1 (HLR-035). A single non-zero return would collapse the two and make every unsupported file look like a failure.

        **The reported line span runs from `@function.name` to the end of `@function.body`**, not from the body's opening brace. A reader asked where a function starts points at its signature, so a span beginning at the brace would be an artefact of how the query is written rather than a property of the code — and a hand-counted fixture would have to encode that artefact. Where a language's query captures the name after the body, the span is the body's alone rather than an inverted one.

*   **`int collect_inactive_regions(const LanguageModule *m, Registry *reg, const ElcOptions *opts, const char *data, TSNode root, SpanList *spans, uint32_t *undecided)`** — Append every region this configuration does not compile to the excluded set, and count the regions left active because their condition could not be decided. A language whose module supplies no `conditionals.scm` has no conditional compilation, which is the truth for a language that has none.
*   **`bool symbol_defined(const ElcOptions *opts, const char *data, TSNode node)`** — Whether a symbol was named by a `-D`. Definedness is the only thing a definition can assert, so "mentioned" and "defined" are the same question — and a symbol that was not mentioned is undecidable rather than undefined (HLR-133).
*   **`int collect_rule_matches(const LanguageModule *m, Registry *reg, const char *data, TSNode root, FileFacts *facts)`** — Record every match of every rule bound to this file's language, with its identity and line range. Runs through the same predicate evaluation the built-in queries use, which is what HLR-107's "same query mechanism" means in practice: a rule author's `#eq?` behaves as it does in `elc`'s own query files rather than being silently dropped. Records no severity, because there is none to record (HLR-111).
*   **`char *component_directory(const char *path)`** — The directory containing `path`, as a fresh allocation. Two edge cases a split on the last separator gets wrong are handled here rather than at each call site: a file directly under the root yields "/" rather than the empty string, and a path carrying no separator yields "." (HLR-160).

    It lives with the model that builds a FileMetrics rather than with the report that renders one. The other way round put an edge from analysis to reporting on top of the edge reporting already has to analysis, and elc reported the resulting cycle in its own source (HLR-084, HLR-181).
*   **`int collect_dead_code(const LanguageModule *m, Registry *reg, const char *data, TSNode root, const FnRangeIndex *ranges, const SpanList *comments, FileFacts *facts)`**
    *   Purpose: Record every statement within a function that cannot execute (HLR-137).
    *   Pre-condition: `ranges` holds the reported functions of this file, so each finding can be attributed to the one containing it.
    *   Post-condition: `facts` holds one span per unreachable statement, and a flag recording whether the language supplied a `deadcode.scm` at all.
    *   Return Value: 0 on success; non-zero only on allocation failure. A language with no dead-code query is not a failure (HLR-139).
    *   Logic:
        1.  If the language module supplies no dead-code query, record that fact and return. The absence is reported as "not analysed for this language", never as "none found" — the two are different claims (HLR-139).
        2.  For each `@dead.branch` capture, record the captured node's line range. The query decides what a literal condition is and which branch it excludes, because both are language-specific: `0` is false in C, `false` in Rust, `False` in Python, and a query predicate can compare the literal's text where C could not without knowing the language.
        3.  For each `@dead.terminator` capture, walk the *following named siblings* of the captured node and record each as unreachable, stopping at the first sibling carrying a `@dead.reentry` capture and skipping any sibling lying within the merged comment set. **A comment is a named sibling**, so a walk that did not exclude one would report the trailing note on the terminator's own line — making every annotated `return` report itself. The exclusion asks the set `comments.scm` already produced rather than recognising a comment for itself, so one mechanism serves this and the ELOC exclusion alike. Sibling traversal is structural and needs no language knowledge; what terminates a block and what can be re-entered are language knowledge, and both stay in the query file.
        4.  Attribute each recorded span to its innermost enclosing reported function, by the same rule ELOC and complexity use, so a dead statement inside a nested function belongs to that function and not to the one around it.
    *   Notes: **The re-entry capture is what keeps the analysis sound, and it is easy to leave out.** In C a `goto` label following a `return` is a sibling of it and is perfectly reachable; recording it as dead would be a false claim of the kind HLR-138 forbids outright. The shape differs by language and even by grammar — in `tree-sitter-c` a `case` label is a *child* of the case construct rather than a sibling of the statements before it, so switch arms need no re-entry pattern there, while a `labeled_statement` does. That a grammar happens to make one case safe is not a reason to omit the pattern for the other; the `deadcode/` fixture group pins both.

        Nothing here evaluates an expression. A branch is dead only where the source writes a literal, which is why `if (0)` is found and `x = 0; if (x)` is not (HLR-138).

*   **`int collect_absent_functions(const LanguageModule *m, Registry *reg, const SymbolSet *image, const char *data, TSNode root, SpanList *excluded, FileMetrics *metrics)`** — Record every function the image does not define and add its whole extent to the excluded set. Returns immediately where no image was supplied, which is what makes an unfiltered run byte-identical to one made before the option existed (HLR-140). A function already excluded — which in practice means one inside a region this configuration does not compile — is skipped rather than reported absent.
*   **`bool function_match(const TSQuery *q, const TSQueryMatch *match, TSNode *name, TSNode *body)`** — Both halves of the function contract from one match, or false where the match supplies only one of them. Shared by the two passes over `functions.scm` so that what counts as a function is decided once and the two cannot disagree.
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
*   **Parse produces an error tree** Tree-sitter always returns a tree, and its error recovery keeps the well-formed parts of a damaged one intact. The error regions are measured in lines and set aside; everything around them is analysed normally, and the line count travels with the file so the report can qualify its figures.

    **This discarded the whole file until experience showed it too blunt**, which this section had anticipated as the one place the tolerance would be relaxed. The cost was not marginal: a single macro-built `printf` the C grammar cannot follow damages one line, and the rule discarded every metric in the file around it. Measured across one embedded project, 0.1%–1.4% damage per file cost half the codebase.

    The objection that motivated discarding — that partial metrics are indistinguishable from sound ones once rendered — is answered rather than abandoned. It is met by making them distinguishable, in the way the call depth is presented beside its unresolved-call count.

    Recovery has a limit worth stating: an unbalanced delimiter leaves the parser nothing to resynchronise on, so everything after it is one damaged region. That is not a silent loss — the line count covers what was swallowed, so a reader sees the scale.
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

        **Call-edge precision is language-dependent, and over-approximation is not uniformly safe.** The paragraph above concerns *missing* edges; the opposite error also occurs. Some languages make a call syntactically indistinguishable from something else without semantic analysis a grammar does not perform — a call and an array index written identically is the usual shape, and a grammar manages such an ambiguity with precedence rules rather than resolving it. Where a language's `calls.scm` cannot separate the two, the graph carries edges that do not correspond to calls. `elc` does not attempt to disambiguate in C: doing so would place language knowledge in the binary, which the design forbids outright.

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
*   **A diagnostic from inside the graph library** Its *error* handler is installed non-aborting, so every failure returns a code the caller checks (LLR-SDG-15). Its **warning** handler is separately installed to discard, and that is a different judgement rather than the same one twice. A warning is by definition a result the library still produced, and it is written to standard error naming one of the library's own source files and lines — `Warning at src/centrality/hub_authority.c:77` — which is not a diagnostic a user of `elc` can act on and not `elc`'s own, the only thing HLR-038 admits to that stream. The warnings that actually arise are properties of a call graph rather than faults: the hub-and-authority decomposition warns whenever a third of the scores are zero, which is true of every program whose functions include leaves.
*   **Duplicate symbol definition** Recorded once with a diagnostic; the first definition in sorted file order wins, keeping resolution deterministic.

    **The artefact reaches the information-flow metric raised to a power.** Where several files define a `static` helper of the same name, every call to that name resolves into the winning definition: it collects every caller's fan-in and the losing definitions collect none. Fan-in and fan-out are each wrong by that amount, and the Henry-Kafura value of HLR-157 multiplies the two and squares the product — so a fan-in overstated by a factor of three overstates the winner's Henry-Kafura value by a factor of nine, and understates each loser's to zero. A four-order-of-magnitude figure can therefore rest entirely on this, and the project total with it. The same imprecision is already recorded for reachability (§11) and for coupling (§9); it is worth stating a third time here because the squared term is what turns a modest resolution error into a dramatic-looking number, and because the metric is ordinal — a reader ranks functions by it, and this artefact moves one to the top of the ranking. Correcting it needs the type resolution the project does not perform; the diagnostic on standard error is what the report is read beside.

## 9. Detailed Design for [src/arch.c](../src/arch.c)

### 9.1 Purpose and Responsibilities
[src/arch.c](../src/arch.c) implements the component-level analyses: afferent and efferent coupling, the Instability metric, bottleneck identification, dependency-cycle detection, and architectural layering validation.

*   Compute `Ca` and `Ce` for every component from the component projection.
*   Compute Instability, reporting it as undefined where both couplings are zero.
*   Identify bottlenecks against the configurable threshold.
*   Detect every component-level dependency cycle.
*   Validate declared strata, reporting both skip-level calls (HLR-079) and direction-inverted calls (HLR-118), when strata were declared.
*   Produce the coupling table and the cycle list on every run, so that omitting the layering for want of a declaration does not omit its neighbours (HLR-115).
*   Aggregate the layering violations already found into the Back-Call and Skip-Call Violation Indices, by counting those findings rather than re-deriving them from the graph (HLR-162 – HLR-164).


### 9.3 Internal Structure
#### 9.3.1 Key Functions

*   **`int arch_analyse(const Sdg *g, const ElcOptions *opts, ArchResults *out)`** — Run every component-level analysis, skipping layering when no strata were declared.
*   **`int conformance_indices(const ArchResults *a, ConformanceIndices *out)`** — Count the recorded layering violations into the two indices, over the inter-layer call edges as denominator; undefined where that denominator is zero (HLR-162, HLR-163).
*   **`void arch_results_free(ArchResults *r)`** — Release the coupling table, instability values, cycle list, and violation list.
*   **`int compute_coupling(const Sdg *g, ArchResults *out)`** — Populate Ca and Ce per component.
*   **`double instability(uint32_t ca, uint32_t ce, bool *defined)`** — Ce/(Ce+Ca), with defined set false when both are zero.
*   **`int find_cycles(const Sdg *g, ArchResults *out)`** — Strongly connected components of the component projection, excluding trivial single-node components, each with a concrete loop through it found by a deterministic search from its lowest-numbered member.
*   **`int check_strata(const Sdg *g, const ElcOptions *opts, ArchResults *out)`** — Report calls that bypass declared layers and calls that invert the declared dependency direction, and count the inter-layer call edges the indices are over as it goes.
*   **`size_t *stratum_of_components(const Sdg *g, const ElcOptions *opts)`** — The declared stratum each component lies in, SIZE_MAX for one no declaration names; exposed so the dependency matrix assigns layers by the same rule the findings do.

#### 9.3.2 Parsing Strategy / Algorithm

Cycles are found as the non-trivial strongly connected components of the *component* projection, not of the function graph. This is what keeps mutual recursion between two functions in one file from being reported as a dependency cycle: within a single component there is no inter-component edge to close a loop. Mutual recursion across two files is legitimately both a recursion finding and a component cycle, because the two facts are different (HLR-083, HLR-089). Stratum checking compares the declared ordinal of the caller's stratum with the callee's, and yields two independent findings from that one comparison. A call descending more than one level is *skip-level* (HLR-079); a call ascending at all runs against the declared direction and is *direction-inverted* (HLR-118). The two are orthogonal — a driver calling one layer up inverts without skipping, and an application reaching two layers down skips without inverting — so each is reported in its own right rather than folded into a single "layering violation". The distance test runs in **both** directions, so a call ascending more than one layer is reported twice — as inverted and as skip-level — because both statements are true of it and each has its own remedy.

Only call edges are considered. A global object two layers share is a different fact, with its own findings in the global-state and execution-scope analyses; folding state edges in here would report a layering violation for a variable two layers merely both read.

**The denominator of both conformance indices is counted where the numerators are.** `check_strata` increments `inter_layer_edges` in the same loop, immediately after the three tests that decide whether an edge is a candidate at all: the edge is a call rather than a global, both of its ends lie inside the declared partition, and the two ends lie in different layers. A second traversal applying those tests again would be a second opinion about which edges the indices are over, and it is exactly the disagreements HLR-164 names — a call touching an unpartitioned component, a call that both skips and inverts, an edge collapsed from several call sites — that such a traversal would eventually get wrong. `conformance_indices` then tallies the recorded violations by kind and divides; it never reads the graph, and could not, because it is not given one.

A call that both skips and inverts contributed one violation of each kind and is therefore counted once in each index. The two are consequently **not** summable: each is a proportion of the same denominator, and a project can hold more violations than candidate edges.

**A layer assignment made twice is a layer assignment that can differ.** `stratum_of_components` is exposed for that reason: the dependency matrix places a component in a layer by calling it rather than by matching the patterns itself, so the cells below the matrix's diagonal account for exactly the back-calls the layering table lists.

**A duplicate function name reaches these analyses too, and here it is more expensive than elsewhere.** Calls resolve by name, so where several files define a `static` helper of the same name every call resolves into one of them: that component gains afferent coupling it has not earned, the others lose it, and where the winner already depends on one of the losers the invented edge closes a dependency cycle that does not exist. A false circular dependency points at an architecture problem rather than at a line to delete, which makes it a worse wrong answer than the false dead-code claim the same artefact produces (SDD §8.5). It is diagnosed on standard error where the graph is built, and the two outputs are read together; correcting it needs the type resolution the project does not perform.

### 9.4 Dependencies

*   The graph library, for strongly-connected-component decomposition.
*   `src/graph.c` for the SDG and its component projection.

### 9.5 Error Handling and Logging

*   **No strata declared** Layering validation is omitted and the omission is stated in the report; it is not an error (HLR-115).
*   **Stratum pattern matches no component** Diagnostic to `stderr`; the declared layer remains in effect and simply contains nothing. Retained rather than dropped, because dropping it would renumber the layers below and change what every remaining call is compared against — turning a typo into a wrong answer rather than a warning. This is also why the layering analysis has two states and not the three reachability carries: "declared but matching nothing" never becomes an omission.

## 10. Detailed Design for [src/calltree.c](../src/calltree.c)

### 10.1 Purpose and Responsibilities
[src/calltree.c](../src/calltree.c) implements the function-level call-tree analyses: fan-out and its threshold classification, fan-in, the Henry-Kafura information-flow value formed from the two, maximum call depth, the deepest call stack in full, and recursion detection.

*   Compute per-function fan-out and classify it against the published width thresholds.
*   Compute per-function fan-in — the number of distinct functions that invoke it — over the call view alone (HLR-156).
*   Compute each function's Henry-Kafura structural complexity from its ELOC and the two degrees, in an integer width no run overflows (HLR-157, HLR-158).
*   Detect direct and mutual recursion among functions.
*   Compute the maximum call depth from the declared entry points, and capture the ordered chain that achieves it.


### 10.3 Internal Structure
#### 10.3.1 Key Functions

*   **`int calltree_analyse(const Sdg *g, const ElcOptions *opts, TreeResults *out)`**
    *   Purpose: Produce every call-tree measurement the report requires.
    *   Post-condition: Either a depth and a deepest chain are present, or the recursion list is non-empty and the depth is marked unbounded.
    *   Return Value: 0 on success.
    *   Logic:
        1.  Compute out-degree and in-degree over the *call* edges per function node, in one pass: fan-out is the number of distinct subroutines a function invokes, fan-in the number of distinct functions that invoke it (HLR-085, HLR-156). One pass rather than two because they are the same traversal read from either end of each edge, and because a second traversal is a second place the `kind == EDGE_CALL` test could be forgotten. The classification of fan-out against the healthy, warning, and critical bands is `thresholds.c`'s (HLR-086 traces to Section 12); this module measures and does not judge, so that one catalogue of thresholds exists rather than one per analysis. Fan-in is never classified at all: no published source bands it.
        2.  Form each function's Henry-Kafura value: `HK = ELOC × (Fan-In × Fan-Out)²`, taking ELOC from the node the graph already carries (HLR-157). **The widening happens before the multiplication, not at the assignment.** The product of two degrees fits in 32 bits comfortably and its square does not, so a 32-bit square assigned to a 64-bit variable has already wrapped by the time the assignment widens it — and a wrapped total renders as a perfectly ordinary number (HLR-158). Computed here rather than by a consumer so that one answer exists for the report, the record, and every renderer.
        3.  Decompose the *call view* of the function graph into strongly connected components; every non-trivial one, and every self-loop, is a recursive cycle (HLR-089). The call view and not the whole SDG: a global-state edge joins a function that writes an object to one that reads it, and two functions sharing a variable in both directions form a cycle in the SDG that is not recursion. Reporting it as such would be a critical finding against MISRA C Rule 17.2 on ordinary code.
        4.  If no entry points were declared, mark depth omitted and stop (HLR-115). If entry points were declared but none names an analysed function, mark depth omitted with a *different* reason and stop: "you declared nothing" and "what you declared is not in what you analysed" call for different actions from the reader, and a single omission message would send them to the wrong one. A symbol matching nothing is diagnosed and skipped rather than failing the run, since analysing one directory of a project whose entry point lives in another is ordinary.
        5.  If any recursion was found, mark the depth unbounded, attach the recursive cycles, and stop — no finite deepest chain exists (HLR-090).
        6.  Otherwise the graph is a DAG: compute the longest path from each entry point by memoised traversal in reverse topological order, retaining the predecessor of each node.
        7.  Walk the retained successors out from the deepest entry point to reconstruct the ordered chain, and record it in full (HLR-088). A tie between two chains of equal length resolves to the lower node identifier, which is sorted-file order, so equal candidates always yield the same report (HLR-032).
    *   Notes: Establishing acyclicity before measuring depth is what makes the longest-path computation terminate; on a cyclic graph the question has no finite answer, which is precisely why MISRA C Rule 17.2 exists. The measured depth is a lower bound on true worst-case depth: a chain that continues through an unresolved indirect call is not followed. The report therefore presents the depth alongside the unresolved-call count of HLR-077, so the reader can judge how completely the graph covers the program.

*   **`int longest_path_dag(const Sdg *g, const NodeSet *entries, Chain *out)`** — Memoised longest-path search with predecessor retention.
*   **`void tree_results_free(TreeResults *r)`** — Release the per-node measurement tables, the recursive-cycle list, and the retained deepest chain.
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
*   Perform the global-access mapping on every run, so that omitting an analysis for want of a declaration does not omit its neighbours (HLR-115).


### 11.3 Internal Structure
#### 11.3.1 Key Functions

*   **`int state_analyse(const Sdg *g, const ElcOptions *opts, StateResults *out)`** — Run the global-state, scope-isolation, and reachability analyses, skipping those whose declarations are absent.
*   **`int classify_globals(const Sdg *g, StateResults *out)`** — Apply the scope-reduction and hidden-channel rules to each global.
*   **`int reachability(const Sdg *g, const uint32_t *roots, size_t root_count, uint32_t **out, size_t *out_count)`** — Breadth-first traversal from the root set; the complement is the unreachable set.
*   **`int collect_roots(const Sdg *g, const ElcOptions *opts, uint32_t **out, size_t *out_count, size_t *resolved)`** — Union of the declared entry points and every address-taken function, de-duplicated. Reports separately how many declared symbols named an analysed function, so the caller can tell "you declared nothing" from "what you declared is not here".
*   **`int unreachable_globals(const Sdg *g, const uint32_t *dead, size_t dead_count, StateResults *out)`** — Globals accessed only by unreachable functions are themselves unreachable.
*   **`int check_scopes(const Sdg *g, const ElcOptions *opts, StateResults *out)`** — Report every edge crossing a declared execution-scope boundary.
*   **`void state_results_free(StateResults *r)`** — Release the global access map, the hidden-channel and scope-reduction lists, and the unreachable sets.

#### 11.3.2 Parsing Strategy / Algorithm

The hidden-channel test asks whether the functions touching a global fall into more than one weakly connected region of the call graph once that global's own edges are disregarded. A global shared within one call-connected region is ordinary shared state; one shared across regions that never call each other is the temporal coupling MISRA C Rule 8.9 is concerned with. Dead-code detection is plain forward reachability, which is exactly why it is immune to the failure mode of textual linters: a clique of unused functions calling one another is still unvisited, because no path reaches it from any entry point (HLR-097). The traversal runs over the **call view**, and that is a decision rather than an inheritance: writing a variable another function later reads is not calling it, so control never travels along a state edge and a function reachable only through one has not been reached. Following it would quietly rescue genuinely dead code from the report.

**The one place this analysis errs toward *un*reachable is not its own doing.** Call resolution is by name, so where two files each define a `static` helper of the same name every call resolves to the first and the second has no incoming edge — and is reported dead when it is not. That is a limit of the graph rather than of the traversal, and it is already diagnosed on standard error where it occurs (SDD §8.5); the report cannot suppress it without the type resolution the project does not perform. `elc` analysing its own source reports exactly this and nothing else, which makes the interaction easy to demonstrate and easy to forget.

### 11.4 Dependencies

*   The graph library, for traversal and connectivity.
*   `src/graph.c`.

### 11.5 Error Handling and Logging

*   **No entry points declared** Reachability analysis omitted with a stated reason. `elc` must never report every function as unreachable merely because nothing was declared (HLR-115).
*   **No execution scopes declared** Scope-isolation analysis omitted with a stated reason (HLR-094, HLR-115).
*   **A duplicate function name** Not an error, and not suppressed. Every call to a name defined more than once resolves to the first definition, so the others are reported unreachable; the duplicate is diagnosed on standard error when the graph is built, and the two are read together.

## 12. Detailed Design for [src/thresholds.c](../src/thresholds.c)

### 12.1 Purpose and Responsibilities
[src/thresholds.c](../src/thresholds.c) applies the published threshold catalogue of PVD Appendix A to every measurement the analyses produced, attaching a severity and a source attribution to each resulting finding.

*   Hold the threshold catalogue as a static table of measurement kind, band boundaries, severity, and citation.
*   Classify each measurement into its band and emit a `Finding` when it falls outside the accepted range.
*   Attribute every threshold to its external source, and mark `elc`'s own heuristics as such.
*   Name the published source of a measurement the catalogue does not band, so that a citation and a threshold stay separate claims (HLR-157, HLR-159).
*   Be the only module that bands a measurement or names a source, so that the claim to carry no opinion is checkable by reading one table (HLR-099, HLR-111).
*   Assign a severity as a *label*: it never reaches the exit status, and no finding carries remediation (HLR-100, HLR-101).

### 12.2 External Interfaces
#### 12.2.1 Threshold Catalogue


| Measurement | Bands | Attribution |
| ----------- | ----- | ----------- |
| Function fan-out | 0–2 below healthy, 3–7 healthy, 8–10 acceptable — all silent; 11–15 warning; >15 critical | Henry–Kafura |
| Call depth | >8 warning; >12 critical, on stack-constrained targets | Embedded practice |
| Recursion present | critical | MISRA C Rule 17.2 |
| Component cycles | any occurrence critical | Martin / acyclic dependencies |
| Single-function global | warning | MISRA C Rule 8.9 |
| Hidden channel | warning | MISRA C Rule 8.9 |
| Instability vs. declared stratum | warning on mismatch | Martin |
| Bottleneck (`Ca` and `Ce` ≥ threshold) | warning | **`elc` heuristic — not a published standard** |

The counted bands are **exclusive upper bounds**: a value strictly greater than the bound falls in that band, which is how the published tables are written — "> 15 is a god function". A catalogue needing mental translation from its source is one nobody can check against it.

The fan-out bands are **exhaustive**: every value from 0 upward classifies exactly once, and three of the five bands produce no finding. The *acceptable* range of 8–10 was a gap in an earlier reading of the thresholds, which is why HLR-086 states exhaustiveness rather than leaving it to be inferred.

The rows whose finding is their mere occurrence — recursion, a dependency cycle, a single-function global, a hidden channel — carry a fixed severity instead of bounds. There is no acceptable count of them.

**One row is not a published standard**, and it says so in the text a reader sees. That label is the whole of what separates shipping MISRA and Martin values from having invented them, and it is asserted by a test that also checks no other row carries it (HLR-099).

**Some measurements are cited and not banded, and they are held apart from this table.** The Henry-Kafura value of Section 20 is the case: the formula is Henry and Kafura's and must be attributed wherever it is reported (HLR-157), and no published source divides it into accepted and unaccepted ranges (HLR-159). A row here with empty bounds would not express that — `occurrence` false with both bounds zero is a *silent band*, every value passing, and `thresholds_lookup` would then answer "there is a threshold for this" to a caller asking precisely because there is not. The citation therefore lives in a second, smaller table that `threshold_attribution` consults after the catalogue, which leaves the catalogue holding only rows a reviewer can check against a published table, and leaves the unbanded measurement on the path Section 12.5 already provides for one.



### 12.3 Internal Structure
#### 12.3.1 Key Functions

*   **`int thresholds_apply(const ArchResults *arch, const TreeResults *tree, const StateResults *state, const Sdg *g, const ElcOptions *opts, FindingList *out)`** — Evaluate every measurement against the catalogue and emit findings.
*   **`const Threshold *thresholds_lookup(MeasurementKind kind)`** — Return the catalogue entry for a measurement kind.
*   **`void findinglist_free(FindingList *f)`** — Release every finding and the detail string each owns.
### 12.4 Dependencies

*   No third-party dependency. Pure evaluation over the analysis results.

### 12.5 Error Handling and Logging

*   **Measurement with no catalogue entry** Reported as a bare value with no severity, rather than being silently dropped or assigned an invented band. The Henry-Kafura value is the first measurement to take this path deliberately and permanently: it will never have an entry, because no published source bands it (HLR-159).
*   **Measurement that was not made** Not banded at all. A depth omitted for want of an entry point is not a depth of zero, and a value that does not exist cannot fall outside a range (HLR-115).
*   **A critical finding** Not an error. The severity is a label within the report and leaves the exit status untouched, which stays reserved for the failure conditions of HLR-120 (HLR-100).

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
*   Resolve each dead-code span to its enclosing function by containment over the assembled model, rather than by the index the parse recorded against an array since reordered for presentation (LLR-RPT-28).
*   Record the conditional-compilation definitions in force and the number of regions whose condition could not be decided, so that a figure which depends on a configuration is reported alongside it (HLR-136, HLR-133).
*   Record the linked image a run was filtered by, the linkage names it could not resolve, the source functions it does not define, and the effective lines belonging to no function, so that a filtered figure is reported alongside the image that produced it (HLR-143, HLR-145, HLR-147).
*   Record every file skipped for want of a language module, so the report accounts for each discovered file (HLR-012).
*   Record the discovery route applied to each directory target, so that an unexpectedly empty or oversized result is diagnosable (HLR-127).
*   Sum the per-function Henry-Kafura values into the project total, in one function that both the live path and the record path call, so that the total *is* the sum of the rows rather than two implementations agreeing (HLR-158).
*   Define how a component's directory is derived from its path, in one function called at each of the two places a `FileMetrics` is constructed — the measurement of a source file and the reader that rebuilds a model from a record — so that every consumer reads one recorded answer rather than slicing the path for itself (HLR-160).
*   Render the two conformance indices, and the complementary conforming proportion of each, into the model as text — "undefined" being one of their legitimate values, exactly as it is for Instability (HLR-162, HLR-163).


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
        5.  Where an image was named, gather each file's absent functions into one list and sum the file-scope ELOC. The list is sorted on its own keys — file, then start line, then name — because a query match arrives in no source order, so without it the rows would carry the order the parser library happened to report them in (HLR-032, HLR-143).
        6.  Sort files by path; functions by start line and then by name; per-language totals by language name; skipped files by path; findings by (severity, kind, primary location); cycles by their lowest member; unreachable functions by (file, line) (HLR-033). The name is the tie-break for functions because two can share a start line — a nested function declared on the line its enclosing body opens — and `qsort` is not stable, so a comparator returning 0 there would leave their order to the implementation.
    *   Notes: Centralising every sort here is deliberate: it is the single place a reviewer must check to be satisfied that HLR-032's byte-identical guarantee holds, rather than auditing six renderers and three analysis modules.

        `discover.c` also sorts, and the two are not redundant. Its sort exists so that de-duplication can collapse equal canonical paths, and so that the *analysis* order is not the filesystem's; this one exists so that *presentation* order is a property of the model. A later phase that changes how files are discovered therefore cannot silently change how they are presented.

*   **`void report_total_henry_kafura(Report *r)`** — Sum the per-function Henry-Kafura values into the project total. One function rather than a line in each of the two paths that need it: a live run calls it once the flow rows are filled, and a run regenerating from a record calls it once they are restored, since the total cannot be derived from the per-file metrics `report_assemble` works over (HLR-158).
*   **`int report_set_image(Report *r, const SymbolSet *image)`** — Record the image the run was filtered by and the number of its linkage names left unresolved. After assembly rather than within it, for the reason the unresolved-call count is: the image belongs to the run and is read before any file is measured, while the *effects* of the filter — which functions were omitted, and how much file-scope code remained — are properties of the measurement and are assembled with it (HLR-147).
*   **`int metrics_add(MetricsAccumulator *acc, FileMetrics *m)`** — Append one file's metrics, taking ownership of them. Grows by doubling through a checked reallocation; on failure the accumulator is left intact and the caller still owns the metrics, so nothing is leaked and nothing is freed twice.
*   **`void metrics_free(MetricsAccumulator *acc)`** — Release the accumulator and every FileMetrics it still owns.
*   **`void report_free(Report *r)`** — Release the report model and everything it owns, the rendered conformance rows and the dependency matrix included.
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
*   Classify each tier as a summary or a detail tier, and present the summary tiers alone unless the verbose report was asked for (HLR-150, HLR-151).
*   Present the two conformance indices as a summary tier and the dependency matrix as a detail tier, delegating the matrix's decoration to `format_dsm.c` in both human-facing formats (HLR-162, HLR-163, HLR-166).
*   Present the recovered layering as a detail tier, stating in the heading that it is a proposal and never the baseline conformance is measured against, and rendering the proposal itself as the argument list a user passes back (HLR-172, HLR-173).


### 14.3 Internal Structure
#### 14.3.1 Key Functions

*   **`int format_table(const Report *r, Verbosity verbosity, FILE *out)`** — Render the aligned ASCII table at the given verbosity. Returns non-zero when the stream reported a write failure, checked once after the last write rather than at every call.
*   **`int format_markdown(const Report *r, Verbosity verbosity, FILE *out)`** — Render GitHub-Flavored Markdown at the given verbosity.
*   **`int render_report(const Report *r, Style style, Verbosity verbosity, FILE *out)`** — The one traversal both formats and both verbosities go through: walks the ordered section list once, emitting the tiers the verbosity selects in the decoration the style selects.
*   **`void render_summary(const Report *r, FILE *out, Style style)`** — Shared project-summary rendering for both formats.

#### 14.3.2 Parsing Strategy / Algorithm

**The two renderers are one traversal.** `render_report()` walks the model once and emits every tier in a fixed order; the `Style` decides only how each tier is decorated. A tier added to the traversal appears in both formats, and cannot be added to one and forgotten in the other, because there is nowhere to forget it — which is what makes HLR-031's uniform composition structural rather than maintained (LLR-SUM-02).

**Verbosity is a second parameter of that same traversal, not a second traversal.** The ordered section list carries, beside each section's render function, the tier it belongs to; the walk emits a section when the verbosity is verbose, when its tier is `TIER_SUMMARY`, or when its analysis was omitted for want of a declaration. That last case is what carries HLR-115's omission notices into the summary: the section is a detail tier, but an omitted analysis produced no rows, so it renders as its heading and the reason in it — which is the notice, and needs no section of its own. A section is therefore written down once and classified once, and the guarantee that a tier cannot exist at one verbosity and not the other holds by the same construction as the guarantee across formats (LLR-SUM-09).

The partition rule is HLR-150's: a tier presenting a project-level aggregate, a file's own totals, or a finding a reader is expected to act on is a summary tier; a tier enumerating one row per analysed entity is a detail tier. Coupling, the cycles, the layering violations, and the recursive chains fall on the detail side even though each row names a component, because they enumerate the graph one entity at a time — a *file-level aggregate* in the rule's sense is a file's own totals, which is what the Files tier presents. Nothing is lost from the summary by it: each of those measurements that crossed a published line is a finding, and the findings tier is a summary tier.

The two tiers of Section 21 divide along that same rule and land on opposite sides of it. The conformance indices are project-level aggregates — two rows whatever the size of the project — and are a summary tier, beside the findings a reader acts on. The matrix enumerates one row per layer or per directory and is a detail tier, like every other table that grows with the graph. They are adjacent in the traversal, so the aggregate and the arrangement it summarises are read together in a verbose report.

The matrix is the one tier whose decoration is not a `Grid`. Its column count is the number of subjects rather than a fixed few, and its cells must escape the Markdown separator, so `dsm_section` calls `format_dsm_markdown` or `format_dsm_table` according to the style. That is two calls from *one* entry in the ordered section list, which is what keeps the guarantee intact: the tier is written down once and classified once, and there is still nowhere to forget it (LLR-SUM-02, LLR-SUM-09).

The conformance tier tests the model rather than the state. `STRATA_MEASURED` is the zero of its enum, so a model carrying no rendered indices at all — a record written before they existed, or a report a test built by hand — would read as measured while holding nothing to print. A renderer is a pure consumer: it presents what the model has, not what the model's state implies it should have.

The image-filter tier is split along the same boundary. `image_filter_section` presents the image and its two counts, which are the provenance of a filtered run and a summary tier; `absent_functions_section` presents the functions the image does not define, which is one row per function and a detail tier. The two are adjacent in the traversal, so a verbose report presents them exactly as one section presented them before the split (LLR-SUM-06).

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

*   Emit every element of the report model, so the record is sufficient to regenerate any report `elc` can produce — the two conformance indices and the dependency matrix among them, since regeneration has no call graph to recompute either from (HLR-054).
*   Carry a format-version identifier in the document root.
*   Escape every character with structural meaning in XML.
*   Parse a saved record back into a report model, rejecting anything malformed, structurally foreign, or of an unsupported version.

### 16.2 External Interfaces
#### 16.2.1 XML Record Structure

A root `<elc-report format-version="N">` carrying `<summary>`, `<languages>`, `<files>` with nested `<function>` elements, `<skipped>`, `<architecture>` holding every graph finding and the two `<conformance>` rows, `<dsm>` holding the dependency matrix, `<custom-rules>`, and `<omissions>`. The format version is incremented whenever an element is removed or its meaning changes.

It is **not** incremented when an element is added, because a reader ignores elements it does not recognise. That asymmetry is what makes a later phase's additions compatible with a record written today, and it is why an element that does not yet exist is absent rather than present and empty.

#### 16.2.2 Reading What elc Did Not Write

The read path treats its input as hostile. The root element must be `elc-report`, its `format-version` must be one this build reads, and an attribute that should be numeric and is not makes the record malformed rather than zero — accepting it would produce a report that renders cleanly and is wrong, which is the outcome HLR-058 exists to prevent.

Nothing reconstructed before a rejection survives it. The partially built model is released rather than rendered, since a partly reconstructed report is indistinguishable from a complete one once it reaches a reader (LLR-XRD-06).


### 16.3 Internal Structure
#### 16.3.1 Key Functions

*   **`int xml_write_report(const Report *r, FILE *out)`** — Serialise the complete report model, escaping every emitted value.
*   **`static void write_dsm(const Report *r, FILE *out)`** — Emit the dependency matrix: the subjects in order, then the non-zero cells alone. A matrix over a real project is mostly zeroes — that is what makes it readable — and a cell absent from the document reads back as the zero it was. The subjects precede the cells so the order of the grid is known before an index into it arrives (HLR-165, HLR-166).
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

**One handler per element, found in a table.** The read path's element callback does nothing but match the incoming name against a table of handlers and call the one that matches; each handler is reached only when its element has already been identified, so it begins where the interesting part begins.

It was, until Phase 17, a single function testing the name against every element in turn. That function reached a cyclomatic complexity of 169 — more independent paths through it than the rest of this module put together, and every one of them the same path: compare a name, read some attributes, append a row. Nothing about the record format made it complicated; the shape of the dispatch did. Split, the largest handler is 15 and the callback itself is 5. An element added to the format is now a row in the table rather than another branch in a function nobody can hold in their head.

The order of the table is the order the elements are documented in, and it carries no meaning: the names are distinct, so at most one matches and the search stops there. An element the table does not name is ignored, which is what makes a later build's additions readable by an earlier one of the same format version.

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
[src/format_graph.c](../src/format_graph.c) writes the graph-shaped outputs: the Graphviz `.dot` call tree for visual inspection, the GraphML export for ingestion by other tools, and the raw and purified drawings that let a user see what purification did.

*   Emit the call tree as `.dot`, annotated with every applicable architectural finding.
*   Decide whether a `.dot` file is warranted, given the default-on setting, the disable switch, and the output destination.
*   Emit the SDG as GraphML when explicitly requested.
*   Emit, on request, the raw graph and the recovery view as two further `.dot` files, the masked and excluded nodes drawn detached rather than deleted, so that purification can be inspected instead of trusted (HLR-178).

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
| node | `fan-in` | Distinct callers, over call edges alone (HLR-156) |
| node | `address-taken` | Whether the function is a reachability root (HLR-096) |
| edge | `kind` | `call` or `global` — the two edge kinds are distinguishable, never merged |
| edge | `global` | For a `global` edge, the object's name |
| edge | `call-sites` | The collapsed call-site count (HLR-085's simple-graph rule) |

Nodes are emitted in ascending stable node-id order and each node's edges in ascending target-id order (LLR-DOT-04); every value is escaped per HLR-065.

The Henry-Kafura value is deliberately **not** a key here. `eloc`, `fan-in` and `fan-out` are all three of its inputs, so an ingesting tool can form it exactly; exporting the value as well would put a second computation of it beside the one in `calltree.c`, and two places computing a figure are two places it can be computed differently (HLR-157, LLR-GML-05).

#### 17.2.2 Companion Artefact Naming

Every companion file derives its name from the report's output path by extension substitution: an output of `report.md` yields `report.dot`, `report.graphml`, `report.raw.dot` and `report.purified.dot`. None takes a path of its own. This is precisely why none is produced when the report goes to standard output — there is no output path to derive a name from, which is the rationale HLR-104 and HLR-106 give (HLR-103, HLR-104, HLR-106, HLR-119).

#### 17.2.3 The Raw and Purified Drawings

Two drawings of one graph, written by one function under a flag, and the pairing is the requirement rather than a convenience (HLR-178). A single drawing of the recovery view cannot show what purification *acted on*: it looks like a clean layering whether the masking was right or wrong. So they are produced together, and they share a writer, because a reader comparing them needs the node set, the labels and the layout to differ only where the masking differs — two writers is how two drawings come to differ in a way that has nothing to do with what was masked.

**Nothing is deleted from the purified drawing.** A masked function is greyed and labelled with its class; an excluded one is greyed, dashed, and labelled likewise; both are left holding no edge. Drawn detached rather than removed, they are what makes the pair legible: the reader sees which functions were set aside and what the graph looks like without them, in one glance.

Which edges survive is asked of `purify.c` through `purify_edge_retained` rather than answered again here. Two answers to one question is how a drawing comes to show a graph the analysis never read, which would defeat the whole purpose of drawing it.

Neither drawing replaces the annotated call tree of HLR-102. That answers a different question — where the findings fall on the call graph — and is enabled by a different default.

#### 17.2.4 DOT Annotation Encoding

The `.dot` file is the **call tree**, so only call edges are drawn. Coupling through a shared global object is not an edge here — it is not a call — and reaches the drawing as a property of the functions that take part in it, which is what HLR-105 asks for: the participants of a hidden channel, not the channel.

Each source file becomes a `subgraph cluster_<n>` labelled with its path, and each function a node within it. Clustering costs the ordering of LLR-DOT-04 nothing, because node identifiers run in the report's sorted file order (LLR-SDG-09) and a component's nodes are therefore contiguous.

**Each annotation takes a different attribute**, so that several may apply to one node without one overwriting another — a function that both takes part in a hidden channel and is unreachable must show both, and a drawing that showed one of the two would be silently incomplete.

| Finding | Attribute | Requirement |
| ------- | --------- | ----------- |
| a warning- or critical-severity finding | `fillcolor` | HLR-081, HLR-086, HLR-098 |
| member of a recursive cycle | `peripheries` | HLR-089 |
| participant in a hidden channel | `shape=octagon` | HLR-093 |
| sole user of a global object | `shape=note` | HLR-092 |
| unreachable | `style=dashed` with a grey `color` | HLR-096 |
| a step of the deepest call chain | `penwidth` and a blue `color`, on the node and on the edge | HLR-088 |
| a finding on the source file | the cluster's `bgcolor` | HLR-081, HLR-083 |

Every one is an attribute a renderer may ignore; ignoring all of them leaves the same nodes and the same edges (HLR-105, LLR-STY-02). The key to the encoding is emitted as a DOT **comment** rather than as a legend subgraph, because a legend would add nodes that are not functions to a graph whose every node is one — and a comment is dropped by every renderer rather than merely ignorable by one.

**No severity is decided here.** Every one arrives in `report->findings`, decided by `thresholds.c`; this module looks up `severity_rank` and chooses a colour. A renderer forming its own view of what counts as exceeding a threshold would put a second opinion in a codebase whose central claim is that it holds exactly one (HLR-098, HLR-099).

**Both cycle kinds are drawn from the report's cycle rows rather than from the findings.** The catalogue locates a cycle at a single subject, because a finding has one subject and a set has no single location; HLR-105 asks for the *members*, plural. `report->cycles` and `report->dep_cycles` carry the membership, and the catalogue's single-subject copy of the same finding is suppressed — matched on the measurement's name, which both sides take from the same catalogue row and which therefore cannot drift.


### 17.3 Internal Structure
#### 17.3.1 Key Functions

*   **`bool graph_dot_warranted(const ElcOptions *opts)`** — True only when .dot generation is enabled and the report goes to a named file.
*   **`int graph_write_dot(const Sdg *g, const Report *r, const char *path)`** — Write the annotated call tree in DOT format.
*   **`bool graph_purify_dot_warranted(const ElcOptions *opts)`** — Whether the raw and purified drawings are to be written: requested, with a named output path to derive their names from, and not in regeneration mode (HLR-104, HLR-119, HLR-178).
*   **`int graph_write_purify_dot(const Sdg *g, const PurifyResults *p, bool purified, const char *path)`** — Write the call graph as built, or the recovery view read off it. One writer for both, because the pair exists to be compared.
*   **`int graph_write_graphml(const Sdg *g, const char *path)`** — Write the SDG in GraphML, nodes in ascending identifier order and each node's edges by ascending target. Reuses `write_escaped` from `format_xml.c` rather than carrying a second escaper: one implementation of HLR-065 means one place for it to be wrong.
*   **`bool graph_graphml_warranted(const ElcOptions *opts)`** — True only when the export was requested *and* the report goes to a named file. Both halves matter: the export is off by default (HLR-106), and requesting it with the report on standard output writes nothing, because there is no path to derive a name from (HLR-104).
*   **`char *graph_companion_path(const char *output_path, const char *extension)`** — The companion's name, by extension substitution on the report's output path. The extension search is scoped to the last path component, so a dot in a directory name is not mistaken for one.
*   **`void node_style(FILE *out, const Annotation *a)`** — Write the Graphviz attributes for a node given the findings that apply to it. `Annotation` is local to the module — a bitset of the marks that apply, the highest severity among them, and those findings joined for the tooltip — because nothing outside the writer has any use for it. Takes a gathered annotation rather than the report and a node identifier: the report is keyed by definition site and the graph by identifier, so matching the two is a search, and a styler that searched would entangle emission order with lookup order.
*   **`int collect(const Sdg *g, const Report *r, Annotation *nodes, Annotation *comps, char **notes)`** — Gather every finding and structural mark onto the node and component it applies to, so that emission is a pure walk. Findings are matched to a node by definition site rather than by name, because a name is not unique and a drawing that marked six functions called `grow` because one was unreachable would be worse than one that marked none.

#### 17.3.2 Parsing Strategy / Algorithm

Both writers are plain text emission, which keeps Graphviz a tool the user may run on the output rather than a library `elc` links against (HLR-102), and keeps GraphML generation independent of any XML library. Unlike the report renderers, these two walk the `Sdg` rather than the sorted report model, so they impose their own order explicitly: nodes are emitted in ascending stable node-id order and each node's adjacency in ascending target-id order. Without that the graph library's internal enumeration would leak into the output and break HLR-032. Annotations use colour and shape attributes that a renderer ignoring them will simply drop, leaving a valid call tree (HLR-105).

The DOT writer runs in two passes rather than one, and the split is what keeps the ordering guarantee cheap to check. `collect()` gathers every finding and structural mark onto the node or component it applies to; emission is then a pure walk in identifier order that consults nothing. A writer that searched the report while emitting would have its output order entangled with its lookup order, and LLR-DOT-04 would have to be argued rather than read.

**A finding is matched to a node by definition site — file and line together — not by name.** Both halves are required: a file alone is a component finding, and a line alone would match the same line in every file. Name matching is used in exactly one place, the recursive cycle, because names are all `report->cycles` carries; it inherits the duplicate-`static` imprecision recorded in the SDG's own limits, and the drawing is where that imprecision is most visible and least caveated.

DOT quoted strings escape two characters, `"` and `\`. That escaper is deliberately *not* folded into `write_escaped`: XML's five entities and DOT's two backslashes are different languages, and one escaper serving both would have to be told which, at which point it is two functions anyway.

### 17.4 Dependencies

*   `src/graph.c` and `src/report.c`. No third-party dependency.

### 17.5 Error Handling and Logging

*   **Report written to stdout** No `.dot` and no GraphML file is produced, since no output path exists from which to derive their names (HLR-104, HLR-106).
*   **Companion file cannot be created** Diagnostic to `stderr`; the primary report is still written, and the run is recorded as failed.

## 18. Detailed Design for [src/elfsyms.c](../src/elfsyms.c)

### 18.1 Purpose and Responsibilities
[src/elfsyms.c](../src/elfsyms.c) reads the function symbols of a linked image the user named, and answers whether a given source function appears in it (HLR-140).

*   Open and validate the named image, and extract every function it defines from the symbol table the linker wrote.
*   Resolve a linkage name to the source name the report presents, where the name carries a published mangling.
*   Answer membership for a source function, and account for both directions of mismatch.
*   Read the image's debug line information from the same open, so that the image is read once and nothing beside it (HLR-141, HLR-153).

### 18.2 External Interfaces
#### 18.2.1 What Counts as a Function the Image Defines

A symbol is taken as a function the image defines when it is of type `STT_FUNC` and is **defined** rather than imported — its section index is not `SHN_UNDEF`. Both halves matter. Without the type test an object and a function of the same name are indistinguishable; without the definedness test every function the image *calls* out to a shared library would be counted as one the image contains, and a filter built from that set would retain source functions the build never compiled.

`.symtab` is read where the image has one and `.dynsym` where it does not. `.dynsym` holds only the dynamically exported subset, so an image reduced to it yields a smaller set and a correspondingly larger unmatched list — which HLR-143 makes visible rather than leaving to be inferred. An image with neither is fatal (HLR-146).

The set is **sorted and de-duplicated** on the resolved name, so that membership is a binary search and so that nothing about symbol-table order can reach the output (HLR-032).

#### 18.2.2 Resolving a Linkage Name

C is the only supported language whose linkage name is its source name. The rest encode it, and HLR-142 requires the encoding be undone rather than worked around.

Resolution is by published scheme, and the scheme is detected from the name rather than from a language the user states — the image does not say which compiler produced which symbol, and a mixed-language image is ordinary:

| Prefix | Scheme | Yields |
| ------ | ------ | ------ |
| none | C, or an `extern "C"` definition | the name unchanged |
| `_Z` | Itanium C++ ABI | a qualified name and a signature |
| `_ZN`…`17h`…`E` | Rust legacy — Itanium-shaped | a path and a hash suffix |
| `_R` | Rust v0 | a path |

A demangled name is not yet a match: the Itanium ABI yields `ns::C::f(int) const`, and the report presents the function name alone (HLR-014). The resolved name is therefore the *identifier* the demangling ends in, with the signature, the qualification, and any hash suffix removed — reduced to the same form on both sides of the comparison, since reducing only one side would make every qualified name a mismatch.

**A scheme with no decoder to hand is not one of these.** A linkage name encoded by a mangling this build cannot decode resolves to nothing and is counted under HLR-143, so a language whose compiler uses one reports a large unresolved count rather than a filter — and says so rather than appearing to work.


### 18.3 Internal Structure
#### 18.3.1 Key Functions

*   **`int elfsyms_open(const char *path, SymbolSet *out)`** — Read the named image and populate the function set. Returns 0; non-zero after a diagnostic naming the path, which the caller turns into a fatal exit rather than a degraded run (HLR-146).
*   **`bool elfsyms_defines(const SymbolSet *s, const char *function)`** — Whether the image defines a function of this name, by binary search over the resolved names.
*   **`size_t elfsyms_unresolved(const SymbolSet *s)`** — How many of the image's function symbols carried an encoding this build does not decode (HLR-143).
*   **`void elfsyms_free(SymbolSet *s)`** — Release the set and every name it owns.
*   **`char *resolved_name(const char *linkage)`** — The source-level function name a linkage name encodes, or NULL where the scheme is not one this build decodes. The reduction to a bare identifier lives here rather than at the call site, so that one definition of "the name the report presents" serves the whole comparison.

#### 18.3.2 Parsing Strategy / Algorithm

`libelf` supplies the container parsing, for the reason `igraph` supplies the graph algorithms (HLR-113): an ELF reader is a well-specified format with a mature implementation, and hand-rolling one would put endianness, class, and section-header handling into this project's defect surface for no benefit. The library is a design choice under HLR-112 and nothing about the requirements depends on it.

The demangler costs no new dependency. `__cxa_demangle` is part of the C++ runtime, which is already on the link line because `igraph` is partly C++ internally — a fact the instrumented dependency allowlist records and which this module now relies on deliberately rather than incidentally. It decodes the Itanium ABI, and therefore C++ and Rust's legacy scheme; Rust v0 decodes where the runtime is new enough and is counted as unresolved where it is not.

The image is opened **once, before discovery**, for the reason the registry is (LLR-MAIN-05): a named image that cannot be read is fatal, and it is fatal before any source file is measured rather than after a full walk whose results are then discarded.

The filter itself is not applied here. A function the image does not define is never recorded by `analyze.c` in the first place, which is what keeps the rest of the pipeline unaware that a filter exists: the graph, the analyses, the thresholds, and every renderer see a smaller set of functions and nothing else. The alternative — recording every function and filtering at each consumer — would put the same test in eleven places and let them disagree. How `analyze.c` achieves the omission — by excluding the function's bytes rather than merely its entry — is §7's decision and is argued there.

**The C++ runtime has to be named on the link line, though it was always loaded.** `libstdc++` arrives as a transitive dependency of `igraph` and has been in `ldd` output since Phase 8. That is not enough to *reference* a symbol in it: a current `ld` will not resolve an undefined symbol from an indirect `DT_NEEDED`, so `-lstdc++` is now explicit (LLR-BLD-19). The dependency allowlist is unchanged — what changed is that the entry is deliberate rather than incidental.

**The reduction has two forms it must not read as a signature, and both were found by writing it.** `operator()` is a name whose punctuation is a pair of parentheses, and `(anonymous namespace)` is a qualification that begins with one; a scan that stopped at the first top-level `(` would reduce the first to nothing and the second to nothing at all. The same applies in the other direction to the angle brackets of `operator<<`, which close nothing and would leave a backwards scan for a qualification permanently inside a template. Each is stepped over as a token rather than counted as structure (LLR-SYM-03).

**What the demangler does not do is worth stating.** GCC's clone suffixes — `f.constprop.0`, `f.isra.0`, `f.cold` — are not a mangling and are returned unchanged, which is what LLR-SYM-01 requires. An optimised build in which a static function survives only under such a name therefore reports that function absent. That is a true statement about the symbol table rather than a defect in the reader, and it is visible in the list HLR-143 requires rather than hidden in a total.

### 18.4 Dependencies

*   `libelf` for the container, and the C++ runtime's `__cxa_demangle` for the Itanium ABI. No dependency on `src/`.

### 18.5 Error Handling and Logging

*   **Image absent, unreadable, or not an object file** Diagnostic naming the path, and a fatal exit before any file is measured. The user named it, so the failure is theirs to correct (HLR-146, HLR-063).
*   **Image carries no function symbols** Fatal, and separately diagnosed. An empty set is not an empty project: filtering every function away would report a code base with none, which no reader could distinguish from a correct result (HLR-146).
*   **A linkage name this build does not decode** Counted, not fatal, and reported with the run. The completeness of the filter is stated in the way the completeness of the graph is (HLR-143, HLR-077).

## 19. Detailed Design for [src/dwarfline.c](../src/dwarfline.c)

### 19.1 Purpose and Responsibilities
[src/dwarfline.c](../src/dwarfline.c) reads the debug line information a linked image carries, where it carries any, and answers which source lines this build produced an instruction for (HLR-153).

*   Read the line programme of every compilation unit from the ELF descriptor `elfsyms.c` already holds, and from nothing else.
*   Answer, for one source file, whether the image's line information covers it at all.
*   Answer, for one line of a covered file, whether this build compiled an instruction for it.
*   Treat an image carrying no line information as an ordinary result rather than a failure, since HLR-141 forbids requiring debug information.

### 19.2 External Interfaces
#### 19.2.1 Coverage Governs Pruning

Every query is in two parts and the first governs the second: **is this file covered**, and only then **is this line within it compiled**. They are separate calls rather than one so that the distinction cannot be made by accident.

A line the mapping does not name produced no instruction — *in a file the mapping describes*. In a file it never described, absence is evidence of nothing at all. A translation unit compiled without debug information contributes no entries whatever, so a rule keyed on absence alone would find every line of it uncompiled and delete the file, leaving a report that is smaller, internally consistent, and wrong (HLR-154).

That is the asymmetry HLR-133 already draws for a conditional region `elc` could not decide and HLR-138 for a language with no dead-code query, applied to a third kind of evidence. `dwarfline_compiled` deliberately answers *false* for an uncovered file: it is the unsafe answer, and `dwarfline_covers` is what makes asking it safe.

#### 19.2.2 libdw, Never libdwfl

`dwarf_begin_elf` reads the sections of the ELF descriptor it is handed and nothing else. The `Dwfl` layer above it resolves separate debug information by `.gnu_debuglink` and build-id, which means opening a file under a separate-debug directory the user never named — forbidden outright by HLR-141.

The two live in one library, so `ldd` is identical either way and the dependency allowlist cannot see the difference. The distinction is held instead by an instrumented test that counts the image's opens for a build carrying debug information, and finds one (LLR-DWL-01).

#### 19.2.3 Path Normalisation Is Lexical

A compiler records a file name that may be relative to the unit's compilation directory and may carry `.` and `..` components. `elc`'s own paths are canonical and absolute, so the two are brought to one form before they are compared — by **lexical** normalisation, never `realpath(3)`.

`realpath` would resolve symbolic links and give a better answer for the unusual build, at the cost of stat-ing every path the image happens to name, headers under `/usr/include` among them. That is filesystem work on files the user did not name, for a module whose whole contract is that it reads the image and nothing else. Lexical normalisation keeps the answer a property of the image's bytes.

The cost is a build reaching its sources through a symbolic link: the two spellings do not meet, the file reports uncovered, and nothing in it is pruned. That is the safe direction — the count of HLR-155 says the coverage was not established, and no measured line is deleted on evidence that did not describe it (LLR-DWL-02).


### 19.3 Internal Structure
#### 19.3.1 Key Functions

*   **`int dwarfline_read(void *elf, LineCoverage *out)`** — Read the line information of an already-opened image. The handle is passed opaquely so that no consumer links a DWARF library merely to ask whether a line was compiled, which is why the SDG carries its graph object the same way. An image with no line information yields an empty set and is not a failure.
*   **`bool dwarfline_covers(const LineCoverage *c, const char *path)`** — Whether the image's line information covers this file. False for a unit compiled without debug information, for a file the mapping does not mention, and for every run with no image.
*   **`bool dwarfline_compiled(const LineCoverage *c, const char *path, uint32_t line)`** — Whether this build compiled an instruction for this line. Meaningful only where the coverage test passed for the same path.
*   **`void dwarfline_free(LineCoverage *c)`** — Release the coverage set and every path and line list it owns.

#### 19.3.2 Parsing Strategy / Algorithm

Every compilation unit's line programme is walked once. The end-of-sequence marker carries the address one past the last instruction and names no line of source; counting it would mark a line compiled on the strength of a marker rather than of an instruction, so it is skipped.

Each file's lines are then sorted and de-duplicated, because a line programme names one line once per instruction sequence attributed to it: the raw list is long and heavily repeated, and collapsing it makes membership a binary search and makes the set independent of how many sequences the compiler emitted. The file table is sorted by path for the same reason.

**The library is a design choice under HLR-112, and the argument is the one that took `libelf`.** The DWARF line-number programme is a state machine whose file and directory tables changed shape at version 5 and reach into `.debug_line_str`, and every compiler this project is aimed at now emits version 5 by default. Hand-rolling it would put a format parser into `elc`'s defect surface for no benefit. `libdw` comes from the same elfutils tree as the `libelf` already linked and is taken on the same terms (doc/notes.md §1.1).

**What the mapping cannot record is the phase's standing limit.** An optimiser may fold one source line's instructions into the entry recorded for a neighbouring line, and a line so folded is indistinguishable here from one that produced no instruction. Nothing in the image records the difference and `elc` does not attempt to recover it (HLR-154). The `elf/debugline/` fixture demonstrates it rather than describing it: at `-O0` exactly the region a `#ifdef` excluded is pruned, and at `-O2` the compiler folds a whole call to a constant and the function's body is pruned with it. The counts of HLR-155 are what let a reader judge how much of a report rests on this.

### 19.4 Dependencies

*   `libdw` for the line programme, from the elfutils tree that supplies `libelf`. Called by `src/elfsyms.c` while it holds the image open; consumed by `src/analyze.c`. No other dependency on `src/`.

### 19.5 Error Handling and Logging

*   **Image carries no debug line information** Not an error, and not a degraded run. HLR-141 forbids *requiring* debug information, so the set comes back empty, every file is uncovered, nothing is pruned, and the reported metrics are those the same run reports at function granularity alone (HLR-153).
*   **A compilation unit with no line programme** Contributes nothing and is not an error. Its files stay uncovered and are counted under HLR-155, which is the whole of what HLR-154 asks for.
*   **Allocation failure** The only failure this module reports. The partially built set is released and the caller turns it into a fatal exit, since a coverage set built halfway would prune on evidence it does not have.

## 20. Detailed Design for [src/purify.c](../src/purify.c)

### 20.1 Purpose and Responsibilities
[src/purify.c](../src/purify.c) builds the *recovery view* of the graph: a masked copy in which the utility sinks, god objects, and peripheral nodes that fuse unrelated domains are set aside, so that a layering can be read off what remains. It also owns the manifest by which a user overrules its classifications.

*   Compute the hub-and-authority and betweenness centralities, and the coreness, of the call view.
*   Classify each function as a utility sink, a god object, peripheral, or ordinary, against configurable thresholds that are `elc`'s own (HLR-171).
*   Produce the masked recovery view, leaving the graph every other analysis reads untouched (HLR-167).
*   Read a manifest where one was named, letting its statements overrule the computed classification, and write one on request (HLR-175 – HLR-177).
*   Record every classification, with the metric and value that produced it, for the report of HLR-174.

### 20.2 External Interfaces
#### 20.2.1 The Recovery View Is a Second Graph, Not an Edit

Masking produces a **copy** of the call view with edges removed; the `Sdg` every other stage reads is not modified. This is HLR-167 made structural rather than remembered: a stage that cannot reach a masked graph cannot accidentally measure one, and the alternative — masking in place and unmasking afterwards — makes every analysis order-dependent and one early return away from reporting a fan-out that omits real calls.

The copy is of the **call view** alone. Global-state edges take no part in a layering: writing an object another function reads is coupling and not invocation (LLR-CTR-07), and including them would join every pair of functions sharing a variable into the layer structure.

#### 20.2.2 What Each Classification Masks

| Class | Trigger | Masked |
| ----- | ------- | ------ |
| Utility sink | high authority, near-zero hub | its **incoming** edges (HLR-168) |
| God object | high betweenness *and* high hub | **all** its edges (HLR-169) |
| Peripheral | coreness below the configured depth | the node, excluded from the view (HLR-170) |

A utility sink keeps its outgoing edges because the fusion it causes is between its *callers*; a god object loses both directions because it short-circuits in both. A function meeting both tests is a god object, the stronger and more useful claim (HLR-169).

#### 20.2.3 The Manifest Format, and What It Costs

The manifest is **JSON**: an object carrying a format version and one array of statements, each naming a function, its file, its class, and whether that class is masked. Two properties of the requirement decide the format between them: HLR-175 requires a user be able to edit it by hand, and HLR-176 requires `elc` read it back.

The version is why the top level is an object rather than the bare array this section first described. A version has to live somewhere a reader meets before the statements, and an array has no such place; the alternative — versioning by a magic first element — is a shape nobody would hand-edit correctly, which is the one property the format was chosen for.

```json
{
  "manifest-version": 1,
  "classifications": [
    { "function": "dispatch", "file": "/p/app/dispatch.c",
      "class": "god object", "mask": true }
  ]
}
```

**The class and the masking action are two facts, and the manifest states both.** The usual correction is not that `elc` misread the graph but that it drew the wrong conclusion from a correct reading: a user agrees their dispatcher is the graph's dispatcher and disagrees that it should be set aside. Setting `mask` to `false` keeps the classification in the transparency report — where it still tells a reader where the function sits — and keeps the function's edges in the recovery view. A format carrying only the class would force such a user to relabel the function as something it is not.

`file` is optional. Naming it is the precise form and is what `elc` writes; omitting it matches the function wherever it is defined, which is what a person adding a statement by hand will reach for. A project with two static functions of one name is the case that makes the distinction earn its keep.

Being read back is what makes this different from every other artefact `elc` emits. The `.dot`, GraphML, CSV and XML *writers* are hand-rolled precisely because emission needs only correct escaping (SDD §16.3.2) — but a format `elc` must also parse needs a parser, and the project has exactly one, Expat, which reads XML alone. So the choice is between reusing XML and taking on a reader for something else.

JSON is chosen over both, and the reason is the audience rather than the engineering: this file exists to be edited by a person who disagrees with a classification, and of the candidates it is the one they are most likely to edit correctly.

**Both directions go through Jansson** (≥ 2.14), the JSON library selected under HLR-112 in the dependency-selection table of the data dictionary. This is the one place `elc` uses a library to *write* a format rather than hand-rolling emission, and the exception is argued in full beside that selection: the manifest is the only artefact that round-trips, so a hand-rolled writer paired with a library reader would be two implementations of one format with `elc` on both ends of the disagreement.

What the module owes the library is bounded. Jansson parses and validates; `purify.c` maps the resulting values onto classifications and rejects anything it does not recognise. A manifest that is well-formed JSON but not a manifest — a missing class, a class name this build does not know, a version it does not read — is rejected exactly as a malformed one is (HLR-176), because well-formedness is a property of the syntax and this module is judging the contents.

The format is versioned in the manner of the XML record (HLR-061), so that a manifest written by a later build is rejected by an earlier one rather than half-understood. Jansson's `json_error_t` carries the line, column, and byte offset of a syntax fault, and the diagnostic quotes them: a person who hand-edited the file needs to be told where they broke it, not merely that they did.


### 20.3 Internal Structure
#### 20.3.1 Key Functions

*   **`int purify_analyse(const Sdg *g, const ElcOptions *opts, Manifest *manifest, PurifyResults *out)`** — Classify every function and build the masked recovery view. `g` is taken by const pointer, which is where HLR-167 is enforced rather than remembered; the manifest of HLR-175 – HLR-177 enters as a further argument, non-const because a statement records whether it named anything.
*   **`int classify_nodes(const Sdg *g, const PurifyThresholds *t, Manifest *manifest, Classification *out)`** — Assign each node its class against the thresholds in force, applying the manifest last so that a statement overrules a computed class rather than competing with one.
*   **`int build_recovery_view(const Sdg *g, const Classification *c, RecoveryView *out)`** — Copy the call view, omitting the masked edges and the peripheral nodes.
*   **`bool purify_edge_retained(const Classification *c, uint32_t from, uint32_t to)`** — Whether the view keeps one call edge. Exposed so that the purified drawing of HLR-178 asks the same question the view was built from rather than reimplementing the three masking rules.
*   **`int purify_score_cmp(double a, double b)`** — Compare two scores to the tolerance HLR-179 requires be stated, returning -1, 0, or 1.
*   **`int report_set_purify(Report *report, const PurifyResults *purify, const Sdg *g, const ElcOptions *opts)`** — Copy the classifications onto an assembled report, resolving each node identifier to the name and location a reader can act on, and recording whether each class was computed or supplied (HLR-174, HLR-177).
*   **`bool purify_class_from_name(const char *name, PurifyClass *out)`** — The inverse of `purify_class_name`, for the manifest read path. A name this build does not know is refused rather than guessed at.
*   **`int manifest_read(const char *path, Manifest *out)`** — Parse a named manifest; reject a malformed one, and one that is JSON but not a manifest, rather than partially applying either (HLR-176).
*   **`int manifest_write(const PurifyResults *r, const Sdg *g, const char *path)`** — Write the classifications in the documented format, ready to be edited and handed back. The `Sdg` is needed and the results alone are not: a classification is held against a node identifier, and an identifier is not something a person can edit.
*   **`void manifest_free(Manifest *m)`** — Release a manifest's statements.
*   **`void purify_results_free(PurifyResults *r)`** — Release the classifications and the recovery view.

#### 20.3.2 Parsing Strategy / Algorithm

**The thresholds are compared against a ranking, not against a raw score.** A betweenness value means nothing on its own — it scales with the size of the graph, so a fixed number would classify every function in a large project and none in a small one. Classification is therefore made against a node's position in the ordered distribution of the score, which is comparable across projects and is what makes one default threshold serviceable for both.

A rank is expressed as the percentage of the **other** nodes scoring strictly below, so the top of any distribution is 100 whatever the size of the graph. Over *all* the nodes it would be 8 of 9 for the highest in a nine-function tree, and no threshold above 89 could ever be met there — one default would then be unusable on small projects and unusably loose on large ones, which is the failure ranking exists to avoid. The comparison itself is made in integers, `below × 100 ≥ percent × (n − 1)`, so the boundary is exact and the only floating-point comparison purification makes is the tolerance below.

**Ties are broken by node identifier** (HLR-179). Two functions with equal scores must classify the same way on every run, and the graph library's enumeration order is not a property of the source tree.

**Floating-point comparison is defined rather than left to the compiler.** HITS is iterative and its scores are approximations, so a comparison at the threshold boundary must be made to a stated tolerance; without one, the same source classifies differently on two machines and HLR-032 fails in a way no fixture would reliably catch.

The two are applied in that order and not the other way about. The ordering is built on an **exact** comparison with the node identifier breaking equal scores, because a sort whose comparator is tolerant rests on a relation that is not transitive, and its result would then depend on the order the library's sort happened to visit the elements in — the very property HLR-179 exists to remove. The tolerance is applied afterwards, over the exact order, where it merges neighbours into one position; a run of equal scores is delimited by comparing each member against the run's *first* element rather than against its predecessor, so the grouping is a property of the sorted order rather than of how far a chain of near-equal neighbours happens to reach.

**Precedence between the classes is fixed** (HLR-169, HLR-170). A god object is decided first, then a utility sink, then a peripheral node. The first two are statements about a function's part in *fusing* domains and each carries a masking action; the third is the residual statement that a function is not part of the mutually connected centre, and a function the centrality tests already named is by construction part of it.

**Coreness is taken over the undirected neighbourhood.** A *k*-core is the mutually connected centre of a program, and a leaf hanging off it is peripheral whichever way its one edge points (HLR-170).

**A manifest statement is applied last, and that is what makes it govern** (HLR-177). Every computed class is in place before the manifest is read over it, so "the statement governs" is a property of the order rather than of a condition scattered through the three tests. The metric and value that would have justified a computed class are cleared with it: no measurement triggered a decision the user made, and reporting one beside a manifest row would present a number as the reason for a judgement it had no part in.

**A statement matching no analysed function is recorded, reported, and ignored.** Analysing one directory of a project whose manifest covers all of it is ordinary use, and rejecting the file there would make a manifest unusable exactly where a large code base most needs one — the rule a declared entry point matching nothing already follows (LLR-CTR-08). It is reported rather than passed over in silence because such a statement is far more often a typo than a deliberate partial run, and a user who never hears about it goes on believing their correction took effect.

**A cyclic recovery view has no layering, and that is reported rather than worked around** (HLR-172). Purification often breaks the cycles that a god object created, which is much of its purpose — but where cycles remain, the cycles are the finding.

### 20.4 Dependencies

*   The graph library, for `igraph_hub_and_authority_scores`, `igraph_betweenness`, and `igraph_coreness` (HLR-113).
*   **Jansson**, for the manifest in both directions — `json_load_file` and `json_error_t` on the read path, `json_dumpf` on the write path. The only third-party writer in the project, for the round-trip reason argued beside the dependency-selection table in the data dictionary. The write goes through a stream of `elc`'s own rather than `json_dump_file`, for one byte: a text file this project writes ends with a newline and Jansson's file writer does not add one, and a manifest is meant to be hand-edited and kept under version control.
*   `src/graph.c` for the SDG and its call view. Depended upon by `src/recover.c`.

### 20.5 Error Handling and Logging

*   **Manifest cannot be read or parsed** Diagnostic naming the path, and a fatal exit. The user named the file, so the failure is theirs to correct (HLR-176).
*   **Manifest names an unknown function** Diagnostic, the statement ignored, the run continues. Analysing one directory of a project whose manifest covers all of it is ordinary use (HLR-177).
*   **Manifest is JSON but not a manifest** Diagnostic naming the path and what is wrong with it, and a fatal exit — exactly as a syntax fault is. Well-formedness is a property of the syntax and this module is judging the contents: a missing or unreadable version, a missing classifications array, and a class name this build does not know are each a file that is not a manifest (HLR-176).
*   **Manifest keeps a classified function in the view** Not an error and not an omission. The classification is reported with its action given as retained rather than masked, and the recovery view keeps the function's edges. The usual correction is not that `elc` misread the graph but that it drew the wrong conclusion from a correct reading (HLR-175, HLR-177).
*   **No functions survive purification** Not an error. The recovery of HLR-172 is omitted with its reason stated, as an analysis short of its inputs always is (HLR-115).
*   **A graph with fewer than two nodes** Not an error, and nothing is classified by centrality. There is no distribution to hold a position in, and a rank over zero other nodes is met by every threshold at once — which would classify the single function as all three at the boundary. The coreness test still applies, since it is absolute.
*   **A call view with no edges** The hub-and-authority decomposition is not asked for. It is undefined there — every score is zero — and the library reports the fact on standard error, which would put a diagnostic naming one of its own source files into the stream HLR-038 reserves for `elc`'s. A program whose functions call nothing has no hub-and-authority structure to find, and leaving the scores at their zero says exactly that.

## 21. Detailed Design for [src/recover.c](../src/recover.c)

### 21.1 Purpose and Responsibilities
[src/recover.c](../src/recover.c) reads a proposed layering off the purified recovery view, for a user who has declared no architecture and wants to know what one their code already has.

*   Order the recovery view topologically, and report the cycles instead where no ordering exists (HLR-172).
*   Group the ordered functions into layers by the directory owning each component (HLR-160).
*   Present the proposal in a form a user can adopt as a declaration without transcribing it (HLR-173).
*   State which functions were masked or excluded in producing the proposal.

### 21.2 External Interfaces
#### 21.2.1 The Boundary Is the Dependency Direction

`arch.c` includes no header of this module, holds no `RecoveryResults`, and is handed no path to one. That is HLR-173 made structural rather than remembered, by the same construction that makes HLR-167 structural one section above: a module that cannot reach a proposal cannot accidentally measure against it, and a rule kept by remembering is a rule one refactor away from being forgotten.

The results travel to the report and stop there. Renderers and the saved record read them; no analysis takes an input from them. `elc` measuring conformance against its own proposal would be a tool marking its own homework — every code base would conform, because the standard would have been read off the thing it was judging.

The corollary is the one a reader is most likely to find surprising, and it is deliberate: a run over a plainly layered tree recovers that layering with complete confidence **and** still reports the conformance analyses as omitted for want of a declaration (HLR-115). Recovery is what a user without strata is *given*; it is not a substitute baseline.

#### 21.2.2 The Proposal Is an Argument List

What this module emits is the command line that would declare the layering, in the form `--stratum` and `--stratum-order` accept:

```text
--stratum app:'/p/app/*' --stratum svc:'/p/svc/*' --stratum-order 'app>svc'
```

Two properties of that line are load-bearing rather than cosmetic.

**It is quoted.** The patterns hold a `*` and the order holds `>`. An unquoted order would not merely fail to be adopted — a shell would read it as a redirection, create files named after the layers, and hand `elc` a partial order it rejects. A proposal that has to be repaired before it can be used is a transcription, which is the thing HLR-173 asks be avoided.

**The declarations are ordered by directory depth, deepest first, and the layer order is stated separately.** `stratum_of_components` takes the *first* declared layer whose pattern matches a file, and a directory wildcard matches everything beneath that directory rather than only the files directly in it — so an ancestor declared before its child would claim the child's files. Declaring the deepest first removes that and costs nothing, because a layer's ordinal comes from `--stratum-order` beside it and not from the order the declarations appear in.

Each layer is named after the basename of the first directory in it, sanitised of anything a shell or the option syntax would take for punctuation, and suffixed on a collision — two layers sharing a name would silently become one, since repeating a name adds patterns to the layer already declared.


### 21.3 Internal Structure
#### 21.3.1 Key Functions

*   **`int recover_layers(const PurifyResults *p, const Sdg *g, const Report *r, RecoveryResults *out)`** — Propose a layering, or report why none could be. The `Sdg` supplies each function's component and the `Report` supplies that component's directory, recorded once at discovery rather than re-derived here (HLR-160).
*   **`int layer_by_directory(const PurifyResults *p, const Sdg *g, const Report *r, const size_t *order, RecoveryResults *out)`** — Fold a topological order of the view into per-directory layers by edge density. Exposed because the fold — and not the ordering — is the whole of what this module decides.
*   **`int report_set_recovery(Report *report, const RecoveryResults *rec)`** — Copy the proposal onto an assembled report. Declared in `recover.h` rather than `report.h`, exactly as `report_set_purify` is, so the report model need not know what a `RecoveryResults` is to be included.
*   **`void recovery_results_free(RecoveryResults *r)`** — Release the proposal, its cycles, and its argument list.

#### 21.3.2 Parsing Strategy / Algorithm

**A topological order is not yet a layering.** It orders functions; an architecture orders *directories*. The order is therefore folded by component directory, and a directory's layer is fixed by where the bulk of its edges point rather than by its earliest or latest member — one function reaching far down the order should not drag its whole directory with it.

**"Where the bulk of its edges point" is the mean position of a directory's functions weighted by the retained edges each carries.** A function holding one edge counts once against the ten held by the rest, so an outlier moves a directory in proportion to how much of the directory's coupling it actually accounts for. The case this is written against is ordinary rather than contrived: a completion callback defined in a service layer and called from the hardware layer beneath it sits at the very bottom of the topological order, and a fold reading a directory's latest member would turn the service layer upside down on the strength of it. A directory all of whose functions are isolated in the view falls back to the unweighted mean, since a weighted mean over a total weight of zero is not a number.

**The positions are compared as exact fractions, never as floating point.** Two directories whose weighted means are equal belong in one layer, and a comparison deciding otherwise on the last bits of a division would split them on one machine and not on another — the property HLR-179 exists to remove. Cross-multiplying is the obvious way to compare two fractions exactly and overflows for a large project, since each numerator is a sum of position × degree over every retained function; the comparison is therefore made by continued fraction, which compares integer parts and then the reciprocals of the remainders and multiplies nothing. Directories at equal positions share a layer: cutting between them would invent a dependency direction the graph does not hold, which is the kind of claim HLR-101 forbids. Ties in the *ordering* break by directory path, so two runs list them alike.

**An excluded function is not folded in at all** (HLR-170). The fold walks the vertices the view still contains, and a directory all of whose functions were excluded receives no layer rather than the bottom one. A fold that read the excluded vertices back in would put every leaf in the lowest layer, which is the one thing that requirement names.

**The proposal is emitted as a declaration.** HLR-173 requires that a user who agrees with the recovered layering be able to adopt it without retyping, so the proposal is rendered in the form the `--stratum` and `--stratum-order` options accept. That is also the boundary the requirement draws made visible: what `elc` produces is an *argument list*, and it takes effect only when a user passes it back.

**A self-call orders nothing, and is disregarded.** The ordering runs over the view with its loops removed. A function calling itself makes the graph cyclic in the strict sense, but the edge runs from a node to itself and says nothing about where that node sits relative to any other; reporting it in place of a layering would repeat a fact the recursion analysis of HLR-089 already states, and would cost every project holding one recursive function the whole of this analysis. It does not weight the function's position either, for the same reason: a function coupled to nothing but itself had no part in choosing where its directory sits. A *mutual* cycle is a different claim and is still reported — there is genuinely no order to read.

**A cycle is reported as its membership, not as a chain of arrows.** Where the recovery view is cyclic the strongly connected components of two or more members are listed in place of a layering, each as its members in ascending node identifier. A strongly connected component is a *set*: every member reaches every other, but the decomposition yields no order, and `a -> b -> c` would assert a path that may not exist. This is the rule the recursion report already follows, and the true statement is the useful one — breaking any edge among these functions is what would make a layering possible.

**Nothing here feeds the conformance analyses** (HLR-173). `recover.c` is depended upon by the report and by nothing in `arch.c`; the dependency direction is what keeps a recovered layering from becoming the baseline it is measured against.

### 21.4 Dependencies

*   The graph library, for the acyclicity test, the topological ordering, and the strongly connected components where no ordering exists. `src/purify.c` for the recovery view and the classifications behind it.
*   `src/graph.c` for each function's component, and `src/report.c` for that component's directory — read from the report's own record of it rather than sliced off the path here, since more than one analysis groups by directory and two consumers each slicing a path for themselves is how two of them come to disagree (HLR-160).

### 21.5 Error Handling and Logging

*   **Recovery view is cyclic** Not an error. The mutually reachable groups are reported in place of a proposed layering, as HLR-090 does for call depth over a cyclic graph (HLR-172).
*   **A function calls itself** Not a cycle for this purpose, and not reported here. The edge orders nothing, the recursion analysis of HLR-089 already states the fact, and treating it as blocking would cost every project holding one recursive function the whole of this analysis.
*   **No strata declared** Not a reason to omit recovery — recovery is what a user without strata is given. It is the *conformance* analyses that stay omitted (HLR-115, HLR-173).
*   **No function survives purification** Not an error. The proposal is omitted with its reason stated, as an analysis short of its inputs always is, rather than reported as an empty layering (HLR-115).
*   **A directory holds only excluded functions** Not an error and not the bottom layer. The directory receives no layer at all, because a function `elc` did not consider is not a function `elc` placed at the edge of the architecture (HLR-170).

## 22. Detailed Design for [src/format_dsm.c](../src/format_dsm.c)

### 22.1 Purpose and Responsibilities
[src/format_dsm.c](../src/format_dsm.c) renders the Dependency Structure Matrix — the square grid whose cells carry the call counts between layers or directories, and whose diagonal separates conforming dependencies from back-calls.

*   Build the matrix over the declared layers, or over the analysed directories where no strata were declared (HLR-165).
*   Order rows and columns identically, by layer index or by path, so a cell's position carries its meaning (HLR-166).
*   Render as CSV, as Markdown, and as the report's aligned table, and state the diagonal convention wherever it renders (HLR-166).
*   Decide whether the CSV companion is warranted for a run: on request, and only where the report has a path to derive a name from (HLR-180).


### 22.3 Internal Structure
#### 22.3.1 Key Functions

*   **`int dsm_build(const Sdg *g, const Report *r, const ElcOptions *opts, Dsm *out)`** — Populate the square matrix of call counts between subjects, in their defined order.
*   **`int format_dsm_csv(const Dsm *m, FILE *out)`** — Render the matrix as CSV, every cell through the RFC 4180 field writer.
*   **`int format_dsm_markdown(const Dsm *m, FILE *out)`** — Render the matrix as a GitHub-Flavored Markdown table, escaping the cell separator.
*   **`int format_dsm_table(const Dsm *m, FILE *out)`** — Render the matrix as the report's aligned table, which is what keeps the tier from being present in one human-facing format and absent from the other (HLR-031).
*   **`bool dsm_warranted(const ElcOptions *opts)`** — Whether the CSV companion is to be written: requested, and with a named output path to derive its own from (HLR-104, HLR-180).
*   **`void dsm_free(Dsm *m)`** — Release the matrix and the subject labels it owns.

#### 22.3.2 Parsing Strategy / Algorithm

**Rows are callers and columns are callees**, both in the same ascending order, so the diagonal has a meaning a reader can rely on: above it are dependencies running the declared way, on it are dependencies inside one subject, and below it are the back-calls of HLR-162. A matrix whose orientation the reader must infer is worse than no matrix, so the convention is printed with it (HLR-166).

**The matrix is dense and its subjects are few.** Rows and columns are layers or directories rather than files or functions, so the grid stays readable at the size an architecture actually has; a per-function DSM of a real project is a matrix nobody can look at.

**Escaping is not this module's own.** The CSV rendering emits every cell through the same `write_field` the per-function renderer uses, and the Markdown rendering escapes the cell separator, so a directory containing a comma or a pipe cannot corrupt the grid (HLR-064). Markdown column widths are measured *after* escaping, since a column measured on the raw text comes out a character short for every pipe in it and the raw document goes ragged.

**Three decorations, one walk.** The three renderings are thin wrappers over a single traversal of the grid, for the reason format_text.c's tiers share one traversal: three walks of one matrix is how two of them come to disagree about what is in it. The aligned table exists beside the Markdown because the matrix is a tier of the human-facing report, and HLR-031 does not allow a tier present in one of those formats and absent from the other; the CSV is the machine-readable copy HLR-180 writes beside the report. Each begins with the convention — a leading single-field record in CSV, which has no comment syntax, and a caption line under the heading in the other two — and each labels the corner cell `caller \ callee`, so the orientation is stated in the place a reader looks first as well as in the sentence above it.

**The layer assignment is `arch.c`'s.** `dsm_build` calls `stratum_of_components` rather than matching the stratum patterns itself. Two matchers over one set of patterns would eventually disagree about which layer a file is in, and the matrix's below-diagonal cells would stop accounting for the back-calls listed beside them — the failure HLR-164 forbids for the indices, arriving instead through the grid.

**The subject sequence is sorted here, and that is one of the two exceptions to `report.c` owning every sort.** It is the same exception the graph writers take (LLR-DOT-04): the order is a property of this artefact rather than of a collection the model already holds. Sorting is genuinely required rather than merely tidy — the components arrive in ascending *path* order, which is not ascending *directory* order. `/p/a-b/x.c` sorts before `/p/a/y.c` because `-` precedes `/`, yet `/p/a` precedes `/p/a-b`, so reading the directories off in component order would produce a sequence no rule describes. Under declared strata no sort is needed at all: a stratum's ordinal *is* its position, so each label is placed at its ordinal rather than appended in the order the options were parsed — which is what keeps `--stratum-order` from moving the back-calls above the diagonal.

**The matrix is part of the report model, not a renderer's scratch space.** It is built once, in `main`, and carried on the `Report`, because the record of a run must be able to regenerate it and a record carries no call graph to rebuild it from (HLR-054). That is also why the CSV companion is the one companion available in regeneration mode.

### 22.4 Dependencies

*   `src/graph.c` for the component projection, `src/arch.c` for the layer assignment, and `src/format_csv.c` for the field writer. No third-party dependency.

### 22.5 Error Handling and Logging

*   **No strata declared** Not an error. The matrix is built over directories instead, so a reader with no declared architecture still receives one (HLR-165).
*   **Write failure** Diagnostic and non-zero return, as for every other renderer.
*   **No output path** Not an error. `dsm_warranted` is false, no companion is written, and the report itself still carries the matrix — the rule the GraphML export follows, since the companion's name is derived from the report's and there is none to derive from (HLR-104).
*   **An empty matrix** Renders as its heading, its convention, and its column names rather than as nothing. A section that vanishes when it has no content makes the report's shape vary with its content.
## 23. Data Dictionary

*   **`ElcOptions`** (defined in [include/elc.h](../include/elc.h)) — The complete, validated configuration of one run. Populated only by cli.c and read-only thereafter.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `mode` | `RunMode` | Analysis or XML regeneration; regeneration requires format md and suppresses both *graph* companion artefacts, the matrix companion excepted (HLR-055, HLR-180) |
| `format` | `OutputFormat` | table, csv, xml, or md. Settled once, after the option loop, from `--format` and the extension of `output_path` together, so the two can be compared rather than one preferred (HLR-148, HLR-149) |
| `output_path` | `const char *` | NULL when writing to stdout. Where it is not NULL its extension names the format, so the field carries a second meaning beyond the destination (HLR-148) |
| `verbose` | `bool` | The *request* for the verbose report, not the selection between two compositions: the summary is the default (HLR-150), so a zeroed ElcOptions must mean the summary, which it does only if the flag records the positive. A property of the rendering alone — it changes no measurement, no finding, and not the exit status, and the complete-record formats ignore it (HLR-151, HLR-152) |
| `complexity_threshold` | `uint32_t` | Default 15 (HLR-022) |
| `bottleneck_threshold` | `uint32_t` | Default 5 (HLR-081) |
| `manifest_path` | `const char *` | The purification manifest to read, or NULL. Borrowed from argv, and the only way a manifest is reached: nothing is discovered from the working directory, the target, an ancestor of either, or a dotfile (HLR-039, HLR-176) |
| `write_manifest` | `bool` | Write the manifest beside the report (HLR-175). Off unless asked for, and silently nothing with the report on standard output, by the companion rule of HLR-119 |
| `purify_dot` | `bool` | Write the raw and purified drawings beside the report (HLR-178). One flag for the pair, because a single drawing of the recovery view cannot show what purification acted on |
| `no_dot` | `bool` | The *refusal* of the `.dot` companion, not the request for it: generation is enabled by default (HLR-103), so a zeroed ElcOptions must mean enabled, which it does only if the flag records the negative |
| `graphml_path` | `const char *` | NULL unless --graphml given |
| `dsm` | `bool` | The *request* for the dependency-matrix CSV companion (HLR-180). Off unless asked for, as the GraphML export is, so a zeroed ElcOptions means no companion; and silently nothing where the report goes to standard output, there being no path to derive a name from (HLR-104). Unlike the two graph companions it is accepted with `--from-xml`, because a record carries the matrix where it carries no topology |
| `strata` | `StratumList` | Empty when undeclared; ordinals from declaration order unless --stratum-order states them (HLR-078) |
| `stratum_order` | `const char *` | The declared dependency direction, resolved after parsing so it may precede the layers it orders (HLR-078) |
| `entry_points` | `SymbolList` | Empty when undeclared (HLR-095) |
| `scopes` | `ScopeList` | Empty when undeclared (HLR-094) |
| `rules` | `const char **` | Custom rule arguments in the `lang:path` form, borrowed from argv and left unsplit: the language and the path are both substrings of one argument, and splitting in the parser would allocate two strings for a decision `registry.c` has to make anyway — it is the module that knows which languages exist (HLR-107) |
| `defines` | `const char **` | Conditional-compilation symbols as given, `NAME` or `NAME=VALUE`, borrowed from argv. Empty when none supplied, and an empty set prunes nothing — not as a special case but because every definedness test is then undecidable, there being no way to assert that a symbol is *un*defined (HLR-131, HLR-133) |
| `image_path` | `const char *` | The linked image to filter functions by, or NULL for no filtering. Borrowed from argv. The path only: the image is read by `elfsyms.c`, which owns the failure, so a run with no image differs from a filtered one in exactly one place (HLR-140) |
| `targets` | `PathList` | One or more file or directory arguments (HLR-071) |
*   **`FileList`** (defined in [include/discover.h](../include/discover.h)) — The discovered files: canonical absolute paths, each appearing exactly once, in ascending byte order. Owns every path it holds.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `paths` | `char **` | Dynamic array, grown by doubling |
| `count` | `size_t` | Populated entries |
| `capacity` | `size_t` | Allocated entries |
*   **`ExtensionList`** (defined in [include/discover.h](../include/discover.h)) — The binary-extension exclusion list read from runtime data (HLR-005). Passed explicitly to every function that consults it rather than held in a global.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `exts` | `char **` | Each including its leading dot; matched case-insensitively |
| `count` | `size_t` | Populated entries |
| `capacity` | `size_t` | Allocated entries |
*   **`Registry`** (defined in [include/registry.h](../include/registry.h)) — Everything loaded from the runtime location, plus the parser and cursor reused across the whole run.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `dir` | `char *` | The resolved runtime location; handed to any other module needing runtime data, so HLR-059's precedence rule exists once |
| `rules` | `CustomRule *` | Every compiled custom rule, from either provenance. Owned, and released with the built-in queries rather than after them: a rule's query points into a grammar exactly as a built-in one's does and is subject to the same teardown ordering (LLR-RCL-01) |
| `map` | `ExtensionMapping *` | Extension to language, from runtime data (HLR-060) |
| `modules` | `LanguageModule *` | Loaded languages, cached after first use |
| `parser` | `TSParser *` | One for the whole run; reuse needs only ts_parser_set_language() per file |
| `cursor` | `TSQueryCursor *` | One for the whole run, for the same reason |
*   **`ExtensionMapping`** (defined in [include/registry.h](../include/registry.h)) — One extension-to-language association read from runtime data.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `extension` | `char *` | Including its leading period; matched without regard to case |
| `language` | `char *` | Names the parser and query directory to load |
*   **`LanguageModule`** (defined in [include/elc.h](../include/elc.h)) — One dynamically loaded language, cached by the registry after first use.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `language_name` | `char *` | Resolved from the extension map |
| `dl_handle` | `void *` | Handle from dlopen(); closed last at teardown |
| `ts_lang` | `const TSLanguage *` | Resolved grammar entry point |
| `queries` | `TSQuery *[]` | Compiled comments, functions, complexity, eloc, calls, and globals queries |
| `usable` | `bool` | False once a load failure has been reported, to avoid retrying (HLR-070) |
*   **`FunctionMetric`** (defined in [include/elc.h](../include/elc.h)) — The metrics for one reported function, including nested named functions.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `name` | `char *` | Copied out of the mapping before it is released |
| `start_line` | `uint32_t` | 1-based; TSPoint.row is 0-based and converted once |
| `end_line` | `uint32_t` | 1-based |
| `eloc` | `uint32_t` | Executable statements attributed to this function only (HLR-068) |
| `complexity` | `uint32_t` | 1 + decision points |
| `node_id` | `uint32_t` | Index of this function's SDG node |
*   **`FileMetrics`** (defined in [include/elc.h](../include/elc.h)) — Per-file totals and the functions the file defines.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `path` | `char *` | Canonical absolute path |
| `directory` | `char *` | The directory containing the file, derived once from the path and owned. A component *is* a file, so this is the directory a component belongs to; recorded rather than recomputed because more than one analysis groups by it, and two consumers each slicing the path for themselves is how two of them come to disagree. No trailing separator, and "/" for a file at the root, so that two files in one directory compare equal (HLR-160) |
| `language` | `const char *` | Borrowed from the language module |
| `physical_lines` | `uint32_t` | Newline count from the mapping |
| `unparsed_lines` | `uint32_t` | Distinct lines the grammar could not follow; non-zero means every other figure covers the rest of the file and not this part (HLR-035) |
| `undecided_regions` | `uint32_t` | Conditional regions left active because their condition could not be decided. Reported for the reason the unresolved-call count is: a figure whose completeness is unstated cannot be acted on (HLR-133) |
| `eloc` | `uint32_t` | File-level ELOC including code outside any function |
| `scope_eloc` | `uint32_t` | The part of that total belonging to no function. Always measured; reported only where a filter is in force, the image's function set saying nothing about code that is not a function (HLR-145) |
| `functions` | `FunctionMetric *` | Dynamic array, grown by doubling |
| `function_count` | `size_t` | Populated entries |
| `absent` | `AbsentFunction *` | The functions this file defines that the image does not, in the order the parse found them. Empty where no image was supplied (HLR-143) |
| `absent_count` | `size_t` | Populated entries |
*   **`AbsentFunction`** (defined in [include/elc.h](../include/elc.h)) — One function the source defines and the linked image does not, as the parse recorded it. Named and located, because the reader's next action is to open the file (HLR-143).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `name` | `char *` | As the source writes it, copied out of the mapping before it is released |
| `line` | `uint32_t` | 1-based, where the definition starts |
*   **`LineCoverage`** (defined in [include/dwarfline.h](../include/dwarfline.h)) — Every source file the image's debug line information covers, and the lines within each that produced at least one instruction (HLR-153).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `files` | `CoveredFile *` | Sorted by path; owned. Each holds the file's path as the image records it, normalised lexically, and an ascending de-duplicated line list — a line programme names one line once per instruction sequence, so the raw list is long and heavily repeated |
| `count` | `size_t` | Files covered |
| `present` | `bool` | Whether the image carried line information at all. Distinguishes a build made without debug information from one whose debug information describes other code: both prune nothing, and only the second says anything about the target |
*   **`SymbolSet`** (defined in [include/elfsyms.h](../include/elfsyms.h)) — The function set one image defines, resolved to source names. Sorted and de-duplicated on the resolved name, so membership is a binary search and no property of symbol-table order can reach the output (HLR-032, LLR-ELF-05).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `names` | `char **` | Resolved source names, sorted and unique; owned |
| `count` | `size_t` | Populated entries |
| `unresolved` | `size_t` | Function symbols whose linkage name carried a mangling this build does not decode. Counted rather than guessed at, and reported with the run: a filter whose completeness is unstated cannot be acted on (HLR-143) |
| `path` | `char *` | The image as the user named it, so the report can say which image it describes without the options having to outlive the run (HLR-147) |
| `lines` | `LineCoverage` | The finer granularity, read from the same open as the symbols and empty where the build wrote no debug information. Here rather than in a structure of its own because it must come from the same *open*: the image is read once and nothing beside it, and a second module opening it again would break that while every unit test still passed (HLR-141, HLR-153) |
*   **`AbsentRow`** (defined in [include/report.h](../include/report.h)) — One function the image does not define, as the report presents it. Structurally an UnreachableRow and deliberately not one: both name a function no build needs, and they are established by different means — one inferred from the call graph, the other observed from what the linker did — so merging them would present an observation as an inference (HLR-143).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `function` | `char *` | Owned |
| `file` | `char *` | Owned |
| `line` | `uint32_t` | 1-based |
*   **`FileFacts`** (defined in [include/elc.h](../include/elc.h)) — The raw graph facts extracted during the same parse that produced FileMetrics.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `calls` | `CallSite *` | Call sites with their enclosing function and callee name |
| `globals` | `GlobalAccess *` | Global reads and writes with their enclosing function |
| `rule_matches` | `RuleMatch *` | Custom rule matches with rule identity and line range, recorded during the same parse the metrics come from (HLR-109) |
| `dead` | `DeadSpan *` | Statements within a function that cannot execute (HLR-137) |
| `dead_analysed` | `bool` | False when the language supplied no dead-code query, so that "not looked for" is distinguishable from "none found" (HLR-139) |
*   **`CustomRule`** (defined in [include/registry.h](../include/registry.h)) — One compiled user-supplied rule, bound to the language it applies to. The binding is not decoration: a query compiles against one specific TSLanguage and has no meaning apart from it. By the time a rule is here the two provenances are indistinguishable, which is the point — what a rule does cannot depend on how it arrived.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `stem` | `char *` | The file's basename with its extension removed — half an identity; the capture name that matched supplies the other half (HLR-109) |
| `language` | `char *` | The language it is bound to, by the directory holding it or by the `lang:path` argument |
| `query` | `TSQuery *` | Compiled against that language's grammar, and deleted with the built-in queries rather than after them |
*   **`RuleMatch`** (defined in [include/elc.h](../include/elc.h)) — One match of a user-supplied rule, as the parse recorded it. Line-ranged and nothing else: a match is not a finding, so there is no severity here and no attribution — nothing to attach either to (HLR-111).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `rule` | `char *` | "<basename>.<capture>", so one file expresses as many named rules as it holds captures |
| `start_line` | `uint32_t` | 1-based |
| `end_line` | `uint32_t` | 1-based; a match may span many lines |
*   **`RuleMatchRow`** (defined in [include/report.h](../include/report.h)) — One rule match as the report presents it, with the file it was found in. Sorted by file, then start line, then end line, then identity — the last key because two rules matching one node are two rows, and without a tiebreak their order would be the order a directory listing produced.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `rule` | `char *` | The identity, as recorded |
| `file` | `char *` | Owned; the row outlives the FileMetrics it was taken from |
| `start_line` | `uint32_t` | 1-based |
| `end_line` | `uint32_t` | 1-based |
*   **`DeadSpan`** (defined in [include/elc.h](../include/elc.h)) — One statement that cannot execute, and why.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `function` | `size_t` | Into FileMetrics.functions, or ELC_NO_FUNCTION for file scope |
| `start_line` | `uint32_t` | 1-based |
| `end_line` | `uint32_t` | 1-based; a dead branch may span many lines |
| `cause` | `DeadCause` | after-terminator or literal-condition — the reader's next action differs, so the two are not merged |
*   **`Sdg`** (defined in [include/elc.h](../include/elc.h)) — The System Dependence Graph and the tables needed to interpret it.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `graph` | `void *` | Opaque handle to the graph library's structure |
| `nodes` | `SdgNode *` | Node table indexed by stable node id |
| `symbols` | `SymbolTable` | Name to node id, for call resolution |
| `components` | `ComponentProjection` | File-level projection used by arch.c (HLR-114) |
| `unresolved` | `size_t` | Call sites with no resolvable target (HLR-077) |
| `touches` | `GlobalTouch *` | Per-object access records, carried beside the state edges rather than derived from them (HLR-091) |
| `component_graph` | `void *` | The component projection as a graph; the view the architectural questions are asked of (HLR-083, HLR-114) |
*   **`GlobalTouch`** (defined in [include/graph.h](../include/graph.h)) — One function's access to one global object. Recorded beside the global-state edges, not derived from them: an edge joins a writer to a reader, so an object touched by exactly one function produces none — and that object is precisely the scope-reduction candidate of HLR-092.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `object` | `const char *` | Into the graph's own name table |
| `node` | `uint32_t` | The accessing function |
| `write` | `bool` | True writes the object, false reads it |
*   **`StratumDecl`** (defined in [include/elc.h](../include/elc.h)) — One declared architectural layer: a name, the component patterns assigned to it, and its position in the declared dependency direction (HLR-078). The ordinal is what makes a direction out of a set of names — layer 0 is the top, permitted to depend on those below — and is fixed when the layer is first named.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `name` | `char *` | Owned |
| `patterns` | `char **` | Owned; matched against component paths with fnmatch(3) |
| `pattern_count` | `size_t` | Populated entries; a repeated name adds to this rather than creating a second layer |
| `ordinal` | `size_t` | 0 is the topmost declared layer; reassigned by --stratum-order where one is given |
*   **`ArchResults`** (defined in [include/arch.h](../include/arch.h)) — What the component-level analyses measured. Owned by main and copied into the report model, as the call-tree and state results are.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `coupling` | `ComponentCoupling *` | Ca, Ce, Instability and the bottleneck flag, one per component (HLR-080 – HLR-082) |
| `cycles` | `ComponentCycle *` | Each mutually dependent group with a concrete loop through it (HLR-083) |
| `strata_state` | `StrataState` | Measured, or omitted because no strata were declared (HLR-115) |
| `violations` | `LayerViolation *` | Skip-level and direction-inverted calls, as distinct entries (HLR-079, HLR-118) |
| `inter_layer_edges` | `size_t` | The denominator of both conformance indices: the run's call edges joining two components in *different* declared layers. Counted by `check_strata` in the same pass that produces the violations above, since the three exclusions it needs — the edge is a call, both ends lie inside the partition, the two ends lie in different layers — are the three tests that loop already makes. A second traversal applying them again would be a second opinion about which edges the indices are over (HLR-162 – HLR-164). Zero where no strata were declared, the loop not having run |
*   **`ConformanceIndices`** (defined in [include/arch.h](../include/arch.h)) — The two conformance indices over one run (HLR-162, HLR-163). Both are proportions of one denominator and neither summarises the other; they are never added, since a call ascending two layers is counted in each and a combined score would count it twice.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `inter_layer_edges` | `size_t` | The shared denominator, taken from ArchResults rather than recounted |
| `back_calls` | `size_t` | Inverted findings counted from the recorded violations, never re-derived from the graph (HLR-164) |
| `skip_calls` | `size_t` | Skip-level findings, counted the same way |
| `back_call_index, skip_call_index` | `double` | Each numerator over the shared denominator; meaningless and untouched where `defined` is false |
| `defined` | `bool` | False where the denominator is zero, in which case the caller reports the index as undefined rather than as 0 or 1. A project with no inter-layer call has not achieved perfect conformance; it has demonstrated nothing either way — the rule HLR-082 already applies to Instability (HLR-162) |
*   **`ComponentCycle`** (defined in [include/arch.h](../include/arch.h)) — One cyclic dependency between components. Two facts, because one alone misleads: the membership is the group that must be broken up, and the path is a concrete loop saying which edge to cut. The path may be shorter than the membership, since a group can hold a number of loops exponential in its size.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `members` | `size_t *` | Component indices, ascending |
| `path` | `size_t *` | A loop through them, in order; the first is not repeated at the end |
*   **`ScopeDecl`** (defined in [include/elc.h](../include/elc.h)) — One declared execution scope: a name, and the component patterns belonging to it (HLR-094). Owned outright, unlike the entry-point symbols: a declaration is split on two separators, so neither half is a terminated substring of any argument.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `name` | `char *` | Owned |
| `patterns` | `char **` | Owned; matched against component paths with fnmatch(3) |
| `pattern_count` | `size_t` | Populated entries |
*   **`StateResults`** (defined in [include/state.h](../include/state.h)) — What the global-state, reachability, and scope-isolation analyses measured. Owned by main and copied into the report model, as the call-tree results are.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `globals` | `GlobalRow *` | One per object touched, with its writers, readers, and verdict (HLR-091 – HLR-093) |
| `reach_state` | `ReachState` | Measured, or omitted for want of a declaration or of a declaration that resolves (HLR-115) |
| `unreachable` | `uint32_t *` | Node identifiers no traversal reached, ascending (HLR-096) |
| `dead_globals` | `const char **` | Objects every accessor of which is unreachable (HLR-096) |
| `scope_state` | `ScopeState` | Measured, or omitted because no execution scopes were declared (HLR-094, HLR-115) |
| `violations` | `ScopeViolation *` | Every call and state edge crossing a declared boundary (HLR-094) |
*   **`Threshold`** (defined in [include/thresholds.h](../include/thresholds.h)) — One row of the published catalogue. The counted bands are exclusive upper bounds, matching how the published tables are written; a kind whose finding is its mere occurrence carries a fixed severity instead of bounds.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `kind` | `MeasurementKind` | Which measurement the row bands |
| `warning_above` | `uint32_t` | A value strictly greater than this warns |
| `critical_above` | `uint32_t` | A value strictly greater than this is critical |
| `fixed` | `Severity` | For a kind whose occurrence is the finding |
| `attribution` | `const char *` | The published source, named for every row (HLR-099) |
| `elc_own` | `bool` | True for the one row that is elc's own heuristic rather than a published standard |
*   **`Finding`** (defined in [include/elc.h](../include/elc.h)) — One reportable observation. Severity is data and never influences the exit status (HLR-100).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `kind` | `MeasurementKind` | Which analysis produced it |
| `severity` | `Severity` | info, warning, or critical |
| `attribution` | `const char *` | Citation, or an explicit marker for elc's own heuristics (HLR-099) |
| `location` | `Location` | File, line, and node where applicable |
| `detail` | `char *` | Rendered description, including cycle members or chain steps |
*   **`MetricsAccumulator`** (defined in [include/report.h](../include/report.h)) — Per-file metrics as they accumulate during the run. Owns every FileMetrics handed to it, until report_assemble takes them.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `files` | `FileMetrics **` | Dynamic array, grown by doubling through a checked reallocation (LLR-RPT-16) |
| `count` | `size_t` | Populated entries |
| `capacity` | `size_t` | Allocated entries |
*   **`ProjectSummary`** (defined in [include/report.h](../include/report.h)) — The project-level totals across every analysed file (HLR-024), and the most-complex callouts once there are metrics to compare (HLR-026).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `file_count` | `size_t` | Files analysed in the run |
| `physical_lines` | `uint64_t` | Combined physical line count; wider than the per-file field because it sums over the whole project |
| `eloc` | `uint64_t` | Combined effective lines of code, summed over every analysed file (HLR-024) |
| `function_count` | `uint64_t` | Functions reported across the run |
| `largest_file, largest_file_eloc` | `const char *, uint32_t` | The file with the highest file-level ELOC, borrowed from the model it was chosen from. NULL where the run analysed nothing (HLR-026, HLR-066) |
| `most_complex, most_complex_file, most_complex_value` | `const char *, const char *, uint32_t` | The function with the highest cyclomatic complexity and the file defining it, both borrowed. Ties are broken by the stable presentation order, so the callout is a property of the report rather than of discovery order (HLR-026, HLR-032) |
| `henry_kafura` | `uint64_t` | The project's combined Henry-Kafura complexity: the sum of the per-function values, never the formula applied to these totals — the metric is defined over one procedure's traffic and a project has no fan-in. Sixty-four bits because the squared term makes it grow far faster than any other figure here (HLR-158) |
*   **`PathList`** (defined in [include/report.h](../include/report.h)) — A sorted, owned list of paths. Used for the files discovered but not analysed, so the report accounts for every discovered file (HLR-012).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `paths` | `char **` | Dynamic array, grown by doubling |
| `count` | `size_t` | Populated entries |
| `capacity` | `size_t` | Allocated entries |
*   **`Report`** (defined in [include/report.h](../include/report.h)) — The format-independent model every renderer consumes. Every collection is sorted before a renderer sees it. Each analysis stage's results are copied in rather than referenced, because the model outlives every input to it and regeneration from a record has no analysis to point at.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `summary` | `ProjectSummary` | Combined totals and the most-complex callouts (HLR-024, HLR-026) |
| `files` | `FileMetrics **` | Sorted by path; owned |
| `languages` | `LanguageList` | Each language's share of the totals, sorted by language name (HLR-025) |
| `routes` | `RouteList` | Per directory target, whether it was enumerated from a repository or traversed from the filesystem (HLR-127) |
| `unresolved_calls` | `size_t` | Call sites with no resolvable target, reported so graph completeness is visible (HLR-077) |
| `over_threshold` | `ThresholdList` | The per-file listing of functions at or above the complexity threshold, built here rather than filtered by a renderer so every format lists the same functions (HLR-021) |
| `fan_out, cycles, depth_state, depth, deepest` | `FanOutRow *, CycleRow *, DepthState, uint32_t, ChainRow *` | The call-tree measurements: one FanOutRow per function carrying its fan-out, fan-in, ELOC and Henry-Kafura value — each reported whether or not a threshold was crossed, and three of the four never banded at all — the recursive cycles, and the deepest chain in full with the state saying why a depth figure is absent (HLR-085, HLR-087 – HLR-090, HLR-156, HLR-157) |
| `coupling, dep_cycles, strata_state, layering` | `CouplingRow *, CycleDependencyRow *, StrataState, LayeringRow *` | The component-level measurements: Ca, Ce and Instability per component, each dependency cycle with a concrete loop through it, and the layering findings with the state of that analysis (HLR-080 – HLR-083, HLR-118) |
| `global_state, reach_state, unreachable, unreachable_globals, scope_state, cross_scope` | `GlobalStateRow *, ReachState, UnreachableRow *, char **, ScopeState, CrossScopeRow *` | The global-state and reachability measurements, each carrying the state that distinguishes a measurement from an analysis omitted for want of a declaration (HLR-091 – HLR-096, HLR-115) |
| `back_call, skip_call` | `ConformanceRow` | The two conformance indices as the report presents them: the violations counted, the inter-layer call edges they are over, and both the index and its complementary conforming proportion as rendered text. Text because "undefined" is one of their legitimate values, exactly as it is for Instability, and a renderer choosing between a number and a word is a decision that would then be made four times (HLR-162, HLR-163) |
| `dsm` | `Dsm` | The dependency matrix: the ordered subject labels and the square grid of call counts between them, with a flag saying whether the subjects are declared layers or directories. Part of the model rather than a renderer's scratch space, because a record of a run must be able to regenerate it and a record carries no call graph to rebuild it from (HLR-165, HLR-166, HLR-054) |
| `purification, purify_thresholds, purified_nodes, purified_edges` | `PurificationRow *, PurifyThresholds, size_t, size_t` | Every classification purification made, the five thresholds they were decided against, and what the masking left behind. Reported before anything is relied on, because automated masking a reader cannot inspect is a black box; ordinary functions are absent, since `elc concluded nothing about this function` is not a classification (HLR-174, HLR-171) |
| `recovery_state, recovery, recovery_count, recovery_strata` | `RecoveryState, RecoveredRow *, size_t, size_t` | The layering read off the purified view: whether one was proposed, the directories placed, and how many distinct layers they fell into. **Never a baseline** — these rows reach the renderers and the record and nothing else, and with no strata declared the conformance analyses stay omitted whatever was recovered (HLR-172, HLR-173, HLR-115) |
| `recovery_cycles, recovery_masked, recovery_excluded` | `PathList, size_t, size_t` | Where the view is cyclic, the mutually reachable groups reported in place of an ordering; and the functions the masking cut edges from or left out entirely, since a layering read from a graph with parts set aside is a claim about that graph and not about the program (HLR-172) |
| `recovery_proposal` | `char *` | The proposal as the argument list `--stratum` and `--stratum-order` accept, or NULL. The boundary HLR-173 draws made visible: elc produces a command line, and it takes effect only when the user passes it back (owned) |
| `findings` | `FindingRow *` | Every measurement that crossed a published line, ranked most severe first, each naming its source (HLR-098, HLR-123) |
| `dead, dead_unanalysed` | `DeadRow *, PathList` | The unreachable statements within functions, sorted by file then start line, and the languages whose module supplied no dead-code query — the second because unanalysed and none-found are different claims (HLR-137, HLR-139) |
| `rule_matches` | `RuleMatchRow *` | What the user's own rules matched, sorted and reported beside the findings rather than among them (HLR-109, HLR-111) |
| `definitions` | `char **` | The configuration the figures describe, copied and sorted: the model outlives argv on the regeneration path, and the order the user typed them in is not a property of the run (HLR-136) |
| `undecided_regions` | `uint64_t` | Conditional regions left active because their condition could not be decided, summed over every file (HLR-133) |
| `image` | `char *` | The linked image the run was filtered by, or NULL where it was not. Every field below is meaningless without it, and NULL is what every renderer tests: with no image the filter sections are not emitted at all, which is the one place the uniform-composition rule gives way — HLR-140 requires a run without the option to report exactly what it reported before the option existed, and an empty section is not nothing (HLR-147) |
| `image_unresolved` | `uint64_t` | Linkage names carrying a mangling this build does not decode. The first direction of mismatch: it states the completeness of the filter as the unresolved-call count states the completeness of the graph (HLR-143) |
| `absent` | `AbsentRow *` | The second direction, and the finding the option exists to produce: the source functions this build did not keep. Sorted by file, then start line, then name (HLR-143) |
| `file_scope_eloc` | `uint64_t` | Effective lines belonging to no function, summed over every file — the part of the total the filter did not narrow (HLR-145) |
| `skipped_files` | `PathList` | Discovered files with no available language module, sorted by path (HLR-012) |
*   **`ConformanceRow`** (defined in [include/report.h](../include/report.h)) — One conformance index as the report presents it (HLR-162, HLR-163). The numerator and the denominator travel beside the rendered figures because a proportion is not interpretable without the count it is over — 50% of two edges and 50% of two hundred are different claims about a code base.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `violations` | `uint64_t` | The findings counted, never re-derived (HLR-164) |
| `edges` | `uint64_t` | Inter-layer call edges; the denominator |
| `index` | `char *` | Owned; a percentage to two places, or "undefined" |
| `conforming` | `char *` | Owned; the complement, computed from the counts rather than subtracted from the rendered index, so the pair is two roundings of one division rather than a rounding of a rounding — and need not read as exactly 100% |
*   **`Dsm`** (defined in [include/report.h](../include/report.h)) — The Dependency Structure Matrix (HLR-165, HLR-166). A square grid over the declared layers, or over the analysed directories where no layer was declared. Rows are callers and columns callees, both in the same ascending order, so a cell's position carries its meaning.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `subjects` | `char **` | The row and column labels, in order; owned. Ordered by ascending layer index under declared strata — a stratum's ordinal *is* its position — and by path otherwise |
| `count` | `size_t` | The order of the square matrix |
| `cells` | `size_t *` | Row-major, count * count; owned. Cell (i, j) is the number of call edges from subject i to subject j |
| `from_strata` | `bool` | True where the subjects are declared layers, false where they are directories. The two are read differently — only a declared order makes a below-diagonal cell a violation — so the reader is told which they are looking at rather than left to infer it |
*   **`PurifyThresholds`** (defined in [include/elc.h](../include/elc.h)) — The five thresholds the recovery view is purified against (HLR-168 – HLR-171). Every one is `elc`'s own heuristic rather than a published standard, and is marked as such wherever a classification made against it is reported. The four centrality figures are rank positions expressed as a percentage of the other functions, never raw scores; the core depth is the one absolute figure, because a coreness is a small integer.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `sink_authority` | `uint32_t` | Authority rank at or above which a function may be a utility sink; 90 by default |
| `sink_hub` | `uint32_t` | Hub rank at or below which a utility sink's own calls count as near zero; 10 by default |
| `god_betweenness` | `uint32_t` | Betweenness rank at or above which a function may be a god object; 90 by default |
| `god_hub` | `uint32_t` | Hub rank a god object must also reach; 90 by default |
| `core_depth` | `uint32_t` | Coreness below which a function is peripheral and excluded from the view; 2 by default |
*   **`Classification`** (defined in [include/purify.h](../include/purify.h)) — One function's centralities, its position in each distribution, and what followed from them (HLR-168 – HLR-171, HLR-174). The ranks are carried beside the raw scores because the rank is what the thresholds are compared against and the score is what a reader recognises: a report naming only the rank could not be checked against the graph, and one naming only the score could not be checked against the threshold that acted on it.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `hub` | `double` | The HITS hub score over the call view |
| `authority` | `double` | The HITS authority score over the call view |
| `betweenness` | `double` | Shortest paths the function lies on, unnormalised |
| `coreness` | `uint32_t` | The k-core the function lies in, over the undirected neighbourhood |
| `hub_rank` | `uint32_t` | Percentage of the *other* nodes scoring strictly below on hub |
| `authority_rank` | `uint32_t` | The same, for authority |
| `betweenness_rank` | `uint32_t` | The same, for betweenness |
| `klass` | `PurifyClass` | Utility sink, god object, peripheral, or ordinary. Ordinary is the zero, so a zeroed table reads as `nothing was concluded` |
| `metric` | `PurifyMetric` | Which measurement triggered the class, for the report of HLR-174 |
| `value` | `double` | Its value — the number the comparison was actually made against |
| `rank` | `uint32_t` | Its rank; unused where the metric is coreness, which is absolute |
| `masked` | `bool` | Whether the recovery view applies this class's masking action. Set with the class by a computed classification, so an ordinary function is unmasked by construction; a manifest may state the class and withhold the action, which is the correction HLR-175 exists for (HLR-177) |
| `from_manifest` | `bool` | True where the class was stated by a manifest rather than computed. Carried so the report can say which of the assumptions in front of a reader are elc's and which are their own team's — without it the two are indistinguishable in the one section whose purpose is to be inspected (HLR-177) |
*   **`ManifestEntry`** (defined in [include/purify.h](../include/purify.h)) — One statement a manifest makes about one function (HLR-175, HLR-177). The class and the masking action are two facts and the entry carries both, because the usual correction is not that `elc` misread the graph but that it drew the wrong conclusion from a correct reading.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `function` | `char *` | The function the statement is about; owned |
| `file` | `char *` | The file defining it, or NULL where the manifest omitted it, in which case the statement matches by function name alone; owned |
| `klass` | `PurifyClass` | The class the statement assigns, which governs and is not recomputed |
| `mask` | `bool` | Whether the view applies that class's action. False keeps the classification reportable and the function's edges in the view |
| `matched` | `bool` | Whether the statement named a function the run analysed. Tracked rather than assumed: one that matched nothing is reported and ignored rather than fatal, since analysing one directory of a project whose manifest covers all of it is ordinary use (LLR-CTR-08) |
*   **`Manifest`** (defined in [include/purify.h](../include/purify.h)) — The statements one named manifest holds (HLR-176). Read only from a path given on the command line — never from the working directory, the analysis target, an ancestor of either, or a dotfile — so the zero-configuration guarantee is unchanged by the format existing (HLR-039).

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `entries` | `ManifestEntry *` | One per statement, in the order the file listed them; owned |
| `count` | `size_t` | Statements read. Zero after a rejection: a manifest is refused whole rather than partly applied |
*   **`RecoveredLayer`** (defined in [include/recover.h](../include/recover.h)) — One directory the proposal places, and where it placed it (HLR-172). The subject is a *directory* because an architecture orders directories; the topological order underneath orders functions and is folded before it reaches here.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `directory` | `char *` | The directory, as discovery recorded it; owned |
| `layer` | `size_t` | 0-based, topmost first. Directories at equal positions share a layer — cutting between them would invent a dependency direction the graph does not hold |
| `functions` | `size_t` | The functions the recovery view retained there. A directory holding only excluded ones is not a row at all (HLR-170) |
*   **`RecoveryResults`** (defined in [include/recover.h](../include/recover.h)) — What recovery concluded, before the report is told about it (HLR-172, HLR-173). The three outcomes are exclusive and each is a complete answer: a layering, the cycles that make one impossible, or the statement that nothing survived purification to order. None is an error.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `state` | `RecoveryState` | Proposed, cyclic, or omitted for an empty view. The omission is the zero, so a model carrying no recovery at all reads as `nothing was proposed` rather than as an empty proposal |
| `layers` | `RecoveredLayer *` | Sorted by layer, then by path; owned |
| `layer_count` | `size_t` | Rows, one per directory placed |
| `strata` | `size_t` | Distinct layers among them |
| `cycles` | `char **` | Where the view is cyclic, each mutually reachable group rendered as its membership; sorted; owned |
| `cycle_count` | `size_t` | Groups reported in place of an ordering |
| `masked` | `size_t` | Functions whose edges the masking cut |
| `excluded` | `size_t` | Peripheral functions left out of the view entirely |
| `proposal` | `char *` | The proposal in the form the stratum options accept, or NULL where there is nothing to propose. An argument list rather than prose: adoption is then a copy rather than a transcription, and the boundary HLR-173 draws is visible in the form of the thing (owned) |
*   **`RecoveredRow`** (defined in [include/report.h](../include/report.h)) — One directory the recovered layering places, as the report presents it (HLR-172). **A proposal, and never the baseline it would be measured against** (HLR-173): nothing in `arch.c` can reach these rows, the conformance analyses take their layer index from the declared strata of HLR-078 and from nothing else, and where none are declared they stay omitted however confidently a layering was recovered.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `directory` | `char *` | The directory placed; owned |
| `layer` | `size_t` | 0-based, topmost first |
| `functions` | `size_t` | The ones the recovery view retained there |
*   **`RecoveryView`** (defined in [include/purify.h](../include/purify.h)) — The masked copy of the call view (HLR-167 – HLR-170). Vertex identifiers are the `Sdg`'s own, so a result read off this graph indexes the node table directly and a tie broken by vertex identifier is a tie broken by the stable node identifier of HLR-033. A peripheral node is therefore excluded by its `included` flag and by holding no edge rather than by being renumbered out of existence — renumbering would put the determinism of HLR-179 on a mapping instead of on the identifier the rest of the run already agrees about.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `graph` | `void *` | igraph_t * over the retained call edges; owned. Opaque, so that no consumer links the graph library merely to read the flags beside it |
| `included` | `bool *` | node_count entries; false for a peripheral node, which is given no recovered layer at all |
| `node_count` | `size_t` | The Sdg's, unchanged |
| `included_count` | `size_t` | Functions retained in the view |
| `edge_count` | `size_t` | Call edges the view retained |
| `masked_edges` | `size_t` | Call edges the masking removed |
*   **`PurifyResults`** (defined in [include/purify.h](../include/purify.h)) — Everything one purification pass produced: a classification per function, the view the masking left behind, and the thresholds that were in force.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `classes` | `Classification *` | One per node, indexed by stable node identifier; owned |
| `node_count` | `size_t` | The graph's node count |
| `classified` | `size_t` | The non-ordinary among them — the rows the transparency report carries |
| `view` | `RecoveryView` | The masked copy; owned |
| `thresholds` | `PurifyThresholds` | The values the classifications were made against |
*   **`PurificationRow`** (defined in [include/report.h](../include/report.h)) — One classification purification made, as the report presents it (HLR-174). **Not a finding**, and the difference is the requirement rather than a presentational choice: there is no severity here and nothing to attach one to, because a classification states where a function sits in a graph rather than that a measurement fell outside a published range (HLR-171, HLR-101). The metric and its value travel rendered, for the reason a component's Instability does — each metric is read on its own scale, and four renderers each choosing a precision is a decision that could differ between them.

    | Field | Type | Description |
    | ----- | ---- | ----------- |
| `function` | `char *` | The classified function; owned |
| `file` | `char *` | The file defining it; owned |
| `line` | `uint32_t` | Where the definition starts |
| `class_name` | `char *` | `utility sink`, `god object`, or `peripheral`; owned |
| `metric` | `char *` | The measurement that triggered it; owned |
| `value` | `char *` | Its value and rank, rendered; owned |
| `action` | `char *` | What the masking did to the view; owned |
| `source` | `char *` | `computed` or `manifest` — where the class came from. Carried on the row rather than inferred, because a reader of this section is being asked to judge whether the masking was right and cannot do that without knowing which rows are elc's own reading of the graph and which are their team's correction of it (HLR-177); owned |
*   **Compile-time constants** (in [include/elc.h](../include/elc.h)):

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
| `ELC_MANIFEST_VERSION` | 1 | Purification manifest format identifier, so a manifest written by a later build is rejected rather than half-understood; declared in include/purify.h (HLR-175, HLR-176) |
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

`clean` leaves the **unpacked dependency sources** alone for the same reason, and the division of labour is the point: `prereqs-clean` is the target named for removing them, and a `clean` that took both made one of the two a lie.

It is load-bearing as well as tidy, because of a failure mode worth recording. The prereq recipes escalate exactly where they need to — `apt-get`, `make install`, `cmake --install` — and a user who runs the whole target under `sudo` instead has the *unpacking* run as root. An unprivileged `tar` ignores the uid and gid an archive records and gives everything to the invoking user; a root `tar` honours them, and igraph's release tarball carries `501:staff`, the account of whoever packaged it. What is left is a source tree the developer who built it cannot delete — and since `clean` runs at the head of both `asan` and `valgrind`, it took both sanitizer gates down with it. The gates now run whatever state that tree is in, the prereq targets refuse to run as root at all, and `prereqs-clean` names the one command that takes ownership back rather than emitting a screen of permission errors.

**Dependency selection.** HLR-112 defers library choice to this document. Every library `elc` links is recorded below, whether or not the PVD named a candidate for its role, so that this table and the instrumented dependency allowlist describe the same set:

| Role | Selected | Rationale |
| ---- | -------- | --------- |
| Parsing and queries | **Tree-sitter** | Not a free choice: its query language and grammar format are a user-visible contract (HLR-112). Actively released. |
| Repository access | **libgit2** | Actively maintained, frequent releases; the only mature C option for tracked-file enumeration. |
| Graph algorithms | **igraph** | The only mature C-native graph library; its 1.0 series carries an explicit long-term API stability commitment. Alternatives (Boost.Graph, LEMON, NetworKit) are C++ and would impose a second toolchain. Build with GraphML support disabled — see below. |
| XML reading | **Expat** | Actively maintained, currently funded, streaming, and namespace-aware — everything the read path needs, and nothing it does not. |
| XML and GraphML writing | **none** | Hand-rolled emission with centralised escaping. Removes a dependency rather than adding one. |
| DOT writing | **none** | Plain text. Graphviz renders the output; `elc` never links it. |
| Object-file reading | **libelf** | A well-specified container with a mature implementation; hand-rolling one would put endianness, class, and section-header handling into this project's defect surface for no benefit (SDD §18). Taken from the distribution rather than built from source — the one linked library that is, because building elfutils imports *more* distribution packages than using it does. |
| Linkage-name demangling | **the C++ runtime's `__cxa_demangle`** | No new dependency: `libstdc++` has been linked as a transitive dependency of igraph since Phase 8, and is now named on the link line deliberately rather than relied upon indirectly (LLR-BLD-19). Decodes the Itanium ABI, and so C++ and Rust's legacy scheme alike. |
| JSON generation and parsing | **Jansson** (≥ 2.14), *from the distribution* | The purification manifest is the one artefact `elc` both writes and reads back (HLR-175, HLR-176), and Jansson does both through one API. C-native, MIT, no dependencies of its own. **Taken from the distribution rather than built from a pinned release**, which is the third exception to that rule and the only one taken because building it is actively unsafe — see below. Its `json_error_t` reports the line, column, and byte position of a fault, which is what lets the rejection HLR-176 requires name *where* a hand-edited manifest went wrong rather than only that it did; and it validates UTF-8 strictly, which matters for a file carrying function names and paths lifted from source. See below for why the alternatives lost. |

**Why a library writes the manifest when nothing else here uses one.** Every other format `elc` emits — XML, GraphML, CSV, DOT — is hand-rolled, because emission needs only correct escaping and a writer library would add a dependency for no benefit (§16.3.2). The manifest is the exception, and the reason is that it is the only artefact that **round-trips**: `elc` writes it, a user edits it by hand, and `elc` reads it back (HLR-175 – HLR-177).

That changes what the writer has to guarantee. A hand-rolled writer paired with a library reader gives two independent implementations of one format, and the failure they produce is the one this project dislikes most — a manifest `elc` emitted that `elc` then rejects, or worse, silently reads as something other than what it wrote. One library on both sides makes the round trip a property of the design rather than of two pieces of code agreeing.

Candidates weighed against Jansson, and why each lost:

*   **jsmn** — a tokeniser. It parses and does not generate, so it fails the requirement outright.
*   **cJSON** — smaller and easily vendored, but its parse failure reports a pointer into the buffer rather than a line and column, which would make the diagnostic of HLR-176 markedly less useful on a file people edit by hand. It has also carried the heavier CVE history of the two.
*   **json-c** — widely packaged and long-lived, but with an older API and a heavier CVE history again, and no advantage here to set against either.
*   **yyjson** — excellent, single-file, and considerably faster. Speed is not the constraint: a manifest holds one entry per classified function and is read once per run. Against Jansson's longer maintenance record — the property every other row in the table above was chosen on — throughput it does not need is not a reason to prefer it.

**Why this one comes from the distribution.** Every other linked library is built from a pinned upstream release so that an advisory is answered by bumping a version rather than by waiting for a distribution (SDP §0). Jansson is the exception, and not for the reason libelf and libdw are: building it works perfectly well, and *installing* it is what does the damage.

GNU `ld` links libjansson, for its JSON map-file output. A second copy installed under `/usr/local/lib` — which `ldconfig` ranks ahead of the distribution's — therefore replaces the system linker's jansson for the whole machine. Nor is it a question of choosing a compatible version: Debian and Ubuntu patch jansson's symbol version node to `libjansson.so.4`, while upstream's own build names it `JANSSON_4`. `ld` looks for `json_delete@libjansson.so.4`, does not find it in the copy that now shadows the one it was linked against, and exits 127 before linking anything at all.

The trade is therefore not "pinned release versus distribution package" but "pinned release versus a working toolchain", and it is not close — the library in question reads one optional file, and the thing it breaks is the linker. The distribution ships 2.14, which is the minimum `check-prereqs` asks for, and `libjansson-dev` joins `libelf-dev` and `libdw-dev` in the package list the pipeline installs. Jansson has no dependencies of its own, so the instrumented allowlist still grows by exactly one entry.

The failure is recorded here rather than merely fixed because nothing about it is visible from the source: the build compiles, the link fails inside `ld` itself, and a developer whose distribution patches the version node the same way upstream does would never see it.

**Ownership of the intermediate structures.** HLR-125's leak gate makes this load-bearing rather than merely tidy, and the pipeline's shape leaves it otherwise ambiguous:

*   A **`FileFacts`** is owned by the caller of `graph_build`, never by the graph. `graph_build` copies what it needs into the SDG's own tables, so the fact list is released with `filefacts_free` as soon as `graph_build` returns; it must not be kept alive for the analyses.
*   **`ArchResults`**, **`TreeResults`**, **`StateResults`**, and the **`FindingList`** are owned by `main`. `report_assemble` copies from them into the report model rather than taking ownership, so `main` releases each with its `*_free` once assembly returns.
*   The **`MetricsAccumulator`** is the exception, and is exceptional because its contents are the model rather than an input to it: `report_assemble` *moves* the per-file metrics into the `Report` and leaves the accumulator empty. `main` still calls `metrics_free` afterwards, which is then a no-op — teardown stays unconditional, and no path frees the same `FileMetrics` twice.
*   Every one of these is released on error paths as well as the success path. A run ending in an invalid target or a rejected record must still exit leak-clean, which means teardown cannot live only at the bottom of a successful pipeline.

**Consequence for the igraph build.** `elc` writes GraphML itself, so igraph's own GraphML reader and writer are unused — and enabling them links a second XML library the project has no other need for. igraph must therefore be built with `IGRAPH_GRAPHML_SUPPORT` **off**. A distribution package built with it enabled reintroduces that dependency transitively, so the condition is checked at configure time rather than assumed; `make check-prereqs` reports it.
## 24. Traceability

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
| Failure handling and exit status (HLR-035 – HLR-038, HLR-063, HLR-117, HLR-120) | §3, §4 |
| Non-functional constraints (HLR-039 – HLR-043, HLR-112, HLR-113) | §3, §22 |
| Memory safety and resource release (HLR-124, HLR-125) | §3, §6, §7, §8, §13, §22 |
| SDG construction (HLR-073 – HLR-077, HLR-115) | §8 |
| Coupling, layering, and cycles (HLR-078 – HLR-084, HLR-114, HLR-118) | §9 |
| Call tree dimensionality (HLR-085 – HLR-090) | §10 |
| Global state and reachability (HLR-091 – HLR-097) | §8, §11 |
| Dead code within functions (HLR-137 – HLR-139) | §6, §7, §13 |
| Threshold evaluation and severity (HLR-098 – HLR-101, HLR-123) | §12 |
| Graph outputs (HLR-102 – HLR-106, HLR-119, HLR-122) | §17 |
| Custom rules (HLR-107 – HLR-111, HLR-116) | §6, §7 |
| Language module contract (HLR-121) | §6, §22 |
| Conditional compilation (HLR-131 – HLR-136) | §4, §6, §7, §13, §16 |
| Linked-image filtering (HLR-140 – HLR-147) | §4, §5, §7, §13, §16, §18 |
| User documentation (HLR-128 – HLR-130) | §1.2 |
| Architecture conformance measurement (HLR-160 – HLR-166) | §7, §9, §13, §21 |
| Graph purification and architecture recovery (HLR-167 – HLR-179) | §9, §12, §13, §14, §17, §19, §20 |
---
