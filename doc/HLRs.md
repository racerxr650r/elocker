# High-Level Requirements

**Version:** 3.0
**Date:** 2026-08-14
**Author(s):** John Anderson

## 1. Target Discovery and Input Routing

Requirements governing how `elc` discovers and selects the set of source files to analyze from its target arguments (PVD §5 item 1, §7.1).

*   <a id="HLR-071"></a>**HLR-071: Multiple Target Arguments.**
    `elc` shall accept one or more target arguments in a single invocation, in any combination of regular file names and directory names. Each target argument shall be classified and routed independently according to its own type (HLR-001, HLR-002, HLR-004), so that files and directories may be freely intermixed on one command line, and the results from every target shall be combined into a single report whose file-level entries and project-level totals (HLR-024 through HLR-026) span all targets analyzed.

*   <a id="HLR-072"></a>**HLR-072: Duplicate File Elimination Across Targets.**
    When the same source file is reached through more than one target argument — for example a file named explicitly on the command line that is also contained within a named directory, or two named directories that overlap — `elc` shall analyze and report that file exactly once, so that no file's metrics contribute more than once to the file-level results or to the project-level totals.

*   <a id="HLR-001"></a>**HLR-001: Single-File Target Handling.**
    `elc` shall accept a path to a single regular file as its analysis target and shall process that file directly, without performing directory traversal, when the target argument is a regular file.

*   <a id="HLR-002"></a>**HLR-002: Git-Repository Target Detection.**
    When a directory target lies within a Git repository, `elc` shall detect that repository — searching the target directory and then its ancestors — and shall enumerate the source files tracked at `HEAD` for analysis rather than walking the raw filesystem. A repository shall be treated as *applicable* to a target only when it tracks the target directory. Where an enclosing repository is found but does not track the target — a build directory excluded by `.gitignore`, or an unrelated repository such as a version-controlled home directory several levels above — that repository shall be disregarded and the target analysed by filesystem traversal instead (HLR-004).

*   <a id="HLR-003"></a>**HLR-003: Git-Aware Exclusion.**
    For a Git-repository target, `elc` shall exclude from analysis any file that is not tracked by Git (including `.gitignore`d and untracked files) and shall exclude binary files, without requiring the user to maintain a separate exclusion list.

*   <a id="HLR-004"></a>**HLR-004: Plain-Directory Fallback Traversal.**
    When a directory target is not part of a Git repository, or lies within one that does not track it (HLR-002), `elc` shall recursively traverse the directory tree at the filesystem level to discover source files.

*   <a id="HLR-126"></a>**HLR-126: Repository Enumeration Scoped to the Target.**
    For a repository target, `elc` shall enumerate only those tracked blobs whose path lies at or beneath the target directory. Naming a subdirectory shall analyse that subdirectory and nothing above it, so that a directory target denotes the same set of files whether it is reached by repository enumeration or by filesystem traversal.

*   <a id="HLR-127"></a>**HLR-127: Discovery Route Reported.**
    `elc` shall report, for each directory target, which discovery route was applied — repository enumeration or filesystem traversal — so that a result which is unexpectedly empty, or unexpectedly larger than the target, can be diagnosed rather than guessed at.

*   <a id="HLR-005"></a>**HLR-005: Filesystem-Fallback Exclusion.**
    During the filesystem-level traversal of HLR-004, `elc` shall exclude files with recognized binary file extensions and shall exclude hidden directories from analysis. The set of recognized binary extensions shall be defined by data in the runtime location rather than compiled into the executable, so that the exclusion list may be adjusted without a rebuild, consistent with HLR-060.

*   <a id="HLR-069"></a>**HLR-069: Symbolic Link Handling During Traversal.**
    During the filesystem-level traversal of HLR-004, `elc` shall not descend into a directory reached through a symbolic link, so that a cyclic or self-referential link cannot cause unbounded traversal, and so that a linked directory's files are not counted more than once. A symbolic link supplied directly as the analysis target shall be resolved and analyzed, since it names the target explicitly.

*   <a id="HLR-006"></a>**HLR-006: Uniform Target Output Shape.**
    Regardless of whether the target was a single file, a plain directory, or a Git repository, `elc` shall produce output with the same structure and fields, so that results from different target types are directly comparable.

## 2. Automatic Language Detection and Extensibility

Requirements governing how `elc` identifies each file's programming language and extends its language support without recompilation (PVD §5 item 2, §6 Principle 2, §7.1).

*   <a id="HLR-007"></a>**HLR-007: Per-File Automatic Language Detection.**
    `elc` shall determine the programming language of each discovered source file automatically, from the file's extension, without the user specifying which language any file is written in.

*   <a id="HLR-008"></a>**HLR-008: Mixed-Language Single-Pass Analysis.**
    `elc` shall analyze a target containing source files written in more than one supported language within a single invocation and a single pass, without requiring the user to invoke `elc` once per language.

*   <a id="HLR-009"></a>**HLR-009: Runtime-Loaded Language Support.**
    `elc` shall load all language-specific parsing and query logic from a runtime location external to the executable, at startup or on first use of that language, rather than compiling language-specific logic into the executable.

*   <a id="HLR-059"></a>**HLR-059: Runtime Location Discovery and Precedence.**
    `elc` shall resolve the location of the runtime language-support directory of HLR-009 from a dedicated environment variable when that variable is set, and otherwise from a path relative to the `elc` executable itself. When both a set environment variable and a runtime directory adjacent to the executable are present, the environment variable shall take precedence.

*   <a id="HLR-060"></a>**HLR-060: Extension Mapping Defined by Runtime Data.**
    The mapping from a source file's extension to a language name, used by the automatic detection of HLR-007, shall be defined by data within the runtime language-support location (HLR-009) rather than compiled into the `elc` executable; associating an additional or alternative extension with a language shall require no modification to, and no recompilation of, the executable.

*   <a id="HLR-010"></a>**HLR-010: No-Recompilation Language Addition.**
    `elc` shall support adding support for a new language by adding files to the runtime location of HLR-009 alone; adding a language shall require no modification to, and no recompilation of, the `elc` executable.

*   <a id="HLR-121"></a>**HLR-121: Language Module Interface Is a Stable Contract.**
    The interface between the `elc` executable and a language module — the set of query files a language is required to supply, and the capture names by which those queries return their results — shall be documented, and a language module supplying exactly the documented set shall function correctly with no further configuration. A module omitting a required query file shall be handled under HLR-070 rather than producing undefined behaviour. This interface is the contract a third party codes against when adding a language (HLR-010): renaming a required query file or a capture name, or changing the meaning of either, is a breaking change to that contract rather than an internal adjustment. That last is a constraint on the project's release process, verified by review, rather than a property observable within any single run.

*   <a id="HLR-011"></a>**HLR-011: Initial Delivered Language Set.**
    The elocker *project* shall deliver runtime language support for C, C++, Rust, Python, and Ada. This requirement constrains the project's deliverables, not the `elc` executable: `elc` shall not require, verify, or assume the presence of any particular language's support files, and shall complete with the exit-status semantics of HLR-120, producing a report per HLR-031, over whatever set of valid language modules the runtime location happens to contain.

*   <a id="HLR-012"></a>**HLR-012: Unsupported-Language File Handling.**
    When a discovered source file's extension does not map to any language available in the runtime location, `elc` shall skip that file rather than terminating the run, and shall report the skip through two observables: the file shall appear in the report's list of skipped files, and a diagnostic naming it shall be written to standard error. A skipped file is not a failure (HLR-037, HLR-120).

## 3. Code Metrics Computation

Requirements governing how `elc` computes Effective Lines of Code and cyclomatic complexity from a parsed syntax tree (PVD §5 items 4 and 6, §6 Principle 1, §7.1).

*   <a id="HLR-013"></a>**HLR-013: AST-Based Metric Extraction.**
    `elc` shall derive every reported metric from a parsed abstract syntax tree (AST) of the source file; `elc` shall not use regular-expression matching, brace/token counting, or any other textual approximation to compute a reported metric.

*   <a id="HLR-014"></a>**HLR-014: Per-Function Identity.**
    For each function discovered in a source file, `elc` shall report the function's name and its start and end line numbers. For the purposes of this and every other requirement in this document, "function" means any named callable unit the source language defines — including a method, a constructor, a destructor, and a nested subprogram — as identified by that language's runtime query configuration.

*   <a id="HLR-015"></a>**HLR-015: Per-Function Effective Lines of Code.**
    For each function discovered in a source file, `elc` shall compute and report the function's Effective Lines of Code (ELOC): the count of executable statements within the function's line span — a statement that assigns or operates on data, directs control flow, invokes a function, returns from the function, or performs exception handling — as distinct from a line that serves only a structural, declarative, blank, or documentary purpose. HLR-044 through HLR-052 enumerate the specific categories counted toward, and excluded from, ELOC; HLR-053 governs how a statement spanning multiple physical lines is counted.

*   <a id="HLR-053"></a>**HLR-053: Multi-Line Statements Counted as a Single Line.**
    `elc` shall count a single statement that spans multiple physical lines of source code as one line toward ELOC, not once per physical line it occupies, so that identical logic yields the same ELOC count whether written across several lines or condensed onto one — for example, an `if` condition split across three lines with its opening brace on its own line shall contribute the same ELOC as the same condition written on a single line.

*   <a id="HLR-016"></a>**HLR-016: Comment Span Merging.**
    When computing ELOC, whether per function or per file, `elc` shall identify comment spans from the AST, sort them by start position, and merge any overlapping or nested spans before excluding them from ELOC, so that no line is excluded more than once regardless of nested or overlapping comment syntax.

*   <a id="HLR-017"></a>**HLR-017: Per-Function Cyclomatic Complexity.**
    For each function discovered in a source file, `elc` shall compute and report the function's cyclomatic complexity, defined as one plus the number of decision points found within the function's body, as identified from the AST.

*   <a id="HLR-018"></a>**HLR-018: Anonymous-Scope Complexity Attribution.**
    A decision point that occurs within an anonymous callable — a lambda, closure, or other unnamed nested function — shall be attributed to the cyclomatic complexity of the nearest enclosing named function, since the anonymous callable is not itself reported as a function, unless the language's runtime query configuration explicitly attributes it to the nested scope instead. Nested *named* functions are governed by HLR-067 and HLR-068 instead.

*   <a id="HLR-067"></a>**HLR-067: Nested Named Functions Reported Independently.**
    A named function declared within the body of another function — such as an Ada nested subprogram, or a nested function or method in any other supported language — shall be discovered and reported as a function in its own right, with its own name, line range, ELOC, and cyclomatic complexity, rather than being folded into its enclosing function.

*   <a id="HLR-068"></a>**HLR-068: Innermost-Function Metric Attribution.**
    Each statement shall contribute to the ELOC and cyclomatic complexity of exactly one reported function: the innermost reported function enclosing it. A statement within a nested named function (HLR-067) shall therefore contribute to that nested function's metrics and shall not also contribute to those of any function enclosing it, so that no statement is counted twice within a single file's per-function results.

*   <a id="HLR-019"></a>**HLR-019: File-Level Totals.**
    For each analyzed file, `elc` shall compute and report the file's total physical line count and the file's total Effective Lines of Code, accounting for every line in the file that qualifies as ELOC under HLR-044 through HLR-052, including such lines that lie outside of any function.

*   <a id="HLR-020"></a>**HLR-020: Files With No Effective Lines of Code.**
    A file containing no line that qualifies as ELOC under HLR-044 through HLR-052 — for example, a file consisting entirely of blank lines, comments, declarations, and/or preprocessor directives — shall be reported with an ELOC of zero, without error.

*   <a id="HLR-044"></a>**HLR-044: Assignments and Operations Count as ELOC.**
    `elc` shall count a line that performs a data assignment or a mathematical, logical, or pointer operation toward ELOC — for example, a variable initialization (`int x = 5;`) or a memory/pointer write (`buffer[0] = 0xAA;`).

*   <a id="HLR-045"></a>**HLR-045: Control-Flow Statements Count as ELOC.**
    `elc` shall count a line containing a control-flow construct that directs the execution path — including `if`, `else`, `while`, `for`, `switch`, `case`, `break`, and `continue`, or a language's equivalent construct — toward ELOC.

*   <a id="HLR-046"></a>**HLR-046: Function-Call Statements Count as ELOC.**
    `elc` shall count a line that invokes a function or method toward ELOC, regardless of whether the call's result is used (e.g. `printf("Hello World");` or `init_hardware();`).

*   <a id="HLR-047"></a>**HLR-047: Return Statements Count as ELOC.**
    `elc` shall count a line that returns from a function, with or without a value, toward ELOC (e.g. `return 0;`).

*   <a id="HLR-048"></a>**HLR-048: Exception-Handling Statements Count as ELOC.**
    `elc` shall count a line containing an exception-handling construct — such as `try`, `catch`, or `throw`, or a language's equivalent construct — toward ELOC.

*   <a id="HLR-049"></a>**HLR-049: Blank Lines Excluded from ELOC.**
    `elc` shall exclude blank lines — lines containing no tokens other than whitespace — from ELOC.

*   <a id="HLR-050"></a>**HLR-050: Standalone Structural Tokens Excluded from ELOC.**
    `elc` shall exclude a line containing nothing but a standalone structural token — such as an opening or closing brace or parenthesis — from ELOC.

*   <a id="HLR-051"></a>**HLR-051: Non-Initializing Declarations Excluded from ELOC.**
    `elc` shall exclude a line that only declares a variable or function, without initializing data or executing code — such as `int my_variable;` or a bare function prototype — from ELOC.

*   <a id="HLR-052"></a>**HLR-052: Preprocessor and Directive Lines Excluded from ELOC.**
    For a language whose grammar defines preprocessor or compile-time directive constructs, `elc` shall exclude a line consisting only of such a directive — such as a C/C++ `#include`, `#define`, or header-guard `#ifndef` — from ELOC, as it does not itself translate to a runtime instruction.

## 4. File-Level and Project-Level Reporting

Requirements governing how `elc` aggregates and summarizes per-function metrics at the file and project level (PVD §7.1, §8 "Project Summary").

*   <a id="HLR-021"></a>**HLR-021: Per-File Complexity-Threshold List.**
    For each file, `elc` shall report the list of functions within that file whose cyclomatic complexity meets or exceeds a threshold value, alongside that file's totals.

*   <a id="HLR-022"></a>**HLR-022: Configurable Complexity Threshold.**
    The threshold value used by HLR-021 shall be configurable by the user, and shall default to 15 when the user does not supply a value.

*   <a id="HLR-023"></a>**HLR-023: Threshold List is Reporting-Only.**
    The complexity threshold of HLR-021 / HLR-022 shall affect only what is listed for a file; it shall have no effect on `elc`'s process exit status.

*   <a id="HLR-024"></a>**HLR-024: Project-Level Totals.**
    Across all files analyzed in a single run, `elc` shall compute and report the combined total physical line count and the combined total Effective Lines of Code for the entire target.

*   <a id="HLR-025"></a>**HLR-025: Project Totals by Source Language.**
    In addition to the combined totals of HLR-024, `elc` shall break down the project-level physical-line and ELOC totals by source language, so the contribution of each language present in the target is separately visible.

*   <a id="HLR-026"></a>**HLR-026: Project-Wide Most-Complex Callouts.**
    `elc` shall identify, across the entire run, the file with the highest file-level ELOC and the function with the highest cyclomatic complexity, and shall include both in the project summary. When two or more files, or two or more functions, tie for the highest value, `elc` shall select whichever sorts first under the stable presentation order of HLR-033, so that the callout is deterministic rather than dependent on discovery order.

## 5. Output Formatting

Requirements governing how `elc` renders its computed results for human and machine consumption (PVD §5 item 7, §7.1).

*   <a id="HLR-027"></a>**HLR-027: Default Human-Readable Output.**
    By default, `elc` shall render its results as an aligned, human-readable table on standard output.

*   <a id="HLR-028"></a>**HLR-028: CSV Output.**
    `elc` shall support rendering the complete, per-function dataset as CSV — one record per function, unfiltered by the complexity threshold of HLR-021 / HLR-022. CSV carries per-function metrics only: the architectural findings of Sections 11 through 14 are not expressible as a single flat record set and are therefore excluded from it. XML (HLR-054) is the format that carries a complete record of a run.

*   <a id="HLR-064"></a>**HLR-064: CSV Field Quoting and Escaping.**
    `elc`'s CSV output (HLR-028) shall quote and escape every field whose value contains a comma, a double-quote character, or a line break, in accordance with RFC 4180, so that a value containing such a character — for example a C++ template signature such as `foo<int, long>` — cannot corrupt the record or field structure of the document.

*   <a id="HLR-054"></a>**HLR-054: XML Output.**
    `elc` shall support rendering the complete dataset of a run as XML; like CSV (HLR-028), the XML output shall not be filtered by the complexity threshold of HLR-021 / HLR-022. The XML output shall carry every element that any report may present — project totals, per-file totals, per-function detail, the architectural findings of Sections 11 through 14, and any custom-rule matches (HLR-109) — so that it serves as a complete, durable record of the run, sufficient on its own to regenerate any report `elc` can produce (see Section 9).

*   <a id="HLR-061"></a>**HLR-061: XML Format-Version Identifier.**
    `elc`'s XML output (HLR-054) shall carry a format-version identifier describing the structure of the document, so that any consumer — including `elc`'s own conversion mode (HLR-055) — can determine whether it understands that structure before interpreting the document's contents.

*   <a id="HLR-065"></a>**HLR-065: XML Well-Formedness and Escaping.**
    Every XML document `elc` emits — the report record of HLR-054 and the GraphML export of HLR-106 alike — shall be well-formed XML. Every character occurring within element content or an attribute value that carries structural meaning in XML — including `&`, `<`, `>`, and quotation marks — shall be escaped, so that a source identifier or file path containing such a character cannot render the document unparseable.

*   <a id="HLR-029"></a>**HLR-029: Markdown Output.**
    `elc` shall support rendering its results as GitHub-Flavored Markdown, with functions grouped under a heading for the file that contains them.

*   <a id="HLR-030"></a>**HLR-030: Optional Output-File Redirection.**
    `elc` shall support writing its rendered output to a user-specified file, as an alternative to writing to standard output.

*   <a id="HLR-031"></a>**HLR-031: Uniform Report Composition Across Formats.**
    Every report format `elc` supports other than CSV (HLR-028), XML (HLR-054), and Graphviz `.dot` (HLR-102) shall present the same tiers of information: the project summary (HLR-024 through HLR-026), the discovery route applied to each directory target (HLR-127), each file's totals and threshold list (HLR-019, HLR-021), full per-function detail (HLR-014, HLR-015, HLR-017), the architectural measurements *and* findings of Sections 11 through 14 — including a measurement that falls within its accepted band and therefore yields no finding — any custom-rule matches (HLR-109), the files skipped for want of a language module (HLR-012), and any analysis omitted for want of a user declaration (HLR-115).

## 6. Determinism and Correctness

Requirements governing the determinism and correctness of `elc`'s output (PVD §6 Principle 4, §8 "Determinism" and "Correctness against hand counts").

*   <a id="HLR-032"></a>**HLR-032: Deterministic Output.**
    Running `elc` twice, unmodified, over the same target shall produce byte-identical output both times.

*   <a id="HLR-033"></a>**HLR-033: Traversal-Order Independence.**
    The order in which `elc` presents files, and the functions within a file, shall not depend on filesystem or Git traversal order; `elc` shall present results in a stable, defined order regardless of the underlying operating system's or filesystem's enumeration order. This requirement extends to every collection `elc` reports — including cycles, bottlenecks, unreachable functions, hidden channels, and custom-rule matches — whose presentation order shall not depend on the enumeration order of any graph library, hash container, or other internal data structure.

*   <a id="HLR-034"></a>**HLR-034: Correctness Against Hand-Counted Fixtures.**
    `elc`'s computed ELOC and cyclomatic-complexity values shall match values counted by hand for a suite of fixture files, including fixture files containing nested comments and comment syntax embedded within string literals.

## 7. Failure Handling and Exit Status

Requirements governing how `elc` responds to failures and what its process exit status communicates (PVD §6 Principle 6).

*   <a id="HLR-035"></a>**HLR-035: Per-File Read- and Parse-Failure Tolerance.**
    A file that cannot be read — for example because permission is denied or its contents cannot be decoded — or that fails to parse, shall not abort the run; `elc` shall emit a diagnostic identifying that file to standard error and shall continue processing the remaining files in the target. A file shall be deemed to have failed to parse when its syntax tree contains any error node, and the whole file shall then be skipped rather than reported from a partially valid tree, since metrics derived from a damaged tree are indistinguishable from sound ones once rendered.

*   <a id="HLR-036"></a>**HLR-036: Setup-Failure Fatality.**
    A runtime language-support location that is absent, unreadable, or that yields no valid language module whatsoever shall be treated as a fatal error; `elc` shall emit a diagnostic and abort the run before any file is processed, since it can perform no analysis at all in that state.

*   <a id="HLR-070"></a>**HLR-070: Malformed Language Module Tolerance.**
    An individual language module that is present but unusable — because it cannot be loaded, does not expose the expected entry point, or carries invalid or unparseable query files — shall not abort the run. `elc` shall emit a diagnostic to standard error identifying that language, exclude it from the run, and continue using the remaining valid language modules. Provided at least one valid language module remains, the run shall proceed and complete normally, and the unusable module shall not by itself cause a non-zero exit status.

*   <a id="HLR-037"></a>**HLR-037: Truthful Exit Status.**
    `elc`'s process exit status shall be non-zero whenever any per-file failure occurred during the run (HLR-035), and shall be zero only when every discovered file was either processed without error or skipped under HLR-012. A file skipped because its language is unavailable is not a failure and shall not by itself make the exit status non-zero.

*   <a id="HLR-120"></a>**HLR-120: Distinct Exit Status Classes.**
    `elc`'s exit status shall distinguish the two classes of failure, so that a caller can tell a degraded run from a run that never happened. A status of `0` shall indicate that every discovered file was processed without error. A status of `1` shall indicate that the run completed and produced a report, but at least one discovered file failed to be read or parsed (HLR-035, HLR-037). A status of `2` shall indicate that the run did not complete and no report was produced — a usage error (HLR-063), an invalid target (HLR-062), a fatal runtime-location failure (HLR-036), or a rejected saved record (HLR-058). No finding severity shall contribute to any of these (HLR-100).

*   <a id="HLR-038"></a>**HLR-038: Diagnostics on stderr, Results on stdout.**
    `elc` shall never write diagnostic or error messages to the same stream used for reported results, so that captured or piped results are never corrupted by diagnostic text.

*   <a id="HLR-062"></a>**HLR-062: Invalid Target Rejection.**
    When any target argument does not exist, cannot be opened or read, or is neither a regular file nor a directory — for example a socket, FIFO, or device node — `elc` shall emit a diagnostic to standard error identifying that target and shall terminate with a non-zero exit status without producing a partial report. All target arguments shall be validated before any analysis begins, so that an invalid target is reported regardless of how many other targets are valid, and no report is emitted that silently covers fewer targets than the user named.

*   <a id="HLR-063"></a>**HLR-063: Invalid Command-Line Rejection.**
    When `elc` is invoked with an unrecognized option, with a missing or malformed argument to an option that requires one, or without a required target, `elc` shall emit a usage message to standard error and shall terminate with a non-zero exit status without analyzing any file.

*   <a id="HLR-117"></a>**HLR-117: Help Request Is Not an Error.**
    `elc` shall provide a command-line option that prints a usage summary — its options, their arguments, and their defaults — to standard output, and shall terminate with a zero exit status when that option is given, since requesting help is not an error. This is distinct from the usage message of HLR-063, which is emitted to standard error with a non-zero status in response to an invalid invocation.

*   <a id="HLR-066"></a>**HLR-066: Run With No Analyzable Files.**
    When a run completes and no file in the target was analyzed — because the target contained no source files, or because no discovered file's extension mapped to an available language — `elc` shall emit a well-formed report showing zero totals rather than emitting no output, and shall terminate with a zero exit status provided no per-file failure occurred (HLR-037).

## 8. Non-Functional Constraints

Requirements constraining `elc`'s runtime environment, dependencies, execution model, and memory behaviour (PVD §6 Principles 3, 5, and 7, §7.2). No performance target is committed; see the Throughput theme in PVD §9.

*   <a id="HLR-039"></a>**HLR-039: Zero Configuration.**
    `elc`'s behavior shall be fully determined by its command-line arguments and the contents of its runtime language-support location. Stated observably: the presence of any configuration-like file — a dotfile, an `.elcrc`, an editor or tooling configuration — in the working directory, in the analysis target, or in any ancestor directory of either, shall produce output byte-identical to its absence. `elc` shall neither read nor discover such a file.

*   <a id="HLR-040"></a>**HLR-040: Excluded Runtime Dependencies.**
    `elc` shall not require an interpreter, a virtual machine, or network access at any point during execution, and shall not require code generation at build time.

*   <a id="HLR-112"></a>**HLR-112: Library Selection Deferred to Design.**
    The specific third-party libraries `elc` links against shall be selected during design. Library names appearing in the PVD — for parsing, repository access, XML handling, and graph mathematics — are *suggested candidates rather than requirements*: a design that substitutes a different library satisfies this document provided the exclusions of HLR-040 are respected and the behaviour required elsewhere is delivered. The one exception is the Tree-sitter query language and grammar format, which are visible to the user in the `.scm` files and runtime grammars they author (HLR-009, HLR-107) and are therefore a product contract rather than an implementation choice.

*   <a id="HLR-113"></a>**HLR-113: Graph Algorithms From an Established Library.**
    `elc` shall obtain its graph algorithms — cycle detection, topological ordering, reachability, and centrality — from an established graph library rather than hand-implementing adjacency structures and traversal algorithms, so that the correctness of the analyses in Sections 11 through 13 rests on proven code. Which library provides them is a design decision under HLR-112.

*   <a id="HLR-041"></a>**HLR-041: Single-Threaded Execution.**
    `elc` shall perform the entire run — target discovery, parsing, metric computation, graph construction, and every graph analysis — sequentially on a single thread.

*   <a id="HLR-124"></a>**HLR-124: Memory Safety.**
    `elc` shall complete every run without a memory-safety error: without reading or writing outside the bounds of any allocation or mapping, without accessing memory after it has been freed or unmapped, without an invalid or repeated free, and without acting on an uninitialised value. This shall hold on error paths as well as on the success path, and shall hold for every target type and every output format.

*   <a id="HLR-125"></a>**HLR-125: Complete Resource Release.**
    `elc` shall release, before it exits, every heap allocation it made, every file mapping it created, and every dynamic-library handle it opened. A run that terminates for any reason other than a fatal signal shall leave no allocation unreleased, including runs that end in a usage error, an invalid target, or a rejected saved record.

*   <a id="HLR-043"></a>**HLR-043: Read-Only Operation.**
    `elc` shall only read the files it analyzes; `elc` shall never modify, rewrite, or delete any file under analysis.

## 9. Report Regeneration from Saved XML

Requirements governing `elc`'s ability to regenerate a Markdown report from a previously generated XML output file (HLR-054), without re-analyzing the original source (PVD §5 item 8, §7.1).

*   <a id="HLR-055"></a>**HLR-055: XML-to-Markdown Conversion Mode.**
    `elc` shall support an operating mode whose input is a previously generated XML output file (HLR-054) rather than a source-code target, and whose output is a Markdown report (HLR-029), without parsing or re-analyzing any original source file.

*   <a id="HLR-056"></a>**HLR-056: Regenerated Report Equivalence.**
    For a given complexity threshold, the Markdown report `elc` produces by converting a previously generated XML output file (HLR-055) shall be **byte-identical** to the Markdown report `elc` would have produced by analyzing the original target directly to Markdown with that same threshold — including the project summary, each file's totals and threshold list, full per-function detail, the architectural findings of Sections 11 through 14, and any custom-rule matches recorded in the source XML.

*   <a id="HLR-057"></a>**HLR-057: User-Supplied Threshold at Regeneration Time.**
    The user shall be able to supply a complexity threshold when converting a saved XML file to Markdown (HLR-055), independently of any threshold that may have applied when the XML file was originally generated; this threshold shall default to 15, consistent with HLR-022, when the user does not supply one.

*   <a id="HLR-122"></a>**HLR-122: No Companion Artefacts From a Saved Record.**
    The XML record of HLR-054 carries the findings of a run rather than the topology of the System Dependence Graph, so neither the Graphviz `.dot` call tree (HLR-102) nor the GraphML export (HLR-106) can be reconstructed from it. The conversion mode of HLR-055 shall therefore produce the Markdown report alone, notwithstanding the default-on rule of HLR-103; and a command line that explicitly requests a companion artefact together with conversion mode shall be rejected as a usage error (HLR-063) rather than silently ignored.

*   <a id="HLR-058"></a>**HLR-058: Malformed or Unsupported Saved-XML Rejection.**
    When the input to the conversion mode of HLR-055 is not well-formed XML, does not match `elc`'s own output structure, or carries a format-version identifier (HLR-061) that this build of `elc` does not support, `elc` shall reject it with a diagnostic and a non-zero exit status rather than attempting a best-effort partial conversion.

## 10. System Dependence Graph Construction

Requirements governing how `elc` resolves per-file syntax trees against one another into a single project-wide System Dependence Graph (SDG), which every analysis in Sections 11 through 14 operates over (PVD §5 item 7, §7.1 "Macro-architectural analysis").

*   <a id="HLR-073"></a>**HLR-073: System Dependence Graph Construction.**
    `elc` shall resolve the per-file syntax trees produced by Section 3 against one another — matching each call site to the definition it invokes across file boundaries — and shall assemble the result into a single directed graph, the System Dependence Graph (SDG), whose nodes are the functions of HLR-014 and whose edges are the calls between them.

*   <a id="HLR-074"></a>**HLR-074: Global State Edges.**
    In addition to call edges, the SDG shall record, for each global variable or fixed memory address referenced in the target, an edge from every function that writes it and an edge to every function that reads it, so that coupling through shared state is represented in the graph alongside coupling through calls.

*   <a id="HLR-075"></a>**HLR-075: Whole-Project Graph Scope.**
    The SDG shall span the entire analysis target — whether that target is an application or a library, and across every target argument supplied under HLR-071 — so that every analysis derived from it describes the project as a whole rather than any single file.

*   <a id="HLR-076"></a>**HLR-076: Graph Built From the Single Parse.**
    `elc` shall construct the SDG entirely from the data produced by the single parse of each file required by HLR-013; it shall not re-read or re-parse any source file in order to resolve cross-file references or to perform any graph analysis.

*   <a id="HLR-077"></a>**HLR-077: Unresolvable Call Handling.**
    A call site whose target cannot be resolved within the analysis target — for example a call into an external library, a system call, or an indirect call through a function pointer that cannot be determined statically — shall not abort graph construction. `elc` shall record the call as unresolved, shall exclude it from analyses that require a known destination, and shall report the count of unresolved calls so that the reader can judge the graph's completeness.

*   <a id="HLR-115"></a>**HLR-115: Analyses Requiring User Declarations.**
    An analysis whose inputs include a user declaration — architectural strata (HLR-078), entry points (HLR-095), or execution scopes (HLR-094) — shall be performed only when that declaration is supplied. Where it is not supplied, `elc` shall omit the analysis and shall state in the report that it was omitted and why, rather than reporting an empty or misleading result, and rather than treating the omission as an error or a failure. In particular, when no entry points are declared, `elc` shall not report every function as unreachable.

## 11. Architectural Layering and Coupling Analysis

Requirements governing the layering, coupling, and dependency-cycle analyses `elc` performs over the SDG (PVD §7.1, Appendix A.1, A.3).

*   <a id="HLR-114"></a>**HLR-114: Definition of a Component.**
    For the purposes of the coupling, instability, and dependency-cycle analyses of this section, a *component* is a single source file (translation unit). A dependency exists from component X to component Y when any function in X calls any function in Y, or when any function in X writes a global that a function in Y reads. Analyses expressed per *function* — fan-out (HLR-085), call-chain depth (HLR-087), and recursion (HLR-089) — operate on the individual function nodes of the SDG and are deliberately distinct from the component-level analyses here.

*   <a id="HLR-078"></a>**HLR-078: User-Declared Architectural Strata.**
    `elc` shall accept, as command-line arguments, a set of user-declared architectural strata — named layers such as Application Logic, Hardware Abstraction, and Driver — together with the mapping of components to those layers and the permitted direction of dependency between them. Strata shall never be discovered automatically from the filesystem or from any configuration file.

*   <a id="HLR-079"></a>**HLR-079: Skip-Level Call Detection.**
    Given declared strata (HLR-078), `elc` shall traverse the SDG and report every "skip-level" call — a call that bypasses one or more intervening layers, such as application code invoking driver logic directly rather than through the hardware abstraction layer — identifying the calling function, the called function, and the layers crossed.

*   <a id="HLR-118"></a>**HLR-118: Direction-Inverted Call Detection.**
    Given declared strata and the permitted direction of dependency between them (HLR-078), `elc` shall report every call whose direction is inverted with respect to that declaration — for example a driver-layer function calling upward into application logic — identifying the calling function, the called function, and the layers involved. A direction-inverted call is a finding distinct from the skip-level call of HLR-079: a call may invert the declared direction without bypassing any intervening layer, and may bypass layers without inverting direction.

*   <a id="HLR-080"></a>**HLR-080: Afferent and Efferent Coupling.**
    For every component (HLR-114) in the SDG, `elc` shall compute and report its afferent coupling (`Ca`, fan-in — the number of components that depend upon it) and its efferent coupling (`Ce`, fan-out — the number of components it depends upon).

*   <a id="HLR-081"></a>**HLR-081: Architectural Bottleneck Identification.**
    `elc` shall identify and report as an architectural bottleneck every component (HLR-114) whose afferent coupling and efferent coupling are *each* greater than or equal to a bottleneck threshold, since such a component is simultaneously depended upon widely and dependent widely, and is therefore both dangerous to change and difficult to isolate. The threshold shall default to 5 and shall be user-configurable. Unlike the thresholds of Section 14, this one is `elc`'s own heuristic rather than a published standard, and shall be identified as such wherever it is reported (HLR-099).

*   <a id="HLR-082"></a>**HLR-082: Instability Metric.**
    For every component (HLR-114) in the SDG, `elc` shall compute and report the Instability metric `I = Ce / (Ce + Ca)` from the coupling values of HLR-080, and shall report it alongside the interpretation guidance of PVD Appendix A.1 — that `I` approaching 0 denotes maximum stability (high fan-in, low fan-out, dangerous to change) and `I` approaching 1 denotes maximum instability (high fan-out, low fan-in, freely changeable). Where a component's `Ce` and `Ca` are both zero, `elc` shall report the metric as undefined rather than dividing by zero.

*   <a id="HLR-083"></a>**HLR-083: Circular Dependency Detection.**
    `elc` shall detect every cyclic dependency *between components* (HLR-114) in the SDG by topological analysis, and shall report each cycle together with the ordered sequence of components that form it (for example A → B → C → A). Recursion between individual functions is a distinct, function-level finding governed by HLR-089: mutual recursion between functions residing in the same component is not a component-level cycle and shall not be reported as one.

*   <a id="HLR-084"></a>**HLR-084: Cycles Reported at Critical Severity.**
    Every cyclic dependency detected under HLR-083 shall be reported at critical severity, the acceptable count of cycles being strictly zero, because a cycle fuses its participants into a single strongly connected unit that cannot be unit-tested in isolation or linked incrementally.

## 12. Call Tree Dimensionality

Requirements governing `elc`'s analysis of the geometric shape of the call tree — its width and its height (PVD §7.1, Appendix A.2).

*   <a id="HLR-085"></a>**HLR-085: Function Fan-Out Measurement.**
    For every function in the SDG, `elc` shall compute and report its fan-out: the number of distinct subroutines it invokes directly.

*   <a id="HLR-086"></a>**HLR-086: Fan-Out Threshold Classification.**
    `elc` shall classify each function's fan-out (HLR-085) against the published thresholds of PVD Appendix A.2. The bands shall be exhaustive, so that every possible fan-out value has exactly one classification: a fan-out of 0 to 2 lies below the healthy band and produces no finding; 3 to 7 is the healthy range and produces no finding; 8 to 10 is acceptable and produces no finding; 11 to 15 produces a **warning**, indicating weak abstraction and poor delegation; and greater than 15 produces a **critical** finding — a monolithic dispatcher or "god function" that violates the Single Responsibility Principle and resists isolation for unit testing.

*   <a id="HLR-087"></a>**HLR-087: Maximum Call-Chain Depth.**
    `elc` shall compute and report the maximum call-chain depth of the SDG — the greatest number of nested call layers reachable from any declared entry point (HLR-095) — and shall report it against the embedded guidance of PVD Appendix A.2, under which depths beyond 8 to 12 layers risk stack-versus-heap collision on severely stack-constrained targets. The reported depth is a lower bound on true worst-case depth, since a chain continuing through a call that could not be resolved (HLR-077) is not followed; `elc` shall therefore report the depth together with the unresolved-call count, so that its completeness can be judged. Where no entry points are declared, this analysis shall be omitted under HLR-115.

*   <a id="HLR-088"></a>**HLR-088: Deepest Call Stack Reported in Full.**
    `elc` shall report the deepest call stack itself — the ordered sequence of functions from entry point to deepest leaf — and not merely its depth as a number, so that the specific path responsible for worst-case stack consumption can be inspected and shortened rather than only measured.

*   <a id="HLR-089"></a>**HLR-089: Recursion Detection.**
    `elc` shall statically detect the presence of recursion — direct or mutual — in the SDG and shall report each recursive cycle, in accordance with MISRA C Rule 17.2, so that the absence of recursion can be established and the call tree confirmed to be a Directed Acyclic Graph whose maximum stack usage is predictable.

*   <a id="HLR-090"></a>**HLR-090: Depth Reporting Under Recursion.**
    Where recursion is detected (HLR-089), maximum call-chain depth is unbounded and no deepest call stack exists. `elc` shall report the recursive cycle in place of a depth figure, rather than reporting a misleading finite number or failing to terminate.

## 13. Global State and Reachability Analysis

Requirements governing `elc`'s analysis of shared-state coupling and of reachability-proven dead code (PVD §7.1, Appendix A.4).

*   <a id="HLR-091"></a>**HLR-091: Global Access Mapping.**
    For every global variable or fixed memory address represented in the SDG (HLR-074), `elc` shall report the set of functions that write it and the set of functions that read it.

*   <a id="HLR-092"></a>**HLR-092: Scope-Reduction Candidates.**
    Where the read and write edges for a global originate from only a single function, `elc` shall flag that object for scope reduction, in accordance with MISRA C Rule 8.9 — an object should be defined at block scope if its identifier appears in only one function.

*   <a id="HLR-093"></a>**HLR-093: Hidden Channel Detection.**
    Where the read and write edges for a global originate from multiple otherwise-disconnected domains of the SDG, `elc` shall flag that object as a hidden channel, identifying the disconnected participants, since such an object constitutes temporal coupling in which function execution order silently governs system stability.

*   <a id="HLR-094"></a>**HLR-094: Memory Map Boundary Validation.**
    `elc` shall accept, as command-line arguments, a declaration of execution scopes for targets whose components share overlapping memory maps and symbol tables — such as host-driven sequential test harnesses — naming the components belonging to each scope. Given such a declaration, `elc` shall traverse the SDG and report every call edge and every global-state edge by which one declared scope reaches a function or object belonging to another, so that scope isolation can be verified. Where no execution scopes are declared, this analysis shall be omitted under HLR-115.

*   <a id="HLR-095"></a>**HLR-095: User-Declared Entry Points.**
    `elc` shall accept, as command-line arguments, the set of entry points from which reachability is measured — for example `main()`, interrupt vector handlers, and exported API boundaries. Entry points shall never be inferred implicitly or read from a configuration file.

*   <a id="HLR-096"></a>**HLR-096: Dead Code Detection by Reachability.**
    `elc` shall traverse the SDG from a root set comprising the declared entry points of HLR-095 *together with* every function whose address is taken without being directly called, and shall report every function and data structure not visited during that traversal as unreachable. Address-taken functions are roots because they may be invoked indirectly — through an interrupt vector table, a callback array, or a stored function pointer — and omitting them would report live code as provably dead. The asymmetry is deliberate: an additional root can only shrink the unreachable set, whereas a missing root produces a false claim of death.

*   <a id="HLR-097"></a>**HLR-097: Dead Code Determined by Graph Mathematics.**
    The unreachability of HLR-096 shall be established solely by graph reachability, never by textual or heuristic means; in particular, a group of unused functions that call one another shall be correctly reported as unreachable, since no path reaches the group from any declared entry point.

## 14. Threshold Evaluation and Severity Reporting

Requirements governing how `elc` evaluates its measurements against published industry and academic thresholds, and the limits of the conclusions it draws (PVD §7.1, §7.3, Appendix A).

*   <a id="HLR-098"></a>**HLR-098: Evaluation Against Published Thresholds.**
    `elc` shall evaluate each architectural measurement it computes against the published academic and safety-critical industry thresholds recorded in PVD Appendix A, and shall report where the measurement falls relative to the accepted range, so that a value is presented with the context needed to act on it rather than as a bare figure.

*   <a id="HLR-099"></a>**HLR-099: Threshold Attribution.**
    Every threshold `elc` reports against shall be attributed to its external source — for example MISRA C and its rule number, Robert C. Martin's Instability metric, or the Henry-Kafura information-flow metrics — so that the reader can distinguish a published standard from a choice made by `elc`.

*   <a id="HLR-123"></a>**HLR-123: Severity Vocabulary.**
    Every finding `elc` reports shall carry exactly one severity, drawn from the closed and ordered set `info` < `warning` < `critical`. No other severity value shall be emitted, and no finding shall be emitted without one. Where more than one threshold band applies to a single measurement, the highest applicable severity shall be the one reported.

*   <a id="HLR-100"></a>**HLR-100: Severity Labels Do Not Affect Exit Status.**
    A finding reported at any severity, including critical, shall be a label within the report and shall have no effect on `elc`'s process exit status, which remains reserved for the failure conditions of Section 7. Deciding what action a finding warrants is the caller's responsibility.

*   <a id="HLR-101"></a>**HLR-101: No Remediation Advice.**
    `elc` shall report measurements, threshold positions, and violations of user-supplied criteria; it shall not propose a fix, rank one design as better than another beyond what a cited standard already states, or apply any style opinion of its own invention.

## 15. Graph Output Formats

Requirements governing `elc`'s graph-specific outputs: the Graphviz call tree for human inspection, and the standard graph serialisation for machine ingestion (PVD §5 item 9, §7.1 "Output").

*   <a id="HLR-102"></a>**HLR-102: Graphviz .dot Call Tree Output.**
    `elc` shall emit the call tree in Graphviz `.dot` format, so that it can be rendered and inspected visually. Producing the `.dot` file shall require no library dependency and no invocation of Graphviz; Graphviz is a tool the user may separately run upon the output.

*   <a id="HLR-103"></a>**HLR-103: .dot Generation Enabled by Default.**
    Generation of the `.dot` call tree (HLR-102) shall be enabled by default, and shall be disableable by a command-line option for runs that do not want the additional artefact.

*   <a id="HLR-104"></a>**HLR-104: No .dot Output to Standard Output.**
    When `elc` writes its report to standard output rather than to a named output file, no `.dot` file shall be produced, whether or not generation was disabled under HLR-103, since no output path exists from which to derive the `.dot` file's name and graph markup must never enter the result stream (HLR-038).

*   <a id="HLR-119"></a>**HLR-119: Companion Artefact Naming.**
    The `.dot` call tree (HLR-102) and the GraphML export (HLR-106) shall each derive its filename from the report's output path by substituting the corresponding extension — an output of `report.md` yielding `report.dot` and `report.graphml`. Neither shall accept an output path of its own. This derivation is precisely why neither can be produced when the report is written to standard output (HLR-104): there is no output path from which to derive a name.

*   <a id="HLR-105"></a>**HLR-105: Annotated .dot Output.**
    When a `.dot` call tree is emitted, `elc` shall annotate it with every architectural finding that applies to a node it contains: components and functions exceeding the coupling and fan-out thresholds (HLR-081, HLR-086), the functions forming the deepest call chain (HLR-088), the members of each dependency cycle (HLR-083) and each recursive cycle (HLR-089), unreachable functions (HLR-096), and functions participating in a hidden channel (HLR-093). Annotations shall use Graphviz attributes that degrade gracefully, so that a renderer ignoring them still produces a valid and readable call tree.

*   <a id="HLR-106"></a>**HLR-106: Standard Graph Serialisation Export.**
    `elc` shall support exporting the SDG in a standard graph serialisation schema (GraphML), so that the graph can be ingested and processed by other tools rather than only rendered for viewing. Export shall be requested by an explicit command-line option and shall be disabled by default, in contrast to the `.dot` call tree of HLR-103. As with `.dot` (HLR-104), no GraphML file shall be produced when the report is written to standard output, since no output path exists from which to derive its name.

## 16. User-Supplied Custom Rules

Requirements governing the mechanism by which a user expresses and checks a custom coding standard (PVD §7.1 "Custom rules").

*   <a id="HLR-107"></a>**HLR-107: User-Supplied Rule Queries.**
    `elc` shall accept user-supplied Tree-sitter `.scm` query files expressing a custom coding standard, and shall check the analysed source against them using the same query mechanism that produces `elc`'s built-in metrics. Because a Tree-sitter query compiles against one specific grammar, every custom rule shall be bound to the language it applies to: a rule placed in the runtime location is bound by the language directory that contains it, and a rule named on the command line shall name its language alongside its path. A rule naming a language for which no module is available shall be reported and skipped, not compiled.

*   <a id="HLR-108"></a>**HLR-108: Custom Rules Require No Rebuild.**
    Adding, altering, or removing a custom rule shall require only a change to a `.scm` file placed in the runtime location or named on the command line; it shall require no modification to, and no recompilation of, the `elc` executable.

*   <a id="HLR-109"></a>**HLR-109: Custom Rule Match Reporting.**
    `elc` shall report each match of a user-supplied rule alongside its built-in findings, identifying the rule that matched and the file and line range of each occurrence. A rule's identity shall be the basename of the `.scm` file that contains it together with the capture name that matched, so that a single rule file may express several independently identified rules.

*   <a id="HLR-110"></a>**HLR-110: No Automatic Rule Discovery.**
    `elc` shall use only those custom rule files explicitly named on the command line, with the language binding required by HLR-107, or present in the appropriate language directory of its runtime location; it shall never discover a rule file automatically from the working directory, the analysis target, or any dotfile, so that two users running the same command on the same tree obtain the same result.

*   <a id="HLR-116"></a>**HLR-116: Invalid Custom Rule File Handling.**
    A custom rule file that cannot be read, or whose query text is not valid, shall be handled according to how it was supplied. A rule file named explicitly on the command line is a user error: `elc` shall emit a diagnostic and terminate with a non-zero exit status without analyzing any file, consistent with HLR-063. A rule file found in the runtime location is a malformed runtime component: `elc` shall emit a diagnostic identifying it, exclude that rule from the run, and continue, consistent with HLR-070.

*   <a id="HLR-111"></a>**HLR-111: Custom Rules Carry No Built-In Opinion.**
    `elc` shall report what a user-supplied rule matched without forming any judgement as to whether the rule itself is appropriate, and without supplying rules of its own beyond the metrics and thresholds specified elsewhere in this document.
