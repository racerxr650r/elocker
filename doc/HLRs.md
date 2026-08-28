# High-Level Requirements

**Version:** 3.11
**Date:** 2026-08-27
**Author(s):** John Anderson

## 1. Target Discovery and Input Routing

Requirements governing how `elc` discovers and selects the set of source files to analyze from its target arguments (PVD §5 item 1, §7.1).

*   <a id="HLR-071"></a>**HLR-071: Multiple Target Arguments.**
    `elc` shall accept one or more target arguments in a single invocation, in any combination of regular file names and directory names. Each target argument shall be classified and routed independently according to its own type (HLR-001, HLR-002, HLR-004), so that files and directories may be freely intermixed on one command line, and the results from every target shall be combined into a single report whose file-level entries and project-level totals (HLR-024 through HLR-026) span all targets analyzed.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 5](SDD.md), [SDD Section 8](SDD.md).

*   <a id="HLR-072"></a>**HLR-072: Duplicate File Elimination Across Targets.**
    When the same source file is reached through more than one target argument — for example a file named explicitly on the command line that is also contained within a named directory, or two named directories that overlap — `elc` shall analyze and report that file exactly once, so that no file's metrics contribute more than once to the file-level results or to the project-level totals.
    *Trace:* [SDD Section 5](SDD.md).

*   <a id="HLR-001"></a>**HLR-001: Single-File Target Handling.**
    `elc` shall accept a path to a single regular file as its analysis target and shall process that file directly, without performing directory traversal, when the target argument is a regular file.
    *Trace:* [SDD Section 5](SDD.md).

*   <a id="HLR-002"></a>**HLR-002: Git-Repository Target Detection.**
    When a directory target lies within a Git repository, `elc` shall detect that repository — searching the target directory and then its ancestors — and shall enumerate the source files tracked at `HEAD` for analysis rather than walking the raw filesystem. A repository shall be treated as *applicable* to a target only when it tracks the target directory. Where an enclosing repository is found but does not track the target — a build directory excluded by `.gitignore`, or an unrelated repository such as a version-controlled home directory several levels above — that repository shall be disregarded and the target analysed by filesystem traversal instead (HLR-004).
    *Trace:* [SDD Section 5](SDD.md).

*   <a id="HLR-003"></a>**HLR-003: Git-Aware Exclusion.**
    For a Git-repository target, `elc` shall exclude from analysis any file that is not tracked by Git (including `.gitignore`d and untracked files) and shall exclude binary files, without requiring the user to maintain a separate exclusion list.
    *Trace:* [SDD Section 5](SDD.md).

*   <a id="HLR-004"></a>**HLR-004: Plain-Directory Fallback Traversal.**
    When a directory target is not part of a Git repository, or lies within one that does not track it (HLR-002), `elc` shall recursively traverse the directory tree at the filesystem level to discover source files.
    *Trace:* [SDD Section 5](SDD.md).

*   <a id="HLR-126"></a>**HLR-126: Repository Enumeration Scoped to the Target.**
    For a repository target, `elc` shall enumerate only those tracked blobs whose path lies at or beneath the target directory. Naming a subdirectory shall analyse that subdirectory and nothing above it, so that a directory target denotes the same set of files whether it is reached by repository enumeration or by filesystem traversal.
    *Trace:* [SDD Section 5](SDD.md).

*   <a id="HLR-127"></a>**HLR-127: Discovery Route Reported.**
    `elc` shall report, for each directory target, which discovery route was applied — repository enumeration or filesystem traversal — so that a result which is unexpectedly empty, or unexpectedly larger than the target, can be diagnosed rather than guessed at.
    *Trace:* [SDD Section 5](SDD.md), [SDD Section 13](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-005"></a>**HLR-005: Filesystem-Fallback Exclusion.**
    During the filesystem-level traversal of HLR-004, `elc` shall exclude files with recognized binary file extensions, and shall exclude hidden files and hidden directories, from analysis. An entry is hidden when its name begins with a period; the exclusion applies below the target and not to the target itself, since naming a hidden path as the target is explicit. Excluding hidden *files* as well as hidden directories is what makes HLR-039 observable: a configuration-like file planted in the analysis target cannot change the output if the traversal never yields it. The set of recognized binary extensions shall be defined by data in the runtime location rather than compiled into the executable, so that the exclusion list may be adjusted without a rebuild, consistent with HLR-060.
    *Trace:* [SDD Section 5](SDD.md).

*   <a id="HLR-069"></a>**HLR-069: Symbolic Link Handling During Traversal.**
    During the filesystem-level traversal of HLR-004, `elc` shall not descend into a directory reached through a symbolic link, so that a cyclic or self-referential link cannot cause unbounded traversal, and so that a linked directory's files are not counted more than once. A symbolic link supplied directly as the analysis target shall be resolved and analyzed, since it names the target explicitly.
    *Trace:* [SDD Section 5](SDD.md).

*   <a id="HLR-006"></a>**HLR-006: Uniform Target Output Shape.**
    Regardless of whether the target was a single file, a plain directory, or a Git repository, `elc` shall produce output with the same structure and fields, so that results from different target types are directly comparable.

    In a human-readable format the guarantee is over the set of sections the report **reaches** rather than the sequence of headings it prints: a section with no rows is not printed and is named in the closing statement instead (HLR-188, HLR-189). A file target has no discovery route to list and a directory target may have one; which of the two places that section appears in is content, and the comparability this requirement is about is unaffected by it.
    *Trace:* [SDD Section 13](SDD.md).

## 2. Automatic Language Detection and Extensibility

Requirements governing how `elc` identifies each file's programming language and extends its language support without recompilation (PVD §5 item 2, §6 Principle 2, §7.1).

*   <a id="HLR-007"></a>**HLR-007: Per-File Automatic Language Detection.**
    `elc` shall determine the programming language of each discovered source file automatically, from the file's extension, without the user specifying which language any file is written in.
    *Trace:* [SDD Section 6](SDD.md).

*   <a id="HLR-008"></a>**HLR-008: Mixed-Language Single-Pass Analysis.**
    `elc` shall analyze a target containing source files written in more than one supported language within a single invocation and a single pass, without requiring the user to invoke `elc` once per language.
    *Trace:* [SDD Section 6](SDD.md).

*   <a id="HLR-009"></a>**HLR-009: Runtime-Loaded Language Support.**
    `elc` shall load all language-specific parsing and query logic from a runtime location external to the executable, at startup or on first use of that language, rather than compiling language-specific logic into the executable.
    *Trace:* [SDD Section 6](SDD.md).

*   <a id="HLR-059"></a>**HLR-059: Runtime Location Discovery and Precedence.**
    `elc` shall resolve the location of the runtime language-support directory of HLR-009 from a dedicated environment variable when that variable is set, and otherwise from a path relative to the `elc` executable itself. When both a set environment variable and a runtime directory adjacent to the executable are present, the environment variable shall take precedence.
    *Trace:* [SDD Section 6](SDD.md).

*   <a id="HLR-060"></a>**HLR-060: Extension Mapping Defined by Runtime Data.**
    The mapping from a source file's extension to a language name, used by the automatic detection of HLR-007, shall be defined by data within the runtime language-support location (HLR-009) rather than compiled into the `elc` executable; associating an additional or alternative extension with a language shall require no modification to, and no recompilation of, the executable.
    *Trace:* [SDD Section 5](SDD.md), [SDD Section 6](SDD.md).

*   <a id="HLR-010"></a>**HLR-010: No-Recompilation Language Addition.**
    `elc` shall support adding support for a new language by adding files to the runtime location of HLR-009 alone; adding a language shall require no modification to, and no recompilation of, the `elc` executable.
    *Trace:* [SDD Section 6](SDD.md), [SDD Section 22](SDD.md).

*   <a id="HLR-121"></a>**HLR-121: Language Module Interface Is a Stable Contract.**
    The interface between the `elc` executable and a language module — the set of query files a language is required to supply, and the capture names by which those queries return their results — shall be documented, and a language module supplying exactly the documented set shall function correctly with no further configuration. A module omitting a required query file shall be handled under HLR-070 rather than producing undefined behaviour. This interface is the contract a third party codes against when adding a language (HLR-010): renaming a required query file or a capture name, or changing the meaning of either, is a breaking change to that contract rather than an internal adjustment. That last is a constraint on the project's release process, verified by review, rather than a property observable within any single run.
    *Trace:* [SDD Section 6](SDD.md), [SDD Section 22](SDD.md).

*   <a id="HLR-011"></a>**HLR-011: Initial Delivered Language Set.**
    The elocker *project* shall deliver runtime language support for C, C++, Rust, and Python. This requirement constrains the project's deliverables, not the `elc` executable: `elc` shall not require, verify, or assume the presence of any particular language's support files, and shall complete with the exit-status semantics of HLR-120, producing a report per HLR-031, over whatever set of valid language modules the runtime location happens to contain.
    *Trace:* [SDD Section 6](SDD.md), [SDD Section 22](SDD.md).

*   <a id="HLR-012"></a>**HLR-012: Unsupported-Language File Handling.**
    When a discovered source file's extension does not map to any language available in the runtime location, `elc` shall skip that file rather than terminating the run, and shall report the skip through two observables: the file shall appear in the report's list of skipped files, and a diagnostic naming it shall be written to standard error. A skipped file is not a failure (HLR-037, HLR-120).
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 6](SDD.md), [SDD Section 13](SDD.md), [SDD Section 14](SDD.md).

## 3. Code Metrics Computation

Requirements governing how `elc` computes Effective Lines of Code and cyclomatic complexity from a parsed syntax tree (PVD §5 items 4 and 6, §6 Principle 1, §7.1).

*   <a id="HLR-013"></a>**HLR-013: AST-Based Metric Extraction.**
    `elc` shall derive every reported metric from a parsed abstract syntax tree (AST) of the source file; `elc` shall not use regular-expression matching, brace/token counting, or any other textual approximation to compute a reported metric.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-014"></a>**HLR-014: Per-Function Identity.**
    For each function discovered in a source file, `elc` shall report the function's name, where it begins, and how far it extends.

    **In a human-readable report (HLR-027, HLR-029) the extent shall be a line count** — the number of lines the function occupies — and the start shall be carried by the navigable location of HLR-210. A count is the figure a reader compares between two functions; a range is a fact about the file that they must subtract before they can use it, and once the start line is in the location column the range would state it twice.

    **The XML record shall continue to report start and end line numbers as separate fields**, because it must remain the thing a report can be rebuilt from and a regenerated report needs both numbers as numbers (HLR-056).

    **The CSV record shall carry the same columns as the human-readable function table, in its order and with its meanings** — the location as `path:line`, the extent as a count, and the visibility and flow degrees the table reports. CSV is that table for a consumer that loads it rather than reads it, and one view of one set of rows must be spelled one way. The `language` column, which no other view of a function reports, goes with the change: the file's language is a property of the file and is reported where the files are.

    *This reverses what this requirement said between Phase 29 and Phase 30* — that every complete-record format keeps a separate start and end line "because a consumer cannot subtract a column it was not given". That reasoning survived contact with XML, which rebuilds a report, and did not survive contact with CSV, where the record *is* the table: the two drifted apart column by column, and a consumer loading the CSV got a different set of figures from the one reading the report.

    For the purposes of this and every other requirement in this document, "function" means any named callable unit the source language defines — including a method, a constructor, a destructor, and a nested subprogram — as identified by that language's runtime query configuration.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 15](SDD.md).

*   <a id="HLR-015"></a>**HLR-015: Per-Function Effective Lines of Code.**
    For each function discovered in a source file, `elc` shall compute and report the function's Effective Lines of Code (ELOC): the count of executable statements within the function's line span — a statement that assigns or operates on data, directs control flow, invokes a function, returns from the function, or performs exception handling — as distinct from a line that serves only a structural, declarative, blank, or documentary purpose. HLR-044 through HLR-052 enumerate the specific categories counted toward, and excluded from, ELOC; HLR-053 governs how a statement spanning multiple physical lines is counted.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-053"></a>**HLR-053: Multi-Line Statements Counted as a Single Line.**
    `elc` shall count a single statement that spans multiple physical lines of source code as one line toward ELOC, not once per physical line it occupies, so that identical logic yields the same ELOC count whether written across several lines or condensed onto one — for example, an `if` condition split across three lines with its opening brace on its own line shall contribute the same ELOC as the same condition written on a single line.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-016"></a>**HLR-016: Comment Span Merging.**
    When computing ELOC, whether per function or per file, `elc` shall identify comment spans from the AST, sort them by start position, and merge any overlapping or nested spans before excluding them from ELOC, so that no line is excluded more than once regardless of nested or overlapping comment syntax.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-017"></a>**HLR-017: Per-Function Cyclomatic Complexity.**
    For each function discovered in a source file, `elc` shall compute and report the function's cyclomatic complexity, defined as one plus the number of decision points found within the function's body, as identified from the AST.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-018"></a>**HLR-018: Anonymous-Scope Complexity Attribution.**
    A decision point that occurs within an anonymous callable — a lambda, closure, or other unnamed nested function — shall be attributed to the cyclomatic complexity of the nearest enclosing named function, since the anonymous callable is not itself reported as a function, unless the language's runtime query configuration explicitly attributes it to the nested scope instead. Nested *named* functions are governed by HLR-067 and HLR-068 instead.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-067"></a>**HLR-067: Nested Named Functions Reported Independently.**
    A named function declared within the body of another function — a nested function, a nested subprogram, or a method in any supported language — shall be discovered and reported as a function in its own right, with its own name, line range, ELOC, and cyclomatic complexity, rather than being folded into its enclosing function.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-068"></a>**HLR-068: Innermost-Function Metric Attribution.**
    Each statement shall contribute to the ELOC and cyclomatic complexity of exactly one reported function: the innermost reported function enclosing it. A statement within a nested named function (HLR-067) shall therefore contribute to that nested function's metrics and shall not also contribute to those of any function enclosing it, so that no statement is counted twice within a single file's per-function results.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-019"></a>**HLR-019: File-Level Totals.**
    For each analyzed file, `elc` shall compute and report the file's total physical line count and the file's total Effective Lines of Code, accounting for every line in the file that qualifies as ELOC under HLR-044 through HLR-052, including such lines that lie outside of any function.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-020"></a>**HLR-020: Files With No Effective Lines of Code.**
    A file containing no line that qualifies as ELOC under HLR-044 through HLR-052 — for example, a file consisting entirely of blank lines, comments, declarations, and/or preprocessor directives — shall be reported with an ELOC of zero, without error.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-044"></a>**HLR-044: Assignments and Operations Count as ELOC.**
    `elc` shall count a line that performs a data assignment or a mathematical, logical, or pointer operation toward ELOC — for example, a variable initialization (`int x = 5;`) or a memory/pointer write (`buffer[0] = 0xAA;`).
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-045"></a>**HLR-045: Control-Flow Statements Count as ELOC.**
    `elc` shall count a line containing a control-flow construct that directs the execution path — including `if`, `else`, `while`, `for`, `switch`, `case`, `break`, and `continue`, or a language's equivalent construct — toward ELOC.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-046"></a>**HLR-046: Function-Call Statements Count as ELOC.**
    `elc` shall count a line that invokes a function or method toward ELOC, regardless of whether the call's result is used (e.g. `printf("Hello World");` or `init_hardware();`).
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-047"></a>**HLR-047: Return Statements Count as ELOC.**
    `elc` shall count a line that returns from a function, with or without a value, toward ELOC (e.g. `return 0;`).
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-048"></a>**HLR-048: Exception-Handling Statements Count as ELOC.**
    `elc` shall count a line containing an exception-handling construct — such as `try`, `catch`, or `throw`, or a language's equivalent construct — toward ELOC.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-049"></a>**HLR-049: Blank Lines Excluded from ELOC.**
    `elc` shall exclude blank lines — lines containing no tokens other than whitespace — from ELOC.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-050"></a>**HLR-050: Standalone Structural Tokens Excluded from ELOC.**
    `elc` shall exclude a line containing nothing but a standalone structural token — such as an opening or closing brace or parenthesis — from ELOC.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-051"></a>**HLR-051: Non-Initializing Declarations Excluded from ELOC.**
    `elc` shall exclude a line that only declares a variable or function, without initializing data or executing code — such as `int my_variable;` or a bare function prototype — from ELOC.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-052"></a>**HLR-052: Preprocessor and Directive Lines Excluded from ELOC.**
    For a language whose grammar defines preprocessor or compile-time directive constructs, `elc` shall exclude a line consisting only of such a directive — such as a C/C++ `#include`, `#define`, or header-guard `#ifndef` — from ELOC, as it does not itself translate to a runtime instruction.
    *Trace:* [SDD Section 7](SDD.md).

## 4. File-Level and Project-Level Reporting

Requirements governing how `elc` aggregates and summarizes per-function metrics at the file and project level (PVD §7.1, §8 "Project Summary").

*   <a id="HLR-021"></a>**HLR-021: Per-File Complexity-Threshold List.**
    For each file, `elc` shall report the list of functions within that file whose cyclomatic complexity meets or exceeds a threshold value, alongside that file's totals. HLR-187 extends that list to every function a published band names, and states how the two rules unite.
    *Trace:* [SDD Section 13](SDD.md).

*   <a id="HLR-022"></a>**HLR-022: Configurable Complexity Threshold.**
    The threshold value used by HLR-021 shall be configurable by the user, and shall default to 15 when the user does not supply a value.
    *Trace:* [SDD Section 4](SDD.md).

*   <a id="HLR-023"></a>**HLR-023: Threshold List is Reporting-Only.**
    The complexity threshold of HLR-021 / HLR-022 shall affect only what is listed for a file; it shall have no effect on `elc`'s process exit status.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-024"></a>**HLR-024: Project-Level Totals.**
    Across all files analyzed in a single run, `elc` shall compute and report the combined total physical line count and the combined total Effective Lines of Code for the entire target.
    *Trace:* [SDD Section 13](SDD.md).

*   <a id="HLR-025"></a>**HLR-025: Project Totals by Source Language.**
    In addition to the combined totals of HLR-024, `elc` shall break down the project-level physical-line and ELOC totals by source language, so the contribution of each language present in the target is separately visible.
    *Trace:* [SDD Section 13](SDD.md).

*   <a id="HLR-026"></a>**HLR-026: Project-Wide Most-Complex Callouts.**
    `elc` shall identify, across the entire run, the file with the highest file-level ELOC and the function with the highest cyclomatic complexity, and shall include both in the project summary. When two or more files, or two or more functions, tie for the highest value, `elc` shall select whichever sorts first under the stable presentation order of HLR-033, so that the callout is deterministic rather than dependent on discovery order.
    *Trace:* [SDD Section 13](SDD.md).

## 5. Output Formatting

Requirements governing how `elc` renders its computed results for human and machine consumption (PVD §5 item 7, §7.1).

*   <a id="HLR-027"></a>**HLR-027: Default Human-Readable Output.**
    By default, `elc` shall render its results as an aligned, human-readable table on standard output. What that table presents by default is the summary composition of HLR-150; this requirement fixes the default *format* and destination, and says nothing about how much of the report they carry.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-028"></a>**HLR-028: CSV Output.**
    `elc` shall support rendering the complete, per-function dataset as CSV — one record per function, unfiltered by the complexity threshold of HLR-021 / HLR-022. CSV carries per-function metrics only: the architectural findings of Sections 11 through 14 are not expressible as a single flat record set and are therefore excluded from it. XML (HLR-054) is the format that carries a complete record of a run.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 15](SDD.md).

*   <a id="HLR-064"></a>**HLR-064: CSV Field Quoting and Escaping.**
    `elc`'s CSV output (HLR-028) shall quote and escape every field whose value contains a comma, a double-quote character, or a line break, in accordance with RFC 4180, so that a value containing such a character — for example a C++ template signature such as `foo<int, long>` — cannot corrupt the record or field structure of the document.
    *Trace:* [SDD Section 15](SDD.md).

*   <a id="HLR-054"></a>**HLR-054: XML Output.**
    `elc` shall support rendering the complete dataset of a run as XML; like CSV (HLR-028), the XML output shall not be filtered by the complexity threshold of HLR-021 / HLR-022. The XML output shall carry every element that any report may present — project totals, per-file totals, per-function detail, the architectural findings of Sections 11 through 14, and any custom-rule matches (HLR-109) — so that it serves as a complete, durable record of the run, sufficient on its own to regenerate any report `elc` can produce (see Section 9).
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 16](SDD.md).

*   <a id="HLR-061"></a>**HLR-061: XML Format-Version Identifier.**
    `elc`'s XML output (HLR-054) shall carry a format-version identifier describing the structure of the document, so that any consumer — including `elc`'s own conversion mode (HLR-055) — can determine whether it understands that structure before interpreting the document's contents.
    *Trace:* [SDD Section 16](SDD.md).

*   <a id="HLR-065"></a>**HLR-065: XML Well-Formedness and Escaping.**
    Every XML document `elc` emits — the report record of HLR-054 and the GraphML export of HLR-106 alike — shall be well-formed XML. Every character occurring within element content or an attribute value that carries structural meaning in XML — including `&`, `<`, `>`, and quotation marks — shall be escaped, so that a source identifier or file path containing such a character cannot render the document unparseable.
    *Trace:* [SDD Section 16](SDD.md), [SDD Section 17](SDD.md).

*   <a id="HLR-029"></a>**HLR-029: Markdown Output.**
    `elc` shall support rendering its results as GitHub-Flavored Markdown, with functions grouped under a heading for the file that contains them.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-030"></a>**HLR-030: Optional Output-File Redirection.**
    `elc` shall support writing its rendered output to a user-specified file, as an alternative to writing to standard output.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md).

*   <a id="HLR-031"></a>**HLR-031: Uniform Report Composition Across Formats.**
    Every report format `elc` supports other than CSV (HLR-028), XML (HLR-054), and Graphviz `.dot` (HLR-102) shall present the same tiers of information *at the same verbosity*: the project summary (HLR-024 through HLR-026), the discovery route applied to each directory target (HLR-127), each file's totals and threshold list (HLR-019, HLR-021), full per-function detail (HLR-014, HLR-015, HLR-017), the architectural measurements *and* findings of Sections 11 through 14 — including a measurement that falls within its accepted band and therefore yields no finding — any custom-rule matches (HLR-109), the files skipped for want of a language module (HLR-012), and any analysis omitted for want of a user declaration (HLR-115).

    The enumeration above is the **verbose** composition of HLR-151. Verbosity selects how much of it a given run presents (HLR-150), and this requirement governs the axis it does not touch: whichever verbosity is in force, every affected format shall present the same tiers as every other. Uniformity is across formats at a fixed verbosity, never across verbosities — a table and a Markdown report of the same run must not differ, and a summary report is not required to match a verbose one.

    The tiers a format **reaches** are what must match, rather than the headings it prints: a tier with no rows is presented in no human format and named in the closing statement of every one of them (HLR-188, HLR-189). The order those tiers are presented in is HLR-182's and HLR-184's, and is likewise the same in every affected format.
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 14](SDD.md), [SDD Section 15](SDD.md).

*   <a id="HLR-148"></a>**HLR-148: Output Format Determined by Filename Extension.**
    Where the user names an output file (HLR-030), the extension of that filename shall determine the report format, and no separate format option shall be required to state what the filename has already said. `elc` shall recognise `.txt` as the aligned table (HLR-027), `.md` as Markdown (HLR-029), `.csv` as CSV (HLR-028), and `.xml` as the complete record (HLR-054).

    An output filename carrying an extension `elc` does not recognise, or carrying none at all, shall be rejected as a usage error (HLR-063) naming the extension found and listing those that are recognised. Guessing a format for an unrecognised extension would write one format under a name promising another, and defaulting silently to the table would produce a file called `report.json` holding no JSON — each of which is a confidently wrong result of exactly the kind this document forbids elsewhere.

    The extension governs the format alone. It has no bearing on the companion-artefact naming of HLR-119, which substitutes its own extension on the same path, so an output of `report.md` continues to yield `report.dot` and `report.graphml`.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md).

*   <a id="HLR-149"></a>**HLR-149: Format Selection Without a Named Output File.**
    Standard output has no filename and therefore no extension, so the format option shall remain the means of selecting a format for a report written there, defaulting to the aligned table when none is given (HLR-027). This is what keeps a machine-readable format available to a caller that pipes rather than redirects.

    Where an output filename and an explicit format option are both supplied and they disagree, `elc` shall reject the invocation as a usage error (HLR-063) naming both, rather than silently preferring one. The two are then two statements of the same fact, and a run that honoured the option while writing to a contradicting filename — or the reverse — would leave the user's own command line disagreeing with the file it produced. Where they agree the invocation is accepted, since nothing is ambiguous about saying a thing twice.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md).

*   <a id="HLR-150"></a>**HLR-150: Summary Report by Default.**
    By default, a report `elc` renders in a human-readable format shall present the **summary** tiers alone: the project summary (HLR-024 through HLR-026), the discovery route of each directory target (HLR-127), the per-language breakdown (HLR-025), each file's totals (HLR-019), the threshold listing of HLR-021 and HLR-187, the findings ranked by severity (HLR-098, HLR-123) — which HLR-182 places directly after the project summary — the files skipped for want of a language module (HLR-012), any analysis omitted for want of a declaration (HLR-115), any partly unparsed files (HLR-035), and the provenance a run carries — the configuration in force (HLR-136) and the image filtered by (HLR-147).

    It shall omit by default the **detail** tiers: every section presenting one row per function, per global object, per unreachable statement, per graph edge, or per custom-rule match. The partition rule is that a tier reporting a project-level or file-level aggregate, or a finding a reader is expected to act on, is a summary tier; a tier enumerating one row per analysed entity is a detail tier. Which tier each section belongs to shall be stated in the delivered documentation (HLR-129), so that the partition is a published property of the report rather than an artefact of how a renderer was written.

    The default changed with this requirement, and that is deliberate: the full report grew past the length at which it can be read in a terminal, and a default nobody reads is a default that serves nobody. Nothing is lost, because HLR-151 restores it in full and HLR-152 exempts the formats whose whole purpose is completeness.
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-151"></a>**HLR-151: Verbose Report on Request.**
    `elc` shall provide a command-line option that selects a **verbose** report, presenting every tier of HLR-031 — the summary tiers of HLR-150 together with the detail tiers it omits — in the order and composition a report presented before HLR-150 was adopted. A verbose run and a run made before that change shall be identical in what they present, so that the capability is a restoration rather than a new format to learn.

    The option shall govern presentation alone. It shall not change any measurement, any finding, any severity, or the process exit status, and a value absent from a summary report shall be absent because it was not printed rather than because it was not computed.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 13](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-152"></a>**HLR-152: Complete-Record Formats Unaffected by Verbosity.**
    The XML record (HLR-054) shall carry every element of a run whatever the verbosity, since its defining purpose is to be a complete and durable record sufficient to regenerate any report `elc` can produce (Section 9). A summarised record would silently destroy the measurements a later regeneration depends on, and the loss would not be visible in the file it produced.

    CSV (HLR-028) is likewise unaffected, and for the same reason rather than a different one: it is defined as the complete per-function dataset, and a summarised CSV would be a record set with no rows in it. Verbosity therefore selects between two presentations of the human-readable formats, and says nothing about the two formats defined as complete.

    It follows that supplying the verbosity option of HLR-151 together with an output format that is already complete changes nothing about the file produced. `elc` shall accept such an invocation rather than reject it: the option is a statement about presentation, the format has no presentation to vary, and nothing about the request is contradictory in the way HLR-149 rejects.
    *Trace:* [SDD Section 15](SDD.md), [SDD Section 16](SDD.md).

## 6. Determinism and Correctness

Requirements governing the determinism and correctness of `elc`'s output (PVD §6 Principle 4, §8 "Determinism" and "Correctness against hand counts").

*   <a id="HLR-032"></a>**HLR-032: Deterministic Output.**
    Running `elc` twice, unmodified, over the same target shall produce byte-identical output both times.
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 17](SDD.md).

*   <a id="HLR-033"></a>**HLR-033: Traversal-Order Independence.**
    The order in which `elc` presents files, and the functions within a file, shall not depend on filesystem or Git traversal order; `elc` shall present results in a stable, defined order regardless of the underlying operating system's or filesystem's enumeration order. This requirement extends to every collection `elc` reports — including cycles, bottlenecks, unreachable functions, hidden channels, and custom-rule matches — whose presentation order shall not depend on the enumeration order of any graph library, hash container, or other internal data structure.
    *Trace:* [SDD Section 5](SDD.md), [SDD Section 8](SDD.md), [SDD Section 13](SDD.md), [SDD Section 17](SDD.md).

*   <a id="HLR-034"></a>**HLR-034: Correctness Against Hand-Counted Fixtures.**
    `elc`'s computed ELOC and cyclomatic-complexity values shall match values counted by hand for a suite of fixture files, including fixture files containing nested comments and comment syntax embedded within string literals.
    *Trace:* [SDD Section 7](SDD.md).

## 7. Failure Handling and Exit Status

Requirements governing how `elc` responds to failures and what its process exit status communicates (PVD §6 Principle 6).

*   <a id="HLR-035"></a>**HLR-035: Per-File Read- and Parse-Failure Tolerance.**
    A file that cannot be read — for example because permission is denied or its contents cannot be decoded — or that cannot be parsed in whole or in part, shall not abort the run; `elc` shall emit a diagnostic identifying that file to standard error and shall continue processing the remaining files in the target.

    Where a syntax tree contains error nodes, `elc` shall measure the file from the parts the grammar could follow, shall record how many lines it could not, and shall report that figure beside the measurements it qualifies. A file wholly unreadable yields no metrics; a file the parser recovered from yields the metrics of its sound parts.

    **This requirement previously discarded any file containing an error node**, on the grounds that metrics from a damaged tree are indistinguishable from sound ones once rendered. That objection is sound and is met by reporting the damage rather than by discarding the file. Discarding proved wrong by two orders of magnitude: a single construct a grammar cannot follow damages a fraction of a percent of a file, and on real embedded code the rule turned 0.1%–1.4% damage into the loss of half a project's metrics and 137 correctly parsed functions. A grammar gap is a permanent condition of parsing a language without compiling it, so tolerating one locally is the general defence rather than a concession to one grammar's shortcomings.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 5](SDD.md), [SDD Section 7](SDD.md), [SDD Section 17](SDD.md).

*   <a id="HLR-036"></a>**HLR-036: Setup-Failure Fatality.**
    A runtime language-support location that is absent, unreadable, or that yields no valid language module whatsoever shall be treated as a fatal error; `elc` shall emit a diagnostic and abort the run before any file is processed, since it can perform no analysis at all in that state.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 6](SDD.md).

*   <a id="HLR-070"></a>**HLR-070: Malformed Language Module Tolerance.**
    An individual language module that is present but unusable — because it cannot be loaded, does not expose the expected entry point, or carries invalid or unparseable query files — shall not abort the run. `elc` shall emit a diagnostic to standard error identifying that language, exclude it from the run, and continue using the remaining valid language modules. Provided at least one valid language module remains, the run shall proceed and complete normally, and the unusable module shall not by itself cause a non-zero exit status.
    *Trace:* [SDD Section 6](SDD.md).

*   <a id="HLR-037"></a>**HLR-037: Truthful Exit Status.**
    `elc`'s process exit status shall be non-zero whenever any per-file failure occurred during the run (HLR-035), and shall be zero only when every discovered file was either processed without error or skipped under HLR-012. A file skipped because its language is unavailable is not a failure and shall not by itself make the exit status non-zero.
    *Trace:* [SDD Section 3](SDD.md).

*   <a id="HLR-120"></a>**HLR-120: Distinct Exit Status Classes.**
    `elc`'s exit status shall distinguish the two classes of failure, so that a caller can tell a degraded run from a run that never happened. A status of `0` shall indicate that every discovered file was processed without error. A status of `1` shall indicate that the run completed and produced a report, but at least one discovered file failed to be read or parsed (HLR-035, HLR-037). A status of `2` shall indicate that the run did not complete and no report was produced — a usage error (HLR-063), an invalid target (HLR-062), a fatal runtime-location failure (HLR-036), or a rejected saved record (HLR-058). No finding severity shall contribute to any of these (HLR-100).
    *Trace:* [SDD Section 3](SDD.md).

*   <a id="HLR-038"></a>**HLR-038: Diagnostics on stderr, Results on stdout.**
    `elc` shall never write diagnostic or error messages to the same stream used for reported results, so that captured or piped results are never corrupted by diagnostic text.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-062"></a>**HLR-062: Invalid Target Rejection.**
    When any target argument does not exist, cannot be opened or read, or is neither a regular file nor a directory — for example a socket, FIFO, or device node — `elc` shall emit a diagnostic to standard error identifying that target and shall terminate with a non-zero exit status without producing a partial report. All target arguments shall be validated before any analysis begins, so that an invalid target is reported regardless of how many other targets are valid, and no report is emitted that silently covers fewer targets than the user named.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 5](SDD.md).

*   <a id="HLR-063"></a>**HLR-063: Invalid Command-Line Rejection.**
    When `elc` is invoked with an unrecognized option, with a missing or malformed argument to an option that requires one, or without a required target, `elc` shall emit a usage message to standard error and shall terminate with a non-zero exit status without analyzing any file.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md), [SDD Section 6](SDD.md).

*   <a id="HLR-117"></a>**HLR-117: Help Request Is Not an Error.**
    `elc` shall provide a command-line option that prints a usage summary — its options, their arguments, and their defaults — to standard output, and shall terminate with a zero exit status when that option is given, since requesting help is not an error. This is distinct from the usage message of HLR-063, which is emitted to standard error with a non-zero status in response to an invalid invocation.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md).

*   <a id="HLR-066"></a>**HLR-066: Run With No Analyzable Files.**
    When a run completes and no file in the target was analyzed — because the target contained no source files, or because no discovered file's extension mapped to an available language — `elc` shall emit a well-formed report showing zero totals rather than emitting no output, and shall terminate with a zero exit status provided no per-file failure occurred (HLR-037).
    *Trace:* [SDD Section 13](SDD.md).

## 8. Non-Functional Constraints

Requirements constraining `elc`'s runtime environment, dependencies, execution model, and memory behaviour (PVD §6 Principles 3, 5, and 7, §7.2). No performance target is committed; see the Throughput theme in PVD §9.

*   <a id="HLR-039"></a>**HLR-039: Zero Configuration.**
    `elc`'s behavior shall be fully determined by its command-line arguments and the contents of its runtime language-support location. Stated observably: the presence of any configuration-like file — a dotfile, an `.elcrc`, an editor or tooling configuration — in the working directory, in the analysis target, or in any ancestor directory of either, shall produce output byte-identical to its absence. `elc` shall neither read nor discover such a file.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 6](SDD.md).

*   <a id="HLR-040"></a>**HLR-040: Excluded Runtime Dependencies.**
    `elc` shall not require an interpreter, a virtual machine, or network access at any point during execution, and shall not require code generation at build time.
    *Trace:* [SDD Section 22](SDD.md).

*   <a id="HLR-112"></a>**HLR-112: Library Selection Deferred to Design.**
    The specific third-party libraries `elc` links against shall be selected during design. Library names appearing in the PVD — for parsing, repository access, XML handling, and graph mathematics — are *suggested candidates rather than requirements*: a design that substitutes a different library satisfies this document provided the exclusions of HLR-040 are respected and the behaviour required elsewhere is delivered. The one exception is the Tree-sitter query language and grammar format, which are visible to the user in the `.scm` files and runtime grammars they author (HLR-009, HLR-107) and are therefore a product contract rather than an implementation choice.
    *Trace:* [SDD Section 22](SDD.md).

*   <a id="HLR-113"></a>**HLR-113: Graph Algorithms From an Established Library.**
    `elc` shall obtain its graph algorithms — cycle detection, topological ordering, reachability, and centrality — from an established graph library rather than hand-implementing adjacency structures and traversal algorithms, so that the correctness of the analyses in Sections 11 through 13 rests on proven code. Which library provides them is a design decision under HLR-112.
    *Trace:* [SDD Section 22](SDD.md).

*   <a id="HLR-041"></a>**HLR-041: Single-Threaded Execution.**
    `elc` shall perform the entire run — target discovery, parsing, metric computation, graph construction, and every graph analysis — sequentially on a single thread.
    *Trace:* [SDD Section 3](SDD.md).

*   <a id="HLR-124"></a>**HLR-124: Memory Safety.**
    `elc` shall complete every run without a memory-safety error: without reading or writing outside the bounds of any allocation or mapping, without accessing memory after it has been freed or unmapped, without an invalid or repeated free, and without acting on an uninitialised value. This shall hold on error paths as well as on the success path, and shall hold for every target type and every output format.
    *Trace:* [SDD Section 6](SDD.md), [SDD Section 7](SDD.md), [SDD Section 8](SDD.md), [SDD Section 13](SDD.md), [SDD Section 22](SDD.md).

*   <a id="HLR-125"></a>**HLR-125: Complete Resource Release.**
    `elc` shall release, before it exits, every heap allocation it made, every file mapping it created, and every dynamic-library handle it opened. A run that terminates for any reason other than a fatal signal shall leave no allocation unreleased, including runs that end in a usage error, an invalid target, or a rejected saved record.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 6](SDD.md), [SDD Section 7](SDD.md), [SDD Section 13](SDD.md), [SDD Section 22](SDD.md).

*   <a id="HLR-181"></a>**HLR-181: Self-Application.**
    The delivered source of `elc` shall satisfy the standard `elc` reports against: no function at or over the complexity threshold it applies by default, no dependency cycle among its own components, no source file it cannot parse, and no call site whose destination it cannot resolve to exactly one definition. A tool that reports a defect it exhibits cannot be relied on to report that defect in anyone else's code, and the claim is cheap to check because the tool is the checker — `elc src/` is the whole of the measurement. This is a property of what is delivered rather than of any run a user makes, and is verified in the manner of the build-configuration requirements.
    *Trace:* [SDD Section 22](SDD.md).

*   <a id="HLR-043"></a>**HLR-043: Read-Only Operation.**
    `elc` shall only read the files it analyzes; `elc` shall never modify, rewrite, or delete any file under analysis.
    *Trace:* [SDD Section 7](SDD.md).

## 9. Report Regeneration from Saved XML

Requirements governing `elc`'s ability to regenerate a Markdown report from a previously generated XML output file (HLR-054), without re-analyzing the original source (PVD §5 item 8, §7.1).

*   <a id="HLR-055"></a>**HLR-055: XML-to-Markdown Conversion Mode.**
    `elc` shall support an operating mode whose input is a previously generated XML output file (HLR-054) rather than a source-code target, and whose output is a Markdown report (HLR-029), without parsing or re-analyzing any original source file.

    Markdown is the mode's output and its default, so no format option is needed to reach it; a format explicitly selected and other than Markdown is a usage error (HLR-063). **The extension of an output filename is such a selection.** HLR-148 makes an extension a statement of the format and HLR-149 makes it the same statement the format option makes, so `--from-xml rec.xml -o out.txt` asks for a table exactly as `-f table` does, and is rejected on the same terms. Reading it as anything less would have the mode write Markdown into a file named `out.txt` — one format under a name promising another, which is the result HLR-148 exists to forbid. An output filename naming Markdown is accepted, since it agrees with what the mode produces.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md), [SDD Section 16](SDD.md).

*   <a id="HLR-056"></a>**HLR-056: Regenerated Report Equivalence.**
    For a given complexity threshold *and verbosity*, the Markdown report `elc` produces by converting a previously generated XML output file (HLR-055) shall be **byte-identical** to the Markdown report `elc` would have produced by analyzing the original target directly to Markdown with that same threshold and verbosity — including the project summary, each file's totals and threshold list, full per-function detail, the architectural findings of Sections 11 through 14, and any custom-rule matches recorded in the source XML.

    Verbosity joins the threshold as a property of the *rendering* rather than of the record. Both are supplied when the report is produced, neither is read back from the record, and the record carries every measurement either could select from (HLR-152) — so one record answers a summary question and a verbose one alike, and answers each identically to a direct run.
    *Trace:* [SDD Section 16](SDD.md).

*   <a id="HLR-057"></a>**HLR-057: User-Supplied Threshold at Regeneration Time.**
    The user shall be able to supply a complexity threshold when converting a saved XML file to Markdown (HLR-055), independently of any threshold that may have applied when the XML file was originally generated; this threshold shall default to 15, consistent with HLR-022, when the user does not supply one.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 16](SDD.md).

*   <a id="HLR-122"></a>**HLR-122: No Companion Artefacts From a Saved Record.**
    The XML record of HLR-054 carries the findings of a run rather than the topology of the System Dependence Graph, so neither the Graphviz `.dot` call tree (HLR-102) nor the GraphML export (HLR-106) can be reconstructed from it. The conversion mode of HLR-055 shall therefore produce the Markdown report alone, notwithstanding the default-on rule of HLR-103; and a command line that explicitly requests a companion artefact together with conversion mode shall be rejected as a usage error (HLR-063) rather than silently ignored.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md), [SDD Section 17](SDD.md).

*   <a id="HLR-058"></a>**HLR-058: Malformed or Unsupported Saved-XML Rejection.**
    When the input to the conversion mode of HLR-055 is not well-formed XML, does not match `elc`'s own output structure, or carries a format-version identifier (HLR-061) that this build of `elc` does not support, `elc` shall reject it with a diagnostic and a non-zero exit status rather than attempting a best-effort partial conversion.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 16](SDD.md).

## 10. System Dependence Graph Construction

Requirements governing how `elc` resolves per-file syntax trees against one another into a single project-wide System Dependence Graph (SDG), which every analysis in Sections 11 through 14 operates over (PVD §5 item 7, §7.1 "Macro-architectural analysis").

*   <a id="HLR-073"></a>**HLR-073: System Dependence Graph Construction.**
    `elc` shall resolve the per-file syntax trees produced by Section 3 against one another — matching each call site to the definition it invokes across file boundaries — and shall assemble the result into a single directed graph, the System Dependence Graph (SDG), whose nodes are the functions of HLR-014 and whose edges are the calls between them.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 8](SDD.md).

*   <a id="HLR-074"></a>**HLR-074: Global State Edges.**
    In addition to call edges, the SDG shall record, for each global variable or fixed memory address referenced in the target, an edge from every function that writes it and an edge to every function that reads it, so that coupling through shared state is represented in the graph alongside coupling through calls.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 8](SDD.md).

*   <a id="HLR-075"></a>**HLR-075: Whole-Project Graph Scope.**
    The SDG shall span the entire analysis target — whether that target is an application or a library, and across every target argument supplied under HLR-071 — so that every analysis derived from it describes the project as a whole rather than any single file.
    *Trace:* [SDD Section 8](SDD.md).

*   <a id="HLR-076"></a>**HLR-076: Graph Built From the Single Parse.**
    `elc` shall construct the SDG entirely from the data produced by the parse of each file required by HLR-013; it shall not re-read or re-parse any source file in order to resolve cross-file references or to perform any graph analysis.

    **A file that is macro-expanded is parsed twice, and only twice** (HLR-208): once as written, to answer the conditional-region questions of HLR-133 that expansion would destroy, and once expanded, for every measurement and every graph edge. Neither parse resolves a cross-file reference, which is what this requirement is about — the second reads the same file's own expanded text, not another file's.

    A file that is not expanded is parsed once, as it always was, so the cost falls only where the benefit does.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 7](SDD.md), [SDD Section 8](SDD.md).

*   <a id="HLR-077"></a>**HLR-077: Unresolvable Call Handling.**
    A call site whose target cannot be resolved within the analysis target — for example a call into an external library, a system call, or an indirect call through a function pointer that cannot be determined statically — shall not abort graph construction. `elc` shall record the call as unresolved, shall exclude it from analyses that require a known destination, and shall report the count of unresolved calls so that the reader can judge the graph's completeness.
    *Trace:* [SDD Section 8](SDD.md), [SDD Section 10](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-115"></a>**HLR-115: Analyses Requiring User Declarations.**
    An analysis whose inputs include a user declaration — architectural strata (HLR-078), entry points (HLR-095), or execution scopes (HLR-094) — shall be performed only when that declaration is supplied. Where it is not supplied, `elc` shall omit the analysis and shall state in the report that it was omitted and why, rather than reporting an empty or misleading result, and rather than treating the omission as an error or a failure. In particular, when no entry points are declared, `elc` shall not report every function as unreachable.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 10](SDD.md), [SDD Section 11](SDD.md), [SDD Section 13](SDD.md), [SDD Section 14](SDD.md).

## 11. Architectural Layering and Coupling Analysis

Requirements governing the layering, coupling, and dependency-cycle analyses `elc` performs over the SDG (PVD §7.1, Appendix A.1, A.3).

*   <a id="HLR-114"></a>**HLR-114: Definition of a Component.**
    For the purposes of the coupling, instability, and dependency-cycle analyses of this section, a *component* is a single source file (translation unit). A dependency exists from component X to component Y when any function in X calls any function in Y, or when any function in X writes a global that a function in Y reads. Analyses expressed per *function* — fan-out (HLR-085), call-chain depth (HLR-087), and recursion (HLR-089) — operate on the individual function nodes of the SDG and are deliberately distinct from the component-level analyses here.
    *Trace:* [SDD Section 8](SDD.md), [SDD Section 9](SDD.md).

*   <a id="HLR-078"></a>**HLR-078: User-Declared Architectural Strata.**
    `elc` shall accept, as command-line arguments, a set of user-declared architectural strata — named layers such as Application Logic, Hardware Abstraction, and Driver — together with the mapping of components to those layers and the permitted direction of dependency between them. Strata shall never be discovered automatically from the filesystem or from any configuration file.

    The architecture recovery of HLR-172 does not qualify that rule and is not an exception to it. What recovery produces is a *proposal* a user reads; what this requirement governs is the *declaration* the conformance analyses measure against, and nothing `elc` derives from a graph ever becomes one. A recovered layering takes effect only where a user has read it, agreed with it, and passed it back as the arguments this requirement describes (HLR-173).
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 9](SDD.md).

*   <a id="HLR-079"></a>**HLR-079: Skip-Level Call Detection.**
    Given declared strata (HLR-078), `elc` shall traverse the SDG and report every "skip-level" call — a call that bypasses one or more intervening layers, such as application code invoking driver logic directly rather than through the hardware abstraction layer — identifying the calling function, the called function, and the layers crossed.
    *Trace:* [SDD Section 9](SDD.md).

*   <a id="HLR-118"></a>**HLR-118: Direction-Inverted Call Detection.**
    Given declared strata and the permitted direction of dependency between them (HLR-078), `elc` shall report every call whose direction is inverted with respect to that declaration — for example a driver-layer function calling upward into application logic — identifying the calling function, the called function, and the layers involved. A direction-inverted call is a finding distinct from the skip-level call of HLR-079: a call may invert the declared direction without bypassing any intervening layer, and may bypass layers without inverting direction.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 9](SDD.md).

*   <a id="HLR-080"></a>**HLR-080: Afferent and Efferent Coupling.**
    For every component (HLR-114) in the SDG, `elc` shall compute and report its afferent coupling (`Ca`, fan-in — the number of components that depend upon it) and its efferent coupling (`Ce`, fan-out — the number of components it depends upon).
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-081"></a>**HLR-081: Architectural Bottleneck Identification.**
    `elc` shall identify and report as an architectural bottleneck every component (HLR-114) whose afferent coupling and efferent coupling are *each* greater than or equal to a bottleneck threshold, since such a component is simultaneously depended upon widely and dependent widely, and is therefore both dangerous to change and difficult to isolate. The threshold shall default to 5 and shall be user-configurable. Unlike the thresholds of Section 14, this one is `elc`'s own heuristic rather than a published standard, and shall be identified as such wherever it is reported (HLR-099).
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 9](SDD.md).

*   <a id="HLR-082"></a>**HLR-082: Instability Metric.**
    For every component (HLR-114) in the SDG, `elc` shall compute and report the Instability metric `I = Ce / (Ce + Ca)` from the coupling values of HLR-080, and shall report it alongside the interpretation guidance of PVD Appendix A.1 — that `I` approaching 0 denotes maximum stability (high fan-in, low fan-out, dangerous to change) and `I` approaching 1 denotes maximum instability (high fan-out, low fan-in, freely changeable). Where a component's `Ce` and `Ca` are both zero, `elc` shall report the metric as undefined rather than dividing by zero.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-083"></a>**HLR-083: Circular Dependency Detection.**
    `elc` shall detect every cyclic dependency *between components* (HLR-114) in the SDG by topological analysis, and shall report each cycle together with the ordered sequence of components that form it (for example A → B → C → A). Recursion between individual functions is a distinct, function-level finding governed by HLR-089: mutual recursion between functions residing in the same component is not a component-level cycle and shall not be reported as one.
    *Trace:* [SDD Section 9](SDD.md).

*   <a id="HLR-084"></a>**HLR-084: Cycles Reported at Critical Severity.**
    Every cyclic dependency detected under HLR-083 shall be reported at critical severity, the acceptable count of cycles being strictly zero, because a cycle fuses its participants into a single strongly connected unit that cannot be unit-tested in isolation or linked incrementally.
    *Trace:* [SDD Section 9](SDD.md).

## 12. Call Tree Dimensionality

Requirements governing `elc`'s analysis of the geometric shape of the call tree — its width and its height (PVD §7.1, Appendix A.2).

*   <a id="HLR-085"></a>**HLR-085: Function Fan-Out Measurement.**
    For every function in the SDG, `elc` shall compute and report its fan-out: the number of distinct subroutines it invokes directly.
    *Trace:* [SDD Section 8](SDD.md), [SDD Section 10](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-086"></a>**HLR-086: Fan-Out Threshold Classification.**
    `elc` shall classify each function's fan-out (HLR-085) against the published thresholds of PVD Appendix A.2. The bands shall be exhaustive, so that every possible fan-out value has exactly one classification: a fan-out of 0 to 2 lies below the healthy band and produces no finding; 3 to 7 is the healthy range and produces no finding; 8 to 10 is acceptable and produces no finding; 11 to 15 produces a **warning**, indicating weak abstraction and poor delegation; and greater than 15 produces a **critical** finding — a monolithic dispatcher or "god function" that violates the Single Responsibility Principle and resists isolation for unit testing.
    *Trace:* [SDD Section 12](SDD.md).

*   <a id="HLR-087"></a>**HLR-087: Maximum Call-Chain Depth.**
    `elc` shall compute and report the maximum call-chain depth of the SDG — the greatest number of nested call layers reachable from any declared entry point (HLR-095) — and shall report it against the embedded guidance of PVD Appendix A.2, under which depths beyond 8 to 12 layers risk stack-versus-heap collision on severely stack-constrained targets. The reported depth is a lower bound on true worst-case depth, since a chain continuing through a call that could not be resolved (HLR-077) is not followed; `elc` shall therefore report the depth together with the unresolved-call count, so that its completeness can be judged. Where no entry points are declared, this analysis shall be omitted under HLR-115.
    *Trace:* [SDD Section 10](SDD.md), [SDD Section 12](SDD.md).

*   <a id="HLR-088"></a>**HLR-088: Deepest Call Stack Reported in Full.**
    `elc` shall report the deepest call stack itself — the ordered sequence of functions from entry point to deepest leaf — and not merely its depth as a number, so that the specific path responsible for worst-case stack consumption can be inspected and shortened rather than only measured.
    *Trace:* [SDD Section 10](SDD.md).

*   <a id="HLR-089"></a>**HLR-089: Recursion Detection.**
    `elc` shall statically detect the presence of recursion — direct or mutual — in the SDG and shall report each recursive cycle, in accordance with MISRA C Rule 17.2, so that the absence of recursion can be established and the call tree confirmed to be a Directed Acyclic Graph whose maximum stack usage is predictable.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 10](SDD.md), [SDD Section 12](SDD.md).

*   <a id="HLR-090"></a>**HLR-090: Depth Reporting Under Recursion.**
    Where recursion is detected (HLR-089), maximum call-chain depth is unbounded and no deepest call stack exists. `elc` shall report the recursive cycle in place of a depth figure, rather than reporting a misleading finite number or failing to terminate.
    *Trace:* [SDD Section 10](SDD.md).

## 13. Global State and Reachability Analysis

Requirements governing `elc`'s analysis of shared-state coupling and of dead code (PVD §7.1, Appendix A.4).

Dead code is reported at two scales, by two different means, and the distinction is worth holding onto. **Between** functions it is a question about the call graph: a function no path reaches from any entry point is unreachable, and that is proved by traversal (HLR-096, HLR-097). **Within** a function it is a question about syntax: a statement after a `return`, or the body of an `if` whose condition is written `0`, cannot execute whatever the graph says, and that is established from the syntax tree alone (HLR-137). A function may be perfectly reachable and still contain code that is not, so neither analysis subsumes the other and both are reported.

*   <a id="HLR-091"></a>**HLR-091: Global Access Mapping.**
    For every global variable or fixed memory address represented in the SDG (HLR-074), `elc` shall report the set of functions that write it and the set of functions that read it.
    *Trace:* [SDD Section 11](SDD.md).

*   <a id="HLR-092"></a>**HLR-092: Scope-Reduction Candidates.**
    Where the read and write edges for a global originate from only a single function, `elc` shall flag that object for scope reduction, in accordance with MISRA C Rule 8.9 — an object should be defined at block scope if its identifier appears in only one function.
    *Trace:* [SDD Section 11](SDD.md).

*   <a id="HLR-093"></a>**HLR-093: Hidden Channel Detection.**
    Where the read and write edges for a global originate from multiple otherwise-disconnected domains of the SDG, `elc` shall flag that object as a hidden channel, identifying the disconnected participants, since such an object constitutes temporal coupling in which function execution order silently governs system stability.
    *Trace:* [SDD Section 11](SDD.md).

*   <a id="HLR-094"></a>**HLR-094: Memory Map Boundary Validation.**
    `elc` shall accept, as command-line arguments, a declaration of execution scopes for targets whose components share overlapping memory maps and symbol tables — such as host-driven sequential test harnesses — naming the components belonging to each scope. Given such a declaration, `elc` shall traverse the SDG and report every call edge and every global-state edge by which one declared scope reaches a function or object belonging to another, so that scope isolation can be verified. Where no execution scopes are declared, this analysis shall be omitted under HLR-115.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 11](SDD.md).

*   <a id="HLR-095"></a>**HLR-095: User-Declared Entry Points.**
    `elc` shall accept, as command-line arguments, the set of entry points from which reachability is measured — for example `main()`, interrupt vector handlers, and exported API boundaries. Entry points shall never be inferred implicitly or read from a configuration file.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 11](SDD.md).

*   <a id="HLR-096"></a>**HLR-096: Dead Code Detection by Reachability.**
    `elc` shall traverse the SDG from a root set comprising the declared entry points of HLR-095 *together with* every function whose address is taken without being directly called, and shall report every function and data structure not visited during that traversal as unreachable. Address-taken functions are roots because they may be invoked indirectly — through an interrupt vector table, a callback array, or a stored function pointer — and omitting them would report live code as provably dead. The asymmetry is deliberate: an additional root can only shrink the unreachable set, whereas a missing root produces a false claim of death.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 11](SDD.md).

*   <a id="HLR-097"></a>**HLR-097: Dead Code Determined by Graph Mathematics.**
    The unreachability of HLR-096 shall be established solely by graph reachability, never by textual or heuristic means; in particular, a group of unused functions that call one another shall be correctly reported as unreachable, since no path reaches the group from any declared entry point.
    *Trace:* [SDD Section 11](SDD.md).

*   <a id="HLR-137"></a>**HLR-137: Intra-Procedural Dead Code Detection.**
    Within each analysed function, `elc` shall detect and report every statement that cannot execute, and shall report each with its file, its enclosing function, and its line range so that it can be located and removed. Two classes shall be detected:

    *   **Statements following a terminator.** Where control leaves a block unconditionally — by returning, breaking, continuing, or transferring control — every statement after that point in the same block is unreachable, up to the first construct that can be entered other than by falling through it, such as a label or a switch case.
    *   **Branches disabled by a literal condition.** Where a condition is written as a literal whose value is fixed by the source text, the branch that value excludes is unreachable: the body of `if (0)` and of a loop whose condition is literally false, and the alternative of `if (1)`.

    This analysis is independent of the reachability of HLR-096: a function reached from an entry point may contain unreachable statements, and a function that is itself unreachable may contain none. Both shall be reported, and a statement shall not be omitted from this analysis because the function containing it was reported unreachable.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-138"></a>**HLR-138: Dead Code Within Functions Determined Syntactically.**
    The unreachability of HLR-137 shall be established from the syntax tree alone. `elc` shall perform no data-flow analysis, no constant propagation, and no evaluation of expressions: a condition is a literal only where the source writes a literal, so `if (0)` is detected and `x = 0; if (x)` is not, and a branch guarded by a variable is never claimed to be dead however clearly its value can be inferred by a reader.

    The consequence is deliberate and asymmetric. `elc` will **miss** dead code that only data-flow analysis could prove, and shall never **claim** dead code that is live. A missed statement costs a cleanup opportunity; a false claim invites the removal of code that runs, which is a defect this tool would have introduced. Where the two cannot both be had, the analysis shall report nothing rather than report a guess.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-139"></a>**HLR-139: Dead-Code Support Is Per Language and Its Absence Is Stated.**
    The constructs that terminate a block, the constructs that can be entered without falling through, and the literals that fix a condition are properties of a language and shall be supplied as runtime data by its language module, never compiled into the executable (HLR-010, HLR-121). A language module that supplies no such data is valid: `elc` shall analyse files in that language for every other measurement, and shall report that dead-code analysis was not performed for that language rather than reporting that none was found. The two are different claims, and a reader who cannot tell them apart has been told nothing.
    *Trace:* [SDD Section 6](SDD.md), [SDD Section 7](SDD.md), [SDD Section 13](SDD.md).

## 14. Threshold Evaluation and Severity Reporting

Requirements governing how `elc` evaluates its measurements against published industry and academic thresholds, and the limits of the conclusions it draws (PVD §7.1, §7.3, Appendix A).

*   <a id="HLR-098"></a>**HLR-098: Evaluation Against Published Thresholds.**
    `elc` shall evaluate each architectural measurement it computes against the published academic and safety-critical industry thresholds recorded in PVD Appendix A, and shall report where the measurement falls relative to the accepted range, so that a value is presented with the context needed to act on it rather than as a bare figure.
    *Trace:* [SDD Section 12](SDD.md).

*   <a id="HLR-099"></a>**HLR-099: Threshold Attribution.**
    Every threshold `elc` reports against shall be attributed to its external source — for example MISRA C and its rule number, Robert C. Martin's Instability metric, McCabe's complexity limits as NIST SP 500-235 records them, or the Henry-Kafura information-flow metrics — so that the reader can distinguish a published standard from a choice made by `elc`.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 11](SDD.md), [SDD Section 12](SDD.md).

*   <a id="HLR-123"></a>**HLR-123: Severity Vocabulary.**
    Every finding `elc` reports shall carry exactly one severity, drawn from the closed and ordered set `info` < `warning` < `critical`. No other severity value shall be emitted, and no finding shall be emitted without one. Where more than one threshold band applies to a single measurement, the highest applicable severity shall be the one reported.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 12](SDD.md).

*   <a id="HLR-100"></a>**HLR-100: Severity Labels Do Not Affect Exit Status.**
    A finding reported at any severity, including critical, shall be a label within the report and shall have no effect on `elc`'s process exit status, which remains reserved for the failure conditions of Section 7. Deciding what action a finding warrants is the caller's responsibility.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-101"></a>**HLR-101: No Remediation Advice.**
    `elc` shall report measurements, threshold positions, and violations of user-supplied criteria; it shall not propose a fix, rank one design as better than another beyond what a cited standard already states, or apply any style opinion of its own invention.

    The classifications and proposal of Section 22 are bounded by this requirement rather than exempt from it, and the boundary is between *describing* a structure and *prescribing* one. Calling a function a god object states where it sits in the graph, and a recovered layering states the order the graph already has; neither says the design is wrong nor what to do about it. That is why no classification carries a severity or becomes a finding (HLR-171), why the thresholds behind them are labelled as `elc`'s own, and why a proposal is never measured against (HLR-173). A recovery that ranked the recovered architecture against the declared one, or named a component as belonging somewhere else, would cross into the advice this requirement forbids.
    *Trace:* [SDD Section 12](SDD.md).

## 15. Graph Output Formats

Requirements governing `elc`'s graph-specific outputs: the Graphviz call tree for human inspection, and the standard graph serialisation for machine ingestion (PVD §5 item 9, §7.1 "Output").

*   <a id="HLR-102"></a>**HLR-102: Graphviz .dot Call Tree Output.**
    `elc` shall emit the call tree in Graphviz `.dot` format, so that it can be rendered and inspected visually. Producing the `.dot` file shall require no library dependency and no invocation of Graphviz; Graphviz is a tool the user may separately run upon the output.
    *Trace:* [SDD Section 17](SDD.md).

*   <a id="HLR-103"></a>**HLR-103: .dot Generation Enabled by Default.**
    Generation of the `.dot` call tree (HLR-102) shall be enabled by default, and shall be disableable by a command-line option for runs that do not want the additional artefact.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md), [SDD Section 17](SDD.md).

*   <a id="HLR-104"></a>**HLR-104: No .dot Output to Standard Output.**
    When `elc` writes its report to standard output rather than to a named output file, no `.dot` file shall be produced, whether or not generation was disabled under HLR-103, since no output path exists from which to derive the `.dot` file's name and graph markup must never enter the result stream (HLR-038).
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 17](SDD.md).

*   <a id="HLR-119"></a>**HLR-119: Companion Artefact Naming.**
    The `.dot` call tree (HLR-102) and the GraphML export (HLR-106) shall each derive its filename from the report's output path by substituting the corresponding extension — an output of `report.md` yielding `report.dot` and `report.graphml`. Neither shall accept an output path of its own. This derivation is precisely why neither can be produced when the report is written to standard output (HLR-104): there is no output path from which to derive a name.

    Every companion artefact `elc` adds shall follow this same rule, including the raw and purified drawings of HLR-178 and the purification manifest of HLR-175 where it is written rather than read. One derivation rule for every companion is what keeps the set of files a run produces predictable from the one path the user gave, and what makes "no output path, no companions" a single fact rather than a list of exceptions.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 17](SDD.md).

*   <a id="HLR-105"></a>**HLR-105: Annotated .dot Output.**
    When a `.dot` call tree is emitted, `elc` shall annotate it with every architectural finding that applies to a node it contains: components and functions exceeding the coupling and fan-out thresholds (HLR-081, HLR-086), the functions forming the deepest call chain (HLR-088), the members of each dependency cycle (HLR-083) and each recursive cycle (HLR-089), unreachable functions (HLR-096), and functions participating in a hidden channel (HLR-093). Annotations shall use Graphviz attributes that degrade gracefully, so that a renderer ignoring them still produces a valid and readable call tree.
    *Trace:* [SDD Section 17](SDD.md).

*   <a id="HLR-106"></a>**HLR-106: Standard Graph Serialisation Export.**
    `elc` shall support exporting the SDG in a standard graph serialisation schema (GraphML), so that the graph can be ingested and processed by other tools rather than only rendered for viewing. Export shall be requested by an explicit command-line option and shall be disabled by default, in contrast to the `.dot` call tree of HLR-103. As with `.dot` (HLR-104), no GraphML file shall be produced when the report is written to standard output, since no output path exists from which to derive its name.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 17](SDD.md).

## 16. User-Supplied Custom Rules

Requirements governing the mechanism by which a user expresses and checks a custom coding standard (PVD §7.1 "Custom rules").

*   <a id="HLR-107"></a>**HLR-107: User-Supplied Rule Queries.**
    `elc` shall accept user-supplied Tree-sitter `.scm` query files expressing a custom coding standard, and shall check the analysed source against them using the same query mechanism that produces `elc`'s built-in metrics. Because a Tree-sitter query compiles against one specific grammar, every custom rule shall be bound to the language it applies to: a rule placed in the runtime location is bound by the language directory that contains it, and a rule named on the command line shall name its language alongside its path. A rule naming a language for which no module is available shall be reported and skipped, not compiled.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 6](SDD.md).

*   <a id="HLR-108"></a>**HLR-108: Custom Rules Require No Rebuild.**
    Adding, altering, or removing a custom rule shall require only a change to a `.scm` file placed in the runtime location or named on the command line; it shall require no modification to, and no recompilation of, the `elc` executable.
    *Trace:* [SDD Section 6](SDD.md).

*   <a id="HLR-109"></a>**HLR-109: Custom Rule Match Reporting.**
    `elc` shall report each match of a user-supplied rule alongside its built-in findings, identifying the rule that matched and the file and line range of each occurrence. A rule's identity shall be the basename of the `.scm` file that contains it together with the capture name that matched, so that a single rule file may express several independently identified rules.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-110"></a>**HLR-110: No Automatic Rule Discovery.**
    `elc` shall use only those custom rule files explicitly named on the command line, with the language binding required by HLR-107, or present in the appropriate language directory of its runtime location; it shall never discover a rule file automatically from the working directory, the analysis target, or any dotfile, so that two users running the same command on the same tree obtain the same result.
    *Trace:* [SDD Section 6](SDD.md).

*   <a id="HLR-116"></a>**HLR-116: Invalid Custom Rule File Handling.**
    A custom rule file that cannot be read, or whose query text is not valid, shall be handled according to how it was supplied. A rule file named explicitly on the command line is a user error: `elc` shall emit a diagnostic and terminate with a non-zero exit status without analyzing any file, consistent with HLR-063. A rule file found in the runtime location is a malformed runtime component: `elc` shall emit a diagnostic identifying it, exclude that rule from the run, and continue, consistent with HLR-070.
    *Trace:* [SDD Section 6](SDD.md).

*   <a id="HLR-111"></a>**HLR-111: Custom Rules Carry No Built-In Opinion.**
    `elc` shall report what a user-supplied rule matched without forming any judgement as to whether the rule itself is appropriate, and without supplying rules of its own beyond the metrics and thresholds specified elsewhere in this document.
    *Trace:* [SDD Section 12](SDD.md).

## 17. User Documentation

Requirements governing the end-user documentation delivered with `elc`. These constrain the project's deliverables rather than the executable's behaviour, in the manner of HLR-011.

*   <a id="HLR-128"></a>**HLR-128: User Manual and Man Page Delivered.**
    The elocker project shall deliver end-user documentation comprising a **user manual**, describing every capability, option, output format, and reported finding with worked examples, and a **`man` page** for `elc` giving the reference form of the same material. Both shall be installed alongside the executable and its runtime directory by the project's install target, the man page into the section-1 manual path.
    *Trace:* [SDD Section 1.2](SDD.md).

*   <a id="HLR-129"></a>**HLR-129: Documentation Describes the Delivered Behaviour.**
    The delivered documentation shall describe the behaviour of the version it ships with. Every command-line option `elc` accepts, every report format it produces, every companion artefact it writes, and every category of finding it reports shall appear in both the manual and the man page. An option present in `elc`'s usage summary but absent from the documentation, or documented but not accepted, shall be a defect of the same standing as a failing test.
    *Trace:* [SDD Section 1.2](SDD.md).

*   <a id="HLR-130"></a>**HLR-130: Documentation Updated With the Behaviour It Describes.**
    Documentation shall be updated in the same change as the behaviour it describes, never in a subsequent one. A change that adds, removes, or alters an option, a format, an artefact, or a finding category and does not update both documents is incomplete.
    *Trace:* [SDD Section 1.2](SDD.md).

## 18. Conditional Compilation

Requirements governing how `elc` reports a code base whose source is conditionally compiled, so that a metric describes the configuration a user names rather than the union of every configuration the source can express.

*   <a id="HLR-131"></a>**HLR-131: Conditional-Compilation Configuration.**
    `elc` shall accept zero or more conditional-compilation symbol definitions as command-line arguments, and shall use them to decide which branches of a conditionally compiled region are active. When no definition is supplied, the reported metrics shall be exactly those `elc` reports for the same source with the option absent, so that the capability is opt-in and adding it changes no existing result. A condition that is a constant decides the same way in every configuration and is therefore applied whether or not any definition was supplied; only a condition depending on a definition needs one.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 7](SDD.md).

*   <a id="HLR-132"></a>**HLR-132: Inactive-Region Exclusion.**
    Every statement, decision point, call site, and global access lying within a region the supplied definitions render inactive shall be excluded from every reported metric and every graph fact derived from that file. A code base in which a substantial part of each file is compiled out for a given target otherwise reports the union of all its configurations, which describes no build that exists and overstates every measure taken from it.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 8](SDD.md).

*   <a id="HLR-133"></a>**HLR-133: Undecidable Conditions Left Active.**
    When the condition governing a region cannot be decided from the supplied definitions alone, `elc` shall treat both branches as active, and shall report the number of regions so treated, so that the completeness of the pruning is visible in the same way the completeness of the graph is (HLR-077). Silently discarding code whose condition was not understood would produce a report that is confidently wrong and indistinguishable from a correct one; over-counting is visible and is the safe direction.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-134"></a>**HLR-134: Conditional Constructs Defined by Runtime Data.**
    The constructs that introduce a conditionally compiled region, the location of the condition governing it, and the branches it selects between, shall be identified by the language's runtime query configuration (HLR-009) rather than by logic compiled into the executable, so that a language whose conditional compilation differs in form — a C preprocessor conditional, a Rust `cfg` attribute — is supported by the same mechanism and requires no change to the executable (HLR-010).
    *Trace:* [SDD Section 6](SDD.md), [SDD Section 7](SDD.md).

*   <a id="HLR-135"></a>**HLR-135: Conditional Regions Decided Without a Preprocessor.**
    `elc` shall determine which regions are active from the syntax tree it has parsed, and shall not read any file the analysed source refers to for the purpose of resolving a condition. A condition whose value depends on definitions `elc` was not given is undecidable rather than false (HLR-133).

    **This requirement previously forbade invoking a toolchain preprocessor at all.** That prohibition is withdrawn and replaced by the bounded permission of HLR-202: `elc` may run the compiler's preprocessor over a source file to expand its macros before parsing it. The two are not in conflict, because they answer different questions. Expansion asks *what does this text mean*, and the compiler is the only authority on that. Conditional-region reporting asks *which branches did this build take, and which could not be decided* — and a preprocessor answers that by silently discarding the branches it did not take, which is the opposite of reporting them (HLR-133, HLR-134).

    So the undecided-region count is computed from the tree as before, and is unaffected by whether the buffer that produced the tree was expanded. What the reasoning behind the old prohibition got right is preserved elsewhere: the dependence on an installed toolchain is real, is bounded by the fallback of HLR-205, and is declared per file by HLR-206 rather than left to be discovered.
    *Trace:* [SDD Section 7](SDD.md).

*   <a id="HLR-136"></a>**HLR-136: Configuration Recorded and Reported.**
    The set of definitions in force shall appear in the report and in the saved XML record (HLR-054), so that a report states the configuration it describes and a report regenerated from a record remains byte-identical to the one produced directly (HLR-056). Because pruning is applied when a file is measured and not when a report is rendered, supplying a definition together with the regeneration mode of HLR-055 shall be rejected as a usage error (HLR-063) rather than silently ignored.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 13](SDD.md), [SDD Section 16](SDD.md).

## 19. Linked-Image Filtering

Requirements governing how `elc` reports only the code a build actually kept, so that a metric describes the image that ships rather than the source it was drawn from.

This is the same question Section 18 asks and a different way of answering it. Conditional compilation *re-decides* the conditions a build resolved, from definitions the user restates; a linked image *observes what the build did*, having been produced by the real toolchain with the real flags. Where both are available the image is the stronger evidence, and neither replaces the other.

The image answers that question at two granularities, and which of them is available depends on what the build wrote. Its **symbol table** names the functions the link kept, and is present in any image a linker produced by default (HLR-140 – HLR-147). Its **debug line information**, where the build emitted any, additionally maps machine instructions back to the source lines that produced them — and so answers the question the symbol table cannot: which lines *inside* a surviving function this build actually compiled (HLR-153 – HLR-155). Debug information is never required; its absence costs the finer granularity and nothing else.

*   <a id="HLR-140"></a>**HLR-140: Linked-Image Function Filter.**
    `elc` shall accept the path of a linked image as a command-line argument, shall extract the set of functions that image defines, and shall restrict every measurement and every analysis to the source functions appearing in that set. Source holds functions a build does not keep — excluded by configuration, discarded by the linker as unreachable, or belonging to a translation unit the link never included — and a report covering them describes no image that exists and overstates every measure taken from it. When no image is supplied, no function shall be excluded and the reported metrics shall be exactly those `elc` reports for the same source with the option absent, so that the capability is opt-in and adding it changes no existing result.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 7](SDD.md), [SDD Section 18](SDD.md).

*   <a id="HLR-141"></a>**HLR-141: Image Read Without a Toolchain.**
    `elc` shall obtain the function set from the named image alone: it shall not invoke a toolchain utility — `nm`, `objdump`, `readelf`, a compiler, a linker, or a build system — shall not search for an image the user did not name, and shall not require the image to carry debugging information. Requiring a toolchain *to read an image* would make a filtered run depend on which one is installed, for no gain: the symbol table is a documented format and `elc` reads it directly. Searching for an image would breach the zero-configuration guarantee of HLR-039, under which nothing is read that the user did not name. This requirement governs the image alone, and is untouched by the preprocessor permission of HLR-202 — which concerns source text, runs before any parse, and cannot affect what an image contains. The symbol table a linker writes by default is sufficient, and requiring more would restrict the option to builds made for debugging.

    "Shall not require" is not "shall not read". Where the image happens to carry debug line information, `elc` reads it to prune at line granularity (HLR-153), and reads it from the image directly under this requirement's own terms — no toolchain utility, and no file the user did not name. What this requirement forbids is *depending* on debug information: an image without it remains fully usable for everything Section 19 describes at function granularity.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 18](SDD.md).

*   <a id="HLR-142"></a>**HLR-142: Linkage Names Resolved to Source Names.**
    An image records a function under its linkage name, which for every language `elc` supports other than C is an encoding of the source name rather than the source name itself. `elc` shall resolve a linkage name to the function name its report presents wherever that name is encoded by a published mangling scheme, and shall match on the resolved name. Matching raw linkage names alone would retain nothing at all for C++ or Rust, and a filter that silently retains nothing is indistinguishable from a project that has no functions in it. Which schemes a build resolves is a design decision under HLR-112; a name encoded by a scheme this build does not resolve is reported under HLR-143 rather than silently dropped.
    *Trace:* [SDD Section 18](SDD.md).

*   <a id="HLR-143"></a>**HLR-143: Both Directions of Mismatch Counted and Reported.**
    `elc` shall report the number of functions the image defines that it could not resolve to a source name, and shall list the source functions the image does not define. Neither shall be a failure of the run. The first states the completeness of the resolution, which is the claim the unresolved-call count of HLR-077 makes about the graph and for the same reason: a filter whose accuracy is unstated cannot be acted on. The second is the finding the option exists to produce — the source functions this build did not keep — and it is dead code established by what the linker did rather than inferred from the call graph (HLR-096), so the two lists are reported separately and neither is presented as the other.
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 18](SDD.md).

*   <a id="HLR-144"></a>**HLR-144: Scope of the Filter.**
    A function the image does not define shall contribute to no reported metric and shall become no node of the System Dependence Graph, so that every analysis of Sections 11 through 14 describes the image rather than the source. A call whose target was filtered out shall be counted as unresolved (HLR-077) rather than resolved to a function that is not in the image; inventing that edge would make the reachability claim of HLR-096 unsound in the one direction it is not already known to err.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 8](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-145"></a>**HLR-145: Code Outside Any Function Retained and Separately Reported.**
    Effective lines of code belonging to no function — an initialised object at file scope, say — shall be retained, and shall be reported as a figure of their own whenever a filter is in force, rather than excluded alongside the functions or folded silently into the totals. The image's *function* set says nothing about code outside a function, so excluding it would be a claim `elc` cannot support; and folding it in would hide the one part of the total the filter did not narrow, leaving a reader unable to tell a file of retained functions from a file of retained data. With no image supplied the figure shall not be reported, so that no existing output changes (HLR-140).
    *Trace:* [SDD Section 5](SDD.md), [SDD Section 7](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-146"></a>**HLR-146: An Unusable Image Is Fatal.**
    An image that is absent, unreadable, not an object file, of a class this build does not read, or carrying no function symbols at all shall be reported with a diagnostic naming it, and shall end the run with no report produced (HLR-063, HLR-120). The user named the file, so the failure is theirs to correct — the provenance rule HLR-116 draws for a custom rule named on the command line. The fully stripped image is the case that most needs this: an empty function set would otherwise filter every function away and report a project containing none, which is a confidently wrong result indistinguishable from a correct one.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 18](SDD.md).

*   <a id="HLR-147"></a>**HLR-147: Filter Recorded and Reported.**
    The image the filter was taken from, and the counts of HLR-143, shall appear in the report and in the saved XML record (HLR-054), so that a report states which image it describes and a report regenerated from a record stays byte-identical to one produced directly (HLR-056). Because the filter is applied when a file is measured and not when a report is rendered, supplying an image together with the regeneration mode of HLR-055 shall be rejected as a usage error (HLR-063) rather than silently ignored — the same rule, and for the same reason, that HLR-136 draws for a conditional-compilation definition.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 13](SDD.md), [SDD Section 16](SDD.md).

*   <a id="HLR-153"></a>**HLR-153: Debug-Line Pruning From the Image.**
    Where the image named under HLR-140 carries debug line information, `elc` shall read the mapping it holds from machine instructions back to source lines, and shall exclude from every reported metric and every graph fact each source line the mapping shows this build did not compile. A line excluded this way is one the build did not keep, whether it was removed by a conditional-compilation directive the build resolved or eliminated by the compiler's optimiser — the mapping records what the build produced and does not distinguish the two, and neither claim needs distinguishing to justify excluding the line from a measure of what shipped.

    This is the function filter of HLR-140 carried one granularity finer. That filter answers which *functions* the link kept; this one answers which *lines* inside a kept function the compiler emitted. The two compose: a function the image does not define is excluded whole (HLR-144), and within each function that survives, the lines this build did not compile are excluded as well.

    Where the image carries no debug line information, no line shall be excluded on this account and the reported metrics shall be exactly those `elc` reports for the same image without it, so that the capability is governed by what the build wrote rather than by an option the user must remember.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 18](SDD.md).

*   <a id="HLR-154"></a>**HLR-154: Pruning Confined to Established Coverage.**
    `elc` shall exclude a line under HLR-153 only where the debug line information positively establishes that the line produced no instruction: the line must lie within a function the image defines, and the source file containing it must be one the image's line information covers. Where a file is not covered — because the translation unit holding it was compiled without debug information, or because the image's line information is partial — no line in that file shall be excluded on this account, and the file shall be counted among those whose coverage could not be established.

    The asymmetry is the one HLR-133 and HLR-138 already draw, applied to a third kind of evidence. Absence of a line from the mapping is evidence that the build emitted nothing for it only where the mapping is known to describe that file; absence from a mapping that never covered the file is evidence of nothing at all. Treating the second as the first would silently delete measured code and produce a report that is confidently wrong and indistinguishable from a correct one, which is the outcome this project forbids in every analysis it performs.

    One limit follows from the evidence rather than from the implementation, and shall be stated in the documentation rather than left to be discovered: an optimiser may fold the instructions of one source line into the entry recorded for a neighbouring line, and a line so folded is indistinguishable, in the mapping alone, from one that produced no instruction. `elc` shall not attempt to recover the difference, since nothing in the image records it. The counts of HLR-155 are what let a reader judge how much of a report rests on this.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 18](SDD.md).

*   <a id="HLR-155"></a>**HLR-155: Debug-Line Pruning Recorded and Reported.**
    `elc` shall report the number of source lines excluded under HLR-153 and the number of analysed files whose debug coverage could not be established under HLR-154, and shall write both to the saved XML record (HLR-054), so that a report states how far its figures were narrowed by the image and how completely the evidence supported the narrowing.

    Both counts exist for the reason the unresolved-call count of HLR-077 and the undecided-region count of HLR-133 exist, and are read the same way: the first states what the filter removed, the second states where it could not look. A large second count beside a small first one says the report describes the source more nearly than the image, whatever the image was named — which a reader cannot infer from the metrics themselves.
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 16](SDD.md), [SDD Section 18](SDD.md).

## 20. Function Fan-In

The converse of the fan-out of Section 12: the number of distinct functions that call one.

The section once held the Henry–Kafura information-flow metric as well — the product `ELOC × (Fan-In × Fan-Out)²` the published work forms from a function's size and its two degrees. HLR-157 through HLR-159 were withdrawn in Phase 24 and the identifiers are retired: the figure is ordinal, separated by orders of magnitude, and unbandable by any published source, and in practice readers took it for a score. The two degrees it was formed from are reported as they are measured, side by side in one function table (HLR-183), and each is banded on a stated authority — fan-out on Henry and Kafura's (HLR-086), fan-in on `elc`'s own and labelled as such (HLR-186).

*   <a id="HLR-156"></a>**HLR-156: Function Fan-In Measurement.**
    For every function in the SDG, `elc` shall compute and report its fan-in: the number of distinct functions that invoke it directly. Fan-in is the converse of the fan-out of HLR-085 and is counted the same way — distinctly, over call edges alone, so that a caller invoking a function in forty places contributes one, and coupling through a global object (HLR-074) contributes nothing, since writing a variable another function reads is not calling it.

    A function that no analysed function calls has a fan-in of zero. That figure is a measurement rather than a finding: an entry point, an exported API boundary, and an interrupt handler reached from a vector table all legitimately have none, and the reachability analysis of HLR-096 is what draws conclusions from an absence of callers.
    *Trace:* [SDD Section 8](SDD.md), [SDD Section 10](SDD.md), [SDD Section 13](SDD.md).

## 21. Architecture Conformance Measurement

Requirements governing how `elc` quantifies a code base's adherence to a layering the user declared, and presents the dependencies between layers as a matrix.

Section 11 already *detects* the two ways a call can breach a declared layering: HLR-118 reports a call running against the declared direction, and HLR-079 a call bypassing an intervening layer. This section adds the two things that detection alone does not give a reader — an aggregate that says how much of the code base conforms, and a matrix that shows where the breaches sit. Neither introduces a second opinion about what a violation is: both count exactly the findings Section 11 already reports.

*   <a id="HLR-160"></a>**HLR-160: Component Directory Recorded.**
    `elc` shall retain, for every analysed component (HLR-114), the directory containing it, so that layers, matrices, and edge densities can be grouped by directory without re-deriving one from a path at each point of use.

    The directory is a property of the component the discovery stage already resolved (HLR-072), not new information about the source, and recording it once is what keeps every consumer of it agreeing about which directory a component belongs to.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-161"></a>**HLR-161: Layer Index Taken From the Declared Strata.**
    The layer index each analysis in this section compares against shall be the ordinal of the declared strata of HLR-078, and `elc` shall accept no second declaration syntax for the same fact. A stratum's ordinal *is* its layer index: the topmost declared layer is index 0, and an index increasing means a layer further from the caller and nearer the hardware.

    A directory is named as a layer by giving it as a stratum pattern, which the existing declaration already accepts. Introducing a separate directory-to-index option beside it would give one concept two spellings, require a rule for what happens when the two disagree, and leave every downstream analysis asking which of them it was compiled against — for no capability the declaration does not already have.

    A component matching no declared stratum lies outside the partition rather than in a layer of its own, exactly as it does for the analyses of Section 11, and shall contribute to no index and to no matrix cell.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 9](SDD.md).

*   <a id="HLR-162"></a>**HLR-162: Back-Call Violation Index.**
    Given declared strata, `elc` shall compute and report the **Back-Call Violation Index**: the proportion of the run's inter-layer call edges whose direction is inverted with respect to the declared order (HLR-118). It shall be reported alongside the complementary conforming proportion, so that a reader is given both the figure the index names and the figure they will quote.

    The denominator shall be the call edges joining two components that lie in *different* declared layers. An edge within one layer has no direction to invert and is not a candidate; an edge touching a component outside the partition is excluded by HLR-161. Where that denominator is zero the index shall be reported as **undefined** rather than as zero or as one, by the rule HLR-082 already applies to Instability: a project with no inter-layer calls has not achieved perfect conformance, it has demonstrated nothing either way.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-163"></a>**HLR-163: Skip-Call Violation Index.**
    Given declared strata, `elc` shall compute and report the **Skip-Call Violation Index**: the proportion of the run's inter-layer call edges that bypass one or more intervening layers (HLR-079), over the same denominator as HLR-162 and reported the same way, including its undefined case.

    The two indices are independent and shall not be summed or combined into a single conformance score. A call may skip without inverting and invert without skipping, and one call may do both and be counted once in each (HLR-118, LLR-LAY-04) — so a combined figure would double-count exactly the calls that most need attention, and would name no remedy, where each index separately names one.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-164"></a>**HLR-164: Indices Counted From the Reported Violations.**
    The indices of HLR-162 and HLR-163 shall be computed by counting the layering findings Section 11 already produced, and `elc` shall not re-derive from the graph what a violation is in order to compute them.

    An index disagreeing with the list of violations printed beside it is the failure this requirement exists to make impossible. Two code paths deciding independently what counts as a back-call would eventually disagree — over a call touching an unpartitioned component, over a call that both skips and inverts, over an edge collapsed from several call sites — and a report carrying a percentage that its own table contradicts is worse than one carrying neither.
    *Trace:* [SDD Section 9](SDD.md).

*   <a id="HLR-165"></a>**HLR-165: Dependency Structure Matrix.**
    `elc` shall produce a **Dependency Structure Matrix**: a square grid whose rows and columns are the same ordered sequence of subjects, and whose cell at row *i*, column *j* holds the number of call edges from subject *i* to subject *j*.

    The subjects shall be the declared layers where strata were declared, and the analysed directories (HLR-160) where they were not, so that the matrix is available to a reader who has declared no architecture and is the shape they expect where they have. The matrix reports call edges alone, for the reason the layering analysis does (LLR-LAY-05): a global object two subjects happen to share is a different fact with its own analyses.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 13](SDD.md), [SDD Section 22](SDD.md).

*   <a id="HLR-166"></a>**HLR-166: Matrix Ordering, the Diagonal, and Its Renderings.**
    The matrix of HLR-165 shall order its subjects by ascending layer index where strata were declared, and by path where they were not, so that the position of a cell carries meaning rather than merely locating a number:

    *   Cells **above** the diagonal are dependencies running in the declared direction, from a layer to one below it.
    *   Cells **on** the diagonal are dependencies within one subject, which no declared order constrains.
    *   Cells **below** the diagonal are back-calls (HLR-162) — the violations, gathered on one side of the grid where a reader can see them at a glance.

    This convention shall be stated wherever the matrix is rendered, since a matrix whose orientation the reader has to infer conveys the opposite of what it is for. `elc` shall render the matrix as CSV and as Markdown, both governed by the escaping and determinism rules every other rendering obeys (HLR-064, HLR-032, HLR-033).

    The matrix is a tier of the report and not a separate artefact, so the aligned table renders it too. HLR-031 does not permit a tier present in one human-facing format and absent from the other, and a rendering that existed only in Markdown would be exactly that. The Markdown rendering of this requirement is therefore the matrix as it appears in a Markdown report; the CSV rendering is the machine-readable copy written beside the report by HLR-180. The matrix is a detail tier by the partition rule of HLR-150, since it enumerates one row per subject, while the indices of HLR-162 and HLR-163 are project-level aggregates and are summary tiers.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 22](SDD.md).

*   <a id="HLR-180"></a>**HLR-180: The Matrix Written Beside the Report on Request.**
    `elc` shall write, on request, the Dependency Structure Matrix of HLR-165 as a CSV file beside the report, named from the report's own output path by the extension substitution of HLR-119, and shall write nothing where the report has no path to derive a name from (HLR-104). This is the CSV rendering HLR-166 requires; the matrix itself is part of the report at either verbosity's detail tier whatever format is rendered, and this requirement governs the machine-readable copy of it alone.

    The companion shall be off unless asked for, by the rule HLR-106 applies to the GraphML export rather than the rule HLR-103 applies to the call tree. A run that did not ask for it shall produce exactly the files it produced before the option existed.

    Unlike the two graph companions, it shall be available in the regeneration mode of HLR-055. A saved record carries the matrix (HLR-054) where it carries no topology, so there is something to write from — and refusing a file the record is sufficient to produce would withhold it for a reason that does not apply.
    *Trace:* [SDD Section 3](SDD.md), [SDD Section 4](SDD.md), [SDD Section 22](SDD.md).

## 22. Graph Purification and Architecture Recovery

Requirements governing how `elc` proposes a layering the user did not declare, and the graph surgery that makes such a proposal meaningful.

A raw call graph rarely sorts into layers. A logger every module calls, and a dispatcher that calls everything, each connect parts of a program that have nothing to do with one another — and a topological ordering computed over such a graph collapses into one tangled stratum that describes nothing. The purification of HLR-167 through HLR-171 sets those nodes aside so that the structure underneath them can be seen; the recovery of HLR-172 reads a layering off what remains.

Two boundaries govern the whole section, and both exist because this is the one place `elc` forms a view of its own. Purification changes **no reported metric** (HLR-167), and a recovered layering is a **proposal that is never a baseline** (HLR-173). Everything else here is subordinate to those two.

*   <a id="HLR-167"></a>**HLR-167: Purification Confined to Recovery.**
    The edge masking of HLR-168 through HLR-170 shall apply to a view of the graph constructed for architecture recovery alone. No measurement, finding, or artefact `elc` reports outside this section shall be computed over a masked graph: fan-out (HLR-085), fan-in (HLR-156), call depth (HLR-087), recursion (HLR-089), coupling and Instability (HLR-080, HLR-082), dependency cycles (HLR-083), reachability (HLR-096), and the indices and matrix of Section 21 shall each be exactly what they would be had no purification run.

    This is the requirement the rest of the section is built on. Masking exists because a topological ordering over a tangled graph yields nothing; it is a lens for one question, not a correction to the graph. A fan-out that quietly omitted the calls into a masked utility would be a wrong number reported with the authority of a measured one, and a reachability analysis over a masked graph would call live code dead — the precise failure HLR-096 and HLR-144 are written to prevent.
    *Trace:* [SDD Section 20](SDD.md), [SDD Section 21](SDD.md).

*   <a id="HLR-168"></a>**HLR-168: Utility-Sink Detection.**
    `elc` shall identify as a **utility sink** every function whose authority score is high and whose hub score is near zero, computed by the hub-and-authority (HITS) decomposition of the call graph, and shall mask that function's *incoming* edges in the recovery view.

    A node many parts of the program call and which calls almost nothing back is domain-agnostic by construction — a logger, a string helper, an arithmetic routine. Its incoming edges join every caller to every other caller through it, fusing domains that share nothing but a dependency on it. Masking the incoming edges alone, rather than the node, is what removes that fusion while leaving the node's own position observable.
    *Trace:* [SDD Section 20](SDD.md).

*   <a id="HLR-169"></a>**HLR-169: God-Object Detection.**
    `elc` shall identify as a **god object** every function whose betweenness centrality is high and whose hub score is also high, and shall mask its edges in the recovery view.

    Betweenness counts the shortest paths a node lies on; a node lying on a great many is an architectural short circuit, joining regions whose only connection is that it dispatches to both. The hub score is required beside it because betweenness alone does not distinguish a dispatcher from a genuine intermediary that a layering ought to keep: a monolithic dispatcher calls widely, and a legitimate waypoint need not.

    Where one function satisfies both this requirement and HLR-168, it shall be classified as a god object and reported as one, since masking its edges subsumes masking its incoming edges and the more specific claim is the more useful one to a reader.
    *Trace:* [SDD Section 20](SDD.md).

*   <a id="HLR-170"></a>**HLR-170: Peripheral Stripping by K-Core Decomposition.**
    `elc` shall compute the coreness of every function and shall exclude from the recovery view those functions lying in the outermost cores, so that a layering is recovered from the mutually connected centre of the program rather than from the leaves hanging off it.

    The core depth below which a function is treated as peripheral shall be user-configurable, since a program's periphery is a function of its size: a threshold that isolates the domain logic of a large code base strips a small one to nothing.

    Peripheral functions shall be reported as excluded rather than silently dropped, and shall be assigned no recovered layer. A function `elc` did not consider is not a function `elc` placed at the edge of the architecture, and a proposal that did not distinguish the two would put every leaf in the bottom layer.
    *Trace:* [SDD Section 20](SDD.md).

*   <a id="HLR-171"></a>**HLR-171: Purification Thresholds Are elc's Own.**
    Every threshold governing the classifications of HLR-168 through HLR-170 — the authority and hub scores that make a utility sink, the betweenness and hub scores that make a god object, and the core depth that makes a function peripheral — is `elc`'s own heuristic rather than a published standard, and shall be identified as such wherever a classification is reported, by the rule HLR-099 already applies to the bottleneck threshold of HLR-081.

    Each shall be user-configurable. These are the values a user is most likely to disagree with, because unlike a published threshold they rest on nothing but this project's judgement, and a heuristic that cannot be adjusted is one whose disagreements have nowhere to go but the manifest of HLR-175.

    No classification made under this section shall carry a severity or become a finding (HLR-123). A god object is an observation about the shape of a graph, not a measurement banded against an accepted range, and presenting one as a finding would place `elc`'s own opinion in the section whose whole claim is that it holds none.
    *Trace:* [SDD Section 12](SDD.md), [SDD Section 20](SDD.md).

*   <a id="HLR-172"></a>**HLR-172: Automated Layer Recovery.**
    `elc` shall propose a layering of the analysed components by ordering the purified recovery view topologically and grouping the result by the directory each component belongs to (HLR-160), so that a user with no declared architecture is given a description of the one their code already has.

    Where the recovery view is cyclic no topological ordering exists, and `elc` shall report the cycles in place of a proposed layering rather than ordering the graph arbitrarily — the rule HLR-090 applies to call depth, applied to the same underlying impossibility.

    The proposal shall state which functions were masked or excluded in producing it (HLR-168 – HLR-170), since a layering recovered from a graph with parts of it set aside is a claim about that graph and not about the program.
    *Trace:* [SDD Section 21](SDD.md).

*   <a id="HLR-173"></a>**HLR-173: A Recovered Layering Is a Proposal, Never a Baseline.**
    A layering recovered under HLR-172 shall never be used as the declared architecture that the conformance analyses measure against. The strata of HLR-078 are the sole baseline for the layering findings of Section 11 and the indices of HLR-162 and HLR-163, and where no strata are declared those analyses shall remain omitted with their reason stated (HLR-115) however confidently a layering was recovered.

    The matrix of HLR-165 is the one part of Section 21 this does not silence, and the distinction is what the requirement is about rather than an exception to it. The matrix *measures nothing against a baseline*: it counts the call edges between subjects, and with no declaration its subjects are the analysed directories — a grouping the discovery stage established (HLR-160) rather than one `elc` proposed. A recovered layering shall not become those subjects either, for the reason it shall not become the baseline: a matrix whose rows were read off the graph it arranges would make every project look layered.

    `elc` measuring conformance against its own proposal would be a tool marking its own homework: every code base would conform, because the standard would have been read off the thing it was judging. HLR-078's rule that strata are never discovered automatically is unchanged by this section — what is added is a *proposal a user may read, adopt, and then declare*, and the declaring is theirs.

    The proposal shall be presented in a form that can be adopted without transcription, so that a user who agrees with it can turn it into a declaration rather than retype it.
    *Trace:* [SDD Section 9](SDD.md), [SDD Section 21](SDD.md).

*   <a id="HLR-174"></a>**HLR-174: Purification Reported Before It Is Relied On.**
    `elc` shall report every classification purification made: the function classified, the class assigned, the metric and value that triggered it, and the action taken upon it. Automated masking that a reader cannot inspect is a black box whose output they have no grounds to trust, and this report is what makes the recovery of HLR-172 something other than an assertion.

    The report shall be a section of the rendered report, presented under the composition rules every other section obeys (HLR-031, HLR-150) and written to the results destination like every other result. It shall **not** be written directly to standard output when the report is going elsewhere: HLR-038 reserves that stream, and a run redirecting its report to a file must not have a second report appear on the terminal.
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 14](SDD.md), [SDD Section 20](SDD.md).

*   <a id="HLR-175"></a>**HLR-175: The Purification Manifest.**
    `elc` shall write, on request, a **purification manifest**: a machine-readable record of every classification it made, in a documented text format, structured so that a user may edit a classification and hand it back.

    The manifest exists because these classifications are heuristics (HLR-171) and heuristics have false positives. A state machine's dispatcher legitimately lies on a great many shortest paths and legitimately calls widely; nothing in the graph distinguishes it from the monolith HLR-169 describes, and only the user knows which it is. Without a way to say so, a wrong classification is a permanent property of every future run.
    *Trace:* [SDD Section 20](SDD.md).

*   <a id="HLR-176"></a>**HLR-176: The Manifest Is Read Only When Named.**
    `elc` shall read a purification manifest only from a path given on the command line, and shall never discover one from the working directory, the analysis target, any ancestor of either, or any dotfile. The zero-configuration guarantee of HLR-039 is unchanged by this section: a manifest is read for the same reason a custom rule file is (HLR-107, HLR-110) — because the user named it — and two people running the same command on the same tree must still obtain the same result.

    A manifest that cannot be read, or whose contents `elc` does not understand, shall be rejected with a diagnostic and a non-zero exit status rather than partially applied. The user named the file, so the failure is theirs to correct — the provenance rule HLR-116 draws for a custom rule named on the command line, and HLR-146 for an unusable image.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 20](SDD.md).

*   <a id="HLR-177"></a>**HLR-177: A Manual Classification Overrides a Computed One.**
    Where a manifest supplied under HLR-176 states a classification for a function, that statement shall govern, and `elc` shall not recompute or overrule it. The report of HLR-174 shall distinguish a classification `elc` computed from one the manifest supplied, so that a reader can tell which of the assumptions in front of them are the tool's and which are the team's.

    A manifest naming a function no analysed file defines shall be reported and ignored rather than ending the run: analysing one directory of a project whose manifest covers all of it is ordinary use, and rejecting it would make the manifest unusable exactly where a large code base most needs one — the rule HLR-095's entry points already follow (LLR-CTR-08).
    *Trace:* [SDD Section 20](SDD.md).

*   <a id="HLR-178"></a>**HLR-178: Raw and Purified Graph Exports.**
    `elc` shall export, on request, two Graphviz `.dot` files: the graph as built, and the recovery view with the masked and excluded nodes of HLR-168 through HLR-170 visually distinguished rather than removed. Seeing what purification did is what lets a user judge whether it did the right thing, and a single drawing of the result cannot show what it acted on.

    Both shall derive their names from the report's output path by extension substitution and shall accept no path of their own, exactly as the call tree and GraphML export do (HLR-119); both shall therefore be absent when the report is written to standard output, since no output path exists from which to derive a name (HLR-104). Neither shall replace the annotated call tree of HLR-102, which answers a different question and is enabled by a different default.
    *Trace:* [SDD Section 17](SDD.md), [SDD Section 20](SDD.md).

*   <a id="HLR-179"></a>**HLR-179: Deterministic Classification.**
    Every classification, ordering, and proposal this section produces shall be identical across two runs over the same target, as HLR-032 requires of every other output. Two properties of the mathematics make that harder to satisfy here than elsewhere, and both shall be addressed rather than assumed.

    The centrality scores of HLR-168 and HLR-169 are floating-point values produced by an iterative computation, so a comparison against a threshold shall be made in a defined way, and a run shall not classify differently on a different machine for want of one. And a ranking of nodes by such a score contains ties, which shall be broken by the stable node identifier of HLR-033 rather than by the order the graph library happened to enumerate them.
    *Trace:* [SDD Section 20](SDD.md), [SDD Section 21](SDD.md).

## 24. Adapted Maintainability Index

Requirements governing a single per-function score formed from the three things Sections 3, 4 and 12 measure separately: how much code a function holds, how branchy it is, and how entangled it is with its neighbours.

Each of those is answerable on its own and none of them answers the question a maintainer actually asks, which is *how hard will this be to change*. A function may be short and central, long and isolated, or branchy and neither. The index here is Coleman and Oman's, with one substitution: their third term is the logarithm of a function's Halstead Volume, and this one uses the information flow through it instead, so that the figure falls when a function is entangled and not only when it is large.

**The substitution is what makes the bands `elc`'s own.** The formula is adapted, so the thresholds published for the unadapted one no longer describe it, and Section 24 says so where a reader sees it rather than borrowing an authority the adaptation forfeited.

*   <a id="HLR-194"></a>**HLR-194: The Debug Companion.**
    `elc` shall provide a command-line option that writes a **debug companion** beside the report, carrying what is needed to diagnose a run on a source tree the reader cannot obtain: the invocation, and every message the run wrote to standard error.

    The companion shall follow the rule every other companion follows (HLR-119): its name is derived from the report's output path by substituting the extension, it takes no path of its own, and nothing is written where the report goes to standard output — there being no name to derive one from. A request with the report on standard output is not a usage error; it produces no companion, exactly as HLR-106 specifies for the GraphML export.

    Each recorded message may be accompanied by detail the terminal did not receive, where the diagnosing code has any to give. The terminal is a place to be told what went wrong; the companion is a place to be told enough to reproduce it.

    **The companion shall be written as the run proceeds, not assembled and flushed at the end.** A run that faults, is killed, or exhausts memory is precisely the run worth diagnosing, and a log that existed only in memory is lost exactly then.

    **It shall change nothing else.** The messages standard error receives, the report, the findings, and the process exit status shall be identical with the option and without it. The companion records a run; it is not a result of one, and a diagnostic aid that altered what it observed would be worse than none.
    *Trace:* [SDD Section 4](SDD.md), [SDD Section 25](SDD.md).

*   <a id="HLR-195"></a>**HLR-195: Unparsable Source Recorded in the Debug Companion.**
    Where the grammar cannot parse a region of a source file (HLR-035), `elc` shall record in the debug companion of HLR-194 the file, the lines the region spans, and **the source text of those lines**.

    The text is the point. A grammar that fails on a construct is debugged from the construct, and the construct is the one thing a maintainer holding only a bug report does not have — the tree may be proprietary, unbuildable elsewhere, or simply large. A line number without its line names a place nobody can visit.

    Every region shall be recorded as the grammar reported it, rather than coalesced the way the line *count* of HLR-035 coalesces overlapping regions. The count exists so a reader can judge how much of a file was measured; this exists so a defect can be reproduced, and two regions the grammar reported separately are two facts about the grammar.

    The recorded text shall be bounded, with the number of lines omitted stated where it is truncated. A file the grammar could follow nowhere would otherwise be copied into the log in its entirety, which serves nobody and may disclose more of a private tree than the reporter intended.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 25](SDD.md).

*   <a id="HLR-193"></a>**HLR-193: Image Symbols Placed by Debug Information.**
    Where a run is filtered by a linked image (HLR-140) and that image carries debug information placing a function in a source file, `elc` shall match a source function to an image symbol by **name and file** rather than by name alone.

    Matching by name alone is wrong wherever a name is not unique across the analysed source. Two translation units defining a `static helper` produce two symbols, which the link may keep or discard independently; a filter keyed on the name retains both definitions or discards both, and in either case one of them is wrong. The debug information records a separate subprogram for each definition and the file it was written in, which answers the question exactly.

    The names compared are the **reduced** forms of HLR-200, not the spellings each side happens to record. The debug information writes a template instantiation as `f<int>` and an out-of-line member definition as plain `f` where the source writes `f` and `S::f`; compared unreduced, an image describing a function completely answers that it does not describe it at all.

    **Where the debug information cannot place a name and two or more analysed files define it, the run shall fail** with a diagnostic naming the function, both files, the image, and the remedy — and shall produce no report. Which remedy is stated depends on which condition was observed (HLR-201). Retaining both definitions overstates what the build contains, and retaining the first is a guess presented with the authority of a measurement; a filtered figure a reader cannot trust is worse than no figure, because nothing distinguishes it from a correct one.

    The failure is confined to that case. A name defined once is unaffected, a name the image does not define is unaffected — the ambiguity changes nothing a reader would see, since both definitions are excluded whichever was linked — and a run with no image never asks the question. Debug information remains **not required** (HLR-141): an image built without it filters exactly as before wherever the source defines each name once.
    *Trace:* [SDD Section 18](SDD.md), [SDD Section 19](SDD.md), [SDD Section 13](SDD.md).

*   <a id="HLR-191"></a>**HLR-191: Adapted Maintainability Index per Function.**
    For every function, `elc` shall compute and report an Adapted Maintainability Index, formed from the function's information flow, its cyclomatic complexity (HLR-017) and its effective lines of code (HLR-015):

    `IF = (Fan-In × Fan-Out)²`

    `MI = 171 − 5.2 ln(IF + 1) − 0.23 v(G) − 16.2 ln(ELOC)`

    `MI′ = max(0, MI ÷ 171 × 100)`

    where **Fan-In** is the measurement of HLR-156 and **Fan-Out** that of HLR-085. The reported figure shall be `MI′` rounded to a whole number, presented in the per-function table of HLR-183.

    **One is added to the information flow before the logarithm** so that a function at either end of the call graph is defined rather than infinite: either degree being zero makes the product zero, and `ln(1)` is zero, so the term vanishes and the figure rests on length and branching alone. That is the intended reading and not a gap — an entry point is not coupled by being an entry point.

    **A function with no effective lines shall be treated as having one**, for the same reason: `ln(0)` is not a number, and a function with nothing in it has nothing to maintain, so the term vanishes and the figure is at the top of the scale.

    **The normalisation and its clamp** put the figure on a 0-to-100 scale, so that a monolith reports a floor rather than a negative score and the number can be read as a proportion of the best a function could do.

    The figure shall be derived once, where the report model is assembled, and both the table and any finding over it shall present that same figure. A band read off a value other than the printed one is a band a reader cannot check against the table.
    *Trace:* [SDD Section 10](SDD.md), [SDD Section 13](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-192"></a>**HLR-192: Maintainability Index Threshold Classification.**
    `elc` shall classify each function's Adapted Maintainability Index (HLR-191) against bands that run **downwards**, this being the one measurement in the catalogue where the low value is the bad one: a score of 65 or above produces no finding; below 65 produces a **warning**; and below 55 produces a **critical** finding.

    **These thresholds are `elc`'s own and shall say so wherever a finding carries them**, in the same words the bottleneck heuristic of HLR-081 and the fan-in band of HLR-186 carry. The figures usually quoted with the Maintainability Index — 85 and 65, from the Software Engineering Institute — were calibrated against Coleman and Oman's unadapted formula, whose third term is a Halstead Volume this one does not have. Removing that term removes some thirty to forty-five points of range, and HLR-191's normalisation rescales what remains; carrying the published numbers across unchanged would put four functions in five in a band, which is a measurement that has stopped discriminating. Presenting a threshold calibrated here under a citation earned elsewhere is precisely what HLR-099 forbids.

    A finding shall state the score and the scale it is out of, and nothing further. It shall not recommend a review, a refactoring, or any other action: HLR-101 governs this measurement as it governs every other, and a metric whose name suggests a verdict is the last place to start advising.

    Every function a band names shall appear in the threshold listing of HLR-187 alongside the functions the other bands name.
    *Trace:* [SDD Section 12](SDD.md), [SDD Section 13](SDD.md).

## 26. Placing Templated Names by Debug Information

Requirements governing the spelling a name is compared in, and what a diagnostic may assert about a condition it did not observe.

`elc` compares names on three paths: a source name against an image symbol, a source name against a subprogram in the debug information, and a source name against another source name. The first path reduced both sides to the identifier the report presents; the second did not. The consequence was not an error but a confident false statement — an image built with `-g`, whose debug information described a function completely, reported as carrying none, with the advice to rebuild it with `-g`.

*   <a id="HLR-200"></a>**HLR-200: Names Compared in One Reduced Form.**
    Wherever `elc` compares two spellings of a function's name, it shall reduce both to the identifier the report presents (HLR-014) before comparing them.

    A name reaches the tool in several spellings. A demangled Itanium symbol is `ns::C::f(int) const`. Debug information records a template instantiation as `f<int>` and an out-of-line member definition as plain `f`. The source declares `f`, or `S::f`. These are the same function, and a comparison that treats any two of them as different names is wrong in a way that produces an answer rather than an error.

    **The reduction shall be one implementation, used by every path that compares names.** A second copy is not a duplication to be tidied later but the defect itself: the two copies begin identical, one is corrected, and the tool resumes disagreeing with itself somewhere no test looks. Where the module structure will not permit both users to reach one implementation, the structure is what changes.

    An `operator` token shall survive the reduction whole. Its own name may contain the characters the other rules key on — `operator<`, `operator>>`, `operator()`, `operator->` — and a reduction that read those as template brackets or scope separators would truncate an operator to nothing, which is worse than not reducing it: an empty key matches every other name that reduced to nothing.
    *Trace:* [SDD Section 26](SDD.md), [SDD Section 19](SDD.md).

*   <a id="HLR-201"></a>**HLR-201: A Diagnostic States the Condition It Observed.**
    Where `elc` fails a run because the debug information could not place an ambiguous name, the diagnostic shall distinguish **an image carrying no debug information** from **debug information that describes no definition of that name**, and shall state the one it observed.

    The two conditions look identical to a lookup that returns false and have different remedies. The first is answered by rebuilding the image with `-g`. The second is not: the image was built with `-g`, and either the translation unit defining the function was not, or the definition was never emitted. A diagnostic that names the first while observing the second sends its reader to rebuild an image that was already correct, and when that changes nothing there is nowhere left to look — the tool has spent the user's time and given them no way to spend their own.

    **The condition shall be checked rather than inferred from the failed lookup.** This is the split HLR-153's queries already take, where `dwarfline_covers` governs `dwarfline_compiled` so that absence of evidence cannot be read as evidence of absence. The requirement here applies the same discipline one layer up, to what the tool then says about it.

    Advising a remedy is required by HLR-193 and does not reopen HLR-101. HLR-101 forbids `elc` recommending changes to the code it measures; this concerns how the image was built, which is a fact about the tool's own inputs.
    *Trace:* [SDD Section 26](SDD.md), [SDD Section 14](SDD.md).

## 25. Repairing What the Grammar Could Not Follow

Requirements governing what `elc` does with source the grammar rejects **and the preprocessor could not reach**, and the bounds that keep doing anything at all defensible.

A macro standing where the grammar expects a keyword, a type, or a string is a parse error. Expansion (HLR-202) answers it exactly, and cannot always be had: it needs a toolchain, the build's include paths, and conditions `elc` can decide, and on a cross-compiled tree it reaches almost nothing. Measured on a 49-file AVR project, expansion alone left **50 unparsed lines** where repair alone left 9; the two together leave **8**.

So these requirements are the floor beneath HLR-202 rather than a rival to it. They were withdrawn once, on the reasoning that a tool which can expand a macro properly has no business guessing at its shape. That reasoning was right about the two mechanisms and wrong about the choice between them: where expansion applies, repair is unnecessary; where it does not, repair is all there is. A file takes one path or the other, and the report says which (HLR-206).

The requirements below are mostly restrictions, and each is what separates a bounded repair from a tool quietly inventing the code it measures.

*   <a id="HLR-196"></a>**HLR-196: Repair Confined to Rejected Regions.**
    Where a source file was **not expanded** (HLR-205) and the grammar reports an error region in it, `elc` may rewrite the contents of **that region** in the buffer it parses, and shall parse the result again.

    **Repair is the fallback beneath expansion, never an alternative to it.** A file the preprocessor expanded has had its macros resolved exactly, and guessing at the shape of a failure that no longer exists could only make it worse. A file the preprocessor could not reach has nothing else, and that is the ordinary condition of a cross-compiled tree.

    No text the grammar accepted shall be rewritten. The rules of HLR-197 are heuristics about the shape of a failure, and a heuristic applied to code that parsed correctly is a tool editing a measurement it had already taken. Confining them to the regions the grammar itself rejected is what makes them safe: the worst a wrong repair can do is fail to fix a region that was already broken.

    A file the grammar parses without error shall not be rewritten and shall be parsed **once**, so the single-parse rule of HLR-076 is unchanged for sound source, and the repair path is unreachable for it.

    The buffer is `elc`'s own copy. No source file is modified, which HLR-039's zero-configuration guarantee and the read-only contract of the analysis both require.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 25](SDD.md).

*   <a id="HLR-197"></a>**HLR-197: Repairs Preserve the Line Count.**
    Every repair shall replace text with text spanning the **same number of lines**.

    This single property is what keeps a repaired measurement correct. Effective lines (HLR-015), cyclomatic complexity (HLR-017) and every location `elc` reports are line-based, so a replacement confined to the lines it replaces cannot move any of them. A repair free to add or remove a line would shift every function beneath it, and the resulting report would carry right-looking figures against wrong locations — which a reader cannot detect and therefore cannot discount.

    Three rules are defined, and each meets that constraint within a single line:

    *   An identifier in upper case adjacent to a string literal shall be replaced by an empty string literal. Such a token is a macro expanding to a string, and an empty literal concatenated with the literal beside it has the same shape and the same effect on every measurement.
    *   An identifier standing in front of a declaration shall be replaced by blanks. Such a token is a storage-class or attribute macro, and the declaration is what carries the function.
    *   An identifier alone before `=` at file scope shall be given a type. Such a token is a macro expanding to a declarator, and what remains is the object it declares.

    A rule shall be applied only where its shape matches within a rejected region. `elc` shall not infer what a macro expands to, and none of the three requires it to.
    *Trace:* [SDD Section 25](SDD.md).

*   <a id="HLR-198"></a>**HLR-198: Repair Terminates.**
    Repair shall proceed in passes, and shall stop when a pass rewrites nothing or fails to reduce the number of rejected regions. A repair that does not reduce them shall be withdrawn, leaving the buffer as the pass found it.

    Termination is a requirement rather than an expectation because the rules operate on source `elc` did not write and cannot anticipate. A rule that repairs one region into a shape another rule rejects would otherwise run until the process was killed, on somebody else's code and for reasons nothing in the report would explain.

    Rules shall be applied in a fixed order, and regions in a fixed order, so that two runs over one target repair identically as HLR-032 and HLR-033 require of every other output.
    *Trace:* [SDD Section 25](SDD.md).

*   <a id="HLR-199"></a>**HLR-199: Repairs Are Declared.**
    A report over source that was repaired shall state how many repairs were made and under which rule, and the debug companion of HLR-194 shall record each one with the text before and after.

    **A repair is a guess the grammar could not make**, and a figure resting on one shall say so. This is the discipline the unresolved-call count of HLR-077 and the undecided-region count of HLR-133 already follow: a measurement whose completeness is unstated cannot be acted on, and one silently reconstructed is worse than the failure it replaced — the figures look ordinary and nothing distinguishes them from measured ones.

    The count is a figure to read rather than a gate to pass. It carries no severity and does not reach the exit status, for the reason no other measurement does (HLR-100).

    The unparsed-line count of HLR-035 shall be what remains **after** repair, since it exists to tell a reader how much of a file was not measured, and repaired lines were.
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 14](SDD.md), [SDD Section 25](SDD.md).

## 27. Preprocessor Macro Expansion and AST Sanitization

Requirements governing the expansion of macros before parsing, the filtering that makes the expanded text measurable, and what `elc` gives up by depending on a toolchain.

A macro standing where the grammar expects a keyword, a type, or a string is a parse error, and no grammar can fix it: `MACRO MACRO` is genuinely ambiguous with `Type var`. The compiler's preprocessor resolves it exactly, and its output is unusable as it stands — 19 lines of C measured 829 after expansion, with the functions of every header included appearing as functions of the file under analysis and every line number moved. The `#line` markers the preprocessor emits are what turn that output into something a measurement can be taken from.

*   <a id="HLR-202"></a>**HLR-202: Macros Expanded by the Compiler's Preprocessor.**
    Before parsing a source file, `elc` shall run the language's preprocessor over it — `gcc -E` for C, `g++ -E` for C++, or the equivalent named on the command line — and shall parse the filtered result of HLR-203 in place of the file's own text.

    **Expansion is the only way to be right about a macro rather than lucky.** A grammar-based tool can recognise the shapes a macro failure commonly takes and rewrite them, which is what the withdrawn Phase 25 did; it cannot know that `local` means `static`, and the shapes it does not recognise stay broken. The compiler knows, and asking it costs one subprocess per file.

    The output shall be captured into memory and **no intermediate file shall be written**. A temporary file would be a path to collide on under parallel runs, a file to leave behind when the process is killed, and a write to a filesystem HLR-043 promises is not modified.

    The preprocessor is invoked on the file named, with no include paths, defines, or flags beyond those the user gave. `elc` does not have the build's configuration and shall not guess at it: a `-I` it invented would read a header the user did not name (HLR-039), and reaching the wrong header is worse than reaching none, because the expansion would succeed and be wrong.
    *Trace:* [SDD Section 22](SDD.md).

*   <a id="HLR-203"></a>**HLR-203: Expanded Output Filtered to the File Under Analysis.**
    `elc` shall filter the preprocessor's output by the `# linenum "filename" flags` markers it emits, retaining only the lines the marker attributes to the file being analysed and discarding every line attributed to any other file.

    **Without this the expansion is not an improvement but a corruption.** A header contributes its own functions, its own lines, and its own complexity, and every one of them would be attributed to whichever file included it. One `#include <iostream>` contributes upwards of fifty thousand lines. The figures would be larger, internally consistent, and about a program nobody wrote — which is the one failure mode this tool exists to avoid.

    The filter shall be a state machine over the marker stream rather than a heuristic over the text. A marker naming the analysed file switches it to **appending**; a marker naming anything else — a system path, a project header, `<built-in>`, `<command-line>` — switches it to **ignoring**. Nothing but the markers decides, so a line of source that happens to look like a marker cannot change what is measured.

    A project's own headers are discarded along with the system's. They are files in their own right, analysed on their own account when the target includes them, and counting a header once per file that includes it would inflate every figure by its inclusion count.
    *Trace:* [SDD Section 22](SDD.md).

*   <a id="HLR-204"></a>**HLR-204: Expansion Preserves Every Reported Location.**
    The buffer `elc` parses shall place every retained line at the line number it occupies in the unexpanded file, padding with blank lines where the filter discarded a region.

    **Every figure `elc` reports is line-based.** Effective lines (HLR-015), cyclomatic complexity (HLR-017), a function's reported range, a finding's location, the debug-line pruning of HLR-153 and the dead-code spans of HLR-137 are all counted or located by line. A filtered buffer that simply concatenated what it kept would place every one of them somewhere the reader cannot find, and the report would carry right-looking figures against wrong locations — undetectable to a reader and therefore impossible to discount.

    Padding is free in every measurement that matters: a blank line is not an effective line, contributes no complexity, and adds nothing to any table. It costs a physical-line count, which shall therefore be taken from the file itself rather than from the parsed buffer.

    Restoring a location is not only a matter of adding lines. A preprocessor lays one source line's replacement out across several — `return NULL;` becomes three — and the buffer must give those back as one, or every location below the first such expansion is displaced by however many the file accumulated.

    **Comments shall survive expansion.** A preprocessor discards them by default and the invocation shall preserve them (`-C`).

    No figure `elc` reports today depends on this: comments are excluded from effective lines (HLR-016) and stripping them removes nothing that was being counted. It is required because the buffer handed to the parser should differ from the source as little as the expansion allows. Every difference is one a reader of a finding has to account for, every future measurement over comments would otherwise read zero without anything to indicate why, and preserving them costs a flag.
    *Trace:* [SDD Section 22](SDD.md), [SDD Section 7](SDD.md).

*   <a id="HLR-205"></a>**HLR-205: Expansion Failure Falls Back to the Source as Written.**
    Where the preprocessor cannot be run, exits non-zero, or produces nothing usable, `elc` shall parse the file's own text and shall complete the run.

    **Falling back is the ordinary case, not the error case.** A cross-compiled tree cannot be preprocessed by a host toolchain at all — the AVR project that prompted this work fails on `avr/io.h` before it reaches a macro — and a source tree analysed without its build environment is the normal condition of a code-metrics tool. A file that falls back is measured exactly as this tool measured it before expansion existed: the grammar's gaps return with it, and the unparsed-line count of HLR-035 reports them as it always has.

    The fallback shall not be a diagnostic on standard error for each file. On a tree the host cannot preprocess, that is one message per file for a condition the reader learns nothing further from after the first. It is recorded per file and summarised once, under HLR-206.

    `elc` shall remain usable with no compiler installed at all, which is what keeps HLR-040's exclusions honest: a toolchain improves the result where it is present and is not required for one.
    *Trace:* [SDD Section 22](SDD.md).

*   <a id="HLR-206"></a>**HLR-206: Expansion Provenance Declared Per File.**
    A report over a run in which any file fell back shall state, for each such file, that it was measured from its own text and why the expansion did not happen; and the project summary shall state how many files were expanded and how many were not.

    **Two files in one report may have been measured two different ways, and a reader cannot tell them apart from the figures.** An expanded file's macros are resolved and its parse is clean; a fallen-back file's are not, and its unparsed-line count may be non-zero for a reason that has nothing to do with the code. This is the discipline the unresolved-call count of HLR-077, the undecided-region count of HLR-133 and the unparsed-line count of HLR-035 already follow.

    It also bounds the honesty cost of HLR-202. `elc` no longer promises that one source tree yields one report on every machine; it promises that one source tree preprocessed by one toolchain does. That is a real narrowing, and the per-file provenance is what makes it visible on the page instead of a surprise.

    The count carries no severity and does not reach the exit status (HLR-100). It is a figure to read.
    *Trace:* [SDD Section 22](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-208"></a>**HLR-208: Conditional Regions Answered Before Expansion.**
    The conditional-region figures of HLR-132, HLR-133 and HLR-134 shall be computed from the source **as written**, before any expansion, and a file whose conditions `elc` could not all decide shall not be expanded.

    **A preprocessor destroys the evidence these requirements report on.** `gcc -E` reads an undefined identifier in an `#if` as zero and discards the branch it did not take. That is a guess, made silently, where HLR-133 requires the guess be declared and both branches kept — so an expanded tree carries no directive to count and no branch to have kept. Measuring it would report *nothing undecided* about a file full of conditions nobody resolved, which is worse than the parse failure expansion was introduced to remove.

    Hence the two parses of HLR-076. The unexpanded parse answers what the source *says*; the expanded parse measures what it *means*.

    **And a file with an undecided region shall not be expanded at all.** Where `elc` has just declared a condition unresolvable, letting the preprocessor resolve it would silently substitute one branch's measurement for the whole region's — the effective-line count of an arbitrary configuration, reported as the file's. Such a file is measured as written and recorded among the fallbacks of HLR-206 with that as its reason.

    The definitions the run was given (`-D`) shall be forwarded to the preprocessor. `elc` deciding a condition one way while the preprocessor decides it the other would measure a build the user never asked for, and the configuration a report names must be the configuration it measured (HLR-132).
    *Trace:* [SDD Section 22](SDD.md), [SDD Section 7](SDD.md).

*   <a id="HLR-207"></a>**HLR-207: C Library Use Outside MISRA Constraints Reported.**
    `elc` shall report, as a **warning** citing the rule that forbids it, every call to a C library facility that MISRA C:2012 §21 states a compliant program does not use.

    The finding shall name the function, the file and the line, and shall cite the rule number — `malloc` against Rule 21.3, `printf` against 21.6, `system` against 21.8. This is the discipline of HLR-099 applied to a coding standard rather than to a measurement study: the band is somebody else's published position, `elc` reports what that position says about this code, and the reader can look the rule up and disagree with it.

    **Constrained by function and never by header.** `<stdlib.h>` supplies `abs`, which MISRA permits, beside `malloc`, which it does not; flagging the include would be a false claim about code that called neither. The unit the standard constrains is the call, and the call is what gets reported — with the line, because a reader fixing one needs to find it, and a function calling `malloc` twice has two things to change.

    Detection shall be from the calls the graph could not resolve (HLR-077), which is where a call into the C library lands. A project supplying its own definition of a constrained name resolves it and shall not be reported: the rule is about the standard library's function, not about every function that shares its spelling.

    **Occurrence is the finding, and no threshold shall be invented.** MISRA states no count at which use becomes unacceptable, and a bound `elc` chose would be its own opinion wearing MISRA's name. The severity is warning throughout, and it reaches no exit status (HLR-100).

    `elc` shall recommend nothing. A great many programs use these facilities correctly and have no obligation to MISRA at all; what the tool supplies is the fact that a rule speaks to this line, and what that is worth is the reader's to judge (HLR-101).

    Where a file's expansion succeeded, `elc` shall additionally report which standard-library headers it drew on, distinguishing the C library from the C++ library. That is the exposure the warnings above are drawn from — a fact rather than a finding, carrying no severity — and it answers what it would take to build the code somewhere else, which a freestanding or embedded target makes urgent. A file that fell back reports nothing here, and shall not be listed as depending on nothing: absence of evidence is not evidence of absence, and the provenance of HLR-206 is what tells a reader which files could be asked.
    *Trace:* [SDD Section 22](SDD.md), [SDD Section 12](SDD.md), [SDD Section 14](SDD.md).

## 29. Function Visibility and Editor-Navigable Locations

Requirements governing what the Functions table says about a function's place in its program, and how a reader gets from the table to the code.

Two questions a reader asks of an unfamiliar module are answered by the source and were not answered by the report: **which of these functions are the interface**, and **where is this one**. The first is stated plainly in every language `elc` reads — `static`, `pub`, a leading underscore — and the second was split across two columns in a form no editor would act on.

*   <a id="HLR-209"></a>**HLR-209: Function Visibility Reported.**
    For each function, `elc` shall report whether the language exposes it outside the file or module that defines it, as **public** or **private**, in a column immediately following the function's name.

    **The rule is the language's own and belongs to that language's module**, supplied as a query in the runtime location like every other language-specific decision (HLR-107). C and C++ report linkage — a `static` definition, or one inside an anonymous namespace, is private. Rust reports its own visibility keyword. Python reports the leading-underscore convention. These are not the same kind of fact, and the requirement does not pretend they are: what is common is the question, not the mechanism that answers it.

    Where two patterns of a language's visibility query match one function, **the earliest shall decide**. That is the rule `elc` already applies to conditional regions (HLR-133), and it lets a query state the specific case before the general one — `static` before "every function", `pub` before "every function" — without either language needing a different convention from the other.

    **A language whose module supplies no visibility query shall report neither.** This is the asymmetry HLR-138 draws for a language with no dead-code query and HLR-133 for an undecidable condition, applied to a third kind of absence: *not analysed* and *public* are different claims, and a reader who cannot tell them apart has been told something false rather than nothing.

    Access control within a class — C++'s `private:` and `public:` sections — is a different axis from linkage and is not what this reports. A private method of an externally visible class has external linkage, and conflating the two would make the column answer neither question.
    *Trace:* [SDD Section 29](SDD.md), [SDD Section 7](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-210"></a>**HLR-210: Function Location Reported Navigably.**
    In a human-readable report, `elc` shall report each function's location as `path:line`, where `line` is the line the function begins on, in the column that today carries the path alone.

    **A location a reader cannot act on is a location they have to retype.** `path:line` is the form editors and terminals already parse — VS Code turns it into a jump to that line of that file — and it costs nothing: the column held the path already, and the line number was in the next column over.

    The path shall remain the canonical absolute path every other path in a report takes (HLR-042). A relative path would be shorter and would resolve against whatever directory the reader's terminal happens to be in, which is the one thing a navigable reference must not depend on.

    This governs the human-readable report alone. A complete-record format shall keep the path and the line as separate fields, because a consumer that has to split a string to recover a number has been handed a worse record than it was given before.
    *Trace:* [SDD Section 14](SDD.md).

## 30. The Image as Evidence

Requirements governing two answers the debug information in a linked image already contains and `elc` used to discard.

`elc` reads the line table (HLR-153) and builds a subprogram origin map keyed on the file each function was written in (HLR-193). Both were built for one question each — which lines a build compiled, and which of two same-named statics an image kept — and both answer a second question the source cannot answer at all: **which branch of an undecidable `#ifdef` this build took**, and **where the functions the grammar cannot see are**.

**Everything here is evidence and not proof, and every requirement in this section says so.** HLR-154 already states the limit: an optimiser folds one line into a neighbour, so absence of a line proves nothing on its own. What is added is the observation that an *entire region* absent between two present neighbours, or absent while its own `#else` is present, is a different quality of evidence — strong enough to act on, and still reported as its own figure rather than folded in with what a `-D` settled.

*   <a id="HLR-211"></a>**HLR-211: Conditional Regions Decided from the Image.**
    Where the image's line information covers a file, `elc` shall decide a conditional region that region's condition left undecidable (HLR-133) from what the build compiled: a region none of whose lines produced an instruction is **inactive** for that build, and one whose lines produced an instruction is **active**.

    **Absence is evidence only against a presence.** A region carrying an alternative shall be decided against that alternative, and only where exactly one of the two branches produced an instruction: both, or neither, is a condition this rule cannot read — a function the build never emitted reads as "neither" — and the region stays undecidable. A region with no alternative shall be decided inactive only where a line before it and a line after it, in the same file, were both compiled; that bracketing is what separates a gap in the middle of compiled code from the edge of what the build described.

    **Coverage governs, per file, exactly as HLR-154 requires.** A translation unit compiled without debug information contributes no line entries at all, so a rule keyed on absence alone would find every region of it inactive and delete each one. No region of a file the line information does not cover may be decided this way.

    **A definition the user gave is consulted first and is never overruled.** A `-D` is what the user says this configuration is; this is evidence about one build that was made. Where both have an answer the definition governs, and the region is not counted among those the image decided (HLR-132).

    **The three dispositions shall be counted apart.** A region settled from the source or a `-D`, a region settled from the image, and a region nothing settled are different claims, and a reader who cannot tell the second from the first has been handed evidence wearing the authority of a definition. The count of regions decided from the image shall be reported with the image's other provenance, beside the count of lines the same evidence excluded at the finer grain (HLR-155).

    **And this is evidence, with two bounds worth stating.** The first is HLR-154's: what an optimiser removed and what a condition excluded are indistinguishable in the line table, so the more optimised the build the more this rule will call inactive. The second is narrower and belongs to this rule alone: a region holding only declarations or data produces no line entries whether or not it was compiled, so one sitting between two compiled functions can be judged inactive on evidence that was never about it. Neither is silent — both are visible in the figure this requirement requires be reported — and the direction of both errors is the same as pruning's, which is why the count is read the way the pruned-line count is.

    **A region decided this way is decided**, and where that leaves a file with nothing undecided the file becomes eligible for expansion, which HLR-208 refuses to a file holding an undecidable region. That is the intended consequence rather than an accident of the implementation: what HLR-208 guards against is the preprocessor silently resolving what `elc` declared unresolvable, and here an answer is available, read off the build the image records, and the preprocessor is run under the flags and definitions describing that same build.

    Debug information remains never required (HLR-141). A run with no image, or with an image carrying none, shall behave exactly as it did before this requirement existed.
    *Trace:* [SDD Section 7](SDD.md), [SDD Section 20](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-212"></a>**HLR-212: Functions the Image Places and the Parse Did Not Reach.**
    On a run filtered by an image, `elc` shall report each function the image's debug information places in an analysed file, at a line no reported function covers, and the link kept.

    **This is the fourth direction of mismatch and the mirror of HLR-143.** That one names a function the source defines and the build dropped; this names a function the build defines and the grammar never saw. A macro expanding to a whole function definition is the case that produces it — `ISR(USART0_DRE_vect)` is a function to the compiler and an expression to tree-sitter — and the repair of HLR-196 cannot reach it, because repair does not know the macro *defines* one.

    **The symbol table governs which functions these are, and the debug information governs only where they are.** The two disagree, ordinarily rather than exceptionally: a link that discards unused sections removes the code while the compiler's subprogram entry for it stays behind. A function shall be reported here only where the image's symbol table defines it, which is the same authority HLR-140's filter asks. The debug information then supplies the location, which is the one thing the symbol table cannot.

    **Its name and its location shall be reported, and no figure shall be reported for it.** `elc` has no body for such a function: it has no ELOC, no complexity, no maintainability index, and no call sites. Reporting zero for any of those would state an absence as a measurement — precisely what HLR-133 refuses for an undecidable condition and HLR-138 for a language with no dead-code query.

    **It shall enter neither the call graph nor any figure computed over one.** It has no parsed body and therefore no outgoing edges, so a fan-out of zero for it would be a measurement of something nobody measured; a call *to* it is counted unresolved, where every call the graph cannot represent is counted (HLR-077). For the same reason it is not counted among the functions the project reports, is not banded, and is not named by a threshold: every one of those figures is derived from a measurement this function does not have.

    Its visibility is likewise not reported. The rule is the language's own and is answered by that language's query (HLR-209), and no query ran over a function the parse did not reach.

    A run with no image reports nothing here, in the way HLR-140 requires of every part of the filter.
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 20](SDD.md), [SDD Section 14](SDD.md).

## 23. Report Composition and Per-Function Banding

Requirements governing the order a human-readable report is presented in, which tables it presents, and the bands that decide which functions it singles out.

Every section before this one added a measurement and, with it, a section of the report. Nothing decided where that section went: it went last, because that is where a new one goes. Thirty sections later the report read in the order the tool was built rather than the order a reader needs — the findings twenty-second, the same six hundred functions enumerated in three tables, and a dozen headings over empty bodies on any project that was in good shape. The requirements here are about that, and about the two measurements that were reported without a band because nobody had gone back and asked whether one existed.

*   <a id="HLR-182"></a>**HLR-182: Findings Presented First.**
    In every human-readable format, `elc` shall present the findings of HLR-098 immediately after the project summary of HLR-024, ahead of every table that supplies their evidence.

    The findings are the one tier a reader is expected to act on (HLR-150), and they were presented last-but-eight because each analysis appended its section as it was built. A reader who has to scroll past six hundred rows to reach them is a reader who does not reach them. Nothing else need move to accommodate this: a finding names its subject, its file and its line, so it is read without the tables and the tables are found from it rather than the other way round.
    *Trace:* [SDD Section 14](SDD.md).

*   <a id="HLR-183"></a>**HLR-183: One Per-Function Table.**
    In every human-readable format, `elc` shall present each function's line range, effective lines, cyclomatic complexity, fan-in and fan-out in a **single** table, and shall present no other table enumerating one row per function for those measurements.

    Three tables did this between them: the per-function detail of HLR-014 through HLR-017, a fan-out table, and an information-flow table. All three enumerated the same functions in the same order, which is three chances to disagree about which functions exist and three places a reader had to look to answer one question about one function.

    The table shall list **every** analysed function, including one for which no flow figure was measured. The degrees are properties of the whole-project graph and the lengths are properties of one file's syntax, so a run that measured the second and not the first shall still present the second — with the degrees reported as the zero they are counted as, since a function at either end of the call graph legitimately has one (HLR-085, HLR-156).
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-184"></a>**HLR-184: The Order of the Architectural Tiers.**
    In every human-readable format, `elc` shall present the tiers following the per-file totals of HLR-019 in this order: component coupling (HLR-080, HLR-082), the dependency cycles between components (HLR-083), the threshold listing (HLR-187), the per-function table (HLR-183), the deepest call chain (HLR-088), and recursion (HLR-089).

    The order is the reader's descent through the program: the component, then what is entangled between components, then the short list of functions a threshold named, then every function, then the two whole-graph shapes that mean nothing until the functions are in view. It is stated as a requirement rather than left to the renderer because the order a report is read in is a property of the report, and because the previous order — the order the analyses were implemented in — was nobody's decision.

    The source functions a linked image does not define (HLR-143) shall be presented **last**, after every other tier. It is the longest table a filtered run produces — one row per function the build dropped — and it answers a question a reader asks after reading the report rather than one they read the report to answer. Its provenance stays where it was, among the summary tiers: the image a run was filtered by is something a reader must see before the figures above it mean anything, and the list of what the image lacked is not.
    *Trace:* [SDD Section 14](SDD.md).

*   <a id="HLR-185"></a>**HLR-185: Cyclomatic Complexity Threshold Classification.**
    `elc` shall classify each function's cyclomatic complexity (HLR-017) against published bands: a complexity of 10 or below produces no finding; 11 to 15 produces a **warning**; and greater than 15 produces a **critical** finding. The bands shall be attributed to McCabe as NIST SP 500-235 records them — 10 is McCabe's own limit, and 15 the highest limit that document reports as having been used successfully, and then only where an organisation has the design, review and test practices to justify it.

    These bands shall be **independent of the threshold of HLR-022**. That value governs which functions are *listed* and carries no severity (HLR-023); a user who moves it is choosing what they want to see, not what McCabe says. A build in which changing `--complexity-threshold` moved a severity would have made the catalogue's attribution false, since the number would then be the user's rather than the cited source's (HLR-099).
    *Trace:* [SDD Section 12](SDD.md).

*   <a id="HLR-186"></a>**HLR-186: Fan-In Threshold Classification.**
    `elc` shall classify each function's fan-in (HLR-156) against a band: a fan-in of 25 or below produces no finding, and greater than 25 produces a **warning**. There shall be no critical band.

    **This threshold is `elc`'s own and shall say so wherever the finding is reported**, in the same words the bottleneck heuristic of HLR-081 carries. No published source divides fan-in into accepted and unaccepted ranges; 25 distinct callers is where this project judges a function to have stopped being a routine and become an interface that cannot be changed, and that is a judgement rather than a citation. Presenting it beside MISRA, Martin and McCabe without the label would lend it an authority it has not got and would make HLR-099's central claim false.

    There is no critical band for the same reason there is a label on the warning one: `elc` has no published basis for a first line and none whatever for a second.
    *Trace:* [SDD Section 12](SDD.md).

*   <a id="HLR-187"></a>**HLR-187: The Threshold Listing Names Every Banded Function.**
    The per-file listing of HLR-021 shall name every function that falls in a warning or critical band for cyclomatic complexity (HLR-185), fan-in (HLR-186), or fan-out (HLR-086), united with the functions whose complexity meets the configured threshold of HLR-022. A function named by both rules shall appear once.

    Each listed function shall be presented with its complexity, its fan-in and its fan-out, and with the **highest** severity any band gave it. A function listed only because its complexity met the configured threshold shall carry **no** severity, since that threshold has never carried one and must not begin to (HLR-023).

    The listing is what a reader works from after the findings: the findings say what crossed a line, and this says which functions did, with the figures beside them.
    *Trace:* [SDD Section 13](SDD.md), [SDD Section 14](SDD.md).

*   <a id="HLR-188"></a>**HLR-188: A Table With No Rows Is Not Presented.**
    In every human-readable format, `elc` shall present no table that has no rows.

    Every table was presented whether or not it had anything in it, on the reasoning that an absent heading cannot be told from a renderer that forgot. At thirty sections that reasoning had inverted: a run over a code base in good order printed a dozen headings with nothing beneath them, and the sections that did have something to say were what a reader lost among them.

    The uniformity requirements are unchanged and are satisfied one level up. What HLR-006 fixes across target types, HLR-031 across formats, and HLR-032 across runs is the set of tiers the report **reaches** — presented where they found rows, and named by HLR-189 where they did not — rather than the sequence of headings a particular run prints. The complete-record formats are unaffected: CSV (HLR-028) and XML (HLR-054) carry every element whatever its content, exactly as HLR-152 exempts them from verbosity.
    *Trace:* [SDD Section 14](SDD.md).

*   <a id="HLR-190"></a>**HLR-190: Markdown Tables Presented Behind a Disclosure Element.**
    In the Markdown report of HLR-029, `elc` shall present every table inside an HTML `<details>` element whose `<summary>` states how many rows the table holds, so that a reader expands the tables they want rather than scrolling past the ones they do not.

    **The section's `##` heading shall remain a heading**, outside the element. The heading is what anchors a section: a Markdown renderer derives a link target from it, a table of contents is built out of it, and the report's own composition is read off it. Folding the heading into the `<summary>` would trade a navigable document for a tidy one — so the summary states what is *inside* the element instead, which is the one thing a reader deciding whether to expand does not already know from the heading above it.

    The row count in the summary shall be derived from the rows presented, never written down beside them. A count maintained separately from what it counts is a count that drifts.

    This requirement governs the Markdown report alone. The aligned table (HLR-027) has no disclosure to offer and is unchanged; CSV (HLR-028) and XML (HLR-054) are complete-record formats whose consumers parse them, and HTML in either would be a defect rather than a convenience. Uniform composition across formats (HLR-031) is unaffected: the tiers each format reaches, and the headings it presents them under, are the same as before.
    *Trace:* [SDD Section 14](SDD.md), [SDD Section 22](SDD.md).

*   <a id="HLR-189"></a>**HLR-189: The Empty Tables Named in a Closing Statement.**
    A human-readable report shall end with a statement naming every table HLR-188 omitted, and shall end with that statement whether or not any table was omitted.

    Each shall be named by its **full heading**, verbatim. Several headings carry the reason an analysis was omitted for want of a user declaration, and HLR-115 requires that reason to be stated wherever the analysis is not — so repeating the heading is what keeps HLR-115 satisfied by the same words whether the section was presented or not. A statement that merely counted the omitted tables, or named them by some shortened label, would not.

    The statement is unconditional because a section that appears only sometimes is the problem it exists to solve: a reader must be able to tell "this run found no recursion" from "this build does not look for recursion", and the difference has to be legible on the page rather than inferred from an absence.
    *Trace:* [SDD Section 14](SDD.md).

## 31. Interactive HTML Reporting and Semantic Zooming

Requirements governing the interactive companion: the graph `elc` already builds, drawn at the level the reader chooses rather than at the one level it has always been drawn at.

`elc` writes the graph twice already — as `.dot` for Graphviz to lay out (HLR-103) and as GraphML for a tool to load (HLR-106). Both are exports, and both scale by getting denser: a project of a few thousand functions produces a drawing nobody looks at twice. The requirements here do not add a measurement. They emit a structure `elc` computes on every run and discards at the point of emission — a function belongs to a file (HLR-114), a file belongs to a declared layer (HLR-078) — so that the same graph can be read as four boxes or as four thousand nodes, at the reader's choice.

**Nothing here is a second opinion about anything.** Every figure the payload carries was computed elsewhere and is copied; every edge it carries is an edge of the SDG. Where the drawing needs a number the report also states, it takes the report's.

*   <a id="HLR-213"></a>**HLR-213: The Graph Serialised as a Containment Hierarchy.**
    `elc` shall serialise the System Dependence Graph as a hierarchical node document in which each function is contained by the file that defines it and each file is contained by the architectural layer it was declared in.

    **Three tiers, and each is an existing answer rather than a new one.** The layer of a component is the one `stratum_of_components` assigns (HLR-078), the component of a function is `SdgNode.component` (HLR-114), and the figures carried on a function node are the ones the report already states for it. This requirement is about *emission*, not measurement: a drawing that recomputed any of the three could disagree with the tables printed beside it, which is the failure HLR-164 forbids for the conformance indices and forbids here for the same reason.

    **Containment is expressed by reference, not by nesting.** Each node names its container, so the document is a flat sequence a consumer may read without recursion and the three tiers may be emitted in one pass. That is the form the rendering library specifies; adopting it is a design choice under HLR-112 rather than a requirement, and what this requirement fixes is that the containment be *stated* rather than left for a viewer to infer from path prefixes.

    **A component in no declared layer shall be contained by nothing.** `stratum_of_components` places a file matching no stratum outside the declared architecture rather than in a layer of its own, because the user said nothing about it and placing it would report a structure nobody drew. The serialisation shall follow that judgement rather than reverse it: such a component is emitted with no container, and a run declaring no strata at all therefore yields a two-tier file-and-function document rather than one rooted in an invented layer. Inferring a layer from the directory tree here would be the filesystem-derived architecture HLR-078 refuses.

    **Identifiers shall be stable across runs.** The document is a companion artefact and is subject to HLR-032 like every other: two runs over unchanged input produce byte-identical output.

    **One serialisation library, not two.** `elc` already links one for the purification manifest (HLR-175). HLR-112 makes the choice a design matter, and the choice is made once: a second library would add a dependency the project already has an answer for.
    *Trace:* [SDD Section 27](SDD.md), [SDD Section 8](SDD.md), [SDD Section 9](SDD.md).

*   <a id="HLR-214"></a>**HLR-214: Edges Between Functions Only.**
    The serialised document shall carry an edge for each call edge of the System Dependence Graph, joining two function nodes, and shall carry no edge joining two containers.

    **A dependency between two files is derived, and derived by the viewer.** A rendering that collapses a file to a single shape synthesises the connections between collapsed shapes from the edges they contain, at the moment of collapsing. An emitted file-to-file edge would therefore be a second and independent statement of the same fact in one artefact.

    **And it would be the weaker statement.** `elc` already reports coupling between components, with a threshold behind it and an attribution beside it (HLR-081, HLR-099). A figure drawn in this document by a rule of its own could not be reconciled with that one, and a reader who found the two disagreeing would have no way to tell which was the measurement.

    **Global-access edges are not call edges and are not emitted here** (HLR-074). Coupling through shared state is a different fact about a design from coupling through calls, and the graph carries the two separately precisely so that an analysis cannot report one as the other. A drawing of the call structure that silently included the global edges would do exactly that.
    *Trace:* [SDD Section 27](SDD.md), [SDD Section 8](SDD.md).

*   <a id="HLR-215"></a>**HLR-215: A Single File That Opens Without a Server.**
    `elc` shall write the interactive companion as one file, named from the report's own output path by the rule every companion follows (HLR-119), which renders when opened directly from the filesystem — requiring no build step, no bundler, and **no local web server**.

    **The no-server constraint decides the artefact's shape**, and it is a usability requirement rather than a technical one: a reader who must start a server to look at a report does not look at the report. It forbids splitting the payload into a sibling file the page fetches, which is what a browser's local-file security policy blocks and what would otherwise be the natural structure.

    **"Standalone" is bounded, and the bound shall be stated rather than discovered.** The rendering library is fetched by the *browser*, at the time the page is viewed. That is not an HLR-040 matter — that requirement governs what a *run* of `elc` needs, and this run writes text and exits, requiring no interpreter, no virtual machine and no network of its own. What it does oblige is precision about the claim: the file needs no server and no build step, and it does need the network the first time it is opened. `elc` shall document that plainly (HLR-128 – HLR-130) rather than let a reader on a disconnected machine discover it as a blank page.

    **The payload shall be embedded such that no content of the graph can terminate its container.** A function name is arbitrary text from the user's source — a C++ template signature contains angle brackets, and `elc` places no restriction on what a name may hold. Serialisation escaping alone is not sufficient here: a name may be perfectly well-formed in the serialisation and still close the element embedding it, or be a line terminator to the embedding language and not to the serialisation. The requirement is on the result — a name containing any character whatsoever renders as that name — and not on the mechanism.

    **Absent an output path, no companion is written and that is not an error.** The name is derived from the report's, so a report on standard output has none to derive from — the rule the GraphML export, the debug log, the matrix, and the manifest all follow (HLR-104, HLR-106, HLR-119).
    *Trace:* [SDD Section 27](SDD.md), [SDD Section 4](SDD.md).

*   <a id="HLR-216"></a>**HLR-216: The View Opens at the Architectural Level.**
    The companion shall open showing the outermost tier the run produced — the declared layers, or the files where no layer was declared — with every container collapsed, and shall let the reader descend to any tier and return.

    **The default is the requirement, not a preference.** What makes the existing graph companions unreadable on a real project is not that they lack a collapsing facility; it is that they begin at maximum density. A view that opened at function level and offered collapsing would reproduce that failure and add a step to recovering from it. The reader is shown the architecture and descends into what interests them, which is the order the question is actually asked in.

    **Descent shall preserve the reader's place.** Expanding a container shall not re-lay-out the whole drawing from nothing: a reader who loses their bearings on every expansion is navigating a new drawing each time rather than one drawing at a new depth.
    *Trace:* [SDD Section 27](SDD.md).
