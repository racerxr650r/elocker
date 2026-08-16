# Low-Level Requirements

**Version:** 2.4
**Date:** 2026-08-21
**Author(s):** John Anderson

## 1. `main` ([src/main.c](../src/main.c))

Orchestration and exit status. `main` performs no analysis; every requirement below concerns sequencing, mode selection, or the status it returns.

*   <a id="LLR-MAIN-01"></a>**LLR-MAIN-01** — `main` shall invoke `cli_parse` before any other stage, and shall perform no file system access when `cli_parse` reports a usage error.
    *Trace:* HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-MAIN-02"></a>**LLR-MAIN-02** — `main` shall return 0 when `cli_parse` reports a help request, after the usage summary has been written to standard output.
    *Trace:* HLR-117 (Help Request Is Not an Error).

*   <a id="LLR-MAIN-03"></a>**LLR-MAIN-03** — When the parsed options select regeneration mode, `main` shall invoke `xml_read_report` and proceed directly to rendering, invoking neither `discover_targets` nor `analyze_file`.
    *Trace:* HLR-055 (XML-to-Markdown Conversion Mode).

*   <a id="LLR-MAIN-04"></a>**LLR-MAIN-04** — In regeneration mode `main` shall invoke neither `graph_write_dot` nor `graph_write_graphml`, since a saved record does not carry the graph.
    *Trace:* HLR-122 (No Companion Artefacts From a Saved Record).

*   <a id="LLR-MAIN-05"></a>**LLR-MAIN-05** — `main` shall invoke `registry_open` before `discover_targets`, and shall return 2 without discovering any file when `registry_open` fails.
    *Trace:* HLR-036 (Setup-Failure Fatality).

*   <a id="LLR-MAIN-06"></a>**LLR-MAIN-06** — `main` shall invoke the analysis stages in the order: discovery, per-file analysis, graph construction, architectural analyses, threshold evaluation, report assembly, rendering — analysis preceding graph construction so that the graph is built from the single parse's output.
    *Trace:* HLR-076 (Graph Built From the Single Parse).

*   <a id="LLR-MAIN-07"></a>**LLR-MAIN-07** — `main` shall record, but not propagate, a non-zero return from `analyze_file`, continuing with the remaining discovered files.
    *Trace:* HLR-035 (Per-File Read- and Parse-Failure Tolerance).

*   <a id="LLR-MAIN-08"></a>**LLR-MAIN-08** — `main` shall return 0 when every discovered file was processed without error or skipped for want of a language module.
    *Trace:* HLR-037 (Truthful Exit Status), HLR-012 (Unsupported-Language File Handling).

*   <a id="LLR-MAIN-09"></a>**LLR-MAIN-09** — `main` shall return 1 when the run completed and produced a report but at least one discovered file failed to be read or parsed.
    *Trace:* HLR-037 (Truthful Exit Status), HLR-120 (Distinct Exit Status Classes).

*   <a id="LLR-MAIN-10"></a>**LLR-MAIN-10** — `main` shall return 2 when the run did not complete and no report was produced, for a usage error, an invalid target, a fatal runtime-location failure, or a rejected saved record.
    *Trace:* HLR-120 (Distinct Exit Status Classes), HLR-062 (Invalid Target Rejection), HLR-036 (Setup-Failure Fatality), HLR-058 (Malformed or Unsupported Saved-XML Rejection).

*   <a id="LLR-MAIN-11"></a>**LLR-MAIN-11** — `main` shall derive the exit status solely from the recorded failure state, and shall not consult the severity of any finding.
    *Trace:* HLR-100 (Severity Labels Do Not Affect Exit Status), HLR-023 (Threshold List is Reporting-Only).

*   <a id="LLR-MAIN-12"></a>**LLR-MAIN-12** — `main` shall write the rendered report to the stream or file selected by the options, and shall write no diagnostic to that destination.
    *Trace:* HLR-038 (Diagnostics on stderr, Results on stdout), HLR-030 (Optional Output-File Redirection).

*   <a id="LLR-MAIN-13"></a>**LLR-MAIN-13** — `main` shall release every acquired resource in reverse order of acquisition on every exit path, including error paths.
    *Trace:* HLR-036 (Setup-Failure Fatality).

*   <a id="LLR-MAIN-14"></a>**LLR-MAIN-14** — `main` shall execute the entire pipeline on the thread on which it was entered, creating no additional thread.
    *Trace:* HLR-041 (Single-Threaded Execution).

*   <a id="LLR-MAIN-15"></a>**LLR-MAIN-15** — `main` shall invoke `graph_write_dot` when, and only when, `graph_dot_warranted` returns true.
    *Trace:* HLR-103 (.dot Generation Enabled by Default), HLR-104 (No .dot Output to Standard Output).

*   <a id="LLR-MAIN-17"></a>**LLR-MAIN-17** — `main` shall open the output destination named by the options, or use standard output when none was named, and shall emit a diagnostic and return 2 when that destination cannot be opened or cannot be written, rather than reporting a truncated report as success.
    *Trace:* HLR-030 (Optional Output-File Redirection), HLR-038 (Diagnostics on stderr, Results on stdout), HLR-120 (Distinct Exit Status Classes).

*   <a id="LLR-MAIN-18"></a>**LLR-MAIN-18** — `main` shall record a file skipped for want of a usable language module in the report's skipped-file list and emit a diagnostic naming it, and shall not count the skip as a per-file failure.
    *Trace:* HLR-012, HLR-037.

*   <a id="LLR-MAIN-16"></a>**LLR-MAIN-16** — `main` shall release every resource it acquired before returning, on the success path and on every error path alike, so that a completed run leaves no allocation, mapping, or handle outstanding.
    *Trace:* HLR-125 (Complete Resource Release), HLR-036 (Setup-Failure Fatality).

*   <a id="LLR-MAIN-19"></a>**LLR-MAIN-19** — `main` shall release the options structure on the usage-error path as well as on every other. A declaration parsed before the offending argument has already allocated — a stratum accepted before a malformed order leaves a layer owning its name and patterns — so a run ending in a usage error would otherwise leak where a successful one does not.
    *Trace:* HLR-125 (Complete Resource Release), HLR-063.

## 2. `cli_parse` ([src/cli.c](../src/cli.c))

Command-line parsing and validation. `cli_parse` is the sole reader of `argv` and the sole source of user-supplied configuration.

*   <a id="LLR-CLI-01"></a>**LLR-CLI-01** — `cli_parse` shall accept one or more target arguments in any combination of file and directory names, and shall reject an invocation supplying none.
    *Trace:* HLR-071 (Multiple Target Arguments), HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-CLI-02"></a>**LLR-CLI-02** — `cli_parse` shall accept a report format selection of `table`, `csv`, `xml`, or `md`, defaulting to `table` when none is supplied.
    *Trace:* HLR-027 (Default Human-Readable Output), HLR-028 (CSV Output), HLR-054 (XML Output), HLR-029 (Markdown Output).

*   <a id="LLR-CLI-18"></a>**LLR-CLI-18** — `cli_parse` shall accept `--entry` repeatedly, accumulating one entry-point symbol per occurrence, and shall never infer an entry point from any other source. The symbols are borrowed from `argv`; only the array holding them is owned.
    *Trace:* HLR-095 (User-Declared Entry Points), HLR-039.

*   <a id="LLR-CLI-03"></a>**LLR-CLI-03** — `cli_parse` shall accept an output file path, and shall record standard output as the destination when none is supplied.
    *Trace:* HLR-030 (Optional Output-File Redirection).

*   <a id="LLR-CLI-04"></a>**LLR-CLI-04** — `cli_parse` shall accept a cyclomatic-complexity threshold and shall default it to 15 when none is supplied.
    *Trace:* HLR-022 (Configurable Complexity Threshold).

*   <a id="LLR-CLI-05"></a>**LLR-CLI-05** — `cli_parse` shall accept a bottleneck coupling threshold and shall default it to 5 when none is supplied.
    *Trace:* HLR-081 (Architectural Bottleneck Identification).

*   <a id="LLR-CLI-06"></a>**LLR-CLI-06** — `cli_parse` shall enable `.dot` call-tree generation by default and shall disable it when the corresponding option is supplied.
    *Trace:* HLR-103 (.dot Generation Enabled by Default).

*   <a id="LLR-CLI-07"></a>**LLR-CLI-07** — `cli_parse` shall accept a GraphML export request as a flag taking no path argument.
    *Trace:* HLR-106 (Standard Graph Serialisation Export), HLR-119 (Companion Artefact Naming).

*   <a id="LLR-CLI-08"></a>**LLR-CLI-08** — `cli_parse` shall accept zero or more entry-point symbol declarations, and shall leave the entry-point set empty when none is supplied.
    *Trace:* HLR-095 (User-Declared Entry Points).

*   <a id="LLR-CLI-09"></a>**LLR-CLI-09** — `cli_parse` shall accept custom rule declarations that name both a language and a path, and shall reject a declaration naming only a path.
    *Trace:* HLR-107 (User-Supplied Rule Queries), HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-CLI-10"></a>**LLR-CLI-10** — `cli_parse` shall accept a regeneration-mode input path. In regeneration mode the report format shall default to Markdown rather than to the table format, and only a format *explicitly* selected and other than Markdown shall be rejected.
    *Trace:* HLR-055 (XML-to-Markdown Conversion Mode), HLR-122 (No Companion Artefacts From a Saved Record).

*   <a id="LLR-CLI-11"></a>**LLR-CLI-11** — `cli_parse` shall accept a complexity threshold in regeneration mode independently of any threshold recorded in the input file, defaulting to 15.
    *Trace:* HLR-057 (User-Supplied Threshold at Regeneration Time).

*   <a id="LLR-CLI-12"></a>**LLR-CLI-12** — `cli_parse` shall report a usage error for an unrecognised option, for a missing or malformed argument to an option requiring one, and for a missing target.
    *Trace:* HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-CLI-13"></a>**LLR-CLI-13** — `cli_parse` shall report a help request when the help option is supplied, without validating any other argument.
    *Trace:* HLR-117 (Help Request Is Not an Error).

*   <a id="LLR-CLI-14"></a>**LLR-CLI-14** — `cli_parse` shall derive the options structure solely from the arguments passed to it, opening no file in the working directory, the analysis target, or any ancestor of either.
    *Trace:* HLR-039 (Zero Configuration).

*   <a id="LLR-CLI-16"></a>**LLR-CLI-16** — `cli_parse` shall reject a numeric option argument that is not a plain decimal number consumed in its entirety, rather than accepting the prefix that a permissive conversion would read from it.
    *Trace:* HLR-063, HLR-022.

*   <a id="LLR-CLI-17"></a>**LLR-CLI-17** — `cli_parse` shall reject a target given alongside a regeneration-mode input path, since the record is the input and a target would name a second source for one report.
    *Trace:* HLR-055, HLR-063.

*   <a id="LLR-CLI-19"></a>**LLR-CLI-19** — `cli_parse` shall accept zero or more conditional-compilation definitions, each naming a symbol and optionally a value, and shall leave the definition set empty when none is supplied.
    *Trace:* HLR-131.

*   <a id="LLR-CLI-20"></a>**LLR-CLI-20** — `cli_parse` shall report a usage error for a definition naming no symbol, rather than recording an empty name that can never match.
    *Trace:* HLR-131, HLR-063.

*   <a id="LLR-CLI-21"></a>**LLR-CLI-21** — `cli_parse` shall reject a command line combining a conditional-compilation definition with the regeneration-mode input path, since a saved record holds measurements already taken and no definition supplied afterwards can change them.
    *Trace:* HLR-136, HLR-063.

*   <a id="LLR-CLI-15"></a>**LLR-CLI-15** — `cli_parse` shall reject as a usage error a command line that combines regeneration mode with an explicit request for a companion artefact, since a saved record does not carry the graph from which either could be produced.
    *Trace:* HLR-122 (No Companion Artefacts From a Saved Record), HLR-063 (Invalid Command-Line Rejection).

## 3. `cli_usage` ([src/cli.c](../src/cli.c))

*   <a id="LLR-USG-01"></a>**LLR-USG-01** — `cli_usage` shall write a summary naming every accepted option, its argument if any, and its default value.
    *Trace:* HLR-117 (Help Request Is Not an Error).

*   <a id="LLR-USG-02"></a>**LLR-USG-02** — `cli_usage` shall write to the stream it is given, so that a help request may be directed to standard output and a usage error to standard error.
    *Trace:* HLR-117 (Help Request Is Not an Error), HLR-063 (Invalid Command-Line Rejection), HLR-038 (Diagnostics on stderr, Results on stdout).

## 4. `parse_stratum` ([src/cli.c](../src/cli.c))

*   <a id="LLR-STR-01"></a>**LLR-STR-01** — `parse_stratum` shall parse a declaration naming an architectural layer and the component patterns assigned to it.
    *Trace:* HLR-078 (User-Declared Architectural Strata).

*   <a id="LLR-STR-02"></a>**LLR-STR-02** — `parse_stratum` shall record the declared ordinal of each layer, so that the permitted direction of dependency between layers is determined.
    *Trace:* HLR-078 (User-Declared Architectural Strata), HLR-118 (Direction-Inverted Call Detection).

*   <a id="LLR-STR-03"></a>**LLR-STR-03** — `parse_stratum` shall report a usage error for a declaration that cannot be parsed, rather than silently ignoring it.
    *Trace:* HLR-063 (Invalid Command-Line Rejection), HLR-078 (User-Declared Architectural Strata).

*   <a id="LLR-STR-04"></a>**LLR-STR-04** — `parse_stratum` shall add the patterns of a repeated layer name to the layer already declared rather than creating a second one, and shall fix a layer's ordinal when it is first named. A second layer of the same name would shift the ordinal of every layer below it, so a user splitting one layer's patterns across two arguments would silently change what the layering is measured against.
    *Trace:* HLR-078 (User-Declared Architectural Strata), HLR-032.

*   <a id="LLR-STR-05"></a>**LLR-STR-05** — `cli_parse` shall accept a declaration of the permitted dependency direction naming the declared layers in order, shall resolve it after every option has been read so that it may be given before or after the layers it orders, and shall report a usage error where it names a layer that was not declared or omits one that was. A partial order determines no direction, and a name for a layer that does not exist is a typo whose silent acceptance would leave the layering validated against something other than what was written.
    *Trace:* HLR-078 (User-Declared Architectural Strata), HLR-063.

*   <a id="LLR-STR-06"></a>**LLR-STR-06** — `parse_stratum` shall copy the name and every pattern it records rather than borrowing them from the argument vector, since a declaration is split on two separators and neither half exists as a terminated substring of any argument.
    *Trace:* HLR-078 (User-Declared Architectural Strata), HLR-125.

## 5. `parse_scope` ([src/cli.c](../src/cli.c))

*   <a id="LLR-SCP-01"></a>**LLR-SCP-01** — `parse_scope` shall parse a declaration naming an execution scope and the components belonging to it.
    *Trace:* HLR-094 (Memory Map Boundary Validation).

*   <a id="LLR-SCP-02"></a>**LLR-SCP-02** — `parse_scope` shall report a usage error for a declaration that cannot be parsed.
    *Trace:* HLR-063 (Invalid Command-Line Rejection), HLR-094 (Memory Map Boundary Validation).

*   <a id="LLR-SCP-03"></a>**LLR-SCP-03** — `parse_scope` shall copy the name and every pattern it records, rather than borrowing them from the argument vector as the entry-point symbols are. A declaration is split on two separators, so neither the name nor any pattern exists as a terminated substring of any argument.
    *Trace:* HLR-094 (Memory Map Boundary Validation), HLR-125.

## 6. `discover_targets` ([src/discover.c](../src/discover.c))

Target validation, classification, and file discovery. Produces the de-duplicated, stably ordered list every later stage consumes.

*   <a id="LLR-DSC-01"></a>**LLR-DSC-01** — `discover_targets` shall validate every target argument with `stat(2)` before walking any of them.
    *Trace:* HLR-062 (Invalid Target Rejection).

*   <a id="LLR-DSC-02"></a>**LLR-DSC-02** — `discover_targets` shall emit a diagnostic naming the offending target and return non-zero, without producing any file list, when a target does not exist, cannot be opened or read, or is neither a regular file nor a directory.
    *Trace:* HLR-062 (Invalid Target Rejection).

*   <a id="LLR-DSC-03"></a>**LLR-DSC-03** — `discover_targets` shall classify each validated target independently of every other, so that files and directories may be intermixed on one command line, and a directory target shall denote the same set of files whichever discovery route it takes.
    *Trace:* HLR-071 (Multiple Target Arguments), HLR-001 (Single-File Target Handling), HLR-126 (Repository Enumeration Scoped to the Target).

*   <a id="LLR-DSC-04"></a>**LLR-DSC-04** — `discover_targets` shall append a target that is a regular file directly to the file list, performing no directory traversal for it.
    *Trace:* HLR-001 (Single-File Target Handling).

*   <a id="LLR-DSC-05"></a>**LLR-DSC-05** — `discover_targets` shall offer each directory target to `git_repository_open_ext`, which searches the target directory and then its ancestors, and shall route the target to `walk_git_tree` only when a repository is found *and* that repository tracks the target directory. In every other case — no repository, or a repository that does not track the target — it shall route the target to `walk_filesystem`.
    *Trace:* HLR-002 (Git-Repository Target Detection), HLR-004 (Plain-Directory Fallback Traversal).

*   <a id="LLR-DSC-06"></a>**LLR-DSC-06** — `discover_targets` shall resolve a target that is a symbolic link, since `stat(2)` follows links and a link named explicitly identifies its referent.
    *Trace:* HLR-069 (Symbolic Link Handling During Traversal).

*   <a id="LLR-DSC-07"></a>**LLR-DSC-07** — `discover_targets` shall canonicalise every accumulated path and shall retain exactly one entry per canonical path, so that a file reached through more than one target is analysed once.
    *Trace:* HLR-072 (Duplicate File Elimination Across Targets).

*   <a id="LLR-DSC-08"></a>**LLR-DSC-08** — `discover_targets` shall sort the completed file list into a defined order that does not depend on the enumeration order of the filesystem or of the repository.
    *Trace:* HLR-033 (Traversal-Order Independence).

*   <a id="LLR-DSC-10"></a>**LLR-DSC-10** — `discover_targets` shall record, for each directory target, whether it was enumerated from a repository or traversed from the filesystem, and shall pass that record forward for reporting. Each record shall name the target's canonical absolute path, as every other path in the report does, and shall hold one record per directory: two spellings of one directory reach the same route by definition, so a second record would report a run over two targets that was a run over one. Each record shall own a copy of the target it names rather than aliasing the caller's string. Aliasing would happen to work on the analysis path, where targets are `argv` entries that outlive the run, and would fail on the regeneration path, where they are parsed out of a record and released — the kind of difference that shows up as a corrupted Discovery section in one mode only.
    *Trace:* HLR-127 (Discovery Route Reported).

*   <a id="LLR-DSC-09"></a>**LLR-DSC-09** — `discover_targets` shall emit a diagnostic and record a per-file failure for a subdirectory that cannot be read, and shall continue the traversal.
    *Trace:* HLR-035 (Per-File Read- and Parse-Failure Tolerance).

## 7. `walk_git_tree` ([src/discover.c](../src/discover.c))

*   <a id="LLR-GIT-01"></a>**LLR-GIT-01** — `walk_git_tree` shall resolve the tree at `HEAD` and enumerate only those blobs whose repository-relative path lies at or beneath the target directory, so that a subdirectory target does not draw in the rest of the repository.
    *Trace:* HLR-002 (Git-Repository Target Detection), HLR-126 (Repository Enumeration Scoped to the Target).

*   <a id="LLR-GIT-04"></a>**LLR-GIT-04** — `walk_git_tree` shall report the repository inapplicable when it tracks nothing at or beneath the target directory, so that the caller falls back to filesystem traversal rather than returning an empty file list. Applicability shall be determined from the tree walk itself rather than by a separate query made beforehand: two traversals can disagree, and a disagreement between them would present as an empty report rather than as an error. It shall be determined from what the repository *tracks* and not from what survived the exclusion filters, since a tracked directory holding only excluded files is still tracked — judging it otherwise would fall back to the filesystem and analyse the untracked files this route exists to exclude. An applicable repository may therefore yield no files at all.
    *Trace:* HLR-002 (Git-Repository Target Detection), HLR-004 (Plain-Directory Fallback Traversal).

*   <a id="LLR-GIT-02"></a>**LLR-GIT-02** — `walk_git_tree` shall include only files tracked at `HEAD`, so that ignored and untracked paths are excluded without a separate exclusion list.
    *Trace:* HLR-003 (Git-Aware Exclusion).

*   <a id="LLR-GIT-03"></a>**LLR-GIT-03** — `walk_git_tree` shall exclude every blob reported as binary.
    *Trace:* HLR-003 (Git-Aware Exclusion).

## 8. `walk_filesystem` ([src/discover.c](../src/discover.c))

*   <a id="LLR-FTS-01"></a>**LLR-FTS-01** — `walk_filesystem` shall traverse the directory tree recursively, appending each regular file that survives filtering.
    *Trace:* HLR-004 (Plain-Directory Fallback Traversal).

*   <a id="LLR-FTS-02"></a>**LLR-FTS-02** — `walk_filesystem` shall exclude hidden entries — files as well as directories — from the traversal, a hidden entry being one whose name begins with a period.
    *Trace:* HLR-005 (Filesystem-Fallback Exclusion).

*   <a id="LLR-FTS-03"></a>**LLR-FTS-03** — `walk_filesystem` shall exclude files whose extension appears in the binary-extension list.
    *Trace:* HLR-005 (Filesystem-Fallback Exclusion).

*   <a id="LLR-FTS-04"></a>**LLR-FTS-04** — `walk_filesystem` shall traverse physically rather than logically, so that a directory reached through a symbolic link is not descended into and a cyclic link cannot cause unbounded traversal.
    *Trace:* HLR-069 (Symbolic Link Handling During Traversal).

*   <a id="LLR-FTS-05"></a>**LLR-FTS-05** — `walk_filesystem` shall not follow a symbolic link encountered during the traversal, whether it names a directory or a regular file, so that a link to a file already within the tree cannot contribute it a second time and a link out of the tree cannot widen what the target denotes.
    *Trace:* HLR-069 (Symbolic Link Handling During Traversal), HLR-072 (Duplicate File Elimination Across Targets).

*   <a id="LLR-FTS-06"></a>**LLR-FTS-06** — `walk_filesystem` shall apply its hidden-entry exclusion below the target only, and shall not apply it to the target itself, so that a hidden directory named on the command line is traversed.
    *Trace:* HLR-005 (Filesystem-Fallback Exclusion).

## 9. `is_excluded_extension` ([src/discover.c](../src/discover.c))

*   <a id="LLR-EXT-01"></a>**LLR-EXT-01** — `is_excluded_extension` shall test a path against the binary-extension list loaded from the runtime location, and shall not consult any list compiled into the executable.
    *Trace:* HLR-005 (Filesystem-Fallback Exclusion), HLR-060 (Extension Mapping Defined by Runtime Data).

*   <a id="LLR-EXT-02"></a>**LLR-EXT-02** — `binary_exts_load` shall read the exclusion list from the runtime location, ignoring blank lines and comment lines and accepting an entry written with or without its leading period, and shall match an extension without regard to case.
    *Trace:* HLR-005 (Filesystem-Fallback Exclusion), HLR-060 (Extension Mapping Defined by Runtime Data).

*   <a id="LLR-EXT-03"></a>**LLR-EXT-03** — `binary_exts_load` shall emit a diagnostic and yield an empty list, rather than aborting the run, when the binary-extension file is absent or unreadable, so that discovery still completes and the reason nothing was excluded is visible.
    *Trace:* HLR-005 (Filesystem-Fallback Exclusion), HLR-038 (Diagnostics on stderr, Results on stdout).

## 10. `registry_open` ([src/registry.c](../src/registry.c))

Runtime location resolution and registry initialisation. The boundary that keeps language knowledge out of the binary.

*   <a id="LLR-ROP-01"></a>**LLR-ROP-01** — `registry_open` shall resolve the runtime location from the dedicated environment variable when that variable is set, and from a path relative to the executable otherwise.
    *Trace:* HLR-059 (Runtime Location Discovery and Precedence).

*   <a id="LLR-ROP-02"></a>**LLR-ROP-02** — `registry_open` shall prefer the environment variable when both it and a runtime directory adjacent to the executable are present.
    *Trace:* HLR-059 (Runtime Location Discovery and Precedence).

*   <a id="LLR-ROP-03"></a>**LLR-ROP-03** — `registry_open` shall load the extension-to-language mapping from data within the runtime location, and shall consult no mapping compiled into the executable.
    *Trace:* HLR-060 (Extension Mapping Defined by Runtime Data).

*   <a id="LLR-ROP-04"></a>**LLR-ROP-04** — `registry_open` shall emit a diagnostic and return non-zero when the runtime location is absent, unreadable, or yields no valid language module whatsoever.
    *Trace:* HLR-036 (Setup-Failure Fatality).

*   <a id="LLR-ROP-05"></a>**LLR-ROP-05** — `registry_open` shall not require, verify, or assume the presence of any particular language's support files, and shall succeed over whatever valid modules are present.
    *Trace:* HLR-011 (Initial Delivered Language Set).

*   <a id="LLR-ROP-07"></a>**LLR-ROP-07** — `registry_open` shall make the resolved runtime location available to every other module that needs runtime data, so that the precedence rule of HLR-059 is implemented once rather than repeated wherever runtime data is read.
    *Trace:* HLR-059, HLR-005.

*   <a id="LLR-ROP-06"></a>**LLR-ROP-06** — `registry_open` shall read no configuration file or dotfile outside the runtime location.
    *Trace:* HLR-039 (Zero Configuration).

## 11. `registry_for_path` ([src/registry.c](../src/registry.c))

*   <a id="LLR-RFP-01"></a>**LLR-RFP-01** — `registry_for_path` shall determine a file's language from its extension alone, without any user declaration of that file's language.
    *Trace:* HLR-007 (Per-File Automatic Language Detection).

*   <a id="LLR-RFP-02"></a>**LLR-RFP-02** — `registry_for_path` shall return the cached module when the language has already been loaded, so that a language is loaded at most once per run and a mixed-language target is analysed in a single pass.
    *Trace:* HLR-008 (Mixed-Language Single-Pass Analysis).

*   <a id="LLR-RFP-03"></a>**LLR-RFP-03** — `registry_for_path` shall load a language module from the runtime location on first use of its extension, resolving the grammar entry point and compiling the query files.
    *Trace:* HLR-009 (Runtime-Loaded Language Support).

*   <a id="LLR-RFP-04"></a>**LLR-RFP-04** — `registry_for_path` shall load every language-specific artefact from the runtime location, requiring no modification or recompilation of the executable to add a language.
    *Trace:* HLR-010 (No-Recompilation Language Addition).

*   <a id="LLR-RFP-05"></a>**LLR-RFP-05** — `registry_for_path` shall return no module when the extension maps to no available language, so that the caller skips the file rather than failing.
    *Trace:* HLR-012 (Unsupported-Language File Handling).

*   <a id="LLR-RFP-06"></a>**LLR-RFP-06** — `registry_for_path` shall emit a diagnostic identifying the language, mark it unusable so that it is not retried, and return no module when a module is present but cannot be loaded, does not expose the expected entry point, or carries invalid query files.
    *Trace:* HLR-070 (Malformed Language Module Tolerance).

*   <a id="LLR-RFP-07"></a>**LLR-RFP-07** — An unusable language module shall not by itself cause `elc` to terminate or to return a non-zero exit status.
    *Trace:* HLR-070 (Malformed Language Module Tolerance).

*   <a id="LLR-RFP-09"></a>**LLR-RFP-09** — `registry_for_path` shall state, in the diagnostic for a query that will not compile, the language, the query file, and the reason in words rather than as a numeric code, since the author of the query file is who acts on that diagnostic.
    *Trace:* HLR-070, HLR-038.

*   <a id="LLR-RFP-10"></a>**LLR-RFP-10** — `registry_for_path` shall load a language's conditional-region query when the module supplies one, and shall treat its absence as the language having no conditional compilation rather than as the module being unusable, since the required set of query files is unchanged by this addition.
    *Trace:* HLR-134, HLR-121, HLR-070.

*   <a id="LLR-RFP-11"></a>**LLR-RFP-11** — `registry_for_path` shall load a language's dead-code query when the module supplies one, and shall treat its absence as the language having no dead-code support rather than as the module being unusable. Making the file required would invalidate every language module already shipped, which is what the stable contract exists to prevent.
    *Trace:* HLR-139 (Dead-Code Support Is Per Language and Its Absence Is Stated), HLR-121, HLR-070.

*   <a id="LLR-RFP-12"></a>**LLR-RFP-12** — `registry_for_path` shall treat an optional query file that is present and will not compile as it treats a required one — a diagnostic naming the language, the file and the reason, and the language excluded. Omitting a file is a decision the contract allows; writing a broken one is a defect, and the two must not have the same outcome.
    *Trace:* HLR-070 (Language Module Failure Isolation), HLR-121.

*   <a id="LLR-RFP-08"></a>**LLR-RFP-08** — `registry_for_path` shall require of a language module only the documented set of query files and capture names, and a module supplying exactly that set shall function without further configuration.
    *Trace:* HLR-121 (Language Module Interface Is a Stable Contract).

## 12. `registry_load_rules` ([src/registry.c](../src/registry.c))

*   <a id="LLR-RLR-01"></a>**LLR-RLR-01** — `registry_load_rules` shall load user-supplied rule query files and compile them with the same mechanism used for the built-in queries.
    *Trace:* HLR-107 (User-Supplied Rule Queries).

*   <a id="LLR-RLR-02"></a>**LLR-RLR-02** — `registry_load_rules` shall bind a rule found in the runtime location to the language of the directory containing it, and a rule named on the command line to the language named alongside its path.
    *Trace:* HLR-107 (User-Supplied Rule Queries).

*   <a id="LLR-RLR-03"></a>**LLR-RLR-03** — `registry_load_rules` shall report and skip a rule naming a language for which no module is available, rather than attempting to compile it.
    *Trace:* HLR-107 (User-Supplied Rule Queries), HLR-070 (Malformed Language Module Tolerance).

*   <a id="LLR-RLR-04"></a>**LLR-RLR-04** — `registry_load_rules` shall require no modification or recompilation of the executable to add, alter, or remove a rule.
    *Trace:* HLR-108 (Custom Rules Require No Rebuild).

*   <a id="LLR-RLR-05"></a>**LLR-RLR-05** — `registry_load_rules` shall use only rule files named on the command line or present in the runtime location, and shall discover none from the working directory, the analysis target, or any dotfile.
    *Trace:* HLR-110 (No Automatic Rule Discovery), HLR-039 (Zero Configuration).

*   <a id="LLR-RLR-06"></a>**LLR-RLR-06** — `registry_load_rules` shall emit a diagnostic and fail the run when a rule file named on the command line cannot be read or is invalid.
    *Trace:* HLR-116 (Invalid Custom Rule File Handling), HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-RLR-07"></a>**LLR-RLR-07** — `registry_load_rules` shall emit a diagnostic, exclude the rule, and continue when a rule file found in the runtime location cannot be read or is invalid.
    *Trace:* HLR-116 (Invalid Custom Rule File Handling), HLR-070 (Malformed Language Module Tolerance).

## 13. `registry_close` ([src/registry.c](../src/registry.c))

*   <a id="LLR-RCL-01"></a>**LLR-RCL-01** — `registry_close` shall delete every compiled query before releasing the parser and cursor, and shall close every dynamic-library handle last, so that no compiled query outlives the grammar it references. Dereferencing a query after its grammar has been unmapped is the memory-safety error this ordering exists to prevent.
    *Trace:* HLR-124 (Memory Safety), HLR-125 (Complete Resource Release), HLR-009 (Runtime-Loaded Language Support).

## 14. `analyze_file` ([src/analyze.c](../src/analyze.c))

The single parse. Extracts per-function metrics and the graph facts in one traversal; no other function reads source text.

Note on the division of labour, which determines where a failure lives: the requirements below constrain what `analyze_file` does with what the queries capture — attribution, merging, counting, and span arithmetic — and are unit-verifiable against a synthetic grammar. Which construct a given language classifies as a statement, a decision point, or a comment is decided by that language's `.scm` files, and conformance of those files is fixture-verified under HLR-034.

*   <a id="LLR-ANL-01"></a>**LLR-ANL-01** — `analyze_file` shall derive every reported metric from the parsed syntax tree, using no regular-expression matching, brace counting, or other textual approximation.
    *Trace:* HLR-013 (AST-Based Metric Extraction).

*   <a id="LLR-ANL-02"></a>**LLR-ANL-02** — `analyze_file` shall open and map each source file read-only, and shall never open a source file for writing.
    *Trace:* HLR-043 (Read-Only Operation).

*   <a id="LLR-ANL-03"></a>**LLR-ANL-03** — `analyze_file` shall parse each source file exactly once per run, and shall not reopen or re-parse it for any later stage.
    *Trace:* HLR-076 (Graph Built From the Single Parse).

*   <a id="LLR-ANL-04"></a>**LLR-ANL-04** — `analyze_file` shall report zero metrics without error for a file of zero length, rather than attempting to map it.
    *Trace:* HLR-020 (Files With No Effective Lines of Code).

*   <a id="LLR-ANL-05"></a>**LLR-ANL-05** — `analyze_file` shall pass the mapped length explicitly to the parser, since the mapping is not null-terminated.
    *Trace:* HLR-013 (AST-Based Metric Extraction).

*   <a id="LLR-ANL-06"></a>**LLR-ANL-06** — `analyze_file` shall count the file's physical lines from the mapped contents, counting a final line that carries no terminating newline, and bounding every scan by the mapped length rather than by a terminator.
    *Trace:* HLR-019 (File-Level Totals).

*   <a id="LLR-ANL-07"></a>**LLR-ANL-07** — `analyze_file` shall report, for each function it discovers, that function's name and its start and end line numbers, converting from the parser's zero-based rows exactly once, and shall copy the name out of the mapping into its own storage before that mapping is released.
    *Trace:* HLR-014 (Per-Function Identity).

*   <a id="LLR-ANL-08"></a>**LLR-ANL-08** — `analyze_file` shall treat as a function any named callable unit the language defines, including a method, constructor, destructor, or nested subprogram, as identified by the language's query configuration.
    *Trace:* HLR-014 (Per-Function Identity).

*   <a id="LLR-ANL-09"></a>**LLR-ANL-09** — `analyze_file` shall discover and report a named function declared within the body of another function as a function in its own right, with its own name, line range, ELOC, and complexity.
    *Trace:* HLR-067 (Nested Named Functions Reported Independently).

*   <a id="LLR-ANL-10"></a>**LLR-ANL-10** — `analyze_file` shall compute each function's ELOC as the count of executable statements attributed to it, excluding lines serving only a structural, declarative, blank, or documentary purpose.
    *Trace:* HLR-015 (Per-Function Effective Lines of Code).

*   <a id="LLR-ANL-11"></a>**LLR-ANL-11** — `analyze_file` shall count a statement spanning several physical lines once, at its start line, so that identical logic yields the same ELOC however it is laid out.
    *Trace:* HLR-053 (Multi-Line Statements Counted as a Single Line).

*   <a id="LLR-ANL-12"></a>**LLR-ANL-12** — `analyze_file` shall count toward ELOC each line performing a data assignment or a mathematical, logical, or pointer operation.
    *Trace:* HLR-044 (Assignments and Operations Count as ELOC).

*   <a id="LLR-ANL-13"></a>**LLR-ANL-13** — `analyze_file` shall count toward ELOC each line containing a control-flow construct that directs the execution path.
    *Trace:* HLR-045 (Control-Flow Statements Count as ELOC).

*   <a id="LLR-ANL-14"></a>**LLR-ANL-14** — `analyze_file` shall count toward ELOC each line invoking a function or method, whether or not the result is used.
    *Trace:* HLR-046 (Function-Call Statements Count as ELOC).

*   <a id="LLR-ANL-15"></a>**LLR-ANL-15** — `analyze_file` shall count toward ELOC each line returning from a function, with or without a value.
    *Trace:* HLR-047 (Return Statements Count as ELOC).

*   <a id="LLR-ANL-16"></a>**LLR-ANL-16** — `analyze_file` shall count toward ELOC each line containing an exception-handling construct.
    *Trace:* HLR-048 (Exception-Handling Statements Count as ELOC).

*   <a id="LLR-ANL-17"></a>**LLR-ANL-17** — `analyze_file` shall exclude from ELOC every line containing no token other than whitespace.
    *Trace:* HLR-049 (Blank Lines Excluded from ELOC).

*   <a id="LLR-ANL-18"></a>**LLR-ANL-18** — `analyze_file` shall exclude from ELOC every line containing nothing but a standalone structural token.
    *Trace:* HLR-050 (Standalone Structural Tokens Excluded from ELOC).

*   <a id="LLR-ANL-19"></a>**LLR-ANL-19** — `analyze_file` shall exclude from ELOC every line that only declares a variable or function without initialising data or executing code.
    *Trace:* HLR-051 (Non-Initializing Declarations Excluded from ELOC).

*   <a id="LLR-ANL-20"></a>**LLR-ANL-20** — `analyze_file` shall exclude from ELOC every line consisting only of a preprocessor or compile-time directive, for a language whose grammar defines such constructs.
    *Trace:* HLR-052 (Preprocessor and Directive Lines Excluded from ELOC).

*   <a id="LLR-ANL-21"></a>**LLR-ANL-21** — `analyze_file` shall compute each function's cyclomatic complexity as one plus the number of decision points found within its body.
    *Trace:* HLR-017 (Per-Function Cyclomatic Complexity).

*   <a id="LLR-ANL-22"></a>**LLR-ANL-22** — `analyze_file` shall attribute a decision point occurring within an anonymous callable to the nearest enclosing named function, unless the language's query configuration attributes it to the nested scope.
    *Trace:* HLR-018 (Anonymous-Scope Complexity Attribution).

*   <a id="LLR-ANL-23"></a>**LLR-ANL-23** — `analyze_file` shall compute the file's total ELOC over every qualifying line, including lines lying outside any function.
    *Trace:* HLR-019 (File-Level Totals).

*   <a id="LLR-ANL-24"></a>**LLR-ANL-24** — `analyze_file` shall report an ELOC of zero without error for a file containing no qualifying line.
    *Trace:* HLR-020 (Files With No Effective Lines of Code).

*   <a id="LLR-ANL-25"></a>**LLR-ANL-25** — `analyze_file` shall record each call site, each global read and write, and each address-taken function, together with its enclosing function, for later cross-file resolution.
    *Trace:* HLR-073 (System Dependence Graph Construction), HLR-074 (Global State Edges), HLR-096 (Dead Code Detection by Reachability).

*   <a id="LLR-ANL-26"></a>**LLR-ANL-26** — `analyze_file` shall evaluate every loaded custom rule query and record each match with the rule's identity and the matching line range.
    *Trace:* HLR-109 (Custom Rule Match Reporting).

*   <a id="LLR-ANL-27"></a>**LLR-ANL-27** — `analyze_file` shall identify a rule match by the basename of the rule file together with the capture name that matched.
    *Trace:* HLR-109 (Custom Rule Match Reporting).

*   <a id="LLR-ANL-28"></a>**LLR-ANL-28** — `analyze_file` shall emit a diagnostic naming the file and return non-zero, without aborting the run, when the file cannot be read or its contents cannot be decoded.
    *Trace:* HLR-035 (Per-File Read- and Parse-Failure Tolerance).

*   <a id="LLR-ANL-29"></a>**LLR-ANL-29** — `analyze_file` shall treat a syntax tree containing any error node as a parse failure and skip the whole file, rather than reporting metrics derived from a damaged tree.
    *Trace:* HLR-035 (Per-File Read- and Parse-Failure Tolerance).

*   <a id="LLR-ANL-30"></a>**LLR-ANL-30** — `analyze_file` shall copy every identifier out of the mapping into separately allocated storage before the mapping is released.
    *Trace:* HLR-014 (Per-Function Identity).

*   <a id="LLR-ANL-31"></a>**LLR-ANL-31** — `analyze_file` shall unmap the file and release the syntax tree on every exit path, including error paths.
    *Trace:* HLR-043 (Read-Only Operation).

*   <a id="LLR-ANL-32"></a>**LLR-ANL-32** — `analyze_file` shall not treat comment syntax occurring inside a string literal as a comment, and shall not treat a string delimiter occurring inside a comment as opening a string, both distinctions following from the syntax tree rather than from any textual scan.
    *Trace:* HLR-034 (Correctness Against Hand-Counted Fixtures).

*   <a id="LLR-ANL-33"></a>**LLR-ANL-33** — `analyze_file` shall confine every read of the mapped buffer to the range reported by `fstat`, so that a node offset returned by the parser cannot cause a read beyond the end of the mapping.
    *Trace:* HLR-124 (Memory Safety).

*   <a id="LLR-ANL-35"></a>**LLR-ANL-35** — `analyze_file` shall report a function's line span from the start of its captured name to the end of its captured body, so that the span begins at the signature a reader sees rather than at the body's opening delimiter. Where a language's query captures the name after the body, the span shall be the body's alone rather than an inverted one.
    *Trace:* HLR-014.

*   <a id="LLR-ANL-36"></a>**LLR-ANL-36** — `analyze_file` shall report a function only for a query match supplying both the name capture and the body capture, and shall discard a match carrying one without the other rather than reporting a function with no line range or a line range with no name.
    *Trace:* HLR-014, HLR-121.

*   <a id="LLR-ANL-37"></a>**LLR-ANL-37** — `analyze_file` shall distinguish a file skipped for want of a usable language module from a file that failed to be read or parsed, and shall report the two as different outcomes, so that a skip leaves the exit status at zero and a failure does not.
    *Trace:* HLR-012, HLR-035, HLR-037.

*   <a id="LLR-ANL-38"></a>**LLR-ANL-38** — `analyze_file` shall compute ELOC as a count of distinct lines carrying a counted statement, so that two statements written on one line contribute one and the same two written on separate lines contribute two.
    *Trace:* HLR-015, HLR-053.

*   <a id="LLR-ANL-39"></a>**LLR-ANL-39** — `analyze_file` shall exclude a captured statement from ELOC when the statement itself lies within the merged comment set, and shall not exclude a statement merely because its line also carries a comment.
    *Trace:* HLR-016, HLR-015.

*   <a id="LLR-ANL-40"></a>**LLR-ANL-40** — `analyze_file` shall attribute a statement lying outside every reported function to no function, counting it toward the file's ELOC alone.
    *Trace:* HLR-019, HLR-068.

*   <a id="LLR-ANL-41"></a>**LLR-ANL-41** — `analyze_file` shall attribute a decision point lying outside every reported function to no function, so that a branch in a file-scope initialiser is charged to nothing.
    *Trace:* HLR-017, HLR-018.

*   <a id="LLR-ANL-42"></a>**LLR-ANL-42** — `analyze_file` shall determine, before running any metric query, the byte ranges of each region the supplied definitions render inactive, from the captures of the language's conditional-region query applied to the tree already parsed.
    *Trace:* HLR-132, HLR-135.

*   <a id="LLR-ANL-43"></a>**LLR-ANL-43** — `analyze_file` shall exclude from every metric and every recorded fact any capture whose position lies within an inactive range, by the same exclusion the merged comment set uses, so that one mechanism governs both and neither can remove a byte range twice.
    *Trace:* HLR-132.

*   <a id="LLR-ANL-44"></a>**LLR-ANL-44** — `analyze_file` shall evaluate a region's condition from the captured condition nodes alone, deciding a region active or inactive only where the condition tests the definedness of symbols the user named or is a literal; any other condition shall leave both branches active and shall increment the count of undecided regions.
    *Trace:* HLR-133, HLR-135, HLR-013.

*   <a id="LLR-ANL-45"></a>**LLR-ANL-45** — `analyze_file` shall report zero inactive ranges when no definition was supplied, so that a run without the option yields byte-identical output to one made before the option existed.
    *Trace:* HLR-131, HLR-032.

*   <a id="LLR-ANL-46"></a>**LLR-ANL-46** — `analyze_file` shall evaluate the predicates a query pattern carries and discard any match whose predicates do not hold. The parser library treats a predicate as data — it parses one and returns its steps, leaving the decision to the caller — so a stage that never asks accepts every match as though the predicate were not written. For a dead-code query that turns “`if (0)` is dead” into “every `if` is dead”, which is the false claim HLR-138 forbids. The evaluation compares the text a capture spans against a string the query file wrote and knows no language.
    *Trace:* HLR-121 (Language Module Interface Is a Stable Contract), HLR-138, HLR-013.

*   <a id="LLR-ANL-47"></a>**LLR-ANL-47** — `analyze_file` shall ignore a directive, which carries information rather than filtering, and shall discard the match on encountering a filtering predicate it does not implement. A filter the build cannot apply is a condition the query author wrote and this build cannot honour; accepting the match would apply that condition's inverse, and under-reporting is the direction every capture in the contract errs in.
    *Trace:* HLR-121 (Language Module Interface Is a Stable Contract), HLR-138.

*   <a id="LLR-ANL-34"></a>**LLR-ANL-34** — `analyze_file` shall assign the result of every reallocation to a temporary and verify it before overwriting the original pointer, so that a failed growth of the function array neither loses the existing allocation nor leaves a dangling pointer.
    *Trace:* HLR-124 (Memory Safety), HLR-125 (Complete Resource Release).

## 15. `collect_dead_code` ([src/analyze.c](../src/analyze.c))

*   <a id="LLR-DED-01"></a>**LLR-DED-01** — `collect_dead_code` shall record, as unreachable, every named sibling following a statement captured as a block terminator, within the same parent, stopping at the first sibling captured as a re-entry point.
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection).

*   <a id="LLR-DED-02"></a>**LLR-DED-02** — `collect_dead_code` shall record, as unreachable, the branch captured as excluded by a literal condition.
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection).

*   <a id="LLR-DED-03"></a>**LLR-DED-03** — `collect_dead_code` shall determine unreachability from captured syntax alone, evaluating no expression and propagating no constant, so that a branch guarded by a variable is never recorded however evidently its value could be inferred.
    *Trace:* HLR-138 (Dead Code Within Functions Determined Syntactically), HLR-013.

*   <a id="LLR-DED-04"></a>**LLR-DED-04** — `collect_dead_code` shall attribute each recorded span to the innermost reported function containing it, by the rule ELOC and complexity already use.
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection), HLR-068.

*   <a id="LLR-DED-05"></a>**LLR-DED-05** — `collect_dead_code` shall record, for each file, whether its language supplied a dead-code query, and shall treat its absence as an unanalysed language rather than as a file containing no dead code.
    *Trace:* HLR-139 (Dead-Code Support Is Per Language and Its Absence Is Stated), HLR-121 (Language Module Interface Is a Stable Contract).

*   <a id="LLR-DED-06"></a>**LLR-DED-06** — `collect_dead_code` shall record a statement's unreachability independently of whether the function containing it is reachable, so that neither dead-code analysis suppresses the other.
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection), HLR-096.

*   <a id="LLR-DED-07"></a>**LLR-DED-07** — `collect_dead_code` shall skip, without stopping at, any following sibling lying within the merged comment set, so that a comment is never recorded as dead code. A comment is a *named* sibling in the grammars this contract addresses, so a walk that did not exclude one would report the trailing note on a terminator's own line — making every annotated `return` report itself. The exclusion consults the set `comments.scm` already produced rather than recognising a comment for itself, so one answer serves both this and the ELOC exclusion, and no node type enters the C.
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection), HLR-138, HLR-016 (Comment Span Merging).

*   <a id="LLR-DED-08"></a>**LLR-DED-08** — `collect_dead_code` shall record no span lying outside every reported function, since HLR-137 asks about statements within a function and a span at file scope has no enclosing function to be reported against.
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection).

*   <a id="LLR-DED-09"></a>**LLR-DED-09** — `collect_dead_code` shall collapse recorded spans naming the same lines of the same function into one, having first ordered them, so that a statement following two terminators is reported once and the result does not depend on the order the query matched.
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection), HLR-032.

## 16. `merge_comment_spans` ([src/analyze.c](../src/analyze.c))

The comment-deduction algorithm. A naive implementation is silently wrong on nested comment syntax.

*   <a id="LLR-MRG-01"></a>**LLR-MRG-01** — `merge_comment_spans` shall sort the captured comment spans by start position before any span is excluded.
    *Trace:* HLR-016 (Comment Span Merging).

*   <a id="LLR-MRG-02"></a>**LLR-MRG-02** — `merge_comment_spans` shall coalesce overlapping and nested spans into a canonical set before their lines are excluded from ELOC.
    *Trace:* HLR-016 (Comment Span Merging).

*   <a id="LLR-MRG-03"></a>**LLR-MRG-03** — `merge_comment_spans` shall exclude no line more than once, so that a block comment containing inline comment syntax cannot drive a file's ELOC below zero.
    *Trace:* HLR-016 (Comment Span Merging), HLR-034 (Correctness Against Hand-Counted Fixtures).

*   <a id="LLR-MRG-05"></a>**LLR-MRG-05** — `merge_comment_spans` shall report the number of distinct lines the merged set covers, counting a line shared by two spans that do not overlap in bytes — two comments written on one line — once rather than once per span.
    *Trace:* HLR-016.

*   <a id="LLR-MRG-04"></a>**LLR-MRG-04** — `merge_comment_spans` shall index the span array only within its populated extent while sorting and coalescing, so that coalescing a run of adjacent spans cannot read past the final element.
    *Trace:* HLR-124 (Memory Safety).

## 17. `innermost_enclosing` ([src/analyze.c](../src/analyze.c))

*   <a id="LLR-INN-01"></a>**LLR-INN-01** — `innermost_enclosing` shall return the narrowest reported function whose byte range contains the given offset.
    *Trace:* HLR-068 (Innermost-Function Metric Attribution).

*   <a id="LLR-INN-02"></a>**LLR-INN-02** — Each statement and decision point shall be attributed, through `innermost_enclosing`, to exactly one reported function, so that a statement within a nested named function contributes to that function alone and not also to any function enclosing it.
    *Trace:* HLR-068 (Innermost-Function Metric Attribution), HLR-067 (Nested Named Functions Reported Independently).

## 18. `graph_build` ([src/graph.c](../src/graph.c))

Cross-file resolution of the per-file facts into the System Dependence Graph.

*   <a id="LLR-SDG-01"></a>**LLR-SDG-01** — `graph_build` shall assign every function discovered across all analysed files a node in the graph.
    *Trace:* HLR-073 (System Dependence Graph Construction).

*   <a id="LLR-SDG-02"></a>**LLR-SDG-02** — `graph_build` shall resolve each recorded call site to the function it invokes, adding a directed edge from caller to callee, including where caller and callee reside in different files.
    *Trace:* HLR-073 (System Dependence Graph Construction).

*   <a id="LLR-SDG-03"></a>**LLR-SDG-03** — `graph_build` shall add, for each global object, an edge from every function that writes it and an edge to every function that reads it.
    *Trace:* HLR-074 (Global State Edges).

*   <a id="LLR-SDG-04"></a>**LLR-SDG-04** — `graph_build` shall construct a simple directed graph, collapsing repeated calls from one function to the same callee into a single edge carrying a call-site count.
    *Trace:* HLR-085 (Function Fan-Out Measurement).

*   <a id="LLR-SDG-05"></a>**LLR-SDG-05** — `graph_build` shall span every analysed file across every target argument, whether the target is an application or a library.
    *Trace:* HLR-075 (Whole-Project Graph Scope), HLR-071 (Multiple Target Arguments).

*   <a id="LLR-SDG-06"></a>**LLR-SDG-06** — `graph_build` shall construct the graph solely from the facts produced by the single parse, reopening no source file.
    *Trace:* HLR-076 (Graph Built From the Single Parse).

*   <a id="LLR-SDG-07"></a>**LLR-SDG-07** — `graph_build` shall record a call site whose target cannot be resolved as unresolved, and shall not abort graph construction.
    *Trace:* HLR-077 (Unresolvable Call Handling).

*   <a id="LLR-SDG-08"></a>**LLR-SDG-08** — `graph_build` shall exclude an unresolved call site from analyses that require a known destination, and shall retain a count of such sites for reporting.
    *Trace:* HLR-077 (Unresolvable Call Handling).

*   <a id="LLR-SDG-09"></a>**LLR-SDG-09** — `graph_build` shall assign node indices in the order functions were discovered from the sorted file list, so that traversal results can be reduced to a deterministic order.
    *Trace:* HLR-033 (Traversal-Order Independence).

*   <a id="LLR-SDG-10"></a>**LLR-SDG-10** — `graph_build` shall derive a component projection in which an edge exists from one source file to another whenever a function in the first calls a function in the second, or writes a global that a function in the second reads.
    *Trace:* HLR-114 (Definition of a Component).

*   <a id="LLR-SDG-12"></a>**LLR-SDG-12** — `graph_build` shall own every string the graph refers to that originates in the fact list — the names of global objects and of unresolved callees — copying them rather than aliasing the facts. The fact list is released as soon as the graph is built, and an edge or an unresolved record holding a freed string does not crash: it renders as a plausible object name, which is the worst way for a graph to be wrong. Strings that originate in the *report* model are borrowed rather than copied, since the report outlives the graph.
    *Trace:* HLR-124 (Memory Safety), HLR-074 (Global State Edges).

*   <a id="LLR-SDG-13"></a>**LLR-SDG-13** — `graph_build` shall treat an identifier captured as a global read or write as global state only when some analysed file declares that name at file scope, and shall treat an identifier captured as address-taken as a reachability root only when it resolves to a defined function. Both classes of capture are deliberately over-broad in every language module, because neither question can be answered from one file's syntax; resolving them here is what allows the query files to capture identifiers in value position without encoding scope rules they cannot express.
    *Trace:* HLR-074 (Global State Edges), HLR-096, HLR-121 (Language Module Interface Is a Stable Contract).

*   <a id="LLR-SDG-14"></a>**LLR-SDG-14** — `graph_build` shall collapse repeated calls between one pair of functions into a single edge, and shall not collapse global-state edges between the same pair: a global edge is per object, and merging two would lose which shared state couples the functions. It shall record a call site whose enclosing function is file scope as unresolved rather than dropping it silently, since the graph does not represent it and the reader is judging completeness.
    *Trace:* HLR-085, HLR-074 (Global State Edges), HLR-077 (Unresolvable Call Handling).

*   <a id="LLR-SDG-16"></a>**LLR-SDG-16** — `graph_build` shall record, for every global object, the set of functions that write it and the set that read it, **beside** the global-state edges rather than as a projection of them. A global edge joins a writer to a reader, so an object touched by exactly one function produces no edge at all — and that object is precisely the scope-reduction candidate of HLR-092. An analysis reading only the edge table would find none of them, and would find no object that is written and never read either. The records shall be ordered by object and then by node identifier and de-duplicated, so that a function writing one object in four places is one writer.
    *Trace:* HLR-091 (Global Access Mapping), HLR-092, HLR-032.

*   <a id="LLR-SDG-17"></a>**LLR-SDG-17** — `graph_build` shall construct a third view of the graph over the component projection, whose vertices are component indices. The architectural questions are asked of it and of nothing else: a cycle here means two files depend on each other, where a cycle in the call view means two functions call each other, and two mutually recursive functions inside one file close a loop in the second and none at all in the first.
    *Trace:* HLR-083 (Circular Dependency Detection), HLR-114, HLR-113.

*   <a id="LLR-SDG-15"></a>**LLR-SDG-15** — `graph_build` shall install a non-aborting error handler on the graph library before making any call to it. The library's default handler calls `abort()`, which makes every return-value check unreachable and turns an allocation failure inside the library into a crash rather than the diagnostic and exit status the run promises. It matters beyond allocation: asking a cyclic graph for a topological ordering is an ordinary, expected error return — and is exactly how the call-depth analysis detects recursion — which the default handler would turn into a crash on a perfectly valid program.
    *Trace:* HLR-124 (Memory Safety), HLR-113, HLR-120 (Distinct Exit Status Classes).

*   <a id="LLR-SDG-11"></a>**LLR-SDG-11** — `graph_build` shall validate every node index against the node table's extent before dereferencing it, so that an unresolved or out-of-range symbol lookup cannot index outside the table.
    *Trace:* HLR-124 (Memory Safety), HLR-077 (Unresolvable Call Handling).

## 19. `arch_analyse` ([src/arch.c](../src/arch.c))

Component-level analyses over the SDG's component projection.

*   <a id="LLR-ARC-01"></a>**LLR-ARC-01** — `arch_analyse` shall identify as an architectural bottleneck every component whose afferent and efferent coupling are each greater than or equal to the configured bottleneck threshold.
    *Trace:* HLR-081 (Architectural Bottleneck Identification).

*   <a id="LLR-ARC-02"></a>**LLR-ARC-02** — `arch_analyse` shall mark the bottleneck threshold as `elc`'s own heuristic rather than a published standard wherever it is reported.
    *Trace:* HLR-081 (Architectural Bottleneck Identification), HLR-099 (Threshold Attribution).

*   <a id="LLR-ARC-03"></a>**LLR-ARC-03** — `arch_analyse` shall omit layering validation, and record the omission with its reason, when no architectural strata were declared.
    *Trace:* HLR-115 (Analyses Requiring User Declarations).

*   <a id="LLR-ARC-04"></a>**LLR-ARC-04** — `arch_analyse` shall emit a diagnostic when a declared stratum pattern matches no component, and shall retain the empty layer.
    *Trace:* HLR-078 (User-Declared Architectural Strata).

## 20. `compute_coupling` ([src/arch.c](../src/arch.c))

*   <a id="LLR-CPL-01"></a>**LLR-CPL-01** — `compute_coupling` shall compute, for every component, the number of components depending upon it as its afferent coupling.
    *Trace:* HLR-080 (Afferent and Efferent Coupling).

*   <a id="LLR-CPL-02"></a>**LLR-CPL-02** — `compute_coupling` shall compute, for every component, the number of components it depends upon as its efferent coupling.
    *Trace:* HLR-080 (Afferent and Efferent Coupling).

*   <a id="LLR-CPL-03"></a>**LLR-CPL-03** — `compute_coupling` shall treat a single source file as the unit of coupling.
    *Trace:* HLR-114 (Definition of a Component).

*   <a id="LLR-CPL-04"></a>**LLR-CPL-04** — `compute_coupling` shall count each depended-upon component once however many calls or shared objects connect the two, taking its figures from the de-duplicated component projection rather than from the call sites. Counting call sites would report a file that calls another in forty places as depending on forty things.
    *Trace:* HLR-080 (Afferent and Efferent Coupling), HLR-114.

## 21. `instability` ([src/arch.c](../src/arch.c))

*   <a id="LLR-INS-01"></a>**LLR-INS-01** — `instability` shall compute the Instability metric as efferent coupling divided by the sum of efferent and afferent coupling.
    *Trace:* HLR-082 (Instability Metric).

*   <a id="LLR-INS-02"></a>**LLR-INS-02** — `instability` shall report the metric as undefined, performing no division, when both couplings are zero.
    *Trace:* HLR-082 (Instability Metric).

*   <a id="LLR-INS-03"></a>**LLR-INS-03** — The reported Instability shall be attributed to its published source.
    *Trace:* HLR-099 (Threshold Attribution), HLR-082 (Instability Metric).

## 22. `find_cycles` ([src/arch.c](../src/arch.c))

*   <a id="LLR-CYC-01"></a>**LLR-CYC-01** — `find_cycles` shall detect every cyclic dependency between components by topological analysis of the component projection.
    *Trace:* HLR-083 (Circular Dependency Detection).

*   <a id="LLR-CYC-02"></a>**LLR-CYC-02** — `find_cycles` shall report each cycle together with the ordered sequence of components forming it.
    *Trace:* HLR-083 (Circular Dependency Detection).

*   <a id="LLR-CYC-03"></a>**LLR-CYC-03** — `find_cycles` shall not report mutual recursion between functions residing in the same component as a component-level cycle.
    *Trace:* HLR-083 (Circular Dependency Detection), HLR-089 (Recursion Detection).

*   <a id="LLR-CYC-04"></a>**LLR-CYC-04** — Every detected cycle shall be reported at critical severity.
    *Trace:* HLR-084 (Cycles Reported at Critical Severity), HLR-123 (Severity Vocabulary).

*   <a id="LLR-CYC-05"></a>**LLR-CYC-05** — `find_cycles` shall report, beside each cyclic group, a concrete loop through it found by a deterministic search from the group's lowest-numbered member. The group is what must be broken up and the loop is which edge to cut, and neither alone is enough to act on. One loop rather than every one: a group of n components can hold a number of distinct loops exponential in n, and the loop may therefore be shorter than the membership.
    *Trace:* HLR-083 (Circular Dependency Detection), HLR-032.

## 23. `check_strata` ([src/arch.c](../src/arch.c))

*   <a id="LLR-LAY-01"></a>**LLR-LAY-01** — `check_strata` shall report every call that bypasses one or more intervening declared layers, identifying the calling function, the called function, and the layers crossed.
    *Trace:* HLR-079 (Skip-Level Call Detection).

*   <a id="LLR-LAY-02"></a>**LLR-LAY-02** — `check_strata` shall report every call whose direction is inverted with respect to the declared dependency direction, identifying the calling function, the called function, and the layers involved.
    *Trace:* HLR-118 (Direction-Inverted Call Detection).

*   <a id="LLR-LAY-03"></a>**LLR-LAY-03** — `check_strata` shall report a skip-level call and a direction-inverted call as distinct findings, since a call may exhibit either without the other.
    *Trace:* HLR-079 (Skip-Level Call Detection), HLR-118 (Direction-Inverted Call Detection).

*   <a id="LLR-LAY-04"></a>**LLR-LAY-04** — `check_strata` shall report a call as skip-level where the ordinal distance between the layers exceeds one in either direction, and as direction-inverted where the callee's ordinal is the lower, so that a call ascending more than one layer is reported as both. Both statements are true of such a call and each has its own remedy — the direction can be corrected without removing the skip.
    *Trace:* HLR-079 (Skip-Level Call Detection), HLR-118.

*   <a id="LLR-LAY-05"></a>**LLR-LAY-05** — `check_strata` shall consider call edges alone, since a global object two layers happen to share is a different fact with its own analyses, and shall treat a component matching no declaration as lying outside the partition rather than in a layer of its own.
    *Trace:* HLR-079 (Skip-Level Call Detection), HLR-118, HLR-093.

## 24. `calltree_analyse` ([src/calltree.c](../src/calltree.c))

Function-level call-tree measurements: width, height, the deepest stack, and recursion.

*   <a id="LLR-CTR-01"></a>**LLR-CTR-01** — `calltree_analyse` shall compute, for every function, the number of distinct subroutines it invokes directly.
    *Trace:* HLR-085 (Function Fan-Out Measurement).

*   <a id="LLR-CTR-02"></a>**LLR-CTR-02** — `calltree_analyse` shall detect direct and mutual recursion by decomposing the function graph, reporting every non-trivial strongly connected component and every self-loop as a recursive cycle.
    *Trace:* HLR-089 (Recursion Detection).

*   <a id="LLR-CTR-03"></a>**LLR-CTR-03** — `calltree_analyse` shall omit the depth and deepest-chain analyses, and record the omission with its reason, when no entry points were declared.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-087 (Maximum Call-Chain Depth).

*   <a id="LLR-CTR-04"></a>**LLR-CTR-04** — `calltree_analyse` shall report the recursive cycles in place of a depth figure, rather than a finite number or a non-terminating computation, when recursion is present.
    *Trace:* HLR-090 (Depth Reporting Under Recursion).

*   <a id="LLR-CTR-07"></a>**LLR-CTR-07** — `calltree_analyse` shall measure fan-out, recursion, and call depth over the graph's call-edge view alone, never over the whole System Dependence Graph. A global-state edge records that one function writes an object another reads; it is coupling and not invocation. Counting one as a callee would inflate fan-out, following one would extend a call chain through a call that never happens, and a pair of functions sharing two objects in opposite directions forms a cycle in the SDG that is not recursion — which would be reported as a critical MISRA C Rule 17.2 violation against ordinary code.
    *Trace:* HLR-085, HLR-089, HLR-074 (Global State Edges).

*   <a id="LLR-CTR-08"></a>**LLR-CTR-08** — `calltree_analyse` shall distinguish, in the omission it records, between no entry points having been declared and declared entry points naming no analysed function, since the two call for different actions from the reader. A declared symbol that matches no analysed function shall be diagnosed and skipped rather than ending the run: analysing one directory of a project whose entry point is defined in another is ordinary use, and rejecting it would make the option unusable there.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-095 (User-Declared Entry Points).

*   <a id="LLR-CTR-09"></a>**LLR-CTR-09** — `calltree_analyse` shall continue to produce the measurements that need no user declaration — fan-out and recursion — when the depth analysis is omitted for want of one. Omitting an analysis is not a reason to omit its neighbours, and a report that fell silent about recursion because no entry point was declared would withhold the finding that matters most on a stack-constrained target.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-089.

*   <a id="LLR-CTR-05"></a>**LLR-CTR-05** — `calltree_analyse` shall compute the maximum call-chain depth reachable from the declared entry points when the function graph is acyclic.
    *Trace:* HLR-087 (Maximum Call-Chain Depth).

*   <a id="LLR-CTR-06"></a>**LLR-CTR-06** — `calltree_analyse` shall report the maximum depth together with the count of unresolved calls, since a chain continuing through an unresolved call is not followed and the depth is therefore a lower bound.
    *Trace:* HLR-087 (Maximum Call-Chain Depth), HLR-077 (Unresolvable Call Handling).

## 25. `longest_path_dag` ([src/calltree.c](../src/calltree.c))

*   <a id="LLR-LPD-01"></a>**LLR-LPD-01** — `longest_path_dag` shall compute the longest path from the declared entry points by memoised traversal in reverse topological order.
    *Trace:* HLR-087 (Maximum Call-Chain Depth).

*   <a id="LLR-LPD-04"></a>**LLR-LPD-04** — `longest_path_dag` shall count the entry point itself as the first layer, so that an entry point calling nothing has a depth of one rather than zero, and shall resolve a tie between two chains of equal length in favour of the lower node identifier — which is sorted-file order — so that equal candidates yield the same report on every run.
    *Trace:* HLR-087 (Maximum Call-Chain Depth), HLR-032.

*   <a id="LLR-LPD-02"></a>**LLR-LPD-02** — `longest_path_dag` shall retain the predecessor of each node so that the deepest chain can be reconstructed.
    *Trace:* HLR-088 (Deepest Call Stack Reported in Full).

*   <a id="LLR-LPD-03"></a>**LLR-LPD-03** — `longest_path_dag` shall return the ordered sequence of functions from entry point to deepest leaf, and not merely the depth as a number.
    *Trace:* HLR-088 (Deepest Call Stack Reported in Full).

## 26. `state_analyse` ([src/state.c](../src/state.c))

Global-state coupling, execution-scope isolation, and reachability.

*   <a id="LLR-STA-01"></a>**LLR-STA-01** — `state_analyse` shall omit the reachability analysis, and record the omission with its reason, when no entry points were declared, and shall in no circumstance report every function as unreachable for want of a declaration.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-096 (Dead Code Detection by Reachability).

*   <a id="LLR-STA-02"></a>**LLR-STA-02** — `state_analyse` shall omit the execution-scope isolation analysis, and record the omission with its reason, when no execution scopes were declared.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-094 (Memory Map Boundary Validation).

*   <a id="LLR-STA-03"></a>**LLR-STA-03** — `state_analyse` shall compute reachability over the call view of the graph and not over the whole SDG. A global-state edge joins a function that writes an object to one that later reads it, and writing a variable another function reads is not calling it: control never travels along that edge, so a function reachable only through one has not been reached. Following it would quietly rescue genuinely dead code from the report, which is the error this analysis exists to avoid making in the other direction.
    *Trace:* HLR-096 (Dead Code Detection by Reachability), HLR-097, HLR-074.

*   <a id="LLR-STA-04"></a>**LLR-STA-04** — `state_analyse` shall perform the global-access mapping whether or not any declaration was supplied, so that omitting one analysis for want of a declaration does not omit its neighbours.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-091.

## 27. `classify_globals` ([src/state.c](../src/state.c))

*   <a id="LLR-GLB-01"></a>**LLR-GLB-01** — `classify_globals` shall report, for every global object, the set of functions that write it and the set that read it.
    *Trace:* HLR-091 (Global Access Mapping).

*   <a id="LLR-GLB-02"></a>**LLR-GLB-02** — `classify_globals` shall flag for scope reduction every global whose read and write edges originate from a single function.
    *Trace:* HLR-092 (Scope-Reduction Candidates).

*   <a id="LLR-GLB-03"></a>**LLR-GLB-03** — `classify_globals` shall flag as a hidden channel every global whose edges originate from multiple otherwise-disconnected regions of the graph, identifying the disconnected participants.
    *Trace:* HLR-093 (Hidden Channel Detection).

*   <a id="LLR-GLB-04"></a>**LLR-GLB-04** — The scope-reduction and hidden-channel findings shall be attributed to their published source.
    *Trace:* HLR-099 (Threshold Attribution), HLR-092 (Scope-Reduction Candidates), HLR-093 (Hidden Channel Detection).

*   <a id="LLR-GLB-05"></a>**LLR-GLB-05** — `classify_globals` shall determine the regions of the hidden-channel test as the *weakly* connected components of the call view, disregarding the object's own state edges. Weakly, because two functions in a one-directional calling relationship are still part of one design and requiring mutual reachability would report every ordinary caller and callee as disconnected; and over the call view, because including the object's own edges would join every pair sharing it and no object could ever be a channel.
    *Trace:* HLR-093 (Hidden Channel Detection), HLR-074.

## 28. `collect_roots` ([src/state.c](../src/state.c))

*   <a id="LLR-RTS-01"></a>**LLR-RTS-01** — `collect_roots` shall form the reachability root set as the union of the declared entry points and every function whose address is taken without being directly called.
    *Trace:* HLR-096 (Dead Code Detection by Reachability), HLR-095 (User-Declared Entry Points).

*   <a id="LLR-RTS-02"></a>**LLR-RTS-02** — `collect_roots` shall include address-taken functions because they may be invoked indirectly, so that a callback or interrupt handler is never reported as unreachable merely for want of a direct call.
    *Trace:* HLR-096 (Dead Code Detection by Reachability), HLR-097 (Dead Code Determined by Graph Mathematics).

## 29. `reachability` ([src/state.c](../src/state.c))

*   <a id="LLR-RCH-01"></a>**LLR-RCH-01** — `reachability` shall traverse the graph forward from the root set and report every function not visited as unreachable.
    *Trace:* HLR-096 (Dead Code Detection by Reachability).

*   <a id="LLR-RCH-02"></a>**LLR-RCH-02** — `reachability` shall establish unreachability solely by graph traversal, using no textual or heuristic means.
    *Trace:* HLR-097 (Dead Code Determined by Graph Mathematics).

*   <a id="LLR-RCH-03"></a>**LLR-RCH-03** — `reachability` shall report as unreachable a group of unused functions that call one another, since no path reaches the group from any root.
    *Trace:* HLR-097 (Dead Code Determined by Graph Mathematics).

## 30. `unreachable_globals` ([src/state.c](../src/state.c))

*   <a id="LLR-UGL-01"></a>**LLR-UGL-01** — `unreachable_globals` shall report as unreachable every global object accessed solely by functions that are themselves unreachable.
    *Trace:* HLR-096 (Dead Code Detection by Reachability).

*   <a id="LLR-UGL-02"></a>**LLR-UGL-02** — `unreachable_globals` shall not report as unreachable an object that no analysed function accesses. Such an object may be touched from file scope, from a language whose global captures record nothing, or from a translation unit outside the target, and the asymmetry that governs the functions governs the storage: an object wrongly called dead invites deleting memory something writes.
    *Trace:* HLR-096 (Dead Code Detection by Reachability), HLR-138.

## 31. `check_scopes` ([src/state.c](../src/state.c))

*   <a id="LLR-ISO-01"></a>**LLR-ISO-01** — `check_scopes` shall report every call edge and every global-state edge by which one declared execution scope reaches a function or object belonging to another.
    *Trace:* HLR-094 (Memory Map Boundary Validation).

*   <a id="LLR-ISO-02"></a>**LLR-ISO-02** — `check_scopes` shall treat a component matching no declaration as lying outside the partition rather than in a scope of its own, so that an edge touching it is not a crossing. The user said nothing about it, and inventing a boundary would report violations against a division nobody drew.
    *Trace:* HLR-094 (Memory Map Boundary Validation), HLR-115.

## 32. `thresholds_apply` ([src/thresholds.c](../src/thresholds.c))

Evaluation of every measurement against the published threshold catalogue, and assignment of severity and attribution.

*   <a id="LLR-THR-01"></a>**LLR-THR-01** — `thresholds_apply` shall evaluate every architectural measurement against the published threshold catalogue and report where the measurement falls relative to the accepted range.
    *Trace:* HLR-098 (Evaluation Against Published Thresholds).

*   <a id="LLR-THR-02"></a>**LLR-THR-02** — `thresholds_apply` shall attribute every threshold to its external source, and shall identify as `elc`'s own any threshold that is not a published standard.
    *Trace:* HLR-099 (Threshold Attribution).

*   <a id="LLR-THR-03"></a>**LLR-THR-03** — `thresholds_apply` shall assign each finding exactly one severity drawn from the ordered set info, warning, critical, and shall emit no finding without one.
    *Trace:* HLR-123 (Severity Vocabulary).

*   <a id="LLR-THR-04"></a>**LLR-THR-04** — `thresholds_apply` shall assign the highest applicable severity where more than one band applies to a single measurement.
    *Trace:* HLR-123 (Severity Vocabulary).

*   <a id="LLR-THR-05"></a>**LLR-THR-05** — `thresholds_apply` shall classify a function fan-out of 10 or fewer as producing no finding, 11 to 15 as a warning, and greater than 15 as critical, the bands being exhaustive over every possible value.
    *Trace:* HLR-086 (Fan-Out Threshold Classification).

*   <a id="LLR-THR-06"></a>**LLR-THR-06** — `thresholds_apply` shall classify call-chain depth against the embedded guidance, warning beyond 8 layers and reporting critically beyond 12.
    *Trace:* HLR-087 (Maximum Call-Chain Depth).

*   <a id="LLR-THR-07"></a>**LLR-THR-07** — `thresholds_apply` shall report the presence of recursion at critical severity, attributed to its safety standard.
    *Trace:* HLR-089 (Recursion Detection), HLR-099 (Threshold Attribution).

*   <a id="LLR-THR-08"></a>**LLR-THR-08** — `thresholds_apply` shall report a measurement for which the catalogue holds no entry as a bare value with no severity, rather than discarding it or inventing a band.
    *Trace:* HLR-098 (Evaluation Against Published Thresholds).

*   <a id="LLR-THR-09"></a>**LLR-THR-09** — `thresholds_apply` shall emit measurements, threshold positions, and violations of user-supplied criteria only, proposing no fix and ranking no design beyond what a cited standard states.
    *Trace:* HLR-101 (No Remediation Advice).

*   <a id="LLR-THR-10"></a>**LLR-THR-10** — `thresholds_apply` shall form no judgement as to whether a user-supplied rule is appropriate, and shall supply no rule of its own beyond the catalogued metrics and thresholds.
    *Trace:* HLR-111 (Custom Rules Carry No Built-In Opinion).

## 33. `report_assemble` ([src/report.c](../src/report.c))

The single place every reported collection is ordered. The audit point for determinism.

*   <a id="LLR-RPT-01"></a>**LLR-RPT-01** — `report_assemble` shall compute the combined project totals of physical lines and ELOC across every analysed file.
    *Trace:* HLR-024 (Project-Level Totals).

*   <a id="LLR-RPT-02"></a>**LLR-RPT-02** — `report_assemble` shall break down the project totals of physical lines and ELOC by source language.
    *Trace:* HLR-025 (Project Totals by Source Language).

*   <a id="LLR-RPT-03"></a>**LLR-RPT-03** — `report_assemble` shall identify the file with the highest file-level ELOC and the function with the highest cyclomatic complexity for the project summary.
    *Trace:* HLR-026 (Project-Wide Most-Complex Callouts).

*   <a id="LLR-RPT-04"></a>**LLR-RPT-04** — `report_assemble` shall break a tie for either most-complex callout by selecting whichever sorts first under the stable presentation order.
    *Trace:* HLR-026 (Project-Wide Most-Complex Callouts), HLR-033 (Traversal-Order Independence).

*   <a id="LLR-RPT-05"></a>**LLR-RPT-05** — `report_assemble` shall produce, for each file, the list of its functions whose cyclomatic complexity meets or exceeds the configured threshold.
    *Trace:* HLR-021 (Per-File Complexity-Threshold List).

*   <a id="LLR-RPT-06"></a>**LLR-RPT-06** — The threshold list shall affect only what is listed for a file, and shall not influence the exit status.
    *Trace:* HLR-023 (Threshold List is Reporting-Only), HLR-100 (Severity Labels Do Not Affect Exit Status).

*   <a id="LLR-RPT-07"></a>**LLR-RPT-07** — `report_assemble` shall record every file skipped for want of a language module in the report's skipped-file list.
    *Trace:* HLR-012 (Unsupported-Language File Handling).

*   <a id="LLR-RPT-08"></a>**LLR-RPT-08** — `report_assemble` shall record the count of unresolved call sites so that the completeness of the graph is visible.
    *Trace:* HLR-077 (Unresolvable Call Handling).

*   <a id="LLR-RPT-09"></a>**LLR-RPT-09** — `report_assemble` shall record each omitted analysis together with the reason it was omitted.
    *Trace:* HLR-115 (Analyses Requiring User Declarations).

*   <a id="LLR-RPT-10"></a>**LLR-RPT-10** — `report_assemble` shall sort every collection in the report model by an explicit key, so that no renderer sorts and no library enumeration order reaches the output.
    *Trace:* HLR-033 (Traversal-Order Independence), HLR-032 (Deterministic Output).

*   <a id="LLR-RPT-11"></a>**LLR-RPT-11** — `report_assemble` shall order files by path, functions within a file by start line and then by name, skipped files by path, findings by severity then kind then location, cycles by their lowest-ordered member, and unreachable functions by file then line. The name shall break a tie between two functions sharing a start line, since the sort is not otherwise stable and their order would then be the implementation's choice.
    *Trace:* HLR-033 (Traversal-Order Independence).

*   <a id="LLR-RPT-23"></a>**LLR-RPT-23** — `report_set_unresolved` shall record the count of unresolved call sites on the assembled report, so that every format presenting the project summary states how complete the graph is (HLR-077). It is set after assembly rather than passed into it because the graph is built *from* the assembled model — its node order is the report's file order — so the count does not exist when `report_assemble` runs.
    *Trace:* HLR-077 (Unresolvable Call Handling), HLR-024.

*   <a id="LLR-RPT-24"></a>**LLR-RPT-24** — `report_set_calltree` shall copy the call-tree measurements into the model, resolving every node identifier to the function name, file, and line a reader can act on. A node identifier indexes a table that exists only while the graph does, and the model outlives it, is rendered in four formats, and round-trips through a saved record — so carrying identifiers into it would make each of those a lookup against a structure that has been released.
    *Trace:* HLR-085, HLR-088, HLR-089.

*   <a id="LLR-RPT-26"></a>**LLR-RPT-26** — `report_assemble` shall present the unreachable statements of HLR-137 as their own tier, ordered by file then by start line, and shall name the languages for which dead-code analysis was not performed rather than leaving their absence to be read as a clean result (HLR-139).
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection), HLR-139 (Dead-Code Support Is Per Language and Its Absence Is Stated), HLR-033 (Traversal-Order Independence).

*   <a id="LLR-RPT-25"></a>**LLR-RPT-25** — The report shall state, wherever a depth figure is absent, which of the three reasons applies: the call graph is recursive and no finite depth exists; no entry points were declared; or the declared entry points name no analysed function. An absence with no stated cause is indistinguishable from a measurement of zero (HLR-115).
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-090 (Depth Reporting Under Recursion).

*   <a id="LLR-RPT-12"></a>**LLR-RPT-12** — `report_assemble` shall produce a complete model with zero totals for a run in which no file was analysed.
    *Trace:* HLR-066 (Run With No Analyzable Files).

*   <a id="LLR-RPT-13"></a>**LLR-RPT-13** — `report_assemble` shall produce a model of the same structure and fields whatever the type of the analysed target.
    *Trace:* HLR-006 (Uniform Target Output Shape).

*   <a id="LLR-RPT-14"></a>**LLR-RPT-14** — `report_assemble` shall include every custom-rule match in the model alongside the built-in findings.
    *Trace:* HLR-109 (Custom Rule Match Reporting).

*   <a id="LLR-RPT-15"></a>**LLR-RPT-15** — `report_assemble` shall carry into the model every architectural measurement computed during the run — per-component afferent and efferent coupling and Instability, and per-function fan-out — ordered by component or by function, so that a measurement lying within its accepted band is reported rather than discarded for want of a finding.
    *Trace:* HLR-080 (Afferent and Efferent Coupling), HLR-082 (Instability Metric), HLR-085 (Function Fan-Out Measurement), HLR-031 (Uniform Report Composition Across Formats).

*   <a id="LLR-RPT-17"></a>**LLR-RPT-17** — `report_assemble` shall carry the discovery route of each directory target into the model, so that every report format can state which route was applied, and shall order the routes by target so that the Discovery section does not present them in the order they were given on the command line. The routes shall be copied into the model rather than referenced: discovery owns its list and releases it once analysis is complete, while the model outlives both, and regeneration from a saved record has no discovery to own anything and builds the same collection from the record.
    *Trace:* HLR-033 (Traversal-Order Independence), HLR-127 (Discovery Route Reported).

*   <a id="LLR-RPT-18"></a>**LLR-RPT-18** — `report_assemble` shall take ownership of the accumulated per-file metrics and leave the accumulator empty, so that a caller releasing both the accumulator and the report — as it must on every exit path — cannot free the same metrics twice.
    *Trace:* HLR-124 (Memory Safety), HLR-125 (Complete Resource Release).

*   <a id="LLR-RPT-19"></a>**LLR-RPT-19** — `report_assemble` shall accumulate the physical-line and ELOC totals of each language present in the run into its own entry, ordered by language name, so that a language's contribution is visible separately from the combined totals.
    *Trace:* HLR-025, HLR-033.

*   <a id="LLR-RPT-20"></a>**LLR-RPT-20** — `report_assemble` shall list a function for its file when its complexity is greater than *or equal to* the configured threshold, and shall build that listing itself rather than leaving a renderer to filter, so that every format lists the same functions.
    *Trace:* HLR-021, HLR-023.

*   <a id="LLR-RPT-21"></a>**LLR-RPT-21** — `report_assemble` shall select each most-complex callout by scanning the ordered model and replacing the incumbent only on a strictly greater value, so that a tie resolves to whichever candidate sorts first under the presentation order and resolves the same way on every run.
    *Trace:* HLR-026, HLR-032, HLR-033.

*   <a id="LLR-RPT-27"></a>**LLR-RPT-27** — `report_assemble` shall carry the definitions in force and the count of undecided regions into the report model, ordered by symbol name, so that every format states the configuration the figures describe.
    *Trace:* HLR-136, HLR-133, HLR-033.

*   <a id="LLR-RPT-28"></a>**LLR-RPT-28** — The report shall resolve a dead-code span to its enclosing function by containment over the assembled model, rather than by the index the parse recorded. The index is into the array the parse produced, and that array has since been ordered for presentation; reading the index would name the right function only for as long as the two orders happened to agree. The rule applied is the one the parse applied — the narrowest reported function containing the span.
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection), HLR-068, HLR-032.

*   <a id="LLR-RPT-29"></a>**LLR-RPT-29** — The report shall order the cross-scope crossings by the boundary crossed and then by the functions at either end, and the global objects by name, so that no collection reaching a renderer carries the order a traversal happened to produce.
    *Trace:* HLR-032 (Deterministic Output), HLR-033.

*   <a id="LLR-RPT-16"></a>**LLR-RPT-16** — `report_assemble` shall grow every dynamic collection through a checked reallocation, and shall release the partially built model without leaking should any growth fail.
    *Trace:* HLR-124 (Memory Safety), HLR-125 (Complete Resource Release).

## 34. `format_table` ([src/format_text.c](../src/format_text.c))

*   <a id="LLR-TBL-01"></a>**LLR-TBL-01** — `format_table` shall render the report as an aligned, human-readable table, computing column widths from the longest path and function name.
    *Trace:* HLR-027 (Default Human-Readable Output).

*   <a id="LLR-TBL-02"></a>**LLR-TBL-02** — `format_table` shall be the format used when the user selects none.
    *Trace:* HLR-027 (Default Human-Readable Output).

*   <a id="LLR-TBL-03"></a>**LLR-TBL-03** — `format_table` shall write only results to the results stream, and no diagnostic.
    *Trace:* HLR-038 (Diagnostics on stderr, Results on stdout).

## 35. `format_markdown` ([src/format_text.c](../src/format_text.c))

*   <a id="LLR-MKD-01"></a>**LLR-MKD-01** — `format_markdown` shall render the report as GitHub-Flavored Markdown, grouping functions under a heading for the file containing them.
    *Trace:* HLR-029 (Markdown Output).

## 36. `render_summary` ([src/format_text.c](../src/format_text.c))

*   <a id="LLR-SUM-01"></a>**LLR-SUM-01** — `render_summary` shall present the project summary, the discovery route of each directory target, each file's totals and threshold list, the full per-function detail, the architectural measurements and findings — including measurements falling within their accepted bands — any custom-rule matches, the skipped-file list, and any omitted analysis with its reason, in every report format other than CSV, XML, and the `.dot` companion.
    *Trace:* HLR-031 (Uniform Report Composition Across Formats), HLR-127 (Discovery Route Reported), HLR-012 (Unsupported-Language File Handling), HLR-115 (Analyses Requiring User Declarations).

*   <a id="LLR-SUM-03"></a>**LLR-SUM-03** — `render_summary` shall emit every tier from one traversal shared by both human-facing formats, so that a tier cannot be present in one format and absent from the other.
    *Trace:* HLR-031.

*   <a id="LLR-SUM-04"></a>**LLR-SUM-04** — The aligned format shall not pad a left-aligned final column, so that no line carries trailing whitespace.
    *Trace:* HLR-027, HLR-032.

*   <a id="LLR-SUM-05"></a>**LLR-SUM-05** — `render_summary` shall present the definitions in force and the count of undecided regions in every format that presents the project summary, since a metric whose value depends on a configuration is not interpretable without it.
    *Trace:* HLR-136, HLR-031.

*   <a id="LLR-SUM-02"></a>**LLR-SUM-02** — `render_summary` shall traverse the report model in a single shared order for both the table and Markdown renderers, so that the two present the same tiers.
    *Trace:* HLR-031 (Uniform Report Composition Across Formats).

## 37. `format_csv` ([src/format_csv.c](../src/format_csv.c))

*   <a id="LLR-CSV-01"></a>**LLR-CSV-01** — `format_csv` shall emit one record per function over the complete dataset, unfiltered by the complexity threshold.
    *Trace:* HLR-028 (CSV Output).

*   <a id="LLR-CSV-02"></a>**LLR-CSV-02** — `format_csv` shall emit per-function metrics only, excluding the architectural findings.
    *Trace:* HLR-028 (CSV Output), HLR-031 (Uniform Report Composition Across Formats).

## 38. `write_field` ([src/format_csv.c](../src/format_csv.c))

*   <a id="LLR-FLD-01"></a>**LLR-FLD-01** — `write_field` shall quote and escape every field whose value contains a comma, a double-quote character, or a line break, in accordance with RFC 4180.
    *Trace:* HLR-064 (CSV Field Quoting and Escaping).

*   <a id="LLR-FLD-02"></a>**LLR-FLD-02** — Every emitted CSV field shall pass through `write_field`, so that a value such as a template signature containing a comma cannot corrupt the record structure.
    *Trace:* HLR-064 (CSV Field Quoting and Escaping).

## 39. `xml_write_report` ([src/format_xml.c](../src/format_xml.c))

*   <a id="LLR-XWR-01"></a>**LLR-XWR-01** — `xml_write_report` shall emit the complete dataset of a run, unfiltered by the complexity threshold.
    *Trace:* HLR-054 (XML Output).

*   <a id="LLR-XWR-02"></a>**LLR-XWR-02** — `xml_write_report` shall emit every element any report may present: the project totals, the per-file totals, the per-function detail, the architectural findings, and any custom-rule matches.
    *Trace:* HLR-054 (XML Output).

*   <a id="LLR-XWR-03"></a>**LLR-XWR-03** — `xml_write_report` shall emit a format-version identifier in the document root.
    *Trace:* HLR-061 (XML Format-Version Identifier).

*   <a id="LLR-XWR-05"></a>**LLR-XWR-05** — `xml_write_report` shall omit an element it has nothing to record rather than emit it empty, and the format-version identifier shall be incremented for a removal or a change of meaning and not for an addition, so that a record written by a later build remains readable by an earlier one of the same version.
    *Trace:* HLR-061, HLR-054.

*   <a id="LLR-XWR-06"></a>**LLR-XWR-06** — `xml_write_report` shall write each target's discovery route to the record. A record that omitted it would regenerate into a report with an empty Discovery section — well-formed, carrying every measurement, and not the same report — which is the one thing HLR-056 forbids.
    *Trace:* HLR-054 (Complete Analysis Record), HLR-056 (Regeneration Fidelity), HLR-127 (Discovery Route Reported).

*   <a id="LLR-XWR-07"></a>**LLR-XWR-07** — `xml_write_report` shall write the unresolved-call count to the record, and `xml_read_report` shall restore it. It is a measurement of the run like any other and cannot be recomputed later: regeneration has no graph, and no source from which to build one.
    *Trace:* HLR-054 (Complete Analysis Record), HLR-056 (Regeneration Fidelity), HLR-077 (Unresolvable Call Handling).

*   <a id="LLR-XWR-09"></a>**LLR-XWR-09** — `xml_write_report` shall write every unreachable statement to the record with its file, function, line range, and cause, together with the set of languages for which the analysis was not performed, and `xml_read_report` shall restore them. Regeneration has no syntax tree and cannot find them again.
    *Trace:* HLR-054 (Complete Analysis Record), HLR-056 (Regeneration Fidelity), HLR-137 (Intra-Procedural Dead Code Detection).

*   <a id="LLR-XWR-08"></a>**LLR-XWR-08** — `xml_write_report` shall write the call-tree measurements to the record — every function's fan-out, every recursive cycle's members, the depth and its state, and the deepest chain in order — and `xml_read_report` shall restore them. None can be recomputed from a record: regeneration has no graph, and no source from which to build one.
    *Trace:* HLR-054 (Complete Analysis Record), HLR-056 (Regeneration Fidelity).

*   <a id="LLR-XWR-10"></a>**LLR-XWR-10** — `xml_write_report` shall emit the definitions in force and the count of undecided regions, so that a record states the configuration it was taken under.
    *Trace:* HLR-136, HLR-054.

*   <a id="LLR-XWR-11"></a>**LLR-XWR-11** — The record shall carry the global-access map with its verdicts, the unreachable functions and objects, the cross-scope crossings, the dead-code spans, and the state of each analysis that may be omitted. None can be recomputed on regeneration, which has neither a graph nor a source tree to build one from.
    *Trace:* HLR-054 (Complete Run Record), HLR-056, HLR-096, HLR-137.

*   <a id="LLR-XWR-12"></a>**LLR-XWR-12** — The record shall carry the languages for which dead-code analysis was not performed, and shall not carry the published source a global-state verdict is attributed to. The first, because a record holding the findings alone would regenerate into a report reading as a clean bill of health for a language nobody analysed. The second, because the citation is derived from the verdict by one function both paths call, so a record cannot carry one that disagrees with a live run's.
    *Trace:* HLR-139 (Dead-Code Support Is Per Language and Its Absence Is Stated), HLR-056, HLR-099.

*   <a id="LLR-XWR-13"></a>**LLR-XWR-13** — The record shall carry the coupling figures with their instability, the dependency cycles with their loops, the layering findings, and the state of the layering analysis. None can be recomputed on regeneration, which has no component projection to derive them from; and the attributions are again absent, being derived from the measurement by one function each path calls.
    *Trace:* HLR-054 (Complete Run Record), HLR-056, HLR-080, HLR-083.

*   <a id="LLR-XWR-04"></a>**LLR-XWR-04** — `xml_write_report` shall emit well-formed XML.
    *Trace:* HLR-065 (XML Well-Formedness and Escaping).

## 40. `write_escaped` ([src/format_xml.c](../src/format_xml.c))

*   <a id="LLR-ESC-01"></a>**LLR-ESC-01** — `write_escaped` shall escape every character carrying structural meaning in XML, so that an identifier or path containing such a character cannot render the document unparseable.
    *Trace:* HLR-065 (XML Well-Formedness and Escaping).

*   <a id="LLR-ESC-02"></a>**LLR-ESC-02** — Every emitted XML element body and attribute value shall pass through `write_escaped`.
    *Trace:* HLR-065 (XML Well-Formedness and Escaping).

## 41. `xml_read_report` ([src/format_xml.c](../src/format_xml.c))

*   <a id="LLR-XRD-01"></a>**LLR-XRD-01** — `xml_read_report` shall reconstruct a report model from a previously written record without parsing or re-analysing any source file.
    *Trace:* HLR-055 (XML-to-Markdown Conversion Mode).

*   <a id="LLR-XRD-02"></a>**LLR-XRD-02** — `xml_read_report` shall parse the record with a streaming parser, materialising no document tree.
    *Trace:* HLR-055 (XML-to-Markdown Conversion Mode).

*   <a id="LLR-XRD-03"></a>**LLR-XRD-03** — `xml_read_report` shall reject, with a diagnostic and a non-zero exit status, an input that is not well-formed XML.
    *Trace:* HLR-058 (Malformed or Unsupported Saved-XML Rejection).

*   <a id="LLR-XRD-04"></a>**LLR-XRD-04** — `xml_read_report` shall reject an input that does not match `elc`'s own output structure.
    *Trace:* HLR-058 (Malformed or Unsupported Saved-XML Rejection).

*   <a id="LLR-XRD-05"></a>**LLR-XRD-05** — `xml_read_report` shall reject an input whose format-version identifier this build does not support, naming the version found.
    *Trace:* HLR-058 (Malformed or Unsupported Saved-XML Rejection), HLR-061 (XML Format-Version Identifier).

*   <a id="LLR-XRD-09"></a>**LLR-XRD-09** — `xml_read_report` shall reconstruct only the measurements a record carries and shall derive every reported total, breakdown, callout, ordering, and threshold listing with the same code that derives them for a live run, so that a regenerated report cannot differ from a direct one by way of a second implementation.
    *Trace:* HLR-056, HLR-032.

*   <a id="LLR-XRD-10"></a>**LLR-XRD-10** — `xml_read_report` shall treat an attribute that should be numeric and is not as a malformed record rather than as a zero, since a record accepted on those terms renders cleanly and reports the wrong figures.
    *Trace:* HLR-058.

*   <a id="LLR-XRD-12"></a>**LLR-XRD-12** — `xml_read_report` shall rebuild the discovery routes from the record and hand them to `report_assemble`, so that a regenerated report presents the same Discovery section as the run that produced the record. A route element lacking either its target or its route, or naming a route this build does not recognise, shall be rejected as a malformed record rather than defaulted: a defaulted route is not an unknown answer but a confident wrong one, in the section whose whole purpose is explaining a surprising file set.
    *Trace:* HLR-055 (Report Regeneration From a Record), HLR-056 (Regeneration Fidelity), HLR-127 (Discovery Route Reported).

*   <a id="LLR-XRD-13"></a>**LLR-XRD-13** — `xml_read_report` shall reconstruct the definitions a record was taken under and present them unchanged, rather than applying any supplied afterwards, so that a regenerated report describes the configuration actually measured.
    *Trace:* HLR-136, HLR-056.

*   <a id="LLR-XRD-06"></a>**LLR-XRD-06** — `xml_read_report` shall attempt no best-effort partial conversion of a rejected input.
    *Trace:* HLR-058 (Malformed or Unsupported Saved-XML Rejection).

*   <a id="LLR-XRD-07"></a>**LLR-XRD-07** — `xml_read_report` shall apply the complexity threshold supplied at conversion time, independently of any threshold in force when the record was written.
    *Trace:* HLR-057 (User-Supplied Threshold at Regeneration Time).

*   <a id="LLR-XRD-08"></a>**LLR-XRD-08** — The reconstructed model shall render byte-identically to the report a direct analysis would have produced at the same threshold.
    *Trace:* HLR-056 (Regenerated Report Equivalence).

## 42. `graph_dot_warranted` ([src/format_graph.c](../src/format_graph.c))

*   <a id="LLR-WAR-01"></a>**LLR-WAR-01** — `graph_dot_warranted` shall return true by default, and false when the user has disabled `.dot` generation.
    *Trace:* HLR-103 (.dot Generation Enabled by Default).

*   <a id="LLR-WAR-02"></a>**LLR-WAR-02** — `graph_dot_warranted` shall return false whenever the report is written to standard output, whether or not generation was disabled.
    *Trace:* HLR-104 (No .dot Output to Standard Output).

*   <a id="LLR-WAR-03"></a>**LLR-WAR-03** — `graph_dot_warranted` shall return false in regeneration mode, since a saved record does not carry the graph.
    *Trace:* HLR-122 (No Companion Artefacts From a Saved Record).

## 43. `graph_write_dot` ([src/format_graph.c](../src/format_graph.c))

*   <a id="LLR-DOT-01"></a>**LLR-DOT-01** — `graph_write_dot` shall emit the call tree in Graphviz DOT format.
    *Trace:* HLR-102 (Graphviz .dot Call Tree Output).

*   <a id="LLR-DOT-02"></a>**LLR-DOT-02** — `graph_write_dot` shall require no library dependency and shall not invoke Graphviz.
    *Trace:* HLR-102 (Graphviz .dot Call Tree Output).

*   <a id="LLR-DOT-03"></a>**LLR-DOT-03** — `graph_write_dot` shall derive its filename from the report's output path by extension substitution, accepting no output path of its own.
    *Trace:* HLR-119 (Companion Artefact Naming).

*   <a id="LLR-DOT-04"></a>**LLR-DOT-04** — `graph_write_dot` shall emit nodes in ascending stable node-identifier order and each node's adjacency in ascending target order, so that no internal enumeration order reaches the output.
    *Trace:* HLR-032 (Deterministic Output), HLR-033 (Traversal-Order Independence).

*   <a id="LLR-DOT-05"></a>**LLR-DOT-05** — `graph_write_dot` shall emit a diagnostic and record a failure, while leaving the primary report written, when the companion file cannot be created.
    *Trace:* HLR-035 (Per-File Read- and Parse-Failure Tolerance).

## 44. `node_style` ([src/format_graph.c](../src/format_graph.c))

*   <a id="LLR-STY-01"></a>**LLR-STY-01** — `node_style` shall annotate nodes exceeding the coupling and fan-out thresholds, the functions forming the deepest call chain, the members of each dependency and recursive cycle, unreachable functions, and functions participating in a hidden channel.
    *Trace:* HLR-105 (Annotated .dot Output).

*   <a id="LLR-STY-02"></a>**LLR-STY-02** — `node_style` shall emit annotations as attributes a renderer may ignore while still producing a valid call tree.
    *Trace:* HLR-105 (Annotated .dot Output).

## 45. `graph_write_graphml` ([src/format_graph.c](../src/format_graph.c))

*   <a id="LLR-GML-01"></a>**LLR-GML-01** — `graph_write_graphml` shall export the graph in the GraphML serialisation schema so that it may be ingested by other tools.
    *Trace:* HLR-106 (Standard Graph Serialisation Export).

*   <a id="LLR-GML-02"></a>**LLR-GML-02** — `graph_write_graphml` shall run only when explicitly requested, being disabled by default.
    *Trace:* HLR-106 (Standard Graph Serialisation Export).

*   <a id="LLR-GML-03"></a>**LLR-GML-03** — `graph_write_graphml` shall derive its filename from the report's output path, and shall produce no file when the report is written to standard output.
    *Trace:* HLR-106 (Standard Graph Serialisation Export), HLR-119 (Companion Artefact Naming).

*   <a id="LLR-GML-04"></a>**LLR-GML-04** — `graph_write_graphml` shall emit well-formed XML with every structurally significant character escaped.
    *Trace:* HLR-065 (XML Well-Formedness and Escaping), HLR-106 (Standard Graph Serialisation Export).

## 46. Build and Link Configuration ([Makefile](../Makefile))

Requirements satisfied by the build rather than by any single function. Verified against the produced binary rather than at runtime.

*   <a id="LLR-BLD-01"></a>**LLR-BLD-01** — The build shall link `elc` against no interpreter and no virtual machine, and the produced binary shall require none at execution.
    *Trace:* HLR-040 (Excluded Runtime Dependencies).

*   <a id="LLR-BLD-02"></a>**LLR-BLD-02** — `elc` shall perform no network access at any point during execution, and shall produce identical results when run without network availability.
    *Trace:* HLR-040 (Excluded Runtime Dependencies).

*   <a id="LLR-BLD-03"></a>**LLR-BLD-03** — The build shall require no code generation step.
    *Trace:* HLR-040 (Excluded Runtime Dependencies).

*   <a id="LLR-BLD-04"></a>**LLR-BLD-04** — The build shall select the third-party libraries `elc` links against, any of which may be substituted for another satisfying the same role, save for the parsing library whose query language and grammar format are a product contract.
    *Trace:* HLR-112 (Library Selection Deferred to Design).

*   <a id="LLR-BLD-05"></a>**LLR-BLD-05** — The build shall link an established graph library providing cycle detection, topological ordering, reachability, and centrality, rather than hand-implemented equivalents. Every optional feature of that library that would alter `elc`'s link line shall be pinned explicitly rather than left to the library's configure-time detection: a default of "use it if it is installed" makes the binary a function of the build machine, which the instrumented dependency allowlist — a fixed list — cannot express.
    *Trace:* HLR-113 (Graph Algorithms From an Established Library).

*   <a id="LLR-BLD-06"></a>**LLR-BLD-06** — The build shall configure the graph library so that it does not itself introduce a dependency on an unmaintained XML library.
    *Trace:* HLR-112 (Library Selection Deferred to Design), HLR-113 (Graph Algorithms From an Established Library).

*   <a id="LLR-BLD-07"></a>**LLR-BLD-07** — The build shall deliver runtime language support for C, C++, Rust, Python, and Ada, each as a grammar and its query files under the runtime location, requiring no change to any source module of the executable.
    *Trace:* HLR-011 (Initial Delivered Language Set).

*   <a id="LLR-BLD-08"></a>**LLR-BLD-08** — The documented set of required query files and capture names shall be published with the delivered runtime, so that a third party may add a language against it.
    *Trace:* HLR-121 (Language Module Interface Is a Stable Contract), HLR-010 (No-Recompilation Language Addition).

*   <a id="LLR-BLD-10"></a>**LLR-BLD-10** — The build shall support link-time symbol interception for the unit level, so that a dependency can be replaced by a test without a seam being carried in `src/`. A toolchain that cannot provide it cannot run the unit suite and is unsupported rather than worked around.
    *Trace:* HLR-113 (Graph Algorithms From an Established Library), HLR-124 (Memory Safety).

*   <a id="LLR-BLD-11"></a>**LLR-BLD-11** — The build shall apply the language standard, the warning set, and the header-dependency generation it requires in a way that a caller-supplied `CFLAGS` cannot displace, so that a build invoked with an added flag is compiled under the same rules as one invoked with none.
    *Trace:* HLR-124 (Memory Safety).

*   <a id="LLR-BLD-12"></a>**LLR-BLD-12** — The build shall produce each delivered language grammar from a pinned upstream release, compiling the generated parser that release publishes, so that no code-generation step is required at build time and the version delivered is a recorded decision rather than whatever was current.
    *Trace:* HLR-040, HLR-011, HLR-009.

*   <a id="LLR-BLD-13"></a>**LLR-BLD-13** — The build shall carry its usage summary as a marked comment block in the makefile's own header and shall print that block for the help target, so that one text serves a reader who opens the file and a reader who runs it, and shall fail its own test suite when that block and the set of declared targets disagree.
    *Trace:* HLR-128.

*   <a id="LLR-BLD-14"></a>**LLR-BLD-14** — The delivered source shall avoid the C constructs the delivered grammar cannot parse — a multi-token type argument to a variadic accessor, and a macro standing between a function's return type and its name — so that `elc` can analyse its own source without a per-file failure.
    *Trace:* HLR-035, HLR-013.

*   <a id="LLR-BLD-15"></a>**LLR-BLD-15** — The build shall take the upstream owner and the archive reference of each grammar as parameters rather than as constants, so that a grammar hosted outside the parsing library's own organisation, or one whose upstream cuts no releases and must be pinned by commit, is added as data without a change to any source module.
    *Trace:* HLR-010, HLR-011.

*   <a id="LLR-BLD-16"></a>**LLR-BLD-16** — The build shall locate a grammar's external scanner at the time the compile command runs rather than when the rule is expanded, since a grammar linked without the scanner it requires fails at load rather than at build.
    *Trace:* HLR-009, HLR-011.

*   <a id="LLR-BLD-17"></a>**LLR-BLD-17** — The build shall report, for each delivered grammar, the reference it is pinned to and the reference upstream carries now, so that a pin which is immutable is not thereby invisible when it falls behind.
    *Trace:* HLR-011.

*   <a id="LLR-BLD-09"></a>**LLR-BLD-09** — The build shall provide a configuration instrumented with AddressSanitizer and UndefinedBehaviorSanitizer, with leak detection enabled, under which the whole test suite can be re-run.
    *Trace:* HLR-124 (Memory Safety), HLR-125 (Complete Resource Release).

## 47. User Documentation ([doc/elc.1](../doc/elc.1), [doc/User_Manual.md](../doc/User_Manual.md))

Requirements met by the delivered documentation rather than by any function. Verified against the built binary and the install tree, in the manner of the build-configuration group.

*   <a id="LLR-DOC-01"></a>**LLR-DOC-01** — A `man` page for `elc` shall be delivered in section-1 roff form, and shall render without diagnostic under a standard man-page formatter.
    *Trace:* HLR-128 (User Manual and Man Page Delivered).

*   <a id="LLR-DOC-02"></a>**LLR-DOC-02** — A user manual shall be delivered describing every capability with at least one worked example per output format and per analysis family.
    *Trace:* HLR-128 (User Manual and Man Page Delivered).

*   <a id="LLR-DOC-03"></a>**LLR-DOC-03** — The install target shall place the man page into the section-1 manual path and the user manual alongside the delivered runtime, both under the staging root when one is given.
    *Trace:* HLR-128 (User Manual and Man Page Delivered).

*   <a id="LLR-DOC-04"></a>**LLR-DOC-04** — Every option appearing in `elc`'s usage summary shall appear in both the man page and the user manual, and every option documented in either shall be accepted by `elc`; the usage summary is the reference against which both are checked.
    *Trace:* HLR-129 (Documentation Describes the Delivered Behaviour).

*   <a id="LLR-DOC-05"></a>**LLR-DOC-05** — Every report format, companion artefact, and category of reported finding shall be described in both documents.
    *Trace:* HLR-129 (Documentation Describes the Delivered Behaviour).

*   <a id="LLR-DOC-06"></a>**LLR-DOC-06** — A change altering an option, a format, an artefact, or a finding category shall update both documents within the same change, and shall be treated as incomplete otherwise.
    *Trace:* HLR-130 (Documentation Updated With the Behaviour It Describes).
