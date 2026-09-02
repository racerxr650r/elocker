# Low-Level Requirements

**Version:** 2.23
**Date:** 2026-09-02
**Author(s):** John Anderson

## 1. `main` ([src/main.c](../src/main.c))

Orchestration and exit status. `main` performs no analysis; every requirement below concerns sequencing, mode selection, or the status it returns.

*   <a id="LLR-MAIN-01"></a>**LLR-MAIN-01** — `main` shall invoke `cli_parse` before any other stage, and shall perform no file system access when `cli_parse` reports a usage error.
    *Trace:* HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-MAIN-02"></a>**LLR-MAIN-02** — `main` shall return 0 when `cli_parse` reports a help request, after the usage summary has been written to standard output.
    *Trace:* HLR-117 (Help Request Is Not an Error).

*   <a id="LLR-MAIN-03"></a>**LLR-MAIN-03** — When the parsed options select regeneration mode, `main` shall invoke `xml_read_report` and proceed directly to rendering, invoking neither `discover_targets` nor `analyze_file`.
    *Trace:* HLR-055 (XML-to-Markdown Conversion Mode).

*   <a id="LLR-MAIN-26"></a>**LLR-MAIN-26** — `main` shall open the debug companion immediately after the command line is parsed and before any other stage, and shall close it last.

    The ordering is the whole of its usefulness: a diagnostic written before the companion existed is a diagnostic the companion does not hold, and the stages that diagnose most — locating the runtime, discovering targets, loading a grammar — are the earliest ones. The name shall be derived from the report's output path by the companion rule of HLR-119, so a report on standard output produces none and that is not a usage error.
    *Trace:* HLR-194 (The Debug Companion), HLR-119.

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

*   <a id="LLR-MAIN-20"></a>**LLR-MAIN-20** — `main` shall read the linked image before discovery begins, so that an image that cannot be read ends the run before any source file is measured rather than after a full walk whose results are then discarded.
    *Trace:* HLR-146 (An Unusable Image Is Fatal).

*   <a id="LLR-MAIN-21"></a>**LLR-MAIN-21** — `main` shall pass the selected verbosity to the two human-facing renderers and to no other, so that the CSV and XML writers produce the same bytes whatever was asked for. The dispatch is where that stops: the complete-record formats have no presentation for a verbosity to select between, and giving them the parameter would create a place for one to be honoured by mistake.
    *Trace:* HLR-152 (Complete-Record Formats Unaffected by Verbosity), HLR-151 (Verbose Report on Request).

*   <a id="LLR-MAIN-22"></a>**LLR-MAIN-22** — `main` shall read a named purification manifest before the classifications it overrules and shall end the run where it cannot be read, the diagnostic already written; shall run recovery after purification and before nothing, handing its result to the report and to no analysis; and shall write the manifest and the pair of drawings as companions, each named from the report's own output path and each written after the report rather than instead of it. The ordering is what makes the boundaries structural: purification produces a second graph and hands it to nobody but the report, and recovery reads that graph and hands its proposal to nobody but the report — so no measurement can be taken over a masked graph and no conformance analysis can reach a proposal, whatever a later change does elsewhere.
    *Trace:* HLR-176 (The Manifest Is Read Only When Named), HLR-173, HLR-167, HLR-119.

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

*   <a id="LLR-CLI-10"></a>**LLR-CLI-10** — `cli_parse` shall accept a regeneration-mode input path. In regeneration mode the report format shall default to Markdown rather than to the table format, and only a format *explicitly* selected and other than Markdown shall be rejected. A format is explicitly selected either by `--format` or by the extension of an `--output` path (LLR-CLI-28), the two being two spellings of one statement (HLR-148, HLR-149); the diagnostic shall name whichever the user wrote.
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

*   <a id="LLR-CLI-22"></a>**LLR-CLI-22** — `cli_parse` shall accept the path of a linked image to filter by, and shall record it unvalidated, the image being read by the module that owns it rather than by the parser. An empty argument is the one thing the parser can settle for itself, and is a usage error: it names no file, so there is nothing for the reader of the image to diagnose.
    *Trace:* HLR-140 (Linked-Image Function Filter).

*   <a id="LLR-CLI-23"></a>**LLR-CLI-23** — `cli_parse` shall reject as a usage error a command line combining regeneration mode with a linked image, since the filter is applied when a file is measured and a saved record holds only what a measured run produced.
    *Trace:* HLR-147 (Filter Recorded and Reported), HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-CLI-24"></a>**LLR-CLI-24** — `cli_parse` shall reject as a usage error a command line combining regeneration mode with a conditional-compilation definition, since pruning is applied when a file is measured and a saved record already describes one configuration.
    *Trace:* HLR-136 (Configuration Recorded and Reported), HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-CLI-25"></a>**LLR-CLI-25** — `cli_parse` shall accept zero or more conditional-compilation definitions and record each as given, `NAME` and `NAME=VALUE` alike, leaving what a definition means to the evaluation that alone knows what a language's conditions can test.
    *Trace:* HLR-131 (Conditional-Compilation Configuration).

*   <a id="LLR-CLI-26"></a>**LLR-CLI-26** — `cli_parse` shall derive the report format from the extension of an `--output` path, recognising `.txt` as the aligned table, `.md` as Markdown, `.csv` as CSV, and `.xml` as the complete record, so that no format option is required to repeat what the filename has already said.

    The extension is the last dot of the *basename*: a directory component carrying one lends nothing to a file that has none, a leading dot names a hidden file rather than an extension, and a trailing dot names nothing. An extension `elc` does not recognise, and a path with none, shall each be rejected as a usage error naming the extension found and listing those that are recognised, the list being generated from the same table that resolves so it cannot come to name a set of formats `elc` no longer has.
    *Trace:* HLR-148 (Output Format Determined by Filename Extension), HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-CLI-27"></a>**LLR-CLI-27** — `cli_parse` shall resolve the format after the option loop rather than within it, so that `--format` and `--output` may be given in either order and still be compared against one another. Where both name a format and they agree, the invocation shall be accepted; where they disagree it shall be rejected as a usage error naming both, neither being silently preferred. Where no output path is given the option alone decides, and its default stands.
    *Trace:* HLR-149 (Format Selection Without a Named Output File), HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-CLI-28"></a>**LLR-CLI-28** — `cli_parse` shall record whether the format was selected by the extension of an output path separately from whether `--format` named it, and shall treat the former as an explicit selection in regeneration mode. An output filename naming any format other than Markdown shall therefore be rejected there on the terms LLR-CLI-10 applies to `--format`, and one naming Markdown accepted, so that the mode can never write Markdown into a file whose name promises something else.
    *Trace:* HLR-055 (XML-to-Markdown Conversion Mode), HLR-148 (Output Format Determined by Filename Extension), HLR-063 (Invalid Command-Line Rejection).

*   <a id="LLR-CLI-29"></a>**LLR-CLI-29** — `cli_parse` shall accept a verbosity option in a short and a long spelling, recording the request in the options structure. The summary shall be the default, stored as the absence of the request, so that a zeroed options structure means the composition a run presents when nothing was asked for.
    *Trace:* HLR-151 (Verbose Report on Request), HLR-150 (Summary Report by Default).

*   <a id="LLR-CLI-31"></a>**LLR-CLI-31** — `cli_parse` shall record a request for the dependency-matrix companion without validating it against the output destination, and shall accept it together with the regeneration mode. A request with the report on standard output is not a usage error — it writes no file, which is what the companion rule says happens — and unlike the GraphML export it is not in conflict with regeneration, since a saved record carries the matrix.
    *Trace:* HLR-180 (The Matrix Written Beside the Report on Request), HLR-104, HLR-055.

*   <a id="LLR-CLI-30"></a>**LLR-CLI-30** — `cli_parse` shall accept the verbosity option together with a complete-record format rather than rejecting the pairing. It is the one option combination this parser decides that is not a usage error: there is no presentation for a verbosity to vary in a format defined as complete, so the request is honoured by changing nothing.
    *Trace:* HLR-152 (Complete-Record Formats Unaffected by Verbosity).

*   <a id="LLR-CLI-32"></a>**LLR-CLI-32** — `cli_parse` shall accept the five purification thresholds of HLR-171 — the sink authority and hub ranks, the god-object betweenness and hub ranks, and the core depth — defaulting each to `elc`'s own value where none is given, and shall reject as a usage error a rank above 100. The four rank thresholds are positions in an ordered distribution, so 100 is the ceiling and a larger figure names a position no function can occupy; accepting it would leave a run silently classifying nothing rather than telling the user their setting is impossible.
    *Trace:* HLR-171 (Purification Thresholds Are elc's Own), HLR-063, HLR-039.

*   <a id="LLR-CLI-33"></a>**LLR-CLI-33** — `cli_parse` shall record the purification manifest's path as given, borrowed from `argv`, and shall neither open nor validate the file — the module that reads a manifest owns the failure, and the parser stays the module that reads `argv` rather than becoming one that reads files. It shall record the requests for the manifest companion and for the pair of drawings without validating either against `--output`: a companion asked for with the report on standard output writes nothing rather than failing, by the rule the GraphML export already follows. A zeroed `ElcOptions` shall name no manifest and request neither companion, which is where the zero-configuration guarantee lives in this structure rather than in a comment.
    *Trace:* HLR-176 (The Manifest Is Read Only When Named), HLR-039, HLR-104, HLR-119, HLR-175, HLR-178.

*   <a id="LLR-CLI-34"></a>**LLR-CLI-34** — `cli_parse` shall reject a manifest read, a manifest write, or a request for the drawings combined with regeneration from a saved record. A record carries what a run concluded and not the graph it concluded it from, so there is nothing there to classify, to mask, or to draw; and a user who asked for a file and received none is told why rather than left to discover the absence — the rule the GraphML export already follows.
    *Trace:* HLR-122 (Companion Artefacts Absent in Regeneration), HLR-063.

*   <a id="LLR-CLI-35"></a>**LLR-CLI-35** — `cli_parse` shall answer a request for the version by writing the version of the build to standard output and ending the parse where it stands, reporting the outcome that ends the run successfully without analysis — the same outcome a request for the usage summary produces, since being asked a question is not an error (HLR-117, HLR-220).

    The answer shall be given **within the option loop, before any validation of the rest of the command line**, so that an invocation whose other arguments are wrong still answers it. That is the state a user is usually in when they are asked which version they are running, and a version option that first requires a working command line is one they cannot use then.

    The version shall be the one the build supplied and there shall be no default. Where the build supplies none, translation shall fail: a literal here that a build system also knows is a second place the version is written, and the one that is forgotten is never the one anybody reads.
    *Trace:* HLR-220 (The Version the Build Was Made As), HLR-117 (Help Request Is Not an Error).

## 3. `cli_usage` ([src/cli.c](../src/cli.c))

*   <a id="LLR-USG-01"></a>**LLR-USG-01** — `cli_usage` shall write a summary naming every accepted option, its argument if any, and its default value.
    *Trace:* HLR-117 (Help Request Is Not an Error).

*   <a id="LLR-USG-02"></a>**LLR-USG-02** — `cli_usage` shall write to the stream it is given, so that a help request may be directed to standard output and a usage error to standard error.
    *Trace:* HLR-117 (Help Request Is Not an Error), HLR-063 (Invalid Command-Line Rejection), HLR-038 (Diagnostics on stderr, Results on stdout).

*   <a id="LLR-USG-07"></a>**LLR-USG-07** — `cli_usage` shall document the linked-image option, including that no image means no filtering and that the option takes the path of an image rather than of a source tree.
    *Trace:* HLR-140 (Linked-Image Function Filter), HLR-117 (Help Request Is Not an Error).

*   <a id="LLR-USG-08"></a>**LLR-USG-08** — `cli_usage` shall emit the summary as more than one string literal, no single literal exceeding the 4095 characters ISO C11 requires a translation unit to support. The build treats a warning as a defect, so a summary that outgrew the guaranteed minimum would stop the build rather than degrade — and it outgrew it as soon as the option list reached this size.
    *Trace:* HLR-117 (Help Request Is Not an Error).

*   <a id="LLR-USG-09"></a>**LLR-USG-09** — `cli_usage` shall document the verbosity option, and shall state under `--output` that the extension of the named file selects the report format and that an unrecognised extension is a usage error rather than a guess. It shall also describe both compositions in its output summary, so that the reference the delivered documentation is checked against (LLR-DOC-04) states what a default run prints as well as which options exist.
    *Trace:* HLR-151 (Verbose Report on Request), HLR-148 (Output Format Determined by Filename Extension), HLR-130 (Documentation Currency), HLR-117 (Help Request Is Not an Error).

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

*   <a id="LLR-ROP-01"></a>**LLR-ROP-01** — `registry_open` shall resolve the runtime location from the dedicated environment variable when that variable is set, and otherwise from the first of an ordered set of paths relative to the executable that exists and is a directory.
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

*   <a id="LLR-ROP-09"></a>**LLR-ROP-09** — The paths `registry_open` tries relative to the executable shall include both the directory beside it and the installed layout the build's install target produces, and the location the install target writes the runtime to shall be one of them. HLR-059 requires a path *relative to* the executable rather than one adjacent to it, and the two are not the same: a tree of grammars and query files does not belong in a directory of executables, so an installed `elc` finds its runtime a level up and across rather than beside itself.
    *Trace:* HLR-059 (Runtime Location Discovery and Precedence), HLR-009, HLR-119.

*   <a id="LLR-ROP-10"></a>**LLR-ROP-10** — `registry_open` shall name every path it examined in the diagnostic for a runtime location it could not find, and shall report a location given through the environment variable against that path alone rather than falling back. The reader's next action is to place the runtime or to set the variable, and a message quoting one path they never chose sends them to the wrong directory.
    *Trace:* HLR-036 (Setup-Failure Fatality), HLR-059.

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

*   <a id="LLR-RLR-08"></a>**LLR-RLR-08** — `registry_load_rules` shall run before any file is discovered, so that a rule named on the command line which cannot be read or compiled fails the run without a file having been analysed.
    *Trace:* HLR-116 (Invalid Custom Rule File Handling).

*   <a id="LLR-RLR-09"></a>**LLR-RLR-09** — `registry_load_rules` shall split a `lang:path` argument at the first colon, so that an absolute path keeps any colon of its own.
    *Trace:* HLR-107 (User-Supplied Rule Queries).

*   <a id="LLR-RLR-10"></a>**LLR-RLR-10** — `registry_load_rules` shall read each language's rules directory once however many extensions map to that language, and shall load a language module only where a rule was actually found for it.
    *Trace:* HLR-107 (User-Supplied Rule Queries).

*   <a id="LLR-RLR-11"></a>**LLR-RLR-11** — `registry_load_rules` shall order the rule files it finds in a language directory by name, so that no property of the filesystem's layout reaches the reported order of matches.
    *Trace:* HLR-032 (Deterministic Output).

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

*   <a id="LLR-ANL-61"></a>**LLR-ANL-61** — `measure_damage` shall record every unparsable region into the debug companion, walking the tree separately from the line tally and only where a companion is open.

    Separately, because the two count different things: the tally coalesces overlapping regions so that a line a reader must look at is counted once, which is right for the figure and wrong for a log meant to reproduce a parser defect — two regions the grammar reported separately are two facts about the grammar. Only where a companion is open, because the walk is work whose sole purpose is to fill a file that may not exist (HLR-195).
    *Trace:* HLR-195 (Unparsable Source Recorded in the Debug Companion), HLR-035.

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

*   <a id="LLR-ANL-29"></a>**LLR-ANL-29** — `analyze_file` shall report a file it cannot map or whose parser produced no tree as a failure carrying no metrics, rather than as a file measuring zero. A file that measures zero and a file that could not be read are different facts, and reporting the second as the first hides a target from its own report.

    **This reverses the rule it replaces**, which said that any error node in the tree made the whole file a parse failure. LLR-ANL-48 states what happens now: a tree the grammar could partly follow is measured from the parts it could, and the damage is counted and reported beside the figures it qualifies. The reversal was made when a real project showed the cost — one macro-built `printf` discarded every metric in the file containing it — and this contract is what survived of the old one: the failure case is now the file with *no* tree rather than the file with a damaged one.
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

*   <a id="LLR-ANL-48"></a>**LLR-ANL-48** — `analyze_file` shall analyse a file whose syntax tree contains error nodes from the parts the grammar could follow, rather than discarding it, and shall record the number of distinct lines the error regions span. Two unparsable constructs on one line are one line, by the rule the comment exclusion already counts by.
    *Trace:* HLR-035 (Per-File Read- and Parse-Failure Tolerance), HLR-016.

*   <a id="LLR-ANL-49"></a>**LLR-ANL-49** — `analyze_file` shall report a partly unparsed file as an outcome distinct from both a clean analysis and a failure, so that its metrics are used and the run is still counted as degraded. A file measured around damage is not a clean run — something in it went unanalysed — and an exit status saying otherwise would not be truthful.
    *Trace:* HLR-035 (Per-File Read- and Parse-Failure Tolerance), HLR-037 (Truthful Exit Status).

*   <a id="LLR-ANL-50"></a>**LLR-ANL-50** — The diagnostic for a partly unparsed file shall state the line the damage begins at, how many lines it spans, and that the remainder was measured. The scale is the whole of what a reader needs in order to decide whether to trust the figures, and a message saying only that something failed withholds it.
    *Trace:* HLR-035 (Per-File Read- and Parse-Failure Tolerance), HLR-038.

*   <a id="LLR-ANL-34"></a>**LLR-ANL-34** — `analyze_file` shall assign the result of every reallocation to a temporary and verify it before overwriting the original pointer, so that a failed growth of the function array neither loses the existing allocation nor leaves a dangling pointer.
    *Trace:* HLR-124 (Memory Safety), HLR-125 (Complete Resource Release).

*   <a id="LLR-ANL-51"></a>**LLR-ANL-51** — `analyze_file` shall omit from its results every function the supplied image does not define, so that no later stage need know a filter was applied and no two consumers can disagree about which functions are in scope. The omission shall be by adding the function's whole extent to the excluded set the collectors already consult, rather than by dropping the function from the reported set alone: a function dropped without its bytes leaves its statements attributed to no function, which is to say counted as the file-scope figure HLR-145 keeps separate.
    *Trace:* HLR-144 (Scope of the Filter).

*   <a id="LLR-ANL-52"></a>**LLR-ANL-52** — `analyze_file` shall record no call site, decision point, or global access belonging to an omitted function, the function not being part of the measured program.
    *Trace:* HLR-144 (Scope of the Filter).

*   <a id="LLR-ANL-53"></a>**LLR-ANL-53** — `analyze_file` shall retain effective lines of code lying outside every function whether or not a filter is in force, and shall report their total separately when one is, the image saying nothing about code that is not a function.
    *Trace:* HLR-145 (Code Outside Any Function Retained and Separately Reported).

*   <a id="LLR-ANL-54"></a>**LLR-ANL-54** — `analyze_file` shall produce, with no image supplied, results identical to those it produces for the same file with the option absent.
    *Trace:* HLR-140 (Linked-Image Function Filter).

*   <a id="LLR-ANL-55"></a>**LLR-ANL-55** — `analyze_file` shall gather the comment spans and the inactive regions into one excluded set before any other collector runs, so that a function inside a region this configuration does not compile never reaches the report.
    *Trace:* HLR-132 (Inactive-Region Exclusion).

*   <a id="LLR-ANL-56"></a>**LLR-ANL-56** — `analyze_file` shall exclude from every metric and every graph fact each statement, decision point, call site, global access, dead-code span, and custom-rule match lying in an inactive region.
    *Trace:* HLR-132 (Inactive-Region Exclusion).

*   <a id="LLR-ANL-58"></a>**LLR-ANL-58** — `analyze_file` shall gather the functions the image does not define into the same excluded set, after the comment spans and the inactive regions and before any other collector runs. The order is load-bearing in both directions: the exclusion must be complete before anything consults it, and a function inside a region this configuration does not compile must not be reported as one the image failed to keep — that would answer a question about the linker with a fact about the preprocessor.
    *Trace:* HLR-144 (Scope of the Filter), HLR-143 (Both Directions of Mismatch Counted and Reported).

*   <a id="LLR-ANL-60"></a>**LLR-ANL-60** — `analyze_file` shall exclude, from a file the image's line information covers, each line within a function the image defines that produced no instruction, and shall count those lines. Where the image's line information does not cover the file, it shall exclude nothing on this account and shall mark the file as one whose coverage could not be established.

    The pass shall run **after** the comment, inactive-region, and absent-function exclusions, and shall skip a line already excluded by one of them: a line inside a comment, inside a region this configuration does not compile, or inside a function the linker discarded is already gone, and pruning it again would count it twice in a figure a reader is meant to act on.

    It shall be **confined to within functions the image defines**, which is why the absent-function pass hands back the kept extents as well. Code at file scope has few line entries to its name and is the one figure HLR-145 requires be kept separate and honest; a rule pruning uncovered lines everywhere would delete precisely that.

    A blank line shall be skipped rather than counted. It produces no instruction in any build, so its absence from the mapping says nothing about this one, and counting it would inflate the figure of HLR-155 with lines no measurement rested on.
    *Trace:* HLR-153 (Debug-Line Pruning From the Image), HLR-154 (Pruning Confined to Established Coverage), HLR-155 (Debug-Line Pruning Recorded and Reported), HLR-145 (Code Outside Any Function Retained and Separately Reported).

*   <a id="LLR-ANL-59"></a>**LLR-ANL-59** — `analyze_file` shall establish the absent set in a pass of its own over the function query rather than as a test inside the pass that records functions, since query matches arrive in no source order and a function nested inside an omitted one would otherwise be recorded before the omission that contains it was known.
    *Trace:* HLR-144 (Scope of the Filter), HLR-032 (Deterministic Output).

*   <a id="LLR-ANL-57"></a>**LLR-ANL-57** — `analyze_file` shall report results identical to those it produces with the option absent when no definition is supplied, save for regions a constant condition excludes, which are the same in every configuration.
    *Trace:* HLR-131 (Conditional-Compilation Configuration).

*   <a id="LLR-ANL-62"></a>**LLR-ANL-62** — `build_exclusions` shall not run the conditional query on a buffer the preprocessor expanded. The conditional figures for such a file are the ones the raw pass recorded before it was expanded.

    The pass that decides whether to expand at all already answers the question, on the source as written, because a preprocessor destroys the directives the answer is read from (HLR-208). Running the query again on the expanded text finds no region, counts none of the three dispositions, and overwrites a measurement with a zero.

    That was harmless while the only figure was the undecided count, which had to be zero for the file to have been expanded at all. It stopped being harmless when a region could be decided *from the image*: such a file expands, and the count saying so is the only record that a region was settled by evidence rather than by the source (HLR-211).
    *Trace:* HLR-211 (Conditional Regions Decided from the Image), HLR-208, HLR-133.

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

## 18. `collect_rule_matches` ([src/analyze.c](../src/analyze.c))

*   <a id="LLR-CRM-01"></a>**LLR-CRM-01** — `collect_rule_matches` shall record every match of every rule bound to the analysed file's language, and shall run no rule bound to another — a query compiled against one grammar has no meaning against a different node table.
    *Trace:* HLR-107 (User-Supplied Rule Queries).

*   <a id="LLR-CRM-02"></a>**LLR-CRM-02** — `collect_rule_matches` shall evaluate a rule's predicates by the same mechanism the built-in queries use, so that a filter a rule author wrote is applied rather than silently discarded.
    *Trace:* HLR-107 (User-Supplied Rule Queries).

*   <a id="LLR-CRM-03"></a>**LLR-CRM-03** — `collect_rule_matches` shall identify each match by the rule file's basename together with the capture name that matched, so that one file expresses several independently identified rules.
    *Trace:* HLR-109 (Custom Rule Match Reporting).

*   <a id="LLR-CRM-04"></a>**LLR-CRM-04** — `collect_rule_matches` shall record the line range each match spans, and shall attach no severity and no attribution to it — there is no judgement to record, and a column with nothing honest to put in it would invite one.
    *Trace:* HLR-109 (Custom Rule Match Reporting), HLR-111 (Custom Rules Carry No Built-In Opinion).

## 19. `collect_inactive_regions` ([src/analyze.c](../src/analyze.c))

*   <a id="LLR-CND-01"></a>**LLR-CND-01** — `collect_inactive_regions` shall determine which regions are active from the syntax tree already parsed, invoking no preprocessor, compiler, or build system and reading no file the source refers to.
    *Trace:* HLR-135 (No External Preprocessor).

*   <a id="LLR-CND-02"></a>**LLR-CND-02** — `collect_inactive_regions` shall take the constructs that introduce a region, the part that is its alternative, and what its condition tests from the language's runtime query configuration rather than from logic compiled into the executable.
    *Trace:* HLR-134 (Conditional Constructs Defined by Runtime Data).

*   <a id="LLR-CND-03"></a>**LLR-CND-03** — `collect_inactive_regions` shall exclude the alternative when a condition holds, everything preceding the alternative when it does not, and the whole region where there is no alternative.
    *Trace:* HLR-132 (Inactive-Region Exclusion).

*   <a id="LLR-CND-04"></a>**LLR-CND-04** — `collect_inactive_regions` shall treat a region whose condition it cannot decide as active in both branches, and shall count it, over-counting being visible where under-counting is not.
    *Trace:* HLR-133 (Undecidable Conditions Left Active).

*   <a id="LLR-CND-05"></a>**LLR-CND-05** — `collect_inactive_regions` shall treat a definedness test over a symbol no definition names as undecided rather than as false, a build being free to define it where `elc` cannot see.
    *Trace:* HLR-133 (Undecidable Conditions Left Active).

*   <a id="LLR-CND-06"></a>**LLR-CND-06** — `collect_inactive_regions` shall decide a region by the earliest query pattern that matched it, so that a file's specific patterns take precedence over its catch-all rather than the library's reporting order deciding.
    *Trace:* HLR-032 (Deterministic Output).

*   <a id="LLR-CND-07"></a>**LLR-CND-07** — `collect_inactive_regions` shall neither exclude again nor count as undecided a region lying inside a region already excluded, a region nobody builds having no condition worth reporting.
    *Trace:* HLR-133 (Undecidable Conditions Left Active).

*   <a id="LLR-CND-08"></a>**LLR-CND-08** — `collect_inactive_regions` shall exclude nothing for a language whose module supplies no conditional query, that language having no conditional compilation.
    *Trace:* HLR-134 (Conditional Constructs Defined by Runtime Data).

## 20. `graph_build` ([src/graph.c](../src/graph.c))

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

## 21. `arch_analyse` ([src/arch.c](../src/arch.c))

Component-level analyses over the SDG's component projection.

*   <a id="LLR-ARC-01"></a>**LLR-ARC-01** — `arch_analyse` shall identify as an architectural bottleneck every component whose afferent and efferent coupling are each greater than or equal to the configured bottleneck threshold.
    *Trace:* HLR-081 (Architectural Bottleneck Identification).

*   <a id="LLR-ARC-02"></a>**LLR-ARC-02** — `arch_analyse` shall mark the bottleneck threshold as `elc`'s own heuristic rather than a published standard wherever it is reported.
    *Trace:* HLR-081 (Architectural Bottleneck Identification), HLR-099 (Threshold Attribution).

*   <a id="LLR-ARC-03"></a>**LLR-ARC-03** — `arch_analyse` shall omit layering validation, and record the omission with its reason, when no architectural strata were declared.
    *Trace:* HLR-115 (Analyses Requiring User Declarations).

*   <a id="LLR-ARC-04"></a>**LLR-ARC-04** — `arch_analyse` shall emit a diagnostic when a declared stratum pattern matches no component, and shall retain the empty layer.
    *Trace:* HLR-078 (User-Declared Architectural Strata).

## 22. `compute_coupling` ([src/arch.c](../src/arch.c))

*   <a id="LLR-CPL-01"></a>**LLR-CPL-01** — `compute_coupling` shall compute, for every component, the number of components depending upon it as its afferent coupling.
    *Trace:* HLR-080 (Afferent and Efferent Coupling).

*   <a id="LLR-CPL-02"></a>**LLR-CPL-02** — `compute_coupling` shall compute, for every component, the number of components it depends upon as its efferent coupling.
    *Trace:* HLR-080 (Afferent and Efferent Coupling).

*   <a id="LLR-CPL-03"></a>**LLR-CPL-03** — `compute_coupling` shall treat a single source file as the unit of coupling.
    *Trace:* HLR-114 (Definition of a Component).

*   <a id="LLR-CPL-04"></a>**LLR-CPL-04** — `compute_coupling` shall count each depended-upon component once however many calls or shared objects connect the two, taking its figures from the de-duplicated component projection rather than from the call sites. Counting call sites would report a file that calls another in forty places as depending on forty things.
    *Trace:* HLR-080 (Afferent and Efferent Coupling), HLR-114.

## 23. `instability` ([src/arch.c](../src/arch.c))

*   <a id="LLR-INS-01"></a>**LLR-INS-01** — `instability` shall compute the Instability metric as efferent coupling divided by the sum of efferent and afferent coupling.
    *Trace:* HLR-082 (Instability Metric).

*   <a id="LLR-INS-02"></a>**LLR-INS-02** — `instability` shall report the metric as undefined, performing no division, when both couplings are zero.
    *Trace:* HLR-082 (Instability Metric).

*   <a id="LLR-INS-03"></a>**LLR-INS-03** — The reported Instability shall be attributed to its published source.
    *Trace:* HLR-099 (Threshold Attribution), HLR-082 (Instability Metric).

## 24. `find_cycles` ([src/arch.c](../src/arch.c))

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

## 25. `check_strata` ([src/arch.c](../src/arch.c))

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

*   <a id="LLR-LAY-06"></a>**LLR-LAY-06** — `check_strata` shall count, in the same pass that produces the violations, the call edges joining two components in different declared layers, and shall exclude from that count a global-state edge, an edge either of whose ends lies outside the declared partition, and an edge whose two ends lie in one layer. Counting it here rather than in a traversal of its own is what stops a second code path forming a second opinion about which edges the conformance indices are over: those three exclusions are the three tests this loop already makes before it decides whether to report anything.
    *Trace:* HLR-162 (Back-Call Violation Index), HLR-163 (Skip-Call Violation Index), HLR-164 (Indices Counted From the Reported Violations), HLR-161.

## 26. `calltree_analyse` ([src/calltree.c](../src/calltree.c))

Function-level call-tree measurements: width, height, the deepest stack, and recursion.

*   <a id="LLR-CTR-01"></a>**LLR-CTR-01** — `calltree_analyse` shall compute, for every function, the number of distinct subroutines it invokes directly.
    *Trace:* HLR-085 (Function Fan-Out Measurement).

*   <a id="LLR-CTR-02"></a>**LLR-CTR-02** — `calltree_analyse` shall detect direct and mutual recursion by decomposing the function graph, reporting every non-trivial strongly connected component and every self-loop as a recursive cycle.
    *Trace:* HLR-089 (Recursion Detection).

*   <a id="LLR-CTR-03"></a>**LLR-CTR-03** — `calltree_analyse` shall omit the depth and deepest-chain analyses, and record the omission with its reason, when no entry points were declared.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-087 (Maximum Call-Chain Depth).

*   <a id="LLR-CTR-04"></a>**LLR-CTR-04** — `calltree_analyse` shall report the recursive cycles in place of a depth figure, rather than a finite number or a non-terminating computation, when recursion is present.
    *Trace:* HLR-090 (Depth Reporting Under Recursion).

*   <a id="LLR-CTR-07"></a>**LLR-CTR-07** — `calltree_analyse` shall measure fan-out, fan-in, recursion, and call depth over the graph's call-edge view alone, never over the whole System Dependence Graph. A global-state edge records that one function writes an object another reads; it is coupling and not invocation. Counting one as a callee would inflate fan-out, counting one as a caller would inflate fan-in, following one would extend a call chain through a call that never happens, and a pair of functions sharing two objects in opposite directions forms a cycle in the SDG that is not recursion — which would be reported as a critical MISRA C Rule 17.2 violation against ordinary code.

    The rule governs fan-in as strictly as fan-out, and the report is where a breach shows: the two degrees sit side by side in one function table (HLR-183), so an in-degree taken over the whole SDG would count a reader of a global as a caller and put a function over the band of HLR-186 that no function calls that often.
    *Trace:* HLR-085, HLR-089, HLR-156 (Function Fan-In Measurement), HLR-074 (Global State Edges).

*   <a id="LLR-CTR-08"></a>**LLR-CTR-08** — `calltree_analyse` shall distinguish, in the omission it records, between no entry points having been declared and declared entry points naming no analysed function, since the two call for different actions from the reader. A declared symbol that matches no analysed function shall be diagnosed and skipped rather than ending the run: analysing one directory of a project whose entry point is defined in another is ordinary use, and rejecting it would make the option unusable there.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-095 (User-Declared Entry Points).

*   <a id="LLR-CTR-09"></a>**LLR-CTR-09** — `calltree_analyse` shall continue to produce the measurements that need no user declaration — fan-out and recursion — when the depth analysis is omitted for want of one. Omitting an analysis is not a reason to omit its neighbours, and a report that fell silent about recursion because no entry point was declared would withhold the finding that matters most on a stack-constrained target.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-089.

*   <a id="LLR-CTR-05"></a>**LLR-CTR-05** — `calltree_analyse` shall compute the maximum call-chain depth reachable from the declared entry points when the function graph is acyclic.
    *Trace:* HLR-087 (Maximum Call-Chain Depth).

*   <a id="LLR-CTR-06"></a>**LLR-CTR-06** — `calltree_analyse` shall report the maximum depth together with the count of unresolved calls, since a chain continuing through an unresolved call is not followed and the depth is therefore a lower bound.
    *Trace:* HLR-087 (Maximum Call-Chain Depth), HLR-077 (Unresolvable Call Handling).

*   <a id="LLR-CTR-10"></a>**LLR-CTR-10** — `calltree_analyse` shall compute, for every function, the number of *distinct* functions that invoke it directly, counted as the in-degree over the call-edge view. The graph is simple, so a caller invoking a function at forty call sites contributes one; it is the converse of LLR-CTR-01 and is counted by the same traversal, reading each call edge from its other end rather than walking the edge table a second time.

    A function that no analysed function calls has a fan-in of zero, and that zero is recorded as a measurement rather than as a finding: an entry point, an exported API boundary, and an interrupt handler reached from a vector table all legitimately have none.
    *Trace:* HLR-156 (Function Fan-In Measurement), HLR-085 (Function Fan-Out Measurement).

*   <a id="LLR-CTR-13"></a>**LLR-CTR-13** — `calltree_analyse` shall accumulate a **Weighted Fan-Out** for every function in the same traversal of the call edges that already accumulates fan-out and fan-in (HLR-222). For each resolved call edge, the Mock Burden Score of the node the edge points at shall be added to the weighted fan-out of the node it leaves.

    **One traversal and not two, for the reason the existing pass gives for fan-in and fan-out.** The three degrees are the same walk of the same edge table read three ways, and a second traversal would be a second place the `kind == EDGE_CALL` test could be forgotten — leaving a function whose weighted fan-out counted edges its fan-out did not.

    An unresolvable call site contributes to neither degree, since it produces no edge to walk (HLR-077). A function with no resolved outgoing call shall therefore have a weighted fan-out of zero, which is the same value a function calling nothing has, and the two are not distinguished here: the unresolved count `graph_unresolved_count` reports is what tells them apart, and it is already presented beside these measurements.

    The accumulation shall be in floating point and shall not be rounded before the index of LLR-WTB-01 consumes it, so that four calls to functions scoring `0.25` sum to `1.0` and not to zero.
    *Trace:* HLR-222 (Weighted Fan-Out), HLR-077.

## 27. `longest_path_dag` ([src/calltree.c](../src/calltree.c))

*   <a id="LLR-LPD-01"></a>**LLR-LPD-01** — `longest_path_dag` shall compute the longest path from the declared entry points by memoised traversal in reverse topological order.
    *Trace:* HLR-087 (Maximum Call-Chain Depth).

*   <a id="LLR-LPD-04"></a>**LLR-LPD-04** — `longest_path_dag` shall count the entry point itself as the first layer, so that an entry point calling nothing has a depth of one rather than zero, and shall resolve a tie between two chains of equal length in favour of the lower node identifier — which is sorted-file order — so that equal candidates yield the same report on every run.
    *Trace:* HLR-087 (Maximum Call-Chain Depth), HLR-032.

*   <a id="LLR-LPD-02"></a>**LLR-LPD-02** — `longest_path_dag` shall retain the predecessor of each node so that the deepest chain can be reconstructed.
    *Trace:* HLR-088 (Deepest Call Stack Reported in Full).

*   <a id="LLR-LPD-03"></a>**LLR-LPD-03** — `longest_path_dag` shall return the ordered sequence of functions from entry point to deepest leaf, and not merely the depth as a number.
    *Trace:* HLR-088 (Deepest Call Stack Reported in Full).

## 28. `state_analyse` ([src/state.c](../src/state.c))

Global-state coupling, execution-scope isolation, and reachability.

*   <a id="LLR-STA-01"></a>**LLR-STA-01** — `state_analyse` shall omit the reachability analysis, and record the omission with its reason, when no entry points were declared, and shall in no circumstance report every function as unreachable for want of a declaration.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-096 (Dead Code Detection by Reachability).

*   <a id="LLR-STA-02"></a>**LLR-STA-02** — `state_analyse` shall omit the execution-scope isolation analysis, and record the omission with its reason, when no execution scopes were declared.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-094 (Memory Map Boundary Validation).

*   <a id="LLR-STA-03"></a>**LLR-STA-03** — `state_analyse` shall compute reachability over the call view of the graph and not over the whole SDG. A global-state edge joins a function that writes an object to one that later reads it, and writing a variable another function reads is not calling it: control never travels along that edge, so a function reachable only through one has not been reached. Following it would quietly rescue genuinely dead code from the report, which is the error this analysis exists to avoid making in the other direction.
    *Trace:* HLR-096 (Dead Code Detection by Reachability), HLR-097, HLR-074.

*   <a id="LLR-STA-04"></a>**LLR-STA-04** — `state_analyse` shall perform the global-access mapping whether or not any declaration was supplied, so that omitting one analysis for want of a declaration does not omit its neighbours.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-091.

## 29. `classify_globals` ([src/state.c](../src/state.c))

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

## 30. `collect_roots` ([src/state.c](../src/state.c))

*   <a id="LLR-RTS-01"></a>**LLR-RTS-01** — `collect_roots` shall form the reachability root set as the union of the declared entry points and every function whose address is taken without being directly called.
    *Trace:* HLR-096 (Dead Code Detection by Reachability), HLR-095 (User-Declared Entry Points).

*   <a id="LLR-RTS-02"></a>**LLR-RTS-02** — `collect_roots` shall include address-taken functions because they may be invoked indirectly, so that a callback or interrupt handler is never reported as unreachable merely for want of a direct call.
    *Trace:* HLR-096 (Dead Code Detection by Reachability), HLR-097 (Dead Code Determined by Graph Mathematics).

## 31. `reachability` ([src/state.c](../src/state.c))

*   <a id="LLR-RCH-01"></a>**LLR-RCH-01** — `reachability` shall traverse the graph forward from the root set and report every function not visited as unreachable.
    *Trace:* HLR-096 (Dead Code Detection by Reachability).

*   <a id="LLR-RCH-02"></a>**LLR-RCH-02** — `reachability` shall establish unreachability solely by graph traversal, using no textual or heuristic means.
    *Trace:* HLR-097 (Dead Code Determined by Graph Mathematics).

*   <a id="LLR-RCH-03"></a>**LLR-RCH-03** — `reachability` shall report as unreachable a group of unused functions that call one another, since no path reaches the group from any root.
    *Trace:* HLR-097 (Dead Code Determined by Graph Mathematics).

## 32. `unreachable_globals` ([src/state.c](../src/state.c))

*   <a id="LLR-UGL-01"></a>**LLR-UGL-01** — `unreachable_globals` shall report as unreachable every global object accessed solely by functions that are themselves unreachable.
    *Trace:* HLR-096 (Dead Code Detection by Reachability).

*   <a id="LLR-UGL-02"></a>**LLR-UGL-02** — `unreachable_globals` shall not report as unreachable an object that no analysed function accesses. Such an object may be touched from file scope, from a language whose global captures record nothing, or from a translation unit outside the target, and the asymmetry that governs the functions governs the storage: an object wrongly called dead invites deleting memory something writes.
    *Trace:* HLR-096 (Dead Code Detection by Reachability), HLR-138.

## 33. `check_scopes` ([src/state.c](../src/state.c))

*   <a id="LLR-ISO-01"></a>**LLR-ISO-01** — `check_scopes` shall report every call edge and every global-state edge by which one declared execution scope reaches a function or object belonging to another.
    *Trace:* HLR-094 (Memory Map Boundary Validation).

*   <a id="LLR-ISO-02"></a>**LLR-ISO-02** — `check_scopes` shall treat a component matching no declaration as lying outside the partition rather than in a scope of its own, so that an edge touching it is not a crossing. The user said nothing about it, and inventing a boundary would report violations against a division nobody drew.
    *Trace:* HLR-094 (Memory Map Boundary Validation), HLR-115.

## 34. `thresholds_apply` ([src/thresholds.c](../src/thresholds.c))

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

*   <a id="LLR-THR-11"></a>**LLR-THR-11** — The catalogue shall hold every band as data in one table, and no other module shall band a measurement or cite the published source a band rests on. The project's claim to carry no opinion of its own is checkable only if a reviewer can read one table rather than audit every analysis for a constant; banding spread across the analyses would leave the claim resting on nobody having hidden one.

    **Citing a band's source is what is confined here, not naming a measure.** A renderer naming the metric it presents — "Instability (I = Ce/(Ce+Ca), Martin)" in a heading — is naming the authority a number is computed after, which is a different thing from citing an authority for a line drawn and is a required one. What may not happen anywhere but the catalogue is a threshold: a constant that decides a severity, and the citation that justifies it.
    *Trace:* HLR-098 (Evaluation Against Published Thresholds), HLR-099, HLR-111.

*   <a id="LLR-THR-12"></a>**LLR-THR-12** — `thresholds_apply` shall band only a measurement that was made, and shall not treat an omitted one as a value. A depth omitted for want of an entry point is not a depth of zero, and banding it would judge a number that does not exist.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-098.

*   <a id="LLR-THR-13"></a>**LLR-THR-13** — The report shall carry the findings ranked by descending severity, then by measurement and subject, and shall present them beside the measurements rather than in place of them. A value inside its accepted band stays in the table that measured it; the findings list is the subset a reader acts on, and its emptiness is a result rather than a missing section.
    *Trace:* HLR-031 (Uniform Report Composition), HLR-123, HLR-032.

*   <a id="LLR-THR-14"></a>**LLR-THR-14** — The record shall carry each finding with its severity, subject, detail and source. Regeneration has no measurements to band and no catalogue call to make against them, so a finding not written is one the regenerated report cannot have.
    *Trace:* HLR-054 (Complete Run Record), HLR-056, HLR-099.

*   <a id="LLR-THR-16"></a>**LLR-THR-16** — The catalogue shall hold a row banding cyclomatic complexity — warning above 10, critical above 15, attributed to McCabe as NIST SP 500-235 records the two limits — and a row banding fan-in, warning above 25 with no critical band, marked as `elc`'s own and carrying the same label the bottleneck row carries.

    The complexity row's bounds shall be constants of this catalogue and shall not be the value `--complexity-threshold` sets. That value governs a listing and carries no severity; a build in which moving it moved a severity would be letting the user choose what McCabe says, which would make the row's attribution false.

    The fan-in row's critical bound shall be the maximum a measurement can take, so that the counted classification's critical test can never fire and the row needs no special case in the code that reads the catalogue.
    *Trace:* HLR-185 (Cyclomatic Complexity Threshold Classification), HLR-186 (Fan-In Threshold Classification), HLR-099 (Threshold Source Attribution), HLR-023 (Threshold List is Reporting-Only).

*   <a id="LLR-THR-17"></a>**LLR-THR-17** — `thresholds_band` shall report where one counted value falls in its kind's band without building a finding for it, so that the report's threshold listing can name the functions a band names without keeping bounds of its own. It shall report that there is no band where the catalogue holds no row for the kind, and equally where the row's finding is its mere occurrence: such a row carries a fixed severity and both bounds zero, and putting a counted value through it would report every non-zero count as critical.

    `thresholds_apply` shall emit a finding for every function whose cyclomatic complexity, fan-in or fan-out falls outside its band, each naming the function, the file and line it is defined at, the value measured, and the source the band came from.

    Complexity shall be banded from the graph's own node table rather than from a second walk of the report model. The table carries the complexity `analyze.c` measured for every function in the report, so banding there cannot disagree with the fan-out finding beside it about which functions exist.
    *Trace:* HLR-185 (Cyclomatic Complexity Threshold Classification), HLR-186 (Fan-In Threshold Classification), HLR-187 (The Threshold Listing Names Every Banded Function), HLR-098 (Evaluation Against Published Thresholds).

*   <a id="LLR-THR-19"></a>**LLR-THR-19** — The catalogue shall hold a row for MISRA library use, occurrence-governed at warning severity and attributed to `MISRA C:2012`; and `thresholds_apply` shall emit one finding per unresolved call site whose callee names a facility MISRA C:2012 §21 forbids, stating the function and the rule number.

    **The table shall be keyed by function, and shall be readable as a table.** MISRA constrains functions, not headers: `<stdlib.h>` supplies `abs`, which is permitted, beside `malloc`, which is not, so a rule keyed on the include would be a false claim about code that called neither. Each entry carries its rule number so a reviewer can check the table against the standard by reading it — the property the whole of this module is arranged around (HLR-099).

    **Per site, not per function or per file.** A reader fixing one needs the line, and a function calling `malloc` twice has two things to change.

    Read from the unresolved calls of HLR-077 because that is where a call into the C library lands. A project supplying its own definition of a constrained name resolves it and is not reported, which is correct: the rule is about the standard library's function and not about every function sharing its spelling.

    No bound shall be invented. MISRA states no count at which use becomes unacceptable, and one `elc` chose would be its own opinion wearing MISRA's name — so occurrence governs, the severity is warning throughout, and no finding reaches the exit status (HLR-100, HLR-101).
    *Trace:* HLR-207 (C Library Use Outside MISRA Constraints Reported), HLR-099 (Threshold Source Attribution), HLR-077.

*   <a id="LLR-THR-20"></a>**LLR-THR-20** — `thresholds_apply` shall classify each function's Weighted Test Burden Index into the bands of HLR-224 — Critical at 45 or above, Warning at 20 or above, and Healthy below 20 — and shall test them in descending order, so that an index of 45 yields one Critical finding and not a Critical and a Warning both.

    The bounds shall live in the one threshold catalogue this module already holds rather than in a table beside it, for the reason every other band in it is there: a catalogue that some measurements are in and others are not is a catalogue a reader cannot trust to be complete.

    The band shall carry the *elc heuristic — not a published standard* label (HLR-099). The index is `elc`'s own and unpublished, so no published calibration describes it, and a citation would put an opinion of `elc`'s own under another name.

    A high value is the bad one here, so the comparison shall be the ordinary one. The catalogue carries an `inverted` flag because the retired Adapted Maintainability Index ran the other way; no row runs that way now, and the flag stays because a measurement that does is a measurement the catalogue must be able to hold rather than one it must be rewritten for.
    *Trace:* HLR-224 (Testing Burden Threshold Classification), HLR-099 (Threshold Attribution).

## 35. `report_assemble` ([src/report.c](../src/report.c))

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

*   <a id="LLR-RPT-30"></a>**LLR-RPT-30** — The report shall carry each file's unparsed-line count, shall total it across the project in the summary, and shall list the partly parsed files in a section of their own. The total sits beside the figures it qualifies rather than below them, because a reader comparing an ELOC against a line count of their own must know of the shortfall before they start looking for its cause; and the per-file list is a section rather than a column because the count is zero for almost every file in almost every project, and a column of zeros hides the rows that matter.
    *Trace:* HLR-035 (Per-File Read- and Parse-Failure Tolerance), HLR-031, HLR-054.

*   <a id="LLR-RPT-28"></a>**LLR-RPT-28** — The report shall resolve a dead-code span to its enclosing function by containment over the assembled model, rather than by the index the parse recorded. The index is into the array the parse produced, and that array has since been ordered for presentation; reading the index would name the right function only for as long as the two orders happened to agree. The rule applied is the one the parse applied — the narrowest reported function containing the span.
    *Trace:* HLR-137 (Intra-Procedural Dead Code Detection), HLR-068, HLR-032.

*   <a id="LLR-RPT-29"></a>**LLR-RPT-29** — The report shall order the cross-scope crossings by the boundary crossed and then by the functions at either end, and the global objects by name, so that no collection reaching a renderer carries the order a traversal happened to produce.
    *Trace:* HLR-032 (Deterministic Output), HLR-033.

*   <a id="LLR-RPT-16"></a>**LLR-RPT-16** — `report_assemble` shall grow every dynamic collection through a checked reallocation, and shall release the partially built model without leaking should any growth fail.
    *Trace:* HLR-124 (Memory Safety), HLR-125 (Complete Resource Release).

*   <a id="LLR-RPT-31"></a>**LLR-RPT-31** — `report_assemble` shall carry the source functions the image does not define, sorted by file, then by start line, then by name as every other collection is, together with the total of the effective lines belonging to no function. Both are properties of the measurement and are assembled with it; the image itself and the count of names left unresolved are recorded by `report_set_image`, the run holding those before any file is measured.
    *Trace:* HLR-143 (Both Directions of Mismatch Counted and Reported), HLR-147 (Filter Recorded and Reported).

*   <a id="LLR-RPT-32"></a>**LLR-RPT-32** — `report_assemble` shall carry the custom-rule matches sorted by file, then by the line a match starts at, then by its end, then by rule identity — the last key because two rules matching one node are two rows whose order would otherwise be the order the rules loaded.
    *Trace:* HLR-109 (Custom Rule Match Reporting), HLR-032 (Deterministic Output).

*   <a id="LLR-RPT-33"></a>**LLR-RPT-33** — `report_assemble` shall carry the definitions in force, sorted, and the count of undecided regions summed over every file, so that a report states the configuration it describes and how completely it could be applied.
    *Trace:* HLR-136 (Configuration Recorded and Reported), HLR-133 (Undecidable Conditions Left Active).

*   <a id="LLR-RPT-34"></a>**LLR-RPT-34** — `report_set_image` shall record on the assembled report the image the run was filtered by and the number of its linkage names left unresolved, so that a report states which image it describes and how completely that image could be read. It is set after assembly rather than passed into it for the reason the unresolved-call count is: the image is read before any file is measured and belongs to the run, while the effects of the filter belong to the measurement.
    *Trace:* HLR-147 (Filter Recorded and Reported), HLR-143 (Both Directions of Mismatch Counted and Reported).

*   <a id="LLR-RPT-36"></a>**LLR-RPT-36** — `report_set_purify` shall copy onto the assembled report one row per function purification classified, resolving each node identifier to the function's name and location and carrying the metric that triggered the classification, the value it took, and the action taken; and shall order the rows by file, then by the line the function starts on. A function purification classified as ordinary shall carry no row, since HLR-174 asks for the classifications that were made and `elc` concluded nothing about that function. No row shall carry a severity, there being nothing for one to mean.
    *Trace:* HLR-174 (Purification Reported Before It Is Relied On), HLR-171, HLR-032.

*   <a id="LLR-RPT-37"></a>**LLR-RPT-37** — `report_set_purify` shall record on each row whether the class it carries was computed by `elc` or stated by a manifest, and shall carry a row for a function a manifest classified as ordinary. A reader of this section is being asked to judge whether the masking was right, and cannot do that without knowing which rows are the tool's own reading of the graph and which are their team's correction of it; and a manifest that returned a function to ordinary overruled `elc` there, which a reader who sees no row learns nothing about.
    *Trace:* HLR-177 (A Manual Classification Overrides a Computed One), HLR-174.

*   <a id="LLR-RPT-38"></a>**LLR-RPT-38** — `report_attach_flow` shall copy each flow row's fan-in and fan-out onto the function metric it describes, locating that function by file path, start line and name, and shall then rebuild the threshold listing over the joined result. It shall be the only place either is done, and both the live path and the path regenerating from a record shall call it — `report_assemble` works over per-file metrics that carry no degree, so a listing built there is a listing built before two of its three inputs exist.

    The pair identifies the definition and neither half suffices: two `static` functions in two translation units may share a name, and a nested function declared on the line its enclosing body opens shares a start line with it. The search shall therefore locate the file by path, the block of functions at that start line, and the one within it bearing the name. A flow row naming no function in the model shall be dropped rather than diagnosed: a live run cannot produce one, since the graph is built from these same metrics, and a hand-written record describing a function it does not define is the record's defect rather than a reason to refuse the rest of it.
    *Trace:* HLR-183 (One Per-Function Table), HLR-187 (The Threshold Listing Names Every Banded Function), HLR-156 (Function Fan-In Measurement), HLR-056 (Regeneration Fidelity).

*   <a id="LLR-RPT-39"></a>**LLR-RPT-39** — `report_assemble` shall build the threshold listing as the union of two rules: a function whose cyclomatic complexity is at or above the configured threshold, and a function whose complexity, fan-in or fan-out `thresholds_band` places in a warning or critical band. A function satisfying both shall appear once, carrying the highest severity any band gave it; a function present only because it met the configured threshold shall carry no severity at all.

    The bands shall be read from `thresholds.c` rather than from constants held here. That module is the only place a line is drawn, and a listing drawing its own would be a second opinion wearing the first's name.
    *Trace:* HLR-187 (The Threshold Listing Names Every Banded Function), HLR-021 (Per-File Complexity-Threshold List), HLR-023 (Threshold List is Reporting-Only), HLR-099 (Threshold Source Attribution).

*   <a id="LLR-RPT-40"></a>**LLR-RPT-40** — `report_attach_flow` shall derive each function's Weighted Test Burden Index onto the per-function metrics once the flow degrees have been joined, and shall be the only place it is derived.

    One derivation, where the Adapted Maintainability Index that stood here needed two. That index had to be derived before the threshold listing was built *and* again with the degrees, because its unset zero was not a neutral figure but the worst score on its scale, and a listing built between the two would have called every function in the project critically unmaintainable. This index runs the other way: zero is the bottom of its range and is what a function with no measured degrees honestly has, so a single derivation at the point the degrees become real is both sufficient and correct.
    *Trace:* HLR-223 (Weighted Test Burden Index per Function), HLR-187 (The Threshold Listing Names Every Banded Function).

*   <a id="LLR-RPT-41"></a>**LLR-RPT-41** — `report_check_image_ambiguity` shall fail a filtered run in which some function name is defined by two or more analysed files, is defined by the image, and is not held by the image's origin map — writing a diagnostic that names the function, both files, the image, and the remedy, and producing no report.

    All three conditions are required. A name the image does not define is excluded from both files whichever was linked, so the ambiguity changes nothing a reader would see; a name the debug information places needs no guess; and a name defined once has nothing to be ambiguous between.

    It shall run over the assembled model rather than during the parse, because it is a question about the whole project and one file's parse cannot see the other definition. The analysis already done is discarded, which is the right trade: a filtered figure resting on a guess is indistinguishable from a correct one, and that is what makes it worse than no figure at all.

    **Which remedy the diagnostic states shall be decided by `dwarfline_any`, not by the lookup that failed.** An image holding no origins at all is missing debug information and is fixed by rebuilding it with `-g`; an image holding origins but none under this name was built with `-g`, and the remedy concerns the translation unit that defined the function, or the fact that no definition was emitted. Inferring the first from a failed lookup is how `elc` came to advise rebuilding images that already carried what it said they lacked (HLR-201).
    *Trace:* HLR-193 (Image Symbols Placed by Debug Information), HLR-201 (A Diagnostic States the Condition It Observed), HLR-120.

## 36. `format_table` ([src/format_text.c](../src/format_text.c))

*   <a id="LLR-TBL-01"></a>**LLR-TBL-01** — `format_table` shall render the report as an aligned, human-readable table, computing column widths from the longest path and function name.
    *Trace:* HLR-027 (Default Human-Readable Output).

*   <a id="LLR-TBL-02"></a>**LLR-TBL-02** — `format_table` shall be the format used when the user selects none.
    *Trace:* HLR-027 (Default Human-Readable Output).

*   <a id="LLR-TBL-03"></a>**LLR-TBL-03** — `format_table` shall write only results to the results stream, and no diagnostic.
    *Trace:* HLR-038 (Diagnostics on stderr, Results on stdout).

## 37. `format_markdown` ([src/format_text.c](../src/format_text.c))

*   <a id="LLR-MKD-01"></a>**LLR-MKD-01** — `format_markdown` shall render the report as GitHub-Flavored Markdown, grouping functions under a heading for the file containing them.
    *Trace:* HLR-029 (Markdown Output).

## 38. `render_summary` ([src/format_text.c](../src/format_text.c))

*   <a id="LLR-SUM-01"></a>**LLR-SUM-01** — `render_summary` shall present the project summary, the discovery route of each directory target, each file's totals and threshold list, the full per-function detail, the architectural measurements and findings — including measurements falling within their accepted bands — any custom-rule matches, the skipped-file list, and any omitted analysis with its reason, in every report format other than CSV, XML, and the `.dot` companion. That enumeration is the composition of the **verbose** report; which of those tiers a given run reaches is settled by LLR-SUM-09, and the uniformity this requirement states holds at whichever verbosity is in force.
    *Trace:* HLR-031 (Uniform Report Composition Across Formats), HLR-127 (Discovery Route Reported), HLR-012 (Unsupported-Language File Handling), HLR-115 (Analyses Requiring User Declarations).

*   <a id="LLR-SUM-03"></a>**LLR-SUM-03** — `render_summary` shall emit every tier from one traversal shared by both human-facing formats, so that a tier cannot be present in one format and absent from the other.
    *Trace:* HLR-031.

*   <a id="LLR-SUM-04"></a>**LLR-SUM-04** — The aligned format shall not pad a left-aligned final column, so that no line carries trailing whitespace.
    *Trace:* HLR-027, HLR-032.

*   <a id="LLR-SUM-05"></a>**LLR-SUM-05** — `render_summary` shall present the definitions in force and the count of undecided regions in every format that presents the project summary, since a metric whose value depends on a configuration is not interpretable without it.
    *Trace:* HLR-136, HLR-031.

*   <a id="LLR-SUM-02"></a>**LLR-SUM-02** — `render_summary` shall traverse the report model in a single shared order for both the table and Markdown renderers, so that the two present the same tiers.
    *Trace:* HLR-031 (Uniform Report Composition Across Formats).

*   <a id="LLR-SUM-06"></a>**LLR-SUM-06** — `render_summary` shall present the image a run was filtered by, the unresolved-linkage-name count, the effective lines belonging to no function, the count of source lines this build compiled no instruction for, the count of analysed files whose debug coverage could not be established, and the source functions the image does not define, in every report format other than CSV, XML, and the `.dot` companion.

    The two line-granularity counts shall appear whether or not the image carried line information: two zeroes state that nothing was pruned and nothing was uncoverable, which is a different claim from a section that omits the question. They are read as the unresolved-call count and the undecided-region count are read — the first states what the filter removed, the second states where it could not look (HLR-155). These sections alone shall be emitted only where a filter was in force, which is the one exception to the rule that every section appears whether or not it has rows: an unfiltered run must report exactly what it reported before the option existed, and an empty section is not nothing.

    The two are separate sections, because the tier boundary of LLR-SUM-09 runs between them: the image and its two counts are the provenance of a filtered run and are a summary tier, while the functions the image does not define are one row per function and are a detail tier. They are no longer adjacent either: the provenance stays among the summary tiers, where a reader meets it before the figures it qualifies, and the list of absent functions closes the report (HLR-184).
    *Trace:* HLR-143 (Both Directions of Mismatch Counted and Reported), HLR-155 (Debug-Line Pruning Recorded and Reported), HLR-031 (Uniform Report Composition Across Formats).

*   <a id="LLR-SUM-07"></a>**LLR-SUM-07** — `render_summary` shall present the custom-rule matches in a section of their own, with no severity column and no source column, and shall emit that section whether or not any rule was supplied.
    *Trace:* HLR-109 (Custom Rule Match Reporting), HLR-111 (Custom Rules Carry No Built-In Opinion), HLR-031 (Uniform Report Composition Across Formats).

*   <a id="LLR-SUM-08"></a>**LLR-SUM-08** — `render_summary` shall present the definitions in force and the undecided-region count, and shall emit the definitions section whether or not any definition was supplied.
    *Trace:* HLR-136 (Configuration Recorded and Reported), HLR-031 (Uniform Report Composition Across Formats).

*   <a id="LLR-SUM-09"></a>**LLR-SUM-09** — `render_summary` shall classify each tier of the traversal as a **summary** or a **detail** tier, and shall emit, at the summary verbosity, the summary tiers alone; at the verbose verbosity it shall emit every tier. The classification shall be a property of the one shared traversal — a filter over the ordered section list both human-facing formats walk — rather than a second traversal beside it, so that a tier cannot be present at one verbosity and forgotten at the other, by the same construction that stops it being present in one format and forgotten in the other (LLR-SUM-02).

    The summary tiers are the project summary and callouts, the discovery route, the per-language breakdown, each file's own totals, the threshold listing, the findings, the definitions in force, the linked-image provenance, the partly parsed files, and the skipped files. Every remaining tier is a detail tier: it enumerates one row per analysed entity — per function, per global object, per unreachable statement, per graph edge, per custom-rule match. The measurements taken over the dependence graph — the per-function table, recursion, the deepest chain, coupling, dependency cycles, layering — are detail tiers on that rule, each row naming one entity of the graph rather than a file's own totals; nothing is thereby lost from the summary, because every one of them that crossed a published line is a finding and the findings are a summary tier, and because a per-function measurement with a project-level total puts that total in the project summary.

    A detail section whose analysis was **omitted for want of a declaration** shall be reached at the summary verbosity as well, since HLR-150 counts the omission notices among the summary tiers. An omitted analysis produced no rows, so since HLR-188 the section presents nothing and the notice reaches the reader through the closing statement, which names it by the heading carrying the reason. The classification still governs whether it is reached at all: a detail section a summary run filtered out is neither presented nor named, and would state its reason nowhere.
    *Trace:* HLR-150 (Summary Report by Default), HLR-151 (Verbose Report on Request), HLR-031 (Uniform Report Composition Across Formats), HLR-115 (Analyses Requiring User Declarations).

*   <a id="LLR-SUM-11"></a>**LLR-SUM-11** — `render_summary` shall present the two conformance indices as a summary tier — each with its violating proportion, its complementary conforming proportion, and the inter-layer call-edge count it is over — and the dependency matrix as a detail tier, in both human-facing formats and from one entry apiece in the ordered section list. The indices are project-level aggregates, two rows whatever the size of the project; the matrix enumerates one row per subject, and falls on the detail side by the same rule that puts every other table growing with the graph there.

    The matrix's decoration is delegated to the renderer that owns the grid rather than built into a `Grid`, its column count being the number of subjects rather than a fixed few and its cells needing the Markdown separator escaped; the delegation is two calls from one section entry, so the tier is still written down once and classified once.

    The tier shall present the indices only where the model carries them rendered, rather than where the strata state says they were measured. `STRATA_MEASURED` is the zero of its enum, so a model holding no indices at all reads as measured while having nothing to print, and a renderer presents what the model has rather than what its state implies it should have.
    *Trace:* HLR-162 (Back-Call Violation Index), HLR-163 (Skip-Call Violation Index), HLR-166 (Matrix Ordering, the Diagonal, and Its Renderings), HLR-150, HLR-031.

*   <a id="LLR-SUM-12"></a>**LLR-SUM-12** — `render_summary` shall present the classifications purification made as a **detail** tier in both human-facing formats, one row per classified function naming the class, the metric and value that triggered it, and the action taken; and shall state in the section's heading that the thresholds are `elc`'s own heuristic rather than a published standard, together with the five values in force and what the masking left behind. The section shall carry no severity column. It is written to the results destination like every other section: HLR-038 reserves standard output, and a run redirecting its report to a file must not have a second report appear on the terminal.
    *Trace:* HLR-174 (Purification Reported Before It Is Relied On), HLR-171, HLR-150, HLR-038, HLR-031.

*   <a id="LLR-SUM-13"></a>**LLR-SUM-13** — `render_summary` shall present the recovered layering as a detail tier in both human-facing formats, stating in its heading that what follows is a proposal and never the baseline conformance is measured against, and shall present the proposal itself as the argument list `--stratum` and `--stratum-order` accept rather than as prose. A table of layers printed under an architecture report is otherwise easy to read as a verdict; and rendering the proposal as arguments is what makes adoption a copy rather than a transcription, and is the boundary HLR-173 draws in the one form a reader cannot mistake for a measurement. Where no layering could be read the heading shall say which of the two reasons applied, and where the view was cyclic the mutually reachable groups shall be listed in place of the layers.
    *Trace:* HLR-173 (A Recovered Layering Is a Proposal, Never a Baseline), HLR-172, HLR-150, HLR-031.

*   <a id="LLR-SUM-14"></a>**LLR-SUM-14** — `render_summary` shall present every analysed function in one table carrying its file, name, line range, effective lines, cyclomatic complexity, fan-in and fan-out, and shall present no second table enumerating one row per function for any of those measurements.

    The table shall be driven by the per-file function metrics rather than by the flow rows. The rows exist only where a graph was built and the metrics exist because the file was parsed, so a table driven by the rows would present no functions at all on a run whose graph was not built — which is exactly the run whose per-function figures a reader still wants.

    The table shall carry no severity column. Which functions a band named is the threshold listing's subject and the findings', and a severity repeated in three places is a severity that can differ between them.
    *Trace:* HLR-183 (One Per-Function Table), HLR-085 (Function Fan-Out Measurement), HLR-156 (Function Fan-In Measurement), HLR-017 (Cyclomatic Complexity per Function).

*   <a id="LLR-SUM-15"></a>**LLR-SUM-15** — `render_summary` shall order the sections of its one traversal so that the findings follow the project summary immediately; so that the sections following the per-file totals run: component coupling, component dependency cycles, the threshold listing, the per-function table, the deepest call chain, recursion; and so that the functions a linked image does not define are the last section emitted.

    The order shall be a property of the ordered section list the traversal walks, so that both human-facing formats take it from one place and a section cannot be moved in one and left in the other.
    *Trace:* HLR-182 (Findings Presented First), HLR-184 (The Order of the Architectural Tiers), HLR-031 (Uniform Report Composition Across Formats).

*   <a id="LLR-SUM-16"></a>**LLR-SUM-16** — `grid_render` shall emit nothing for a grid with no rows, recording that grid's heading instead; and `render_summary` shall close the report with a statement naming every recorded heading, verbatim and in the order the traversal reached them. The statement shall be emitted whether or not any heading was recorded.

    The headings shall be **copied** rather than borrowed. Several sections build theirs into a local buffer carrying a count or a threshold, and a borrowed pointer into one of those would dangle by the time the statement is written.

    The tier a section belongs to shall continue to govern whether it is reached at all: a detail section filtered out of a summary report is neither presented nor named, because it was not rendered. A section whose analysis was omitted for want of a declaration is reached at either verbosity, presents no rows, and is therefore named — which is where its reason reaches the reader once the heading itself is no longer printed.
    *Trace:* HLR-188 (A Table With No Rows Is Not Presented), HLR-189 (The Empty Tables Named in a Closing Statement), HLR-115 (Analyses Requiring User Declarations), HLR-066 (Empty Analysis Result).

*   <a id="LLR-SUM-17"></a>**LLR-SUM-17** — `render_summary` shall emit, in the Markdown style alone, each table inside an HTML `<details>` element opened beneath the section's `##` heading, with a `<summary>` stating the number of rows the table holds and a blank line separating the element from the table on both sides.

    The heading shall stay outside the element and stay a heading, so that a section keeps its anchor and the composition stays readable off the `##` lines.

    The blank lines are load-bearing rather than cosmetic: GitHub-Flavored Markdown parses the contents of an HTML block as Markdown only where a blank line separates the two, and without them the table is rendered as its own source text.

    The count shall be taken from the rows about to be emitted. The project summary is not built from a grid and shall gather its figures before printing any of them, so that its count is derived from the same array the rows are, rather than written down beside it.
    *Trace:* HLR-190 (Markdown Tables Presented Behind a Disclosure Element), HLR-029 (Markdown Output Format).

*   <a id="LLR-SUM-19"></a>**LLR-SUM-19** — `render_summary` shall carry **two** tier classifications for each section — one for the Markdown style and one for the aligned style — and shall select between them by the style already in force for the traversal. At the summary verbosity the aligned style shall present the project summary, the findings and the per-function table alone; the Markdown style shall present the tiers LLR-SUM-09 enumerates (HLR-218).

    The two classifications shall be **two fields of the one ordered section list**, never a second list beside it. A second list would satisfy the requirement and destroy the property the first one exists for: a section is written down once and classified where it is written, so there is nowhere to forget it, and a section added to one list and not the other would be silently unclassified in a format until a reader noticed a missing table. A second field cannot be filled in halfway, because the initialiser does not compile without it.

    The omission predicate of LLR-SUM-09 shall apply under either classification. It asks a question about the *run* and not about the format — whether an analysis was skipped for want of a declaration — so a detail section carrying such a notice is reached at the summary verbosity in both styles, and its heading reaches the reader through the closing statement in both.

    Nothing in this requirement shall change what a tier presents. A tier reached in both styles shall present the same rows and the same figures; only whether a style reaches it without being asked may differ (HLR-031).
    *Trace:* HLR-218 (The Terminal Report's Own Composition), HLR-150 (Summary Report by Default), HLR-151 (Verbose Report on Request), HLR-031 (Uniform Report Composition Across Formats).

*   <a id="LLR-SUM-20"></a>**LLR-SUM-20** — The aligned style shall hold its lines to 128 columns where — and only where — the stream it is writing to is a terminal, determined by asking that stream and not by an option, an environment variable, or a value threaded from the caller (HLR-219).

    The limit shall be applied by choosing the width each column is rendered at before any row is written. Columns marked numeric shall keep their measured width. The remaining columns shall be capped at one common value, the largest under which the line fits, found by bisection over the measured widths. A common cap rather than a proportional share, because a common cap narrows the columns in the order they are widest: a column already narrower than the cap is untouched until every wider one has come down to it, which is what stops a short column being wrapped to pay for a long one.

    Where no cap at or above the floor makes the line fit, every column shall keep its measured width and the table shall be emitted unwrapped (HLR-219).

    The determination shall be made per table rather than once per run, so that a report written to a terminal and a companion written to a file are each laid out for their own destination without either being told about the other.
    *Trace:* HLR-219 (A Line the Terminal Can Hold), HLR-027 (Default Human-Readable Output), HLR-032 (Deterministic Output).

*   <a id="LLR-SUM-21"></a>**LLR-SUM-21** — A cell longer than the width its column was capped to shall be divided at the last of the following opportunities that falls within that width: a space, which is consumed; a `/` or a `:`, which is kept on the line it ends; failing both, the width itself. Nothing shall be discarded — every byte of the cell reaches the reader on one line or the next (HLR-219).

    A division taken at the width shall be moved back to a character boundary, so that a multi-byte character is never split between two lines. The grid measures in bytes throughout, which is a separate matter and predates this requirement; cutting a character in half would put a replacement glyph in the middle of a name, which is a corruption rather than a measurement error.
    *Trace:* HLR-219 (A Line the Terminal Can Hold), HLR-027 (Default Human-Readable Output).

*   <a id="LLR-SUM-22"></a>**LLR-SUM-22** — A row whose cells did not all fit shall be written as consecutive lines, each carrying the next division of every column that still has content, padded so that a continuation appears beneath the column it continues (HLR-219).

    A continuation line shall end at the last column with anything left on it, so that no line carries trailing whitespace across the gap where a short column ran out — which is the same rule LLR-SUM-04 states for a row's first line, applied to the lines beneath it.

    A row whose cells all fit shall be written exactly as it was before any limit existed. The wrapped and unwrapped renderings shall be the *same* code path taken at different widths, so that an unwrapped table cannot drift from a wrapped one and a table written to a file is byte-identical to what the same run produced before this requirement (HLR-032).

    No cell shall be copied into a fixed-size buffer to be written. A cell is bounded only by what a filesystem allows in a path, and a buffer sized to the limit would truncate every unwrapped table wider than the limit — which is precisely the case the limit does not apply to.
    *Trace:* HLR-219 (A Line the Terminal Can Hold), HLR-032 (Deterministic Output), HLR-027 (Default Human-Readable Output).

*   <a id="LLR-SUM-23"></a>**LLR-SUM-23** — The project summary shall take the width of both its columns from the rows it is about to present, rather than from any figure or label named individually.

    The label column was the length of one label written out and the value column the widest of four of the figures named one by one. Both were true of a summary of five totals and neither survived its growth: every label longer than the constant pushed its own value out of line with the rest, so the tier that heads every report was the one tier in it that did not line up. A width derived from the rows cannot fall out of step with them, and a row added to the summary needs nothing else changed (HLR-027).
    *Trace:* HLR-027 (Default Human-Readable Output), HLR-024 (Project-Level Totals).

*   <a id="LLR-SUM-18"></a>**LLR-SUM-18** — `render_summary` shall present each function's Weighted Test Burden Index, with the band as a word beside it, as columns of the one function table of LLR-SUM-14; and shall present the index as a column of the threshold listing beside the three measurements already there, so that a listed function's burden is readable without going back to the table.

    **The Mock Burden Score and the weighted fan-out shall not be columns of any presented format.** They are the terms the index is built from, not measurements a reader acts on: a score of 0.85 against a callee tells nobody what to do, and the sum of such scores tells them less. What a reader acts on is the index and its band, and the two figures behind it remain in the saved record, where a consumer that wants to recompute or re-band can read them (HLR-152).
    *Trace:* HLR-223 (Weighted Test Burden Index per Function), HLR-221 (Mock Burden Score per Function), HLR-183 (One Per-Function Table), HLR-187 (The Threshold Listing Names Every Banded Function).

*   <a id="LLR-SUM-24"></a>**LLR-SUM-24** — `grid_render_table` shall colour the table where, and only where, the stream it writes to is a terminal (HLR-226), and shall decide that by asking the stream rather than by reading any option or environment variable.

    The question shall be asked through a predicate of its own rather than by reading the width `table_limit` returns. That function answers the same question for the same reason, but answers it as a *number*, and a caller reading 128 to mean "a terminal" is a caller that breaks the day the limit moves.

    Each data row shall take the background its index selects, alternating between the two and **beginning with the dark grey**, so the first row of the body is plainly a row rather than the rule above it continued. The header shall take neither, being the legend for the block rather than the first row of it. Every physical line of a wrapped row shall carry that row's background, so a row continued over four lines reads as one row.

    **Every line shall run the full set of columns while colouring**, empty cells included. A line otherwise stops at the last column with anything left in it, which keeps a trailing blank cell from putting two spaces at the end of a line; but a line that stops early stops its background with it, and a row whose ground is a ragged staircase down the right-hand side reads as a rendering fault rather than as one row.
    *Trace:* HLR-226 (Colour on a Terminal), HLR-219.

*   <a id="LLR-SUM-25"></a>**LLR-SUM-25** — `table_cell` shall write a cell whose content is a band name in that band's colour — green for `healthy`, yellow for `warning`, red for `critical` — and shall then restore the row's own foreground rather than resetting, so the background the line was opened with continues underneath the rest of the row (HLR-226).

    **The match shall be on the word and not on the column.** The Severity column of the findings and the Burden column of the function table carry the same vocabulary, and a reader scanning for red should not have to know which column he is looking at. A cell holding anything else shall be left in the row's own colour.

    Under colour the final text column shall be padded, which it is not otherwise. A row's background is what makes the alternation readable, and one stopping at the last character of the last cell would leave a ragged edge that reads as a rendering fault rather than as a row.
    *Trace:* HLR-226 (Colour on a Terminal).

## 39. `format_csv` ([src/format_csv.c](../src/format_csv.c))

*   <a id="LLR-CSV-01"></a>**LLR-CSV-01** — `format_csv` shall emit one record per function over the complete dataset, unfiltered by the complexity threshold.
    *Trace:* HLR-028 (CSV Output).

*   <a id="LLR-CSV-02"></a>**LLR-CSV-02** — `format_csv` shall emit per-function metrics only, excluding the architectural findings.
    *Trace:* HLR-028 (CSV Output), HLR-031 (Uniform Report Composition Across Formats).

*   <a id="LLR-CSV-03"></a>**LLR-CSV-03** — `format_csv` shall write the columns `file`, `language`, `function`, `visibility`, `lines`, `eloc`, `complexity`, `fan_in`, `fan_out`, `mi`, in that order; `file` shall carry `path:line`, `lines` shall be `end - start + 1`, and an unknown visibility shall be the empty field.

    **These are the Functions table's columns, and that is the requirement rather than a choice.** CSV is that table for a consumer that loads it rather than reads it, and the two had drifted column by column — the table gained a visibility, a navigable location and the flow degrees, and this still wrote a `language` and a start and end line nothing else reported (HLR-014).

    The empty field is the unknown visibility because that is what a loader reads as "no value"; the em dash the report prints is a typographic answer to a human and would be a value here. It is never written as `public`, which is a claim, where the absence is the absence of one (HLR-209).

    The `language` field is the same rule read the other way. It was dropped when the two were first matched, because the table carried no such column; the table carries one now, so this does too (HLR-014).
    *Trace:* HLR-014 (Per-Function Identity), HLR-028, HLR-210.

## 40. `write_field` ([src/format_csv.c](../src/format_csv.c))

*   <a id="LLR-FLD-01"></a>**LLR-FLD-01** — `write_field` shall quote and escape every field whose value contains a comma, a double-quote character, or a line break, in accordance with RFC 4180.
    *Trace:* HLR-064 (CSV Field Quoting and Escaping).

*   <a id="LLR-FLD-02"></a>**LLR-FLD-02** — Every emitted CSV field shall pass through `write_field`, so that a value such as a template signature containing a comma cannot corrupt the record structure.
    *Trace:* HLR-064 (CSV Field Quoting and Escaping).

## 41. `xml_write_report` ([src/format_xml.c](../src/format_xml.c))

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

*   <a id="LLR-XWR-08"></a>**LLR-XWR-08** — `xml_write_report` shall write the call-tree measurements to the record — every function's fan-out, fan-in and effective lines, every recursive cycle's members, the depth and its state, and the deepest chain in order — and `xml_read_report` shall restore them. None can be recomputed from a record: regeneration has no graph, and no source from which to build one. A record carrying fan-out alone would regenerate every fan-in as zero, which renders as an ordinary number and reads as a project where nothing is called.

    The degrees are **joined onto the restored function metrics** by the same function the live path calls, and the threshold listing is rebuilt over the joined result, so that neither the function table nor the listing can differ between a live run and a regenerated one (HLR-183, HLR-187). The `hk` attribute this element carried until Phase 24 is gone with the metric it held; that is a removal, which is what the format version counts, so a record written by this build carries version 2 and a version-1 record is rejected rather than read with a field missing (HLR-061, HLR-058).
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

*   <a id="LLR-XWR-14"></a>**LLR-XWR-14** — `xml_write_report` shall record the image the filter was taken from and the counts derived from it — the unresolved linkage names, the effective lines belonging to no function, the source lines this build compiled no instruction for, and the analysed files whose debug coverage could not be established — without which a regenerated report would describe a filtered run while naming no filter, or would name one and understate what it did.

    The line-granularity counts travel with the image element rather than with the per-file metrics, for the reason the file-scope figure does: they are properties of what the filter did to the run, and `report_assemble` on the regeneration path has no image to re-derive them from. Both shall be optional on read, since a record written before they existed carries the same format version and a run whose image held no line information writes them as zero either way (HLR-155).
    *Trace:* HLR-147 (Filter Recorded and Reported).

*   <a id="LLR-XWR-15"></a>**LLR-XWR-15** — `xml_write_report` shall record each custom-rule match with its identity, file, and line range, without which a regenerated report would omit a section the direct run presented.
    *Trace:* HLR-054 (XML Output), HLR-056 (Regenerated Report Equivalence).

*   <a id="LLR-XWR-17"></a>**LLR-XWR-17** — `xml_write_report` shall record the two conformance indices as the run rendered them, with the counts they were formed from, and shall record the matrix as its ordered subjects followed by its non-zero cells alone. Neither can be recomputed on the way back — regeneration has no call graph — so a record carrying the layering rows alone would regenerate an undefined index and an empty grid for a run whose report showed neither. The zero cells are omitted because a matrix over a real project is mostly zeroes, and a cell absent from the document reads back as the zero it was; the subjects precede the cells so that the order of the grid is known before an index into it arrives.
    *Trace:* HLR-054 (XML Output), HLR-165 (Dependency Structure Matrix), HLR-162.

*   <a id="LLR-XWR-16"></a>**LLR-XWR-16** — `xml_write_report` shall record the definitions in force and the undecided-region count, without which a regenerated report would describe a configuration it does not name.
    *Trace:* HLR-136 (Configuration Recorded and Reported).

*   <a id="LLR-XWR-18"></a>**LLR-XWR-18** — `xml_write_report` shall record every classification purification made — the function, its file and line, the class assigned, the metric and value that triggered it, and the action taken — together with the five thresholds they were decided against and the counts the recovery view was left with. A record carries no graph, so a classification absent from it is one a regenerated report cannot present; and the thresholds travel beside the rows because a record read a year later has no command line to consult.
    *Trace:* HLR-174 (Purification Reported Before It Is Relied On), HLR-054, HLR-171.

*   <a id="LLR-XWR-19"></a>**LLR-XWR-19** — `xml_write_report` shall record the recovered layering — whether one was proposed, each directory placed and its layer, the groups reported where none could be, the counts of what was masked and excluded, and the argument list a user would pass back — together with the provenance of every classification the purification element already carries. A record holds no graph to re-order, so a proposal absent from it is one a regenerated report cannot present; and a record that dropped the provenance would leave a reader of a regenerated report unable to tell the tool's assumptions from their own team's.
    *Trace:* HLR-054 (The Complete XML Record), HLR-172, HLR-177.

## 42. `write_escaped` ([src/format_xml.c](../src/format_xml.c))

*   <a id="LLR-ESC-01"></a>**LLR-ESC-01** — `write_escaped` shall escape every character carrying structural meaning in XML, so that an identifier or path containing such a character cannot render the document unparseable.
    *Trace:* HLR-065 (XML Well-Formedness and Escaping).

*   <a id="LLR-ESC-02"></a>**LLR-ESC-02** — Every emitted XML element body and attribute value shall pass through `write_escaped`.
    *Trace:* HLR-065 (XML Well-Formedness and Escaping).

## 43. `xml_read_report` ([src/format_xml.c](../src/format_xml.c))

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

*   <a id="LLR-XRD-14"></a>**LLR-XRD-14** — `xml_read_report` shall reconstruct the filter provenance a record carries and render it identically to the run that wrote it.
    *Trace:* HLR-147 (Filter Recorded and Reported), HLR-056 (Regenerated Report Equivalence).

*   <a id="LLR-XRD-15"></a>**LLR-XRD-15** — `xml_read_report` shall reconstruct the custom-rule matches a record carries and render them identically to the run that wrote it.
    *Trace:* HLR-056 (Regenerated Report Equivalence).

*   <a id="LLR-XRD-17"></a>**LLR-XRD-17** — `xml_read_report` shall restore the two conformance indices from the record as text rather than recomputing them from the counts beside them, and shall restore the matrix from its subjects and cells, treating a record carrying neither as one whose conformance section is omitted and whose grid is empty rather than as malformed. Recomputing a figure would let a regenerated report round it differently from the report it came from; rejecting a record written before the elements existed would break the rule that adding an element is an addition an older reader ignores.
    *Trace:* HLR-056 (Regenerated Report Equivalence), HLR-054, HLR-061.

*   <a id="LLR-XRD-16"></a>**LLR-XRD-16** — `xml_read_report` shall reconstruct the configuration a record carries and render it identically to the run that wrote it.
    *Trace:* HLR-136 (Configuration Recorded and Reported), HLR-056 (Regenerated Report Equivalence).

*   <a id="LLR-XRD-18"></a>**LLR-XRD-18** — `xml_read_report` shall restore the classifications and the thresholds a record carries as the run rendered them, recomputing no centrality, and shall reject as malformed a classification element lacking the class, the metric, the value, or the action. A regenerated report must say what the run it describes said rather than what this build would conclude today; and a classification without the number that produced it is precisely what HLR-174 forbids reporting, so it is rejected rather than half-read.
    *Trace:* HLR-174 (Purification Reported Before It Is Relied On), HLR-056, HLR-058.

*   <a id="LLR-XRD-19"></a>**LLR-XRD-19** — `xml_read_report` shall restore the recovered layering as the record states it, re-deriving nothing, and shall read it back as a proposal and never as a declaration: the strata state, the layering rows, and the conformance indices of a regenerated report shall be exactly what the record carries, so that a report regenerated from a run with no declared strata reports those analyses as omitted just as that run did. What a regenerated report presents is what the run it describes proposed, and a record read as a declaration would make `elc` measure against its own proposal one remove away from where HLR-173 forbids it.
    *Trace:* HLR-173 (A Recovered Layering Is a Proposal, Never a Baseline), HLR-056, HLR-115.

## 44. `graph_dot_warranted` ([src/format_graph.c](../src/format_graph.c))

*   <a id="LLR-WAR-01"></a>**LLR-WAR-01** — `graph_dot_warranted` shall return true by default, and false when the user has disabled `.dot` generation.
    *Trace:* HLR-103 (.dot Generation Enabled by Default).

*   <a id="LLR-WAR-02"></a>**LLR-WAR-02** — `graph_dot_warranted` shall return false whenever the report is written to standard output, whether or not generation was disabled.
    *Trace:* HLR-104 (No .dot Output to Standard Output).

*   <a id="LLR-WAR-03"></a>**LLR-WAR-03** — `graph_dot_warranted` shall return false in regeneration mode, since a saved record does not carry the graph.
    *Trace:* HLR-122 (No Companion Artefacts From a Saved Record).

## 45. `graph_write_dot` ([src/format_graph.c](../src/format_graph.c))

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

## 46. `node_style` ([src/format_graph.c](../src/format_graph.c))

*   <a id="LLR-STY-01"></a>**LLR-STY-01** — `node_style` shall annotate nodes exceeding the coupling and fan-out thresholds, the functions forming the deepest call chain, the members of each dependency and recursive cycle, unreachable functions, and functions participating in a hidden channel.
    *Trace:* HLR-105 (Annotated .dot Output).

*   <a id="LLR-STY-02"></a>**LLR-STY-02** — `node_style` shall emit annotations as attributes a renderer may ignore while still producing a valid call tree.
    *Trace:* HLR-105 (Annotated .dot Output).

*   <a id="LLR-STY-03"></a>**LLR-STY-03** — Each node's `tooltip` shall carry its definition site, the ELOC and cyclomatic complexity the report states for it, and the findings upon it, one to a line.

    **The same three things the interactive report shows when a reader points at a box** (LLR-HTM-09). They are two drawings of one graph, and a reader who has asked one of them a question should not have to learn what the other will answer; the figures were absent here and present there, which is a difference in what the artefacts *know* rather than in how they draw it.

    **The line separator shall be a newline character and not DOT's `\n` escape.** Graphviz resolves that escape for SVG output and *not* for the xdot format, so a viewer reading xdot — which is what `xdot` reads — shows the two characters of the escape where the line break should be. A newline inside a quoted string is legal DOT and reaches both: SVG still writes the separators as character references, and the xdot stream now carries the breaks themselves.

    **The cost is stated because it is real.** A node's attribute list then spans several physical lines, so the file is no longer one line per node, and anything reading it line-wise — a `grep` for a node and its findings, the suite's own stripping of the decoration — must join the quoted strings first. That is accepted here and would not be accepted for the other export: this artefact is a drawing for someone to look at, and the machine-readable rendering of the same graph is GraphML, which carries no tooltip and is untouched (SDD §17).
    *Trace:* HLR-105 (Annotated .dot Output), HLR-217 (The Drawing Carries the Findings It Was Drawn From).

## 47. `graph_write_graphml` ([src/format_graph.c](../src/format_graph.c))

*   <a id="LLR-GML-01"></a>**LLR-GML-01** — `graph_write_graphml` shall export the graph in the GraphML serialisation schema so that it may be ingested by other tools.
    *Trace:* HLR-106 (Standard Graph Serialisation Export).

*   <a id="LLR-GML-02"></a>**LLR-GML-02** — `graph_write_graphml` shall run only when explicitly requested, being disabled by default.
    *Trace:* HLR-106 (Standard Graph Serialisation Export).

*   <a id="LLR-GML-03"></a>**LLR-GML-03** — `graph_write_graphml` shall derive its filename from the report's output path, and shall produce no file when the report is written to standard output.
    *Trace:* HLR-106 (Standard Graph Serialisation Export), HLR-119 (Companion Artefact Naming).

*   <a id="LLR-GML-04"></a>**LLR-GML-04** — `graph_write_graphml` shall emit well-formed XML with every structurally significant character escaped.
    *Trace:* HLR-065 (XML Well-Formedness and Escaping), HLR-106 (Standard Graph Serialisation Export).

*   <a id="LLR-GML-05"></a>**LLR-GML-05** — `graph_write_graphml` shall carry each node's fan-in beside its fan-out, both computed over call edges alone, so that an ingesting tool has the same per-function figures the report presents in its function table. It shall carry no value derived from them: a derived figure in the export is a second place it can be computed, and two places computing a figure are two places it can be computed differently.
    *Trace:* HLR-156 (Function Fan-In Measurement), HLR-106 (Standard Graph Serialisation Export).

## 48. `elfsyms_open` ([src/elfsyms.c](../src/elfsyms.c))

*   <a id="LLR-ELF-01"></a>**LLR-ELF-01** — `elfsyms_open` shall read the image named on the command line and shall populate the function set from the symbol table that image carries, preferring `.symtab` and falling back to `.dynsym`.
    *Trace:* HLR-140 (Linked-Image Function Filter).

*   <a id="LLR-ELF-02"></a>**LLR-ELF-02** — `elfsyms_open` shall retain a symbol only where it is of function type and is defined by the image rather than imported by it, so that a function the image calls out to a shared library is not taken for one the image contains.
    *Trace:* HLR-140 (Linked-Image Function Filter).

*   <a id="LLR-ELF-03"></a>**LLR-ELF-03** — `elfsyms_open` shall read no file other than the image it was given, and shall invoke no toolchain utility, compiler, linker, or build system.
    *Trace:* HLR-141 (Image Read Without a Toolchain).

*   <a id="LLR-ELF-04"></a>**LLR-ELF-04** — `elfsyms_open` shall require no debugging information in the image, taking the function set from the symbol table a linker writes by default.
    *Trace:* HLR-141 (Image Read Without a Toolchain).

*   <a id="LLR-ELF-05"></a>**LLR-ELF-05** — `elfsyms_open` shall sort the function set on the resolved name and remove duplicates, so that no property of symbol-table order can reach the output.
    *Trace:* HLR-032 (Deterministic Output).

*   <a id="LLR-ELF-06"></a>**LLR-ELF-06** — `elfsyms_open` shall fail with a diagnostic naming the path when the image is absent, unreadable, not an object file, or of a class this build does not read.
    *Trace:* HLR-146 (An Unusable Image Is Fatal).

*   <a id="LLR-ELF-07"></a>**LLR-ELF-07** — `elfsyms_open` shall fail, with a diagnostic distinguishing the case, when the image carries no function symbols at all, rather than returning an empty set that would filter every function away.
    *Trace:* HLR-146 (An Unusable Image Is Fatal).

*   <a id="LLR-ELF-08"></a>**LLR-ELF-08** — `elfsyms_open` shall count the symbols whose linkage name it could not resolve, and shall make that count available to the report.
    *Trace:* HLR-143 (Both Directions of Mismatch Counted and Reported).

## 49. `resolved_name` ([src/elfsyms.c](../src/elfsyms.c))

*   <a id="LLR-SYM-01"></a>**LLR-SYM-01** — `resolved_name` shall return an unencoded linkage name unchanged, that being the C case and the `extern "C"` case alike.
    *Trace:* HLR-142 (Linkage Names Resolved to Source Names).

*   <a id="LLR-SYM-02"></a>**LLR-SYM-02** — `resolved_name` shall decode a linkage name encoded by a published mangling scheme, detecting the scheme from the name rather than from a language the user states, since an image may hold symbols from several compilers and states which produced none of them.
    *Trace:* HLR-142 (Linkage Names Resolved to Source Names).

*   <a id="LLR-SYM-03"></a>**LLR-SYM-03** — `resolved_name` shall reduce a decoded name to the function name the report presents, discarding the signature, the enclosing qualification, the template argument list, any leading return type, and any hash suffix, so that both sides of the comparison are in one form. Two parenthesised forms are part of a name rather than a signature and shall be kept: the empty pair of `operator()`, and the `(anonymous namespace)` an internal-linkage definition is qualified by. Each would otherwise truncate the name to nothing, and the punctuation of an operator would likewise unbalance a scan that took it for structure.
    *Trace:* HLR-142 (Linkage Names Resolved to Source Names), HLR-014 (Per-Function Identity).

*   <a id="LLR-SYM-09"></a>**LLR-SYM-09** — `elfsyms_defines_in` shall report whether the image defines a function of a given name **written in a given file**: where the debug information holds any definition of the name, the file governs and a definition in any other file is not one the image kept; where it holds none, or where the caller supplies no file, the answer is the name-only one `elfsyms_defines` gives.

    The fallback is safe because it is guarded elsewhere: a run in which the fallback would be ambiguous has already been refused (LLR-RPT-41), so reaching it means the name is unique in the analysed source and the file could not change the answer.
    *Trace:* HLR-193 (Image Symbols Placed by Debug Information), HLR-140.

*   <a id="LLR-SYM-04"></a>**LLR-SYM-04** — `resolved_name` shall return no name for a linkage name encoded by a scheme this build does not decode, so that the symbol is counted as unresolved rather than matched against a guess.
    *Trace:* HLR-143 (Both Directions of Mismatch Counted and Reported).

## 49.1. `dwarfline_read` ([src/dwarfline.c](../src/dwarfline.c))

*   <a id="LLR-DWL-01"></a>**LLR-DWL-01** — `dwarfline_read` shall obtain the image's debug line information from the ELF descriptor `elfsyms_open` already holds, using the low-level DWARF interface and never the `Dwfl` layer above it. That layer resolves separate debug information by `.gnu_debuglink` and build-id, which opens a file under a separate-debug directory the user never named — forbidden by HLR-141.

    Nothing in the module shall open, stat, or resolve a path against the filesystem, so that the answer is a property of the image's bytes and not of the machine reading it. Both live in one library and neither is visible in the link line, so the distinction shall be held by a test that observes the image's opens for a build carrying debug information rather than by the dependency allowlist.
    *Trace:* HLR-141 (Image Read Without a Toolchain), HLR-153 (Debug-Line Pruning From the Image).

*   <a id="LLR-DWL-02"></a>**LLR-DWL-02** — `dwarfline_read` shall bring each file name the image records to the form `elc`'s own paths take, joining a relative name to its unit's compilation directory and resolving `.` and `..` **lexically**. It shall not call `realpath(3)`: doing so would stat every path the image happens to name, headers among them, which is filesystem work on files the user did not name.

    Where a build reaches its sources through a symbolic link the two spellings will not meet, the file shall report uncovered, and nothing in it shall be pruned. That is the safe direction, and the count of HLR-155 states it.
    *Trace:* HLR-154 (Pruning Confined to Established Coverage), HLR-141 (Image Read Without a Toolchain).

*   <a id="LLR-DWL-03"></a>**LLR-DWL-03** — `dwarfline_read` shall skip the end-of-sequence entry of each line programme, which carries the address one past the last instruction and names no line of source. Counting it would mark a line compiled on the strength of a marker rather than of an instruction.

    Each file's lines shall be sorted and de-duplicated, since a line programme names one line once per instruction sequence attributed to it: the raw list is long and heavily repeated, and collapsing it makes membership a binary search and makes the set independent of how many sequences the compiler emitted.
    *Trace:* HLR-153 (Debug-Line Pruning From the Image), HLR-032 (Deterministic Output).

*   <a id="LLR-DWL-04"></a>**LLR-DWL-04** — An image carrying no debug line information shall yield an empty coverage set and shall not be a failure, since HLR-141 forbids requiring debug information. `dwarfline_read` shall return non-zero on allocation failure alone, and shall release the partially built set before doing so — a coverage set built halfway would prune on evidence it does not have.
    *Trace:* HLR-153 (Debug-Line Pruning From the Image), HLR-141 (Image Read Without a Toolchain), HLR-125 (Complete Resource Release).

*   <a id="LLR-DWL-06"></a>**LLR-DWL-06** — `dwarfline_read` shall additionally record, for every subprogram the image's debug information describes with a code address, the function's name and the source file it was written in, as a set of **name-and-file pairs** made absolute against the compilation directory by the same rule the line table's file names are.

    The name in each pair shall be the **reduced** form of LLR-SNM-01, and `dwarfline_knows` and `dwarfline_places` shall reduce the name they are given before searching. Debug information records a template instantiation under its instantiated name and an out-of-line member definition under its bare one, while the source side arrives as the declarator `elc` parsed; unreduced, `serialize_seq<int>` and `serialize_seq` are two names and the map answers that it holds no definition of a function it holds three (HLR-200).

    The pair is the unit of the map, not the name. Keying by name alone would collapse two translation units defining a `static helper` into one unusable entry, which is precisely the case the map exists to resolve — the debug information carries a separate subprogram for each and knows which file each was written in.

    Read from the subprogram DIEs rather than from the line table and a symbol address: the declaration attribute says where a function was *written*, which is the question, while an address lookup answers where its first instruction ended up — a different question that inlining and identical-code folding give a plausible wrong answer to.

    A subprogram with no code address shall be skipped, since a declaration is not a definition and would place a name in a file that merely mentioned it. An image with no debug information shall yield an empty map rather than a failure, exactly as it yields an empty line coverage (HLR-141).
    *Trace:* HLR-193 (Image Symbols Placed by Debug Information), HLR-141.

*   <a id="LLR-DWL-07"></a>**LLR-DWL-07** — `dwarfline_knows` shall report whether the map holds any definition of a name, and `dwarfline_places` whether it holds one in a given file. They shall be separate calls, and `dwarfline_places` shall be meaningful only where `dwarfline_knows` is true for the same name.

    The split is the one `dwarfline_covers` and `dwarfline_compiled` already take, for the same reason: a single call returning false would conflate "the debug information says this function is not in that file" with "there is no debug information", and a caller that made the distinction by accident would exclude every function of an image built without it.

    `dwarfline_any` shall report whether the map holds any origin at all, which is the question neither of the other two can answer. A false result from `dwarfline_knows` has two causes — the image describes nothing, or it describes plenty and nothing under this name — and a caller that must state which it observed cannot infer it from the lookup that failed (HLR-201).
    *Trace:* HLR-193 (Image Symbols Placed by Debug Information).

*   <a id="LLR-DWL-05"></a>**LLR-DWL-05** — Coverage and compilation shall be two queries rather than one, and the first shall govern the second. `dwarfline_covers` shall answer whether the image's line information describes a file at all; `dwarfline_compiled` shall answer whether a line of it produced an instruction, and shall answer *false* for a file that is not covered.

    That answer is deliberately the unsafe one, and separating the calls is what makes asking it safe. A caller that skipped the coverage test would find every line of a file compiled without debug information uncompiled and would delete the file — absence from a mapping that never described it read as evidence about it, which is the one failure in this phase that is silent (HLR-154).
    *Trace:* HLR-154 (Pruning Confined to Established Coverage).

*   <a id="LLR-DWL-08"></a>**LLR-DWL-08** — `dwarfline_compiled_between` shall answer whether any line in the inclusive range `from`..`to` of a covered file produced an instruction, and shall answer false for an uncovered file and for a range whose `from` exceeds its `to`.

    The question a conditional region asks, and one a caller cannot build out of the per-line call: a region is judged **whole**, because a single absent line is the optimiser folding it into a neighbour and an entire absent region is evidence about the build (HLR-154, HLR-211).

    The empty range answers false rather than true, which is the direction that leaves a region undecidable rather than deciding it on nothing.

    It shares the lower-bound search with `dwarfline_compiled`: on an ascending, de-duplicated list, "is this line present" and "is any line of this range present" are the same lookup asked two questions, and two searches would be two places for the ordering assumption to be got wrong.
    *Trace:* HLR-211 (Conditional Regions Decided from the Image), HLR-154.

*   <a id="LLR-DWL-09"></a>**LLR-DWL-09** — Each `FunctionOrigin` shall carry the line `DW_AT_decl_line` records, and zero where the producer recorded none. `dwarfline_origin_count` and `dwarfline_origin_at` shall expose the map for walking, the latter answering NULL past the end.

    `dwarfline_knows` and `dwarfline_places` answer "where is this function", which is a lookup on a key. "Which functions does the image place in this file" is the opposite question and has no key to search on, so it is answered by walking — and the walk is bounded by the accessor rather than by the caller's arithmetic (HLR-212).

    **A subprogram with no recorded line keeps its entry.** The entry is a *placement*, which is what HLR-193 asks of this map and which does not depend on a line; dropping it would answer "not defined here" for a function the image plainly defines. Zero is read as "no location" by the one caller that wants one.
    *Trace:* HLR-212 (Functions the Image Places and the Parse Did Not Reach), HLR-193.

## 50. Build and Link Configuration ([Makefile](../Makefile))

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

*   <a id="LLR-BLD-07"></a>**LLR-BLD-07** — The build shall deliver runtime language support for C, C++, Rust, and Python, each as a grammar and its query files under the runtime location, requiring no change to any source module of the executable.
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

*   <a id="LLR-BLD-18"></a>**LLR-BLD-18** — The sanitized build target shall set the sanitizer options it runs under rather than inheriting whatever the invoking environment supplies, and shall set the same ones the pipeline does. `abort_on_error` is what makes a leak reported inside a forked test child reach the parent's exit status; without it a leaking unit test is reported and the run still succeeds. A local gate weaker than the pipeline's is worse than none, because it is trusted — Phase 11 shipped two leaking tests past a green local run and was caught only by CI.
    *Trace:* HLR-125 (Complete Resource Release), HLR-124, HLR-119.

*   <a id="LLR-BLD-19"></a>**LLR-BLD-19** — The build shall name the C++ runtime on the link line from the point `elc` references a symbol in it, even though the library was already loaded as a transitive dependency of the graph library. A symbol resolved through an indirect `DT_NEEDED` is not resolved at all by a current linker, so the demangler of HLR-142 fails to link without it — and the dependency allowlist is unchanged by the addition, the library having been there since the graph arrived.
    *Trace:* HLR-142 (Linkage Names Resolved to Source Names), HLR-112.

*   <a id="LLR-BLD-20"></a>**LLR-BLD-20** — The build shall link `libdw` for the debug line information of HLR-153, taking it from the distribution as it takes `libelf` and for the same reason: it is the same elfutils tree, produced by the same configure run, and building it from source would import more distribution packages than taking it does. It shall appear in the installable package list, in the version report of `make check-prereqs`, and in the dependency allowlist together with the `libbz2` and `liblzma` it brings with it.

    The allowlist cannot hold the distinction that matters most about it. `libdw` contains both the low-level DWARF interface `elc` uses and the `Dwfl` layer that would open a file the user never named, and the link line is identical either way — so the rule is held by the instrumented test that counts the image's opens for a build carrying debug information (HLR-141, LLR-DWL-01).

    **The package list and the pipeline's shall be held in agreement by a test.** They are two statements of one fact in two files, and nothing connected them: this library was added to the Makefile and not to the workflow, and every compiling job failed at once while the local gate stayed green, the developer machine having the package already. That is the drift LLR-BLD-18 guards for the sanitizer options, in the other place the two definitions meet.
    *Trace:* HLR-153 (Debug-Line Pruning From the Image), HLR-141 (Image Read Without a Toolchain), HLR-112.

*   <a id="LLR-BLD-21"></a>**LLR-BLD-21** — The build shall link a JSON library that both parses and generates, for the purification manifest, and the instrumented dependency allowlist shall name it. The manifest is the one artefact `elc` both writes and reads back, so a hand-rolled writer paired with a library reader would be two implementations of one format with `elc` on both ends of the disagreement; and a library added to the link line without a matching entry in the allowlist is a dependency nothing observes, which is what a fixed list exists to prevent.
    *Trace:* HLR-112 (Library Selection Deferred to Design), HLR-040 (Excluded Runtime Dependencies), HLR-175.

*   <a id="LLR-BLD-22"></a>**LLR-BLD-22** — The pipeline shall build the from-source libraries by invoking the makefile target that names them, and shall not enumerate individual libraries itself. Which libraries are built from source is one fact, and a pipeline restating it drifts from the makefile the moment either gains an entry — twice now, each time failing every compiling job at once while the suite stayed green on a developer machine that had the library already. A test over two lists is not enough on its own: Expat sat in the makefile's list and absent from the pipeline for several phases without anything failing, because the runner image ships it, so the pipeline linked a distribution copy where the project's policy is a pinned release it can bump against an advisory. Calling the target leaves no second list to keep in step.
    *Trace:* HLR-112 (Library Selection Deferred to Design), HLR-124.

*   <a id="LLR-BLD-23"></a>**LLR-BLD-23** — The delivered source shall report no function at or over the default complexity threshold when analysed by the delivered binary. The threshold `elc` applies to other people's code is the one its own must meet; a report naming a function at 30 while the tool's own `manifest_write` stood at 30 is the one kind of finding a reader is entitled to discount.
    *Trace:* HLR-181 (Self-Application), HLR-023.

*   <a id="LLR-BLD-24"></a>**LLR-BLD-24** — The delivered source shall report no component dependency cycle when analysed by the delivered binary. A cycle is the finding `elc` raises at critical severity in anyone else's code (HLR-084), and one among its own modules would be the same defect reported by the module that contains it.
    *Trace:* HLR-181 (Self-Application), HLR-084.

*   <a id="LLR-BLD-26"></a>**LLR-BLD-26** — The build shall take the version it stamps into the binary from the `VERSION` file in the project root, and shall fail with a diagnostic naming that file where it is absent or empty rather than substituting a default (HLR-220).

    Every object shall depend on that file. The version reaches the binary as a translation-time definition, so editing `VERSION` alters no source file and an incremental build would otherwise leave the objects as they were and go on reporting the release the image was previously made as — the failure HLR-220 exists to prevent, arriving through the build rather than through the documentation. The dependency is on every object rather than on the one that reads the definition, since the definition is supplied to every translation unit and a second file using it must not require this to be revisited.
    *Trace:* HLR-220 (The Version the Build Was Made As).

*   <a id="LLR-BLD-25"></a>**LLR-BLD-25** — No two file-local functions in the delivered source shall share a name. `elc` resolves a call by name, so two definitions of one name make every call to it resolve to whichever the graph indexed first — an edge pointing at the wrong module, in the analysis on which the acyclicity of LLR-BLD-24 rests. The diagnostic `elc` already emits for an ambiguous resolution is the check.
    *Trace:* HLR-181 (Self-Application), HLR-077.

*   <a id="LLR-BLD-09"></a>**LLR-BLD-09** — The build shall provide a configuration instrumented with AddressSanitizer and UndefinedBehaviorSanitizer, with leak detection enabled, under which the whole test suite can be re-run.
    *Trace:* HLR-124 (Memory Safety), HLR-125 (Complete Resource Release).

## 51. User Documentation ([doc/elc.1](../doc/elc.1), [doc/User_Manual.md](../doc/User_Manual.md))

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

## 52. `conformance_indices` ([src/arch.c](../src/arch.c))

The aggregate over the layering findings: how much of the code base conforms to the declared layering, as two independent proportions of one denominator.

*   <a id="LLR-CNF-01"></a>**LLR-CNF-01** — `conformance_indices` shall compute the Back-Call and Skip-Call Violation Indices by counting the layering violations already recorded, tallied by kind, over the inter-layer call-edge count recorded beside them, and shall not read the graph in order to do so. It is not given one: an implementation that re-derived what a violation is could not satisfy this signature, which is what makes the index and the table printed beside it two views of one answer rather than two opinions that can drift apart.
    *Trace:* HLR-164 (Indices Counted From the Reported Violations), HLR-162 (Back-Call Violation Index), HLR-163 (Skip-Call Violation Index).

*   <a id="LLR-CNF-02"></a>**LLR-CNF-02** — `conformance_indices` shall report both indices as undefined, performing no division, where the inter-layer call-edge count is zero, and the guard shall precede the division rather than follow it. A project whose layers never call one another has not achieved perfect conformance; it has demonstrated nothing either way, and reporting 0 — or 100% conforming — there would be the confidently wrong answer the same rule already prevents for Instability.
    *Trace:* HLR-162 (Back-Call Violation Index), HLR-163 (Skip-Call Violation Index), HLR-082.

*   <a id="LLR-CNF-03"></a>**LLR-CNF-03** — `conformance_indices` shall count a call that both bypasses a layer and inverts the declared direction once in each index, and shall produce no combined score. The two are independent proportions of one denominator, so their sum may exceed the denominator itself; combining them would count twice exactly the call most worth acting on, and would name no remedy where each index separately names one.
    *Trace:* HLR-163 (Skip-Call Violation Index), HLR-118.

## 53. `component_directory` ([src/analyze.c](../src/analyze.c))

Where a component's directory is derived, once, for every consumer that groups by one.

*   <a id="LLR-DIR-01"></a>**LLR-DIR-01** — `component_directory` shall return the portion of a path preceding its last separator, carrying no trailing separator, and shall return `/` for a path whose last separator is its first character and `.` for a path carrying none. The two edge cases are handled here rather than at each call site because a split on the last separator gets both wrong: a file directly under the root would yield the empty string, which names no directory, and a path with no separator would yield nothing at all.
    *Trace:* HLR-160 (Component Directory Recorded).

*   <a id="LLR-DIR-02"></a>**LLR-DIR-02** — Every `FileMetrics` shall carry the directory containing it, set at construction by both of the paths that construct one — the measurement of a source file and the reader that rebuilds a model from a saved record — and every consumer that groups components by directory shall read that field rather than derive one from the path. One derivation reached from two construction sites is what keeps a component's directory the same string whichever way the model arrived.
    *Trace:* HLR-160 (Component Directory Recorded).

## 54. `dsm_build` ([src/format_dsm.c](../src/format_dsm.c))

The Dependency Structure Matrix: the arrangement of the graph's call edges into a square grid whose position carries meaning.

*   <a id="LLR-DSM-01"></a>**LLR-DSM-01** — `dsm_build` shall take the matrix's subjects to be the declared strata where any were declared and the distinct directories of the analysed components otherwise, ordering the first by ascending layer index and the second ascending by path. A stratum's ordinal *is* its position, so each label is placed at its ordinal rather than appended in declaration order — without which `--stratum-order` would move the back-calls above the diagonal. The directories are sorted rather than read off in component order, since ascending path order over files is not ascending path order over their directories.
    *Trace:* HLR-165 (Dependency Structure Matrix), HLR-166 (Matrix Ordering, the Diagonal, and Its Renderings), HLR-160.

*   <a id="LLR-DSM-02"></a>**LLR-DSM-02** — `dsm_build` shall obtain each component's layer from `check_strata`'s own assignment rather than by matching the stratum patterns itself, and shall place no component that matches no declaration in any subject. Two matchers over one set of patterns would eventually disagree about which layer a file is in, and the cells below the matrix's diagonal would then stop accounting for the back-calls listed beside them.
    *Trace:* HLR-161 (Layer Index Taken From the Declared Strata), HLR-164.

*   <a id="LLR-DSM-03"></a>**LLR-DSM-03** — `dsm_build` shall count call edges alone, one per distinct edge of the graph rather than one per call site, and shall count no global-state edge. A global object two subjects share is coupling and not invocation, and counting one would put a number below the diagonal that no call put there; counting call sites rather than edges would give a cell a figure the conformance denominator does not share.
    *Trace:* HLR-165 (Dependency Structure Matrix), HLR-093.

*   <a id="LLR-DSM-04"></a>**LLR-DSM-04** — `dsm_build` shall place the count of call edges from subject *i* to subject *j* at row *i*, column *j*, so that rows are callers and columns callees, and the cells below the diagonal shall therefore total exactly the number of direction-inverted findings the layering analysis reported. A matrix built the other way round is still a square of plausible numbers with every reading taken from it exactly backwards, which is why the orientation is asserted rather than inferred.
    *Trace:* HLR-166 (Matrix Ordering, the Diagonal, and Its Renderings), HLR-162.

## 55. The matrix renderings ([src/format_dsm.c](../src/format_dsm.c))

`format_dsm_csv`, `format_dsm_markdown`, and `format_dsm_table` — three decorations of one walk of the grid, and the decision of whether the CSV companion is written at all.

*   <a id="LLR-DSM-05"></a>**LLR-DSM-05** — Every rendering of the matrix shall state the orientation convention with the grid, and shall emit the heading, the convention, and the column names even where the matrix holds no subjects. The three renderings shall share one traversal of the grid, so that they cannot disagree about what is in it. A matrix whose orientation the reader must infer conveys the opposite of what it is for half the time, and a section that vanishes when it has no content makes the report's shape vary with its content.
    *Trace:* HLR-166 (Matrix Ordering, the Diagonal, and Its Renderings), HLR-031.

*   <a id="LLR-DSM-06"></a>**LLR-DSM-06** — The CSV rendering shall emit every cell through `write_field`, and the Markdown rendering shall escape the cell separator and measure its column widths after that escaping. A directory containing a comma would otherwise split a record, one containing a pipe would shift every cell to its right by a column — each producing a grid that still parses and says something else — and a column measured on the raw text comes out a character short for every pipe in it.
    *Trace:* HLR-064 (CSV Field Quoting and Escaping), HLR-166.

*   <a id="LLR-DSM-07"></a>**LLR-DSM-07** — `dsm_free` shall release the subject labels and the cell array and leave the matrix zeroed, and shall tolerate a null pointer and a matrix already released, so that teardown is unconditional on every exit path.
    *Trace:* HLR-125 (No Resource Leaks), HLR-165.

*   <a id="LLR-DSM-08"></a>**LLR-DSM-08** — `dsm_warranted` shall be true only where the CSV companion was requested and the report has a named output path, and shall not be made false by regeneration mode. The first two are the tests the GraphML export makes, since the companion's name is derived from the report's and a report on standard output offers none; the third is where this companion differs from the two graph companions, a saved record carrying the matrix where it carries no topology.
    *Trace:* HLR-180 (The Matrix Written Beside the Report on Request), HLR-104, HLR-119.

*   <a id="LLR-DSM-09"></a>**LLR-DSM-09** — The Markdown rendering of the matrix shall place the grid inside an HTML `<details>` element stating its row count, as every other Markdown table is (HLR-190), and shall leave the heading and the convention note **outside** it.

    The convention stays outside because of what it says: it is the sentence that makes a cell below the diagonal a back-call rather than a number, and a reader who has not expanded the grid is exactly the reader deciding whether to. HLR-166 requires it to travel with the grid, and it still does — above it rather than inside it.
    *Trace:* HLR-190 (Markdown Tables Presented Behind a Disclosure Element), HLR-166 (Matrix Ordering, the Diagonal, and Its Renderings).

## 56. `purify_analyse` ([src/purify.c](../src/purify.c))

The purification pass: the centralities of the call view, the classifications read off them, and the masked copy a layering can later be read from. The one place `elc` forms a view of its own about a code base, and every requirement below is shaped around containing that.

*   <a id="LLR-PUR-01"></a>**LLR-PUR-01** — `purify_analyse` shall compute the hub-and-authority scores, the betweenness centrality, and the coreness of the **call view** alone, and of no other view of the graph. A global-state edge joins a writer to a reader; it is coupling and not invocation, and a layering read off a graph containing them would join every pair of functions sharing a variable.
    *Trace:* HLR-168 (Utility-Sink Detection), HLR-169 (God-Object Detection), HLR-170 (Peripheral Stripping by K-Core Decomposition).

*   <a id="LLR-PUR-02"></a>**LLR-PUR-02** — `purify_analyse` shall take the `Sdg` by constant pointer and shall leave it unmodified, producing the masked graph as a separate object no other stage is given. Masking in place and unmasking afterwards would make every analysis order-dependent and leave the run one early return away from reporting a fan-out that omits real calls — a wrong number carrying the authority of a measured one. A stage that cannot reach a masked graph cannot accidentally measure one.
    *Trace:* HLR-167 (Purification Confined to Recovery).

*   <a id="LLR-PUR-03"></a>**LLR-PUR-03** — `purify_analyse` shall check the return of every call it makes into the graph library and shall release every vector it allocated on both the success and the failure paths, the library's error handler having been installed non-aborting when the graph was built.
    *Trace:* HLR-125 (Memory Discipline), HLR-113.

*   <a id="LLR-PUR-04"></a>**LLR-PUR-04** — `purify_analyse` shall not ask the graph library for a hub-and-authority decomposition of a call view holding no edges, and shall leave every score at zero there instead. The decomposition is undefined on such a graph and the library says so on standard error, naming one of its own source files — which is neither a diagnostic a user of `elc` can act on nor `elc`'s own, the only thing that stream admits. A program whose functions call nothing has no hub-and-authority structure to find.
    *Trace:* HLR-038 (Diagnostics on stderr, Results on stdout), HLR-115.

*   <a id="LLR-PUR-05"></a>**LLR-PUR-05** — `purify_analyse` shall treat a graph too small to hold a distribution as an ordinary outcome rather than a failure, classifying nothing by centrality where fewer than two functions exist. A rank over zero other nodes is met by every threshold at once, which would classify a lone function as all three classes simultaneously.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-066.

## 57. `classify_nodes` ([src/purify.c](../src/purify.c))

The three classifications, the precedence between them, and the two properties of the mathematics that make determinism harder to satisfy here than anywhere else in the program.

*   <a id="LLR-CLS-01"></a>**LLR-CLS-01** — `classify_nodes` shall compare each threshold against a node's **position in the ordered distribution** of the score rather than against the score itself, expressed as the percentage of the *other* nodes scoring strictly below it, and shall make that comparison in integers. A betweenness value means nothing on its own — it scales with the size of the graph — so a fixed cut-off would classify every function in a large project and none in a small one; and a rank over *all* the nodes rather than the others would cap the top of a nine-function distribution at 88 per cent, leaving one default unusable on small projects and unusably loose on large ones.
    *Trace:* HLR-168 (Utility-Sink Detection), HLR-169 (God-Object Detection), HLR-171.

*   <a id="LLR-CLS-02"></a>**LLR-CLS-02** — `classify_nodes` shall decide whether two scores hold the same position in a distribution by a comparison made to a stated relative tolerance, floored at one so that the same figure serves a betweenness in the thousands and a HITS score in the thousandths. HITS is iterative and its scores are approximations; without a stated tolerance the same source classifies differently on two machines, and HLR-032 fails in a way no fixture would reliably catch.
    *Trace:* HLR-179 (Deterministic Classification), HLR-032.

*   <a id="LLR-CLS-03"></a>**LLR-CLS-03** — `classify_nodes` shall order the nodes of each distribution by an **exact** score comparison with the stable node identifier of HLR-033 breaking equal scores, and shall apply the tolerance of LLR-CLS-02 afterwards over that order, delimiting a run of equal scores by comparing each member against the run's first element. A sort whose comparator is tolerant rests on a relation that is not transitive, and its result would then depend on the order the library's sort happened to visit the elements in — the very property this requirement exists to remove.
    *Trace:* HLR-179 (Deterministic Classification), HLR-033.

*   <a id="LLR-CLS-04"></a>**LLR-CLS-04** — `classify_nodes` shall classify as a **utility sink** every function whose authority rank is at or above the configured sink-authority threshold *and* whose hub rank is at or below the configured sink-hub threshold. Both halves are required: a function passing the hub half alone is any leaf or entry point, and reporting one would name half the functions in a program a utility sink.
    *Trace:* HLR-168 (Utility-Sink Detection).

*   <a id="LLR-CLS-05"></a>**LLR-CLS-05** — `classify_nodes` shall classify as a **god object** every function whose betweenness rank and whose hub rank are each at or above their configured thresholds, and shall decide that class before the utility-sink test so that a function satisfying both is reported as a god object. Betweenness alone does not separate a monolithic dispatcher from a genuine intermediary a layering ought to keep; and masking a god object's edges subsumes masking its incoming ones, so the more specific claim is the more useful one to a reader.
    *Trace:* HLR-169 (God-Object Detection).

*   <a id="LLR-CLS-06"></a>**LLR-CLS-06** — `classify_nodes` shall classify as **peripheral** every function whose coreness, taken over the undirected neighbourhood of the call view, is strictly below the configured core depth, and shall make that test only where neither centrality test spoke. A *k*-core is the mutually connected centre of a program and a leaf hanging off it is peripheral whichever way its one edge points; and a function the centrality tests named is by construction part of that centre.
    *Trace:* HLR-170 (Peripheral Stripping by K-Core Decomposition).

*   <a id="LLR-CLS-07"></a>**LLR-CLS-07** — `classify_nodes` shall record, for each classification it makes, the measurement that triggered it and the value that measurement took, and shall attach no severity to any of them. A classification a reader cannot trace back to a number is an assertion; and a god object is an observation about the shape of a graph rather than a measurement banded against an accepted range, so there is nothing for a severity to mean and no published source to attribute one to.
    *Trace:* HLR-171 (Purification Thresholds Are elc's Own), HLR-174, HLR-101.

*   <a id="LLR-CLS-08"></a>**LLR-CLS-08** — `classify_nodes` shall apply the statements of a supplied manifest **after** every computed classification is in place, taking each statement's class as the class of the function it names and neither recomputing nor overruling it, and shall clear the metric and value that a computed class would have carried. Applying the manifest last is what makes "the statement governs" a property of the order rather than of a condition scattered through the three tests; and no measurement triggered a decision the user made, so reporting one beside a manifest row would present a number as the reason for a judgement it had no part in.
    *Trace:* HLR-177 (A Manual Classification Overrides a Computed One), HLR-174.

*   <a id="LLR-CLS-09"></a>**LLR-CLS-09** — `classify_nodes` shall record, for each manifest statement, whether it named a function the run analysed, and a statement naming none shall leave every classification unchanged rather than ending the run. Analysing one directory of a project whose manifest covers all of it is ordinary use, and rejecting the file there would make a manifest unusable exactly where a large code base most needs one — the rule a declared entry point matching nothing already follows (LLR-CTR-08).
    *Trace:* HLR-177 (A Manual Classification Overrides a Computed One).

## 58. `build_recovery_view` ([src/purify.c](../src/purify.c))

The masked copy itself: which edges each class costs a function, and why the three answers differ.

*   <a id="LLR-RCV-01"></a>**LLR-RCV-01** — `build_recovery_view` shall construct a new graph over the same vertex set from the graph's own call-edge table, and shall leave the `Sdg`'s call view holding every edge it held before. The copy is what makes HLR-167 structural: every measurement reported outside architecture recovery is taken over the graph as built, and nothing in the program can reach a masked one.
    *Trace:* HLR-167 (Purification Confined to Recovery).

*   <a id="LLR-RCV-02"></a>**LLR-RCV-02** — `build_recovery_view` shall omit every edge whose *target* is a utility sink and shall retain every edge whose source is one. The fusion a sink causes is between its callers, who are joined to one another through it; its own calls join nothing that was not joined already, and masking the node rather than the edges into it would remove the function's own position from view along with the fusion.
    *Trace:* HLR-168 (Utility-Sink Detection).

*   <a id="LLR-RCV-03"></a>**LLR-RCV-03** — `build_recovery_view` shall omit every edge touching a god object in either direction, and shall retain the node itself in the view. It short-circuits in both directions, so it loses both; and it is masked rather than excluded, since it is part of the connected centre the layering is being read from.
    *Trace:* HLR-169 (God-Object Detection).

*   <a id="LLR-RCV-04"></a>**LLR-RCV-04** — `build_recovery_view` shall mark every peripheral function as excluded from the view and shall omit every edge touching one, so that nothing downstream can assign it a recovered layer. A function `elc` did not consider is not a function `elc` placed at the edge of the architecture, and a view that did not distinguish the two would put every leaf in the bottom layer.
    *Trace:* HLR-170 (Peripheral Stripping by K-Core Decomposition).

*   <a id="LLR-RCV-05"></a>**LLR-RCV-05** — `build_recovery_view` shall number the vertices of the view with the graph's own stable node identifiers rather than renumbering the retained nodes, and shall account for every call edge as either retained or masked. Renumbering would put the determinism of HLR-179 on a mapping instead of on the identifier the rest of the run already agrees about; and a view that dropped an edge without counting it would report a masking a reader could not check.
    *Trace:* HLR-179 (Deterministic Classification), HLR-033, HLR-174.

*   <a id="LLR-RCV-06"></a>**LLR-RCV-06** — `build_recovery_view` shall decide each of the three masking rules on whether the classification's *action* is in force, not on the class alone, so that a class a manifest stated and left unmasked changes what is reported and not what the view holds. The class and the action are two facts: the usual correction a user makes is not that `elc` misread the graph but that it drew the wrong conclusion from a correct reading, and a view keyed on the class alone would force such a user to relabel the function as something it is not. The same predicate shall answer the question wherever else it is asked, so that a drawing of the view cannot show a graph the analysis never read.
    *Trace:* HLR-177 (A Manual Classification Overrides a Computed One), HLR-175, HLR-178.

## 59. `manifest_read` ([src/purify.c](../src/purify.c))

The read half of the one artefact `elc` round-trips. Two properties govern it: a manifest is reached only from a path the user gave, and *well-formed is not valid* — Jansson decides whether the file is JSON, and whether it is a manifest is this module's judgement.

*   <a id="LLR-MFR-01"></a>**LLR-MFR-01** — `manifest_read` shall read the file at the path it is given and shall search for a manifest nowhere: not in the working directory, not in the analysis target, not in any ancestor of either, and in no dotfile. A manifest is read because the user named it, exactly as a custom rule file is, and the zero-configuration guarantee is unchanged by the format existing — two people running the same command on the same tree must still obtain the same result.
    *Trace:* HLR-176 (The Manifest Is Read Only When Named), HLR-039.

*   <a id="LLR-MFR-02"></a>**LLR-MFR-02** — `manifest_read` shall reject a file it cannot read or parse with a diagnostic naming the path and quoting the line and column of the fault, shall return a failure the caller ends the run on, and shall leave no statement applied. The user named the file, so the failure is theirs to correct — the provenance rule HLR-116 draws for a custom rule named on the command line — and a person who hand-edited the file needs to be told *where* they broke it. A run governed by half of what its author wrote is worse than one that stops, because the half that took effect is invisible.
    *Trace:* HLR-176 (The Manifest Is Read Only When Named).

*   <a id="LLR-MFR-03"></a>**LLR-MFR-03** — `manifest_read` shall reject, in the same manner and with the same status, a file that is well-formed JSON and not a manifest: one carrying no format version, one carrying no array of classifications, one whose statement names no function, and one naming a class this build does not know. Well-formedness is a property of the syntax; whether the contents are a manifest is this module's judgement, and a class name it guessed at would apply a classification nobody wrote.
    *Trace:* HLR-176 (The Manifest Is Read Only When Named).

*   <a id="LLR-MFR-04"></a>**LLR-MFR-04** — `manifest_read` shall accept only the format version this build reads and shall reject any other, naming the version found and the version expected. Versioned in the manner of the XML record: a manifest written by a later build is rejected rather than half-understood, which is the failure a reader would never see because a file that parses and means something else looks like a working manifest right up to the point where it silently classifies nothing.
    *Trace:* HLR-176 (The Manifest Is Read Only When Named), HLR-061.

*   <a id="LLR-MFR-05"></a>**LLR-MFR-05** — `manifest_read` shall treat a statement's file as optional, matching by function name alone where it is absent or empty and requiring an exact match of the analysed file's path where it is given; and shall default an absent masking flag to the action the stated class carries. Naming the file is the precise form and is what `elc` writes; omitting it is the convenience a person hand-editing the file reaches for, and a project with two static functions of one name is the case that makes the distinction earn its keep.
    *Trace:* HLR-175 (The Purification Manifest), HLR-177.

## 60. `manifest_write` ([src/purify.c](../src/purify.c))

The write half. The only place in the project a third-party library *emits* a format, and the exception is the round trip: a hand-rolled writer paired with a library reader would be two implementations of one format with `elc` on both ends of the disagreement.

*   <a id="LLR-MFW-01"></a>**LLR-MFW-01** — `manifest_write` shall record one statement for every classification the run made — naming the function, the file defining it, the class assigned, and whether that class is masked — and shall omit the functions `elc` concluded nothing about, save where a manifest statement made the conclusion. It shall order the statements by file and then by the line the function starts on, which is the order the transparency report presents the same rows in: a person reading the report and editing the manifest then finds them in the same place, and two runs over one tree produce identical files.
    *Trace:* HLR-175 (The Purification Manifest), HLR-032.

*   <a id="LLR-MFW-02"></a>**LLR-MFW-02** — `manifest_write` shall stamp the file with the format version this build reads, and shall write it in a form `manifest_read` accepts unmodified. The round trip is why the format goes through one library in both directions rather than a writer of `elc`'s own: two independent implementations of one format produce the failure this project dislikes most — a manifest `elc` emitted that `elc` then rejects, or worse, silently reads as something other than what it wrote.
    *Trace:* HLR-175 (The Purification Manifest), HLR-176.

*   <a id="LLR-MFW-03"></a>**LLR-MFW-03** — `manifest_write` shall be given a path derived from the report's own output path by the extension substitution every companion follows, shall accept no path of its own, and shall write nothing where the report has no path to derive a name from. The file it writes shall end with a newline: a manifest is meant to be hand-edited and kept under version control, and a text file without a final newline is one every such tool complains about.
    *Trace:* HLR-119 (Companion Artefact Naming), HLR-104, HLR-175.

## 61. `recover_layers` ([src/recover.c](../src/recover.c))

The recovery pass: an ordering of the purified view, the fold that turns it into a layering, and the argument list a user passes back. Everything here is subordinate to one boundary — what this produces is a *proposal*, and a proposal is never the baseline it is measured against.

*   <a id="LLR-RCY-01"></a>**LLR-RCY-01** — `recover_layers` shall order the recovery view topologically and fold that order into per-directory layers, over the vertices the view retained and over no others. A layering is read from the purified view rather than from the graph as built, which is the whole purpose of the purification preceding it: a topological ordering over a graph a logger and a dispatcher have fused collapses into one tangled stratum that describes nothing.
    *Trace:* HLR-172 (Automated Layer Recovery), HLR-160.

*   <a id="LLR-RCY-02"></a>**LLR-RCY-02** — `recover_layers` shall test the recovery view for acyclicity before ordering it, and where it is cyclic shall report the strongly connected components in place of a layering, each rendered as its membership in ascending stable node identifier rather than as a chain of arrows, and shall propose nothing. Ordering a cyclic graph anyway would present an invention as a reading — the rule HLR-090 applies to call depth, applied to the same impossibility. A strongly connected component is a *set*: every member reaches every other, but the decomposition yields no order, and an arrow chain would assert a path that may not exist.
    *Trace:* HLR-172 (Automated Layer Recovery), HLR-033.

*   <a id="LLR-RCY-03"></a>**LLR-RCY-03** — `recover_layers` shall treat a recovery view retaining no function as an omission with its reason stated rather than as an error or an empty proposal, and shall count separately the functions whose edges the masking cut and those excluded from the view entirely. An analysis short of its inputs is omitted with its reason stated; and a layering recovered from a graph with parts of it set aside is a claim about that graph and not about the program, so the proposal states how much was set aside before its rows say anything.
    *Trace:* HLR-115 (Analyses Requiring User Declarations), HLR-172.

*   <a id="LLR-RCY-04"></a>**LLR-RCY-04** — `recover_layers` shall render the proposal as the `--stratum` and `--stratum-order` arguments that would declare it, quoting every pattern and the order itself, naming each layer after the first directory in it and suffixing a name that would otherwise repeat, and emitting the declarations deepest directory first. Adoption is then a copy rather than a transcription, which is what HLR-173 requires, and the argument list is the boundary that requirement draws in the one form a reader cannot mistake for a measurement. The quoting is load-bearing: `>` is a shell redirection, and an unquoted order would create files named after the layers and hand `elc` a partial order. So is the declaration order: `elc` takes the first declared layer whose pattern matches a file and a directory wildcard matches everything beneath it, so an ancestor declared before its child would claim the child's files — while the ordinals come from `--stratum-order` beside them, leaving the declaration order free to be chosen for correctness. Two layers sharing a name would silently become one, since repeating a name adds patterns to the layer already declared.
    *Trace:* HLR-173 (A Recovered Layering Is a Proposal, Never a Baseline), HLR-078.

*   <a id="LLR-RCY-06"></a>**LLR-RCY-06** — `recover_layers` shall order the recovery view with its self-calls disregarded, and shall report a *mutual* cycle in place of a layering while reporting a self-call not at all. A function calling itself makes the graph cyclic in the strict sense and orders nothing: the edge runs from a node to itself and says nothing about where that node sits relative to any other. Reporting it here would repeat a fact the recursion analysis of HLR-089 already states, and would cost every project holding one recursive function the whole of this analysis — while a mutual cycle genuinely leaves no order to read and is still reported. A self-call shall likewise not weight a function's position, since a function coupled to nothing but itself had no part in choosing where its directory sits.
    *Trace:* HLR-172 (Automated Layer Recovery), HLR-089.

*   <a id="LLR-RCY-05"></a>**LLR-RCY-05** — `recover_layers` shall produce identical rows and an identical argument list on two runs over one graph, breaking every ordering it makes by a key that is a property of the source tree rather than of the order a library enumerated something in. The ordering underneath is the graph library's and the fold's positions are computed rather than given, so neither may reach the output carrying an enumeration order with it.
    *Trace:* HLR-179 (Deterministic Classification), HLR-032.

## 62. `layer_by_directory` ([src/recover.c](../src/recover.c))

The fold, which is the whole of what this module decides. A topological order is not a layering: it orders functions, and an architecture orders directories.

*   <a id="LLR-LYR-01"></a>**LLR-LYR-01** — `layer_by_directory` shall group the ordered functions by the directory owning the component each belongs to, reading that directory from the record discovery already made of it rather than deriving it from the path again. An architecture orders directories; a proposal listing functions would be a topological order wearing a layering's name. And more than one analysis groups by directory — two consumers each slicing a path for themselves is how two of them come to disagree about which directory a file is in.
    *Trace:* HLR-172 (Automated Layer Recovery), HLR-160 (Directory Grouping).

*   <a id="LLR-LYR-02"></a>**LLR-LYR-02** — `layer_by_directory` shall fix a directory's position as the mean topological position of its functions weighted by the retained edges each carries, falling back to the unweighted mean where the directory's functions carry no retained edge at all, and shall not fix it at the directory's earliest or latest member. One function reaching far down the order must not drag its whole directory with it: a completion callback defined in a service layer and called from the layer beneath it sits at the very bottom of the order, and a fold reading the latest member would turn the service layer upside down on the strength of it. A weighted mean over a total weight of zero is not a number, which is what the fallback is for.
    *Trace:* HLR-172 (Automated Layer Recovery).

*   <a id="LLR-LYR-03"></a>**LLR-LYR-03** — `layer_by_directory` shall compare two positions exactly, as fractions and never as floating point, by a method no product of which can overflow; shall place directories at equal positions in one layer; and shall break an ordering tie by the directory path. A comparison deciding two equal means unequal on the last bits of a division would split a layer on one machine and not on another. Cross-multiplying is the obvious exact comparison and overflows for a large project, since each numerator is a sum of position × degree over every retained function. And cutting between two directories the ordering places level with one another would invent a dependency direction the graph does not hold.
    *Trace:* HLR-179 (Deterministic Classification), HLR-101.

*   <a id="LLR-LYR-04"></a>**LLR-LYR-04** — `layer_by_directory` shall skip every function the recovery view excluded, and shall give no layer to a directory all of whose functions were excluded. A function `elc` did not consider is not a function `elc` placed at the edge of the architecture, and a fold that read the excluded vertices back in would put every leaf in the bottom layer — the one thing HLR-170 names.
    *Trace:* HLR-170 (Peripheral Stripping by K-Core Decomposition), HLR-172.

## 63. `report_set_recovery` ([src/recover.c](../src/recover.c))

Where the proposal goes, and — more to the point — where it does not.

*   <a id="LLR-PRP-01"></a>**LLR-PRP-01** — `report_set_recovery` shall copy onto the assembled report the directories placed and their layers, the groups reported where no ordering existed, the counts of what was masked and excluded, and the argument list a user would pass back. A record of a run carries no graph to re-order, so a proposal absent from the model is one a regenerated report cannot present.
    *Trace:* HLR-172 (Automated Layer Recovery), HLR-054.

*   <a id="LLR-PRP-02"></a>**LLR-PRP-02** — `report_set_recovery` shall write to no field the conformance analyses read: not the declared-strata state, not the layering rows, not either conformance index, and not the subjects of the dependency matrix. The proposal reaches the renderers and the saved record and nothing else, and `arch.c` shall be given no path to a recovery result at all. `elc` measuring conformance against its own proposal would be a tool marking its own homework — every code base would conform, because the standard would have been read off the thing it was judging — so the boundary is kept by the dependency direction rather than by care.
    *Trace:* HLR-173 (A Recovered Layering Is a Proposal, Never a Baseline), HLR-115, HLR-101.

## 64. `graph_write_purify_dot` ([src/format_graph.c](../src/format_graph.c))

The raw and purified drawings. Two of them because seeing what purification did is what lets a user judge whether it did the right thing, and one drawing of the result cannot show what it acted on.

*   <a id="LLR-DRW-01"></a>**LLR-DRW-01** — The drawings shall be written only where they were asked for, where the report has an output path to derive their names from, and where the run is not regenerating from a saved record. Off by default, unlike the annotated call tree and like the GraphML export; absent with the report on standard output because there is no path to derive a name from; and absent in regeneration because a record carries what a run concluded and not the graph it concluded it from.
    *Trace:* HLR-178 (Raw and Purified Graph Exports), HLR-104, HLR-122.

*   <a id="LLR-DRW-02"></a>**LLR-DRW-02** — Both drawings shall derive their names from the report's output path by the extension substitution every companion follows, shall accept no path of their own, and shall be written together. Neither shall replace the annotated call tree, which answers a different question and is enabled by a different default. The pair exists to be compared, so producing one without the other would answer half the question the option exists to answer.
    *Trace:* HLR-119 (Companion Artefact Naming), HLR-178, HLR-102.

*   <a id="LLR-DRW-03"></a>**LLR-DRW-03** — The purified drawing shall contain every function the raw drawing contains, drawing a masked one greyed and an excluded one greyed and dashed, each labelled with the class assigned to it and each holding no edge — and shall delete none of them. A drawing that removed them could not show what purification did, which is the entire reason there are two; distinguished rather than removed is what lets a reader see which functions were set aside and what the graph looks like without them, in one glance.
    *Trace:* HLR-178 (Raw and Purified Graph Exports), HLR-174.

*   <a id="LLR-DRW-04"></a>**LLR-DRW-04** — The raw drawing shall hold every call edge of the graph as built and shall carry no classification on any node; the purified drawing shall hold the edges the recovery view retained, decided by the same predicate the view itself was built from rather than by rules restated here. The raw drawing is the graph *before* the masking — one that anticipated it would leave the pair with nothing to compare — and two answers to one question about which edges survive is how a drawing comes to show a graph the analysis never read.
    *Trace:* HLR-178 (Raw and Purified Graph Exports), HLR-167.

## 65. `diag_printf` ([src/diag.c](../src/diag.c))

The diagnostic stream, and the debug companion that records it. The one module holding global mutable state, for the reasons SDD §25 sets out and under the bounds it states.

*   <a id="LLR-DBG-01"></a>**LLR-DBG-01** — `diag_printf` shall write each diagnostic to standard error, and additionally to the debug companion where one is open, so that the two cannot diverge and no call site chooses between them. Its signature shall be the one a caller would have passed to `fprintf(stderr, ...)`, which is what makes the conversion of every existing call site mechanical rather than a rewrite of each.

    The bytes standard error receives shall be identical whether or not a companion is open. The companion records a run and is not a result of one; a diagnostic aid that altered what it observed would be worse than none (HLR-194).
    *Trace:* HLR-194 (The Debug Companion), HLR-038.

*   <a id="LLR-DBG-02"></a>**LLR-DBG-02** — `diag_open` shall write the invocation at the head of the companion, and shall leave the module inert where it is given no path — every later call then writing to standard error alone, so that no call site tests whether a companion exists.

    A companion that cannot be opened shall be a diagnostic and a recorded failure rather than a fatal one: the user asked for a report and a debug file, and losing the second is no reason to withhold the first.
    *Trace:* HLR-194 (The Debug Companion), HLR-119.

*   <a id="LLR-DBG-03"></a>**LLR-DBG-03** — Every write to the companion shall be flushed before the call returns.

    This is the property the companion exists for rather than an implementation detail. A run that faults, is killed, or exhausts memory is precisely the run worth diagnosing, and a log assembled in memory and written at exit is lost exactly then. The cost is a flush per diagnostic, which a run producing a diagnostic per file will not notice.
    *Trace:* HLR-194 (The Debug Companion).

*   <a id="LLR-DBG-04"></a>**LLR-DBG-04** — `diag_parse_failure` shall record one unparsable region as the file, the lines it spans, and the source text of those lines, read out of the mapping with an explicit length since the mapping is not NUL-terminated.

    The text shall be bounded, and where it is truncated the number of lines omitted shall be stated. A file the grammar could follow nowhere would otherwise be copied into the log entire, which serves no reader and may disclose more of a private tree than whoever attached the log intended (HLR-195).
    *Trace:* HLR-195 (Unparsable Source Recorded in the Debug Companion).

*   <a id="LLR-DBG-05"></a>**LLR-DBG-05** — The companion shall carry a timestamp on each entry, and the report shall carry none. A log nobody watched being produced needs to say when each thing happened; a report must be byte-identical across two runs over one target (HLR-032). The companion is a record *of* a run rather than a result *of* one, which is the line the timestamps sit on.
    *Trace:* HLR-194 (The Debug Companion), HLR-032.

## 67. `symname_reduce` ([src/symname.c](../src/symname.c))

The one reduction every name comparison goes through. Both requirements below are about a spelling meeting another spelling of the same name, and the second is about the one family of names that must not be reduced at all.

*   <a id="LLR-SNM-01"></a>**LLR-SNM-01** — `symname_reduce` shall reduce a name to its last `::`-separated component with any parameter list, trailing template argument list, leading return type, and Rust legacy hash removed, and shall be the only implementation of that reduction in `elc`.

    Scope resolution and template nesting shall be tracked together: the last `::` that begins a component is the last one at angle depth zero, or the qualification inside `Copy_seq<FACE::Sequence<int> >` is read as the name's own and the result is a fragment of a type.

    **One implementation, because the second copy is the defect.** Two reductions begin identical, one is corrected, and the tool resumes disagreeing with itself in a place no test looks — which is exactly how the debug-information path came to compare unreduced names while the image-symbol path reduced them (HLR-200).
    *Trace:* HLR-200 (Names Compared in One Reduced Form), HLR-014.

*   <a id="LLR-SNM-02"></a>**LLR-SNM-02** — `symname_reduce` shall step over an `operator` token whole, and shall remove a trailing template argument list **only where that list closes** — that is, only where the name ends in `>` and a matching `<` is found at depth zero before it. A name with nothing left to compare shall reduce to nothing rather than to the empty string.

    Four operators end in a bracket that opens nothing: `operator>`, `operator>>`, and — through the scope and signature scans rather than this one — `operator<` and `operator<=`. A reduction that truncated at any trailing `>` would leave `operator`, and one that also stripped the scope would leave nothing. An empty key is the worse outcome of the two, because it does not fail: it matches every other name that reduced to nothing, and the map answers confidently about a function it has never seen.

    `operator()` and the `(anonymous namespace)` qualifier are the same hazard in the signature scan, and are stepped over for the same reason.
    *Trace:* HLR-200 (Names Compared in One Reduced Form).

## 66. `preproc_expand` ([src/preproc.c](../src/preproc.c))

Expansion and filtering of one file. The first requirement is the subprocess; the rest are all about not letting the subprocess's output become the measurement.

*   <a id="LLR-PRE-01"></a>**LLR-PRE-01** — `preproc_expand` shall run the preprocessor as a child process with its standard output on a pipe and its standard error discarded, shall read that pipe to end-of-file **before** collecting the child's exit status, and shall write no file.

    Draining before waiting is the requirement rather than the natural order. A file whose expansion exceeds the pipe capacity — which is every C++ file that includes anything — would otherwise deadlock: the child blocked writing to a full pipe, the parent blocked waiting for a child that cannot exit.

    Standard error is discarded because the preprocessor's complaints are about a build configuration `elc` does not have and cannot fix. A missing header on a cross-compiled tree would otherwise produce pages of diagnostics per file for a condition HLR-206 states once.
    *Trace:* HLR-202 (Macros Expanded by the Compiler's Preprocessor), HLR-043.

*   <a id="LLR-PRE-02"></a>**LLR-PRE-02** — `preproc_expand` shall invoke the preprocessor so that **comments are preserved** (`-C`) and line markers are emitted, and shall pass no include path, define, or flag it invented.

    A preprocessor discards comments by default. No figure depends on them today — they are excluded from effective lines rather than counted (HLR-016) — and they are kept anyway, because the parsed buffer should differ from the source only where the expansion required it. A difference nothing needs is one a reader of a finding must still account for.

    An invented `-I` would read a header the user did not name, which HLR-039 forbids, and would be worse than reading none: reaching the *wrong* header makes the expansion succeed and be wrong, where reaching no header makes it fail and fall back.
    *Trace:* HLR-202 (Macros Expanded by the Compiler's Preprocessor), HLR-039.

*   <a id="LLR-PRE-03"></a>**LLR-PRE-03** — The filter shall recognise a line marker as `#` in the first column followed by a space and a decimal line number, then a quoted file name, and shall change state on nothing else. A marker naming the file under analysis shall select the appending state; every other marker, including one naming a project header, `<built-in>` or `<command-line>`, shall select the ignoring state.

    Only the appending state copies. That is what keeps the cost of a fifty-thousand-line C++ expansion proportional to what is kept rather than to what was produced.

    The quoted name shall be unescaped before comparison and compared as a canonical path. A path holding a quote or a backslash reaches the marker escaped and would otherwise never compare equal; a build reaching its sources through a symbolic link compares unequal, never enters the appending state, and falls back — the safe direction, and the one `dwarfline.c` already takes for the same reason.

    A project header is discarded like a system one. It is a file in its own right, analysed on its own account when the target reaches it, and counting it once per file that includes it would inflate every figure by its inclusion count.
    *Trace:* HLR-203 (Expanded Output Filtered to the File Under Analysis).

*   <a id="LLR-PRE-04"></a>**LLR-PRE-04** — Before appending the lines following a marker that announces line *N*, the filter shall emit newlines until the buffer holds *N* − 1 lines. A marker announcing a line the buffer has already passed shall be ignored.

    This is what makes the expanded buffer measurable rather than merely parseable. Every location `elc` reports and every line-based figure it computes would otherwise be displaced by however much the filter discarded above it, and a reader cannot detect the displacement to discount it (HLR-204).

    **A buffer that is *ahead* of the marker shall be brought back, and only in the two ways that lose nothing.** Trailing blank lines the filter emitted as padding are withdrawn. Beyond that, the newlines separating the physical lines one source line's expansion was spread across are turned back into spaces — `return NULL;` reaches the buffer as `return` / `((void *)0)` / `;`, three lines where the source had one, and every line below it would otherwise sit two too low.

    Neither loses a token. Those lines *were* one line, and rejoining them restores the line the source holds. The rule is not "never move backwards" but "never lose a line": a line the buffer never had is one it must give back, or the drift accumulates over the whole file and every location below the first multi-line expansion is wrong.
    *Trace:* HLR-204 (Expansion Preserves Every Reported Location), HLR-032.

*   <a id="LLR-PRE-05"></a>**LLR-PRE-05** — `preproc_expand` shall return a null buffer, and a status naming the reason, where the child could not be started, exited non-zero, or produced output holding no marker for the file under analysis. It shall return non-zero only on allocation failure.

    **A caller that declines an expansion it did obtain shall release the buffer with the decision**, so that a null `text` is the single meaning of "this file was not expanded" everywhere it is read. That is not tidiness: the pointer selects the parse *and* suppresses the repair of HLR-196, so a file left holding a buffer nobody used is parsed as written and denied the repair — neither of the two paths, and worse than either.

    **Output naming the file nowhere is a failure and not an empty file.** A zero-line measurement of a file that has lines is the silent wrong answer this module exists not to produce, and it is indistinguishable in the report from a file that is genuinely empty.

    No failure here shall fail a run or emit a per-file diagnostic. Falling back is the ordinary condition of a tree analysed away from its build environment (HLR-205), and one message per file teaches a reader nothing after the first.
    *Trace:* HLR-205 (Expansion Failure Falls Back to the Source as Written).

*   <a id="LLR-PRE-07"></a>**LLR-PRE-07** — `analyze_file` shall parse the file as written and run the conditional-region pass over that tree before expanding it, shall expand only where that pass left nothing undecided, and shall forward the run's `-D` definitions to the preprocessor.

    The order is the requirement. Running the conditional pass after expansion would run it over a tree with no directives in it, and it would report nothing undecided about a file full of conditions the preprocessor guessed at (HLR-208).

    Refusing to expand a file with an undecided region is what keeps the effective-line count honest. `elc` leaves such a region whole and counts both branches; the preprocessor keeps one. Expanding it would replace the region's measurement with an arbitrary configuration's, under a figure the reader has been told is complete.

    The definitions must reach the preprocessor or the two disagree, and the report would name one configuration while measuring another.
    *Trace:* HLR-208 (Conditional Regions Answered Before Expansion), HLR-076.

*   <a id="LLR-PRE-06"></a>**LLR-PRE-06** — The filter shall record each standard-library header the marker stream names, de-duplicated, classifying it as belonging to the C or the C++ standard library, and shall leave the list empty for a file that fell back.

    The markers name every file the preprocessor opened, so the list is a by-product of the filter rather than a second analysis. Classification is by name against the two standards' header sets, because the path alone cannot distinguish them — both live under the same system directories, and a C++ implementation's `<cstdio>` and C's `<stdio.h>` sit side by side.

    An empty list on a fallen-back file is not a claim that the file uses no standard library. It is the absence of an answer, which HLR-206's provenance is what lets a reader tell apart from the answer "none".
    *Trace:* HLR-207 (Standard-Library Dependence Reported).

## 68. `repair_parse` ([src/repair.c](../src/repair.c))

Repair of the regions the grammar rejected. Every requirement below is a restriction, and each is what separates a bounded repair from a tool inventing the code it measures.

*   <a id="LLR-RPR-01"></a>**LLR-RPR-01** — `repair_parse` shall parse the buffer as given, and where the root node carries no error shall return that tree and the caller's own buffer, having copied nothing and parsed once.

    The early return is the requirement rather than an optimisation. It is what leaves the single-parse rule of HLR-076 exactly as it was for source that needs no repair, and what makes the cost of this feature to such a code base one test of the root node.
    *Trace:* HLR-196 (Repair Confined to Rejected Regions), HLR-076.

*   <a id="LLR-RPR-02"></a>**LLR-RPR-02** — Where the grammar rejects anything, `repair_parse` shall copy the buffer before rewriting it, and shall rewrite only bytes lying within a rejected region.

    The copy is required because the buffer is a read-only mapping of a file `elc` does not own. Confinement to the rejected regions is required because the rules are heuristics about the shape of a failure: applied to text the grammar accepted, a heuristic is a tool editing a measurement it had already taken correctly.
    *Trace:* HLR-196 (Repair Confined to Rejected Regions), HLR-039.

*   <a id="LLR-RPR-03"></a>**LLR-RPR-03** — Every rule shall replace text with text of the **same width in bytes**, within a single line.

    Same width rather than merely the same line count, because it costs nothing and buys the byte offsets too: a region's extent stays valid across a repair, so the regions collected before a pass need not be recollected during it. The line count is what HLR-197 requires and what keeps every line-based measurement and every reported location correct; the width is how this implementation obtains it.

    An upper-case identifier adjacent to a string literal shall become an empty string literal padded to the width it replaced; an identifier in front of a declaration shall become blanks; an identifier alone before `=` at file scope shall be given a type by consuming the blanks around it. A rule whose replacement would not fit the width it replaces shall decline to fire.
    *Trace:* HLR-197 (Repairs Preserve the Line Count).

*   <a id="LLR-RPR-04"></a>**LLR-RPR-04** — `repair_parse` shall re-parse after each pass and compare the number of rejected regions with the number before it. Where the number did not fall, it shall restore the buffer to its state before the pass and stop.

    This is what makes a wrong rule cheap rather than dangerous. A rule matching a shape it should not produces a buffer the grammar rejects in the same places or in worse ones; the comparison notices, the restore undoes it, and the file is measured unrepaired — which is where it started. Without the comparison a rule that repairs one region into a shape another rule rejects would run until the process was killed, on source `elc`'s authors never saw.
    *Trace:* HLR-198 (Repair Terminates).

*   <a id="LLR-RPR-05"></a>**LLR-RPR-05** — Rules shall be tried in a fixed order against regions taken in document order, and the first rule whose shape matches a region shall be the one applied.

    Two runs over one target must repair identically, or every figure downstream of a repair varies between runs and HLR-032's byte-identical guarantee fails in the one place hardest to notice — a report that is *nearly* the same.
    *Trace:* HLR-198 (Repair Terminates), HLR-032, HLR-033.

*   <a id="LLR-RPR-06"></a>**LLR-RPR-06** — `repair_parse` shall count the repairs it made, by rule, and `analyze.c` shall carry that tally onto the file's metrics so the report can declare it and the debug companion can record each repair with the text before and after.

    A repair is a guess the grammar could not make. A report that presented a repaired figure as a measured one would be the confidently-wrong result `elc` exists to avoid, and worse than the parse error it replaced — the figures look ordinary, and nothing on the page distinguishes them.
    *Trace:* HLR-199 (Repairs Are Declared), HLR-194.

## 69. `collect_visibility` ([src/analyze.c](../src/analyze.c))

What the language says about a function's reach, and the one rule that lets four languages express it in the same file format.

*   <a id="LLR-VIS-01"></a>**LLR-VIS-01** — `collect_visibility` shall run the language's `visibility.scm` where the module supplies one, and shall record, for each `@function.public` or `@function.private` capture, the **byte offset of the captured node** and which of the two it was. A capture whose name begins `_` shall be ignored.

    The byte offset is the key because it is the same node `functions.scm` captures as `@function.name`. Matching on it means the two queries agree on the identity of a function without either knowing how the other finds one — where matching on a name would collapse two same-named statics, and matching on a line would collapse a nested function with its host.

    The `@_` captures are how a pattern binds a node for a predicate to test — the C query compares a storage-class specifier against `static` that way — and are not functions.
    *Trace:* HLR-209 (Function Visibility Reported), HLR-107.

*   <a id="LLR-VIS-02"></a>**LLR-VIS-02** — Where more than one pattern captures the same node, `collect_visibility` shall keep the **first** recorded and discard the rest.

    Since a query cursor reports matches in pattern order, this is "the earliest pattern in the file decides", and it is the whole of what makes one file format serve four languages. C's default is external linkage, so its query states `static` first and a catch-all claims everything else public; Rust's default is private, so its query states `pub` first and the catch-all claims everything else private. A rule such as "private wins" would serve C and invert Rust.

    `collect_inactive_regions` already resolves overlapping conditional patterns this way, and a second convention for the same problem would be one too many.
    *Trace:* HLR-209 (Function Visibility Reported).

*   <a id="LLR-VIS-03"></a>**LLR-VIS-03** — A function whose name node no capture claimed shall be reported as **unknown**, and a module supplying no `visibility.scm` shall leave every function unknown. The renderer shall present that state as a dash and never as `public`.

    *Not analysed* and *public* are different claims, and the second is the one that misleads: a reader scanning for a module's interface would take every function of an unanalysed language for part of it. This is the asymmetry HLR-138 draws for a language with no dead-code query, applied to a third kind of absence.
    *Trace:* HLR-209 (Function Visibility Reported), HLR-138.

*   <a id="LLR-VIS-04"></a>**LLR-VIS-04** — `functions_section` shall render the location as `path:line` in the File column, the file's language immediately after it, the visibility immediately after the name, and the extent as `end - start + 1`.

    The column order is the requirement rather than a presentation choice: the visibility answers a question about the function just named, so it belongs beside the name and not at the end of eight numeric columns (HLR-209).

    The complete-record writers shall be unchanged in their line fields and shall carry the visibility as its own attribute, so that a report regenerated from a record says what the direct run said (HLR-056) without a consumer having to split a string to recover a number (HLR-014).
    *Trace:* HLR-210 (Function Location Reported Navigably), HLR-014 (Per-Function Identity), HLR-056.

## 70. `region_evidence` ([src/analyze.c](../src/analyze.c))

What the image's line information says about a conditional region the source could not decide, and the two shapes the evidence takes.

*   <a id="LLR-EVD-01"></a>**LLR-EVD-01** — `region_evidence` shall answer `EVIDENCE_NONE` where no image was supplied and where `dwarfline_covers` is false for the file under analysis.

    **The coverage test comes first, and it governs.** A translation unit compiled without debug information contributes no line entries at all, so a rule keyed on absence alone would find every region of it inactive and delete each one — evidence of nothing at all read as evidence of everything. This is the same two-part contract `prune_uncompiled_lines` follows at the finer grain, and it is followed here for the same reason (HLR-154).

    A run with no image reaches the first test and stops, which is what makes HLR-141's promise hold without a special case: the answer is "no evidence", the region stays undecidable, and the figures are the ones the run reported before this function existed.
    *Trace:* HLR-211 (Conditional Regions Decided from the Image), HLR-154, HLR-141.

*   <a id="LLR-EVD-02"></a>**LLR-EVD-02** — Where the region carries an alternative, `region_evidence` shall answer `EVIDENCE_ACTIVE` where the region's own lines produced an instruction and the alternative's did not, `EVIDENCE_INACTIVE` where the alternative's did and the region's did not, and `EVIDENCE_NONE` otherwise. The region's own lines are those from its first line to the line the alternative begins on.

    **The strongest form the evidence takes, and self-contained**: exactly one of two branches was compiled, and the image says which. Nothing outside the region is consulted, so nothing outside it can mislead.

    `EVIDENCE_NONE` for both and for neither is the requirement rather than a gap. *Neither* is what a function the build never emitted looks like, and deciding a region from it would prune half of a function that is already absent for a different reason. *Both* is a contradiction the line table can produce under inlining, and a rule that resolved it would be picking one of two answers by the order they were tested in.
    *Trace:* HLR-211 (Conditional Regions Decided from the Image).

*   <a id="LLR-EVD-03"></a>**LLR-EVD-03** — Where the region carries no alternative, `region_evidence` shall answer `EVIDENCE_ACTIVE` where its lines produced an instruction, and `EVIDENCE_INACTIVE` where they did not **and** a line before the region and a line after it, in the same file, both did. Otherwise it shall answer `EVIDENCE_NONE`.

    The bracketing is what makes the absence mean anything: code before the region and code after it were compiled, so the line table was being written across this stretch of the file, and the gap is a gap rather than the edge of what the build described. Without it a region past the last line the build emitted — or in a file whose only compiled code lies elsewhere — would be judged inactive on the strength of a mapping that stops short of it.

    Measured on `avrOS`, the bracket is what refuses the case this rule reads worst: a file-scope `#ifdef CPU_CLI` holding one `ADD_COMMAND(...)` near the top of `cpu.c`, above every function, has no compiled line before it and stays undecidable — where a rule without the bracket would have judged it from evidence that was never about it, since a data definition produces no line entry whether or not it was compiled.

    The active answer needs no bracket, because a region with no alternative that holds excludes nothing: the decision is real but it removes no code, and the only thing it changes is that the region is no longer counted undecided.
    *Trace:* HLR-211 (Conditional Regions Decided from the Image), HLR-154.

*   <a id="LLR-EVD-04"></a>**LLR-EVD-04** — `apply_cond_region` shall consult the evidence only after the query's own constants and the `-D` set have both failed to settle the region, and shall count each region it settles this way in a figure separate from the undecided count.

    **Order of authority.** A `-D` is what the user says this configuration is; the evidence is about one build that was made. Asking the definitions first is what stops a measurement overruling a declaration (HLR-132), and it is why a run that supplies the defining `-D` reports no region decided from the image at all.

    The separate count is the requirement's own: a region settled from evidence and a region settled from a definition are different claims, and a reader who cannot tell them apart has been handed evidence wearing the authority of a definition (HLR-211). It is reported with the image's other provenance, beside the line count the same evidence produced at the finer grain (HLR-155).
    *Trace:* HLR-211 (Conditional Regions Decided from the Image), HLR-132, HLR-133.

## 71. `collect_placed` ([src/report.c](../src/report.c))

The functions the image defines that the parse never reached, and the one rule that keeps this table from contradicting the table beside it.

*   <a id="LLR-PLC-01"></a>**LLR-PLC-01** — `collect_placed` shall record one row — the function's name, the analysed file, and the line — for each entry of the image's origin map whose file is an analysed file, whose recorded line is non-zero, and whose line lies within no reported function's extent. Rows shall be ordered by file, then line, then name.

    The map holds every definition the image describes, most of them written in files this run was never pointed at, so the file comparison is what makes the answer per file. A definition with no recorded line is skipped rather than placed at line zero: the entry is still a placement for HLR-193's purposes and a location is the whole of what this table adds.

    The extent test is a linear scan over the file's functions rather than a search, because their ranges are not disjoint — a nested named function lies inside its host (HLR-067) — and a binary search over start lines would answer for whichever run it landed in.

    The ordering is the absent list's, because the two are the two directions of one mismatch and a reader consults them together (HLR-032, HLR-143).
    *Trace:* HLR-212 (Functions the Image Places and the Parse Did Not Reach), HLR-032.

*   <a id="LLR-PLC-02"></a>**LLR-PLC-02** — A row shall be recorded only where `elfsyms_defines` is true for the same name: the symbol table decides *which* functions these are, and the debug information decides only *where* they are.

    **The two disagree, and ordinarily.** A link that discards unused sections removes the code while the compiler's subprogram entry for it stays in `.debug_info`, describing a function the image no longer contains. Measured on an AVR build of `avrOS`, believing the debug information alone reported **82** such functions where **11** were real — and every one of the other 71 was already named, rightly, in the list of functions the image does *not* define (HLR-143). The two tables would have contradicted each other on the same page.

    So this asks the question HLR-140's filter asks, of the same authority. The debug information then supplies the location, which is the one thing the symbol table cannot.
    *Trace:* HLR-212 (Functions the Image Places and the Parse Did Not Reach), HLR-143, HLR-140.

*   <a id="LLR-PLC-03"></a>**LLR-PLC-03** — The rows shall be held on the report beside the per-file metrics and never appended to a file's function list, and shall be rendered as three columns — function, file, line — with no figure beside them.

    **Where they are kept is the requirement, not an implementation preference.** `graph.c` builds one node per entry of every file's function list, indexed by a running offset over the counts; a row added there would acquire a node, a fan-out of nought, an ELOC of nought and a complexity of one, and would be banded and counted with the rest. Every one of those figures would be a measurement of something nobody measured (HLR-133, HLR-138), and a call to such a function is counted unresolved instead, where every call the graph cannot represent is counted (HLR-077).

    The renderer has no columns for the figures, so no later change can fill them in by accident. `collect_placed` runs from `report_set_image`, which is the one entry point holding both the image and an assembled report, and runs after `order_collections` so that the function lists it compares against are the ones the report presents.
    *Trace:* HLR-212 (Functions the Image Places and the Parse Did Not Reach), HLR-133, HLR-138.

*   <a id="LLR-PLC-04"></a>**LLR-PLC-04** — The rows and the count of regions decided from the image shall be written into the XML record and read back from it, the rows as `placed` elements within the `image` element and the count as an attribute of it. A record written before either existed shall read back as no rows and a count of zero.

    Neither can be recomputed from a record: a regenerated report has no image, and no debug information to read either off. A row the record did not carry would vanish and a count it did not carry would come back as zero, and in both cases the regenerated report would disagree with the one it came from about what the build contains (HLR-056).
    *Trace:* HLR-212 (Functions the Image Places and the Parse Did Not Reach), HLR-211, HLR-056.

## 72. `html_elements` ([src/report_html.c](../src/report_html.c))

The compound-node data model: three tiers of nodes joined by a `parent` reference, and the edges that are allowed to exist between them.

*   <a id="LLR-CYT-01"></a>**LLR-CYT-01** — `html_elements` shall append one node object per declared stratum, before any other node, with `id` set to `layer_` followed by the stratum's ordinal and `label` set to its declared name.

    The ordinal rather than the name, because the name is user text: two strata may be declared with names differing only in characters an identifier cannot carry, and a collision would silently reparent every file of one layer into the other. The ordinal is already the stratum's identity everywhere else in the program — it is what makes a direction out of a set of names (HLR-078) — so using it here keeps one notion of which layer is which.

    Layers are emitted first so that a consumer reading the sequence forward never meets a `parent` naming a node it has not yet seen. Nothing requires that, but a document whose containment resolves in one forward pass is one a reader can check by eye.

    Where no stratum was declared this appends nothing, and the document has two tiers rather than three.
    *Trace:* HLR-213 (The Graph Serialised as a Containment Hierarchy), HLR-078.

*   <a id="LLR-CYT-02"></a>**LLR-CYT-02** — `html_elements` shall append one node object per component of the graph, with `id` set to `file_` followed by the component's index, `label` set to the component's path with the longest directory prefix shared by every component removed, `path` set to the component's path in full, and `parent` set to `layer_` followed by that component's stratum ordinal.

    **The label is for reading and `path` is the record.** A drawing is read at label width, and within one document the shared prefix distinguishes nothing — every component carries it — while consuming most of each label; on a real project it is what makes the file tier illegible. The prefix ends at a path separator, never inside a name, so `/p/app/` beside `/p/apple/` sheds `/p/` and not the `/p/app` the bytes share; a lone component, whose whole directory is a prefix nothing else contests, is labelled by its file name. What the label sheds stays recoverable from `path` on the same node rather than lost, and the reduction is computed from the document's own components, so the artefact remains byte-identical across runs (HLR-032). The complete-record formats are untouched: this is a presentation of the drawing, not a change to what any record states.

    **The stratum comes from `stratum_of_components` and is not matched here.** That is `format_dsm.c`'s rule and it is this module's for the same reason: two matchers over one set of patterns eventually disagree about which layer a file is in, and this drawing would then place a file in one layer while the matrix beside it placed the file in another (HLR-164).

    **A component defining no function shall not be emitted at all.** Such a component can hold no node and join no edge, so a box for it states nothing — and on a C project that set is exactly the headers, which is half the components of `elc`'s own sources. The `.dot` companion has never drawn them, and not by a rule of its own: it opens a cluster while walking the *functions*, so a component with none is never reached. This renderer walks the components, so it must say the same thing deliberately or the two drawings of one graph disagree about what is in it (HLR-217).

    **The omission is of a box, never of a measurement.** Such a file is discovered, measured and counted everywhere the report counts a file; what is withheld is a node in a drawing of the call structure, which is the one thing the file demonstrably takes no part in. It is also what `--elf` leaves behind when an image defines none of a file's functions: the filter empties the component and the drawing then omits it, on the image's evidence rather than on a rule of this renderer's.

    **The shed prefix shall be measured over the components that remain**, since a prefix shared by a file nobody will see is not shared by anything on the page.

    **Where `stratum_of_components` returns `SIZE_MAX` the `parent` key shall be omitted entirely**, not set to empty and not set to a synthesised layer. A file matching no stratum lies outside the declared architecture — the judgement is argued where it is computed and this renderer follows it rather than reversing it. A layer named `other` would be a structure nobody declared, and would arrive on every run that declares no strata at all, wrapping the whole project in a fiction.
    *Trace:* HLR-213 (The Graph Serialised as a Containment Hierarchy), HLR-114.

*   <a id="LLR-CYT-03"></a>**LLR-CYT-03** — `html_elements` shall append one node object per node of the graph, with `id` set to `func_` followed by the node's index, `parent` set to `file_` followed by `SdgNode.component`, and `label` set to the function's name; and shall carry on it the file, the first line, the ELOC, and the cyclomatic complexity the node already holds.

    The index is the SDG's own, which is the report's sorted file order (LLR-SDG-09). Taking it rather than assigning one here is what makes the document byte-identical across runs without this module sorting anything (HLR-032), and it is what lets a reader match a node in the drawing to a row in the GraphML export, whose identifiers are the same indices.

    The figures are copied, never recomputed. A drawing that derived a complexity of its own would be a second opinion in a program that keeps exactly one, and the one with a threshold behind it is the report's (HLR-099).

    A node whose `component` is not a valid component index carries no `parent`. That is unreachable by construction and is handled rather than asserted, because the failure it would otherwise produce is a `parent` naming a node that does not exist — which a viewer reports as a corrupt document rather than as the bug it is.
    *Trace:* HLR-213 (The Graph Serialised as a Containment Hierarchy), HLR-032.

*   <a id="LLR-CYT-04"></a>**LLR-CYT-04** — `html_elements` shall append one edge object for each edge of the graph whose kind is `EDGE_CALL`, with `source` and `target` naming the two function nodes it joins and `weight` carrying the collapsed call-site count; and shall append no edge whose `source` or `target` names a file node or a layer node.

    **No meta-edges** (HLR-214). The connection between two collapsed containers is synthesised by the viewer from the edges crossing between them, so an emitted one would state the same fact twice from two rules — and the emitted one would be the statement with no threshold behind it, unreconcilable with the Ca/Ce figures the report prints (HLR-081).

    **Global edges are excluded** (HLR-074). A global edge joins a writer to a reader and is not a call; including it in a drawing of the call structure would report one kind of coupling as the other, which is the distinction the graph carries two views to preserve.

    Edges shall be emitted in ascending source-then-target order, which is `format_graph.c`'s ordering for the same artefact-level reason: the order is a property of this document rather than of a collection the model holds, so it is imposed here — the second of the two exceptions to `report.c` owning every sort (LLR-DOT-04, LLR-RPT-10).
    *Trace:* HLR-214 (Edges Between Functions Only), HLR-074, HLR-032.

*   <a id="LLR-CYT-05"></a>**LLR-CYT-05** — `html_elements` shall carry on each function node and each component node the annotation `annotate.c` placed on it: `severity` spelling the rank of the worst finding, `finding` carrying those findings as text, and a key per structural mark — `unreachable`, `recursive`, `deepest`, `hidden`, `soleUser` — and shall carry `chain` on each edge that is a step of the deepest call chain.

    **The severity is spelled here and decided nowhere.** It arrives as a rank on the annotation and is converted to the word the stylesheet selects on; a renderer that banded a measurement itself would be the second opinion this program's central claim forbids (HLR-098, HLR-099). The same annotation becomes a Graphviz fill in `format_graph.c`, which is what makes the two drawings agree about a node rather than merely resemble one another (HLR-217).

    **A key absent means the mark does not hold**, rather than a key present carrying `false`. The stylesheet tests these with a truthy selector, and a payload that stated all five marks on every node would say the same thing in several times the bytes on a drawing whose nodes are mostly unremarkable. A node nothing was found about carries no annotation keys at all.

    **The findings travel as text, not as structure.** The drawing shows *that* a function is critical and the text says *why*; a reader who needs to sort or filter findings has the CSV and the XML record, and duplicating their structure here would be a second machine-readable copy of the catalogue with no consumer.
    *Trace:* HLR-217 (The Drawing Carries the Findings It Was Drawn From), HLR-099, HLR-088.

*   <a id="LLR-CYT-06"></a>**LLR-CYT-06** — `html_elements` shall carry, in the `data` object of every function node it emits, the function's Weighted Test Burden Index as `wtbi` and its band as `wtbi_status`, taking exactly one of the strings `healthy`, `warning`, or `critical` (HLR-225).

    **The band shall be the string the C already decided, and the page shall not derive it.** Emitting the index and letting the stylesheet compare it against 20 and 45 would put the bounds in a second place — in a script, where nothing checks them against the catalogue that decided the text report's finding — and the two would disagree the first time a bound moved. That is the disagreement HLR-149 refuses between two spellings of a format, arriving here between two spellings of a threshold.

    The numbers shall be emitted through the same serialiser as every other numeric field on the node, so that a score is written in the locale-independent form the payload requires and a reader of the JSON sees `0.85` on every machine.
    *Trace:* HLR-225 (Testing Burden in the Interactive Report Payload), HLR-213.

## 73. `format_html` ([src/report_html.c](../src/report_html.c))

The page itself: when it is written, what its shell contains, how the payload survives being embedded in it, and what the viewer is told to do with it.

*   <a id="LLR-HTM-01"></a>**LLR-HTM-01** — `cli.c`'s extension table shall map `html` to `FORMAT_HTML`, and its format-name table shall spell that format `html`, so that `--output report.html` selects it and `--format html` names it.

    **Both tables are the single statement of their fact.** `format_extensions()` builds the "not a report format extension" diagnostic from the first, so the new extension is named in that message without a second list to keep in step; the second is indexed by the enumerator, so the disagreement diagnostic of HLR-149 names `html` for free. Adding a format to either without the other is what makes an error message name a set the parser does not accept.

    **No option shall request this format.** A flag would be a second way of saying what the filename has already said — the disagreement HLR-149 exists to prevent, arriving as a third spelling rather than a second (HLR-215).
    *Trace:* HLR-215 (The Interactive Drawing as an Output Format), HLR-148, HLR-149.

*   <a id="LLR-HTM-02"></a>**LLR-HTM-02** — `format_html` shall emit a complete HTML document whose head references the rendering library and its expand-collapse extension, and whose body carries one container element for the drawing.

    The two references are the only external content in the file, and their being fetched at view time is the bound HLR-215 places on the word "standalone". Nothing is fetched by `elc`: this function writes text and returns, so the run remains free of the network, the interpreter and the virtual machine HLR-040 excludes.

    The document shall be written even where the graph holds no nodes. A page that is absent when the project has no call structure makes the artefact's existence vary with its content, which is the rule `format_dsm.c` follows for an empty matrix and the report follows for an empty section.
    *Trace:* HLR-215 (A Single File That Opens Without a Server), HLR-040.

*   <a id="LLR-HTM-03"></a>**LLR-HTM-03** — `write_payload` shall emit the serialised document into the script element with `<` escaped as `\u003c`, `&` escaped as `\u0026`, and U+2028 and U+2029 escaped as `\u2028` and `\u2029`.

    **This is not the serialisation's escaping and cannot be delegated to it.** A C++ template signature containing `</script>` is *well-formed* JSON; the serialiser emits it verbatim and is right to, and the HTML parser then ends the script element at it, turning the remainder of the graph into body text and rendering an empty page. Escaping `<` closes that case, and escaping `&` closes the second half of it — an entity reference in the text would otherwise be decoded before the script ever ran.

    **U+2028 and U+2029 are the case that is invisible in review.** They are line terminators to a JavaScript parser and ordinary characters to a JSON one, so a name containing either is valid in the document and a syntax error the moment it is embedded. A `\u`-escape is chosen for all four because it is legal inside a JSON string literal and inside a JavaScript one, so the escaped text remains parseable by either.

    The escape is applied to the serialised text and not to each name before serialisation: applying it earlier would put the escape sequence in the data, and a viewer would render the literal characters `\u003c` in a function's label.
    *Trace:* HLR-215 (A Single File That Opens Without a Server), HLR-064.

*   <a id="LLR-HTM-04"></a>**LLR-HTM-04** — `format_html` shall emit an initialisation script that constructs the viewer over the embedded payload with no layout of its own, initialises the expand-collapse extension with its fisheye and animation behaviours enabled, collapses every container, and only then runs a force-directed layout — so the one layout the page opens with is computed over the collapsed view it opens showing.

    **The collapse is not a default this script chooses; it is HLR-216.** The page opens showing the declared layers and the reader descends. Initialising without it would reproduce, with an extra step, the density failure the existing graph companions have.

    **The layout follows the collapse rather than preceding it, and the ordering is load-bearing twice over.** A layout run at construction is of the expanded graph — larger by the whole function tier, and a drawing HLR-216 forbids opening with, so the most expensive layout the page ever computes would be spent on a picture that is immediately discarded. And because layouts settle asynchronously, collapsing while that layout still runs is a race the collapse loses on any real project: the relayout it requests is stomped, and the page opens fitted to a drawing that no longer exists — a graph shrunk past visibility, presented as though rendered.

    **The layout shall rank the drawing by call direction while keeping a container's members inside it**, and shall be re-run after a descent so that what is arranged is the drawing now on screen.

    **A file shall open where it stands, and the drawing shall move to make room for it.** The reader clicked a particular box; if opening it puts that box somewhere else, they have to find their file again before they can read what they opened, and the descent has cost them the place it was supposed to keep. So the opened file's centre is unchanged, and every other file is displaced by half the room this one gained, on the side it already lay — which is what makes the guarantee exact rather than approximate: a file left of the opened one is still left of it, so the order the reader was reading is the order they are still reading. A layer needs no displacement of its own, being sized by the files within it. Each file moves as one piece, so a file opened elsewhere keeps its own arrangement rather than being pulled apart around this one.

    **Neither the extension's layout hook nor its fisheye can provide that, and both were measured before being set aside.** A layout run after an expansion re-ranks the whole drawing, so an opened file changes place among its siblings — observed moving from first to fourth in its layer. The fisheye repositions the box directly, by some three thousand pixels in the same drawing. Both also re-lay-out the drawing from nothing, which HLR-216 forbids in terms. So the extension is asked only to add and remove the children: with its layout, fisheye and animation all off the expansion is synchronous, and the placement is this page's own.

    **Opening a file shall not move the viewport.** A layout fits the view unless told otherwise, so the layout of an opened file's functions re-zoomed the whole drawing around it — the reader asked to see inside one box and had the scale of everything changed under them. The zoom and the pan are the reader's; the only place the page takes them back is the control of LLR-HTM-07, and only because that control is a request to.

    **The functions of an opened file are arranged in a grid rather than ranked.** The reason is size and it is worth stating: over `elc`'s own sources a ranked arrangement of one 76-function file is some 6400px wide against a grid's 1500, and a box that wide shoulders the rest of the drawing off the screen — which defeats the displacement above by making the room needed larger than the drawing. The calls between those functions are still drawn; what is given up is reading their direction from the arrangement, inside one file, and only there. A call graph is read as a hierarchy — who calls whom, in which direction — which is how the `.dot` companion is drawn and why it reads as an architecture rather than a mesh; a force-directed arrangement of the same graph is a hairball however the edges run, and was the first thing a reader noticed between the two artefacts.

    **That requires a layout the viewer does not ship, and the dependency is argued rather than assumed.** Of the two layouts in the viewer's core, the force-directed one keeps a container's members together but ranks nothing, and the breadth-first one ranks by call direction but places a container's members by their global rank — which scatters one declared layer's files across the drawing and leaves its box overlapping another's. Neither is a drawing of the architecture. A layered algorithm that understands containment is a fourth and fifth script fetched at view time, on a page that already fetches two and already states that it needs the network the first time it is opened (HLR-215); what it buys is that this drawing and the `.dot` companion are two renderings of one picture rather than two different pictures.

    **Only the file tier shall open and close** (HLR-216). The gesture shall be bound on the file tier alone, so that a tap on a layer or on a function reaches no handler and the only thing that opens is the thing the key names. A collapsed file shall be styled as a single box carrying its name, centred rather than set above an empty container, because at the level the view opens at that box *is* what the reader is reading.

    **Fisheye and animation are what make the descent navigable**, which HLR-216 also requires: expanding a container in place, and moving to the new arrangement rather than cutting to it, is what keeps the reader's bearings across an expansion. Without them each expansion presents a freshly laid-out drawing and the reader is navigating a new picture every time.

    **The descent gesture shall be bound by this script rather than inherited, and bound on every node rather than on compound nodes.** HLR-216 requires that the reader can descend and return; a requirement met only by whatever gesture the extension's current release happens to bind is one that can stop being met without this file changing, and the extension is fetched from a CDN at view time rather than pinned. The binding uses the viewer's own event, so it holds whatever the extension's defaults are. The scope is load-bearing: a collapsed container is not a compound node — the extension holds its children removed until expansion — so a binding scoped to parents fires for the return half of the gesture and never for the descent half, and the asymmetry is invisible in review because the same selector reads as exactly the set of boxes that open and close. The handler decides by the collapsed marker first and containment second, so a tap on a function does nothing rather than collapsing the file above it.

    **The view shall be fitted when a layout settles, not at a moment of the script's choosing, and refitting shall stop at the reader's first gesture.** A `fit()` call placed in the script's own sequence frames whatever drawing is mid-flight when it runs, which is how this page first shipped opening on an invisibly small graph. Fitting on the viewer's own layout-settled event frames what is actually drawn, however many layouts run; unbinding it at the first tap is what keeps the viewport the reader's from the moment they take it.

    A force-directed layout is chosen because it is the family that keeps a cluster's members near one another, so a collapsed container occupies the space its contents did.
    *Trace:* HLR-216 (The View Opens at the Architectural Level).

*   <a id="LLR-HTM-05"></a>**LLR-HTM-05** — `format_html` shall return -1 where serialisation or allocation fails, or where an error is observed on the stream after writing, and 0 otherwise.

    **Nothing shall be written where serialisation failed.** The document is built and serialised before the first byte reaches the stream, so a failure leaves an empty destination rather than half a page. A partially written page is worse than an absent one: it opens, renders a truncated graph, and states a structure that is wrong while looking exactly like one that is right.

    **The stream is the caller's and is neither flushed nor closed here**, exactly as it is for every other renderer: `emit` opens the destination, dispatches, and reports a write failure naming it. That is what makes this a format rather than a companion — a companion owns its file, and this one owns none.

    The error is checked once after the writing rather than per call, because a full disk shows up on the flush and a report claimed as written when it was truncated is the failure worth catching.
    *Trace:* HLR-215 (The Interactive Drawing as an Output Format), HLR-030, HLR-038.

*   <a id="LLR-HTM-06"></a>**LLR-HTM-06** — `format_html` shall emit a stylesheet giving each annotation of LLR-CYT-05 a distinct visual attribute — fill for severity, shape for the role a function takes part in, border for whether it is reached and for the deepest chain — and a legend naming every one of them.

    **Each mark shall take a different attribute**, so that several holding at once compose rather than overwrite: a function that is both recursive and critical is a red box with a double border, not whichever of the two the stylesheet happened to apply last. This is the rule `node_style` follows in the `.dot` writer, and following it here is what keeps a reader's understanding of one drawing good for the other (HLR-217).

    **The severity pigments shall be the ones the `.dot` companion uses.** Amber for a warning and red for a critical, because a reader moving between the two artefacts should not have to learn two colour schemes for one judgement — and because the pigments being the same makes a disagreement between the drawings visible at a glance rather than plausible.

    **An edge shall pass behind a box rather than across its face, and a box shall be opaque enough to hide what passes behind it.** Two things are required and each is insufficient alone. A node that becomes a container is drawn at a lower compound depth than an edge between two nodes outside it, so every edge is placed at the bottom of the compound order; and an opened file's box is filled opaquely, because a translucent one shows every unrelated edge in the drawing straight through the one part of it the reader has just opened. A collapsed file was already opaque, which is why the fault appeared only on opening one — the edges were behind the box the whole time and simply visible through it.

    **Every edge touching an opened file shall be raised back above its fill.** Placing every edge beneath every box is what puts an unrelated edge behind this one, and it would equally hide the calls the reader opened the file to see. The set that must rise is not the file's own calls alone: an edge with one end inside is the call path *into* or *out of* the file, which is the greater part of what opening it was for, and an opaque box that hid those would answer the first fault by committing a worse one. So every edge with at least one end among an opened file's functions is raised, and only an edge with neither end inside passes behind — which is exactly the case the opaque fill exists for.

    **The set shall be recomputed from the open files rather than adjusted as each one opens.** An edge between two opened files belongs to both, and marks that were merely added and removed would drop it the moment either closed.

    **A mark that is a shape shall be drawn in the key, not named.** The two shapes were named — "octagon", "tag" — and that told the reader nothing twice over: `tag` is the rendering library's word for a shape and no word of `elc`'s, and a reader cannot match a name they have never met to a box in the drawing. The key carries the shape itself, so the vocabulary is not needed at all; the fills and borders are drawn for the same reason and always were.

    **What a mark means shall be stated as its consequence, in the report's own words.** "Sole namer of a global" named the mechanism and left the reader to work out what to do about it. The finding is that one function is the only user of some global, and what follows is that the global could be a local one — which is MISRA C Rule 8.9 and is what HLR-092 flags it for. The term the report uses is kept where it has one, so "hidden channel" appears in the key and in the tables alike, and the key explains it rather than replacing it.

    **The legend is part of the page** (HLR-217). The `.dot` companion states its key in a comment at the head of the file; this one states it above the drawing, where a reader of a rendered artefact will actually meet it. A drawing whose colours are explained only in the manual is one the reader has to leave in order to read.
    *Trace:* HLR-217 (The Drawing Carries the Findings It Was Drawn From), HLR-105.

*   <a id="LLR-HTM-07"></a>**LLR-HTM-07** — `format_html` shall emit one control on the page, closing every file and returning the drawing to the view the page opened at; and shall emit no control that opens every file.

    **Closing everything is a return, not a new state**, so the control restores the layout and the framing as well as the containment: a reader who has opened several files and moved about is otherwise left zoomed into a drawing that is no longer there. This is the one place the viewport is taken back after the reader's first gesture, and it is taken back because they asked for it.

    **There is deliberately no "expand all".** It is the same gesture in the opposite direction and it would be as easy to provide, which is exactly why the omission has to be argued rather than left to look like an oversight: expanding every file is the drawing at function level, at maximum density, which is the state HLR-216 exists to keep the view out of. A control that reaches it in one click would undo the requirement the default is there to satisfy.

    **The bar the control sits in shall be layered above the viewer's canvases.** The expand-collapse extension draws its own canvas over the whole element at a z-index of 999, so a bar merely drawn there is visible and not clickable: the press reaches the canvas underneath, the drawing pans, and the one control on the page cannot be used at all. Raising the bar also stops a drag begun on it from moving the drawing, which is right for the same reason — it is a bar, not part of the picture.

    **The restoration shall be of the opening viewport itself, not a fresh fit.** The layout is deterministic and returns every file to the position it opened at, so what remains is the scale and the offset; recomputing a fit gets close and is not the same thing. The zoom and pan the reader was first given are remembered and put back, after the layout settles rather than before, since a layout fits as it finishes.

    **The marks an opened file made shall be cleared with it**, so the drawing is left as the page first drew it rather than carrying the residue of what the reader had opened.
    *Trace:* HLR-216 (The View Opens at the Architectural Level).

*   <a id="LLR-HTM-08"></a>**LLR-HTM-08** — No two file boxes shall overlap. After anything moves a box, any pair still overlapping shall be pushed apart along the shorter of their two penetrations until none overlaps, and one box may be held fixed while the others move around it.

    **This is a guarantee rather than a description of the usual case.** The displacement of LLR-HTM-04 opens exactly the room an opened file needs and so creates no overlap by itself, which is precisely why the rule has to be stated separately: it is not the only thing that moves a box. The viewer lets the reader drag one, and a box dropped on top of another stays there until something moves it. A drawing whose boxes may overlap under *some* sequence of gestures is a drawing that overlaps, and the reader has no way to tell which arrangement they are looking at.

    **Along the shorter penetration**, because that is the direction that moves the pair least and leaves each on the side of the other it was already on — the same reasoning that keeps the displacement order-preserving. **A pair is parted by its penetration and a margin**, so boxes come to rest apart rather than exactly touching.

    **The fixed box is the one the reader just acted on** — the file they opened, or the one they dropped — so the drawing moves around their action rather than undoing it. Where nothing was acted on, both boxes of a pair move by half.

    The pass repeats until nothing overlaps; the iteration cap is a guard against a pathological drawing rather than a working limit, a piled-up drawing of thirty-six files settling in some thirty-seven passes and about fifty milliseconds.
    *Trace:* HLR-216 (The View Opens at the Architectural Level).

*   <a id="LLR-HTM-09"></a>**LLR-HTM-09** — `format_html` shall emit an element that shows, while the reader points at a box, that box's definition site, the figures the report states for it, and each finding upon it; and that element shall take no pointer event of its own.

    **The drawing marks a box and this is what says why.** A fill states that a function is critical and a shape that it is the only user of some global; neither says *which* finding decided it, and a reader who cannot ask has been given a colour scheme rather than a report (HLR-217). The `.dot` companion answers by putting the same text in each node's `tooltip`, which an SVG renderer shows on hover — a canvas has no element to hang one on, so this page provides the behaviour rather than inheriting it.

    **Each finding shall be on its own line.** They are joined with `; ` for the record, which is right for a record and wrong for a reader scanning three of them at once.

    **It shall take no pointer event**, so that pointing at a box never intercepts a click meant for it, and shall be dismissed by the gestures that make it stale — leaving the box, and any tap, pan or zoom.

    **It shall stay within the window.** A box near an edge would otherwise push its own description off the screen, which fails precisely where the drawing is most crowded.
    *Trace:* HLR-217 (The Drawing Carries the Findings It Was Drawn From).

*   <a id="LLR-HTM-10"></a>**LLR-HTM-10** — While the reader points at a function, that function's outline and every call it takes part in — those it makes and those made to it — shall be drawn in a distinguishing colour, and returned to their ordinary appearance when the reader points elsewhere.

    **A function's row states its fan-in and fan-out as numbers; this says which calls they are.** The two figures tell a reader that a function is called by eleven others without telling them which eleven, and finding out means tracing lines across a drawing that may hold a thousand — the one task a drawing should make easy and this one did not.

    **Both directions are marked and not distinguished from one another.** The question the gesture answers is what the function takes part in; which way each call runs is already drawn, in the arrowhead. Colouring the two differently would answer a question the drawing has already answered and spend the reader's attention twice.

    **The marked calls shall be lifted above the boxes.** Every edge is drawn beneath every box so that an unrelated one passes behind an opened file (LLR-HTM-06); a highlighted call left there would be hidden by the very file it runs into, which is most of what the reader is following. The lift is for the marked edges alone and lasts as long as the mark does.

    **The mark shall follow the current graph rather than the graph as emitted.** Where a call runs into a file that is closed, what the reader can see is the viewer's meta-edge, and that is what is marked.

    **It shall be cleared when a file opens or closes**, since the functions it holds are about to arrive or depart, and a mark surviving on an element the reader is no longer pointing at is a claim about the drawing that nothing made.
    *Trace:* HLR-216 (The View Opens at the Architectural Level), HLR-085.

## 74. `annotations_build` ([src/annotate.c](../src/annotate.c))

Placing a run's findings on the graph they describe, once, for every drawing that shows them.

*   <a id="LLR-ANN-01"></a>**LLR-ANN-01** — `annotations_build` shall produce one `Annotation` per graph node and one per component, carrying the highest severity of any finding placed on it, those findings joined as text, and a bitset of the structural marks that hold; and shall publish separately the findings belonging to no single node and the deepest chain as node identifiers.

    **The highest severity, not the last or the sum.** A node carrying a critical and a warning is a critical one: severities rank, they do not accumulate (HLR-123). A drawing filling by the most recent finding placed would colour a node by the order the catalogue happened to be walked in.

    **Every output is assigned as soon as it exists**, so that a caller's teardown releases what was allocated when a later step fails. The alternative — publishing all four only on success — leaks whichever were built before the failure.
    *Trace:* HLR-217 (The Drawing Carries the Findings It Was Drawn From), HLR-123.

*   <a id="LLR-ANN-02"></a>**LLR-ANN-02** — `annotations_build` shall place a finding on a node whose definition site it names, failing that on a component whose path it names, and failing that on every function that touches a global object of its name; and a finding matching none of the three shall be published as belonging to the graph as a whole rather than dropped.

    **A definition site is a file and a line together.** A file alone is a component finding, and a line alone matches the same line in every file — so both halves are required, and matching on the *name* instead would mark all six of the functions `elc`'s own sources call `grow` because one of them was unreachable.

    **A finding about a global shall reach each such function once, not once per access.** The touch set is deduplicated by object, node *and direction*, so a function that both writes and reads one object appears in it twice while the finding remains one finding; placing it per touch wrote it into the note twice, which reads as two findings where the report states one. The set is sorted by object then node, so a node's touches of one object are adjacent and only the previous one need be remembered.

    **A finding about a global object goes to the functions that touch it**, which is the only place it can go on a graph whose nodes are functions, and where a reader would look for it (HLR-091, HLR-105). The touch set rather than the edges: an object named by exactly one function produces no edge at all, and that object is precisely the scope-reduction candidate (HLR-092).

    **Nothing is dropped silently.** A finding that describes the graph rather than anything in it — the depth of the call tree is the case that exists — reaches the caller, which puts it where its artefact can carry it.
    *Trace:* HLR-217 (The Drawing Carries the Findings It Was Drawn From), HLR-091, HLR-092.

*   <a id="LLR-ANN-03"></a>**LLR-ANN-03** — `annotations_build` shall mark both kinds of cycle on every member from the report's cycle rows rather than from the catalogue's finding, and shall skip the catalogue's own copy of a recursion or component-cycle finding.

    **The catalogue locates a cycle at one subject; HLR-105 asks for the members.** A finding has one location and a set has none, so the membership is read from the cycle rows — and the catalogue's copy is then a second note repeating on one member what the first already said about all of them.

    **The severity is still looked up rather than restated**, so this module names no threshold of its own even where it decides membership (HLR-099).

    **Recursion membership is matched by name**, which is all the report model carries: a node identifier means nothing to a reader and does not survive a record round trip. That inherits the duplicate-`static` imprecision the manual documents — two functions sharing a name are both marked when one recurses — which is visible in the drawing and therefore better than a silent wrong answer.
    *Trace:* HLR-089, HLR-105, HLR-217 (The Drawing Carries the Findings It Was Drawn From).

## 75. `collect_mock_burden` ([src/analyze.c](../src/analyze.c))

*   <a id="LLR-MBS-01"></a>**LLR-MBS-01** — `collect_mock_burden` shall compute the Mock Burden Score of HLR-221 for one function from the captures of the signature query, and shall be the only definition of the weighting. The score is needed by the graph, by the threshold catalogue, and by every format that reports per-function detail; a second computation would be a second place the weights could differ.

    The score shall be the base tax of `0.25`, plus the return contribution — `0.0` for `void`, `0.1` for a primitive, `0.25` for a pointer or a `struct` — plus, for each parameter, `0.1` where it is a primitive and `0.25` where it is a pointer or an array.
    *Trace:* HLR-221 (Mock Burden Score per Function).

*   <a id="LLR-MBS-02"></a>**LLR-MBS-02** — `collect_mock_burden` shall read the return type, the parameter list, and the pointer and array tokens from a query file in the runtime directory, and shall contain no language-specific token text of its own (HLR-009, PVD §6 Principle 2).

    **The classification of a type shall be by the captures the query returns, not by comparing spellings in C.** A list of primitive type names compiled into the binary would be a language fact in the one place this project forbids language facts, and would be wrong for the first project that types its own integers — which on the bare-metal targets this measurement exists for is every project.
    *Trace:* HLR-221, HLR-009 (Language Data in the Runtime Directory).

*   <a id="LLR-MBS-03"></a>**LLR-MBS-03** — `collect_mock_burden` shall settle the three signature cases HLR-221 names, and settle them where the score is computed rather than at each call site:

    A variadic `...` shall not be counted as a parameter and shall contribute nothing. An empty parameter list, and one consisting of the single token `void`, shall yield no parameter contribution at all — so `void f(void)` scores exactly the base tax of `0.25`. A parameter that is a pointer to a pointer, or a `struct` passed through a pointer, shall be charged once at the pointer rate, the score being a tax on the kind of a type rather than a count of its tokens.
    *Trace:* HLR-221.

*   <a id="LLR-MBS-04"></a>**LLR-MBS-04** — `collect_mock_burden` shall run inside the traversal that already extracts each function (LLR-ANL-01), and shall not re-parse or re-walk the tree. PVD §6 Principle 7 admits one parse and no copies; a signature is available at the node the traversal is already standing on, and a second walk to fetch it would buy nothing.

    A function whose signature the query cannot match shall be scored at the base tax alone and the shortfall diagnosed, rather than the function being omitted from the measurement. A function absent from a score column is indistinguishable from one that scored zero, and the run continues under HLR-070 either way.
    *Trace:* HLR-221, HLR-070.

## 76. `calltree_burden` ([src/calltree.c](../src/calltree.c))

*   <a id="LLR-WTB-01"></a>**LLR-WTB-01** — `calltree_burden` shall return the Weighted Test Burden Index of HLR-223 for one function, computed as its cyclomatic complexity multiplied by one plus the lesser of its fan-in and its Weighted Fan-Out, and shall be the only definition of the formula — the report needs it for the function table and `thresholds.c` needs it to band.

    **The lesser of the two degrees and not their product**, which is the whole of what distinguished this index from the Adapted Maintainability Index it replaced. A widely shared leaf has a large fan-in and a weighted fan-out near zero, and the index shall collapse towards its cyclomatic complexity; a coordinator has the reverse and shall do the same. Only a function large in both degrees shall produce a large index.

    The two degrees shall be compared as the same type before the lesser is taken. Fan-in is a count and the weighted fan-out is not, so the count shall be widened rather than the weight truncated — truncating would make every weighted fan-out below `1.0` compare equal to zero and would silently return the complexity for the whole lower half of the range.
    *Trace:* HLR-223 (Weighted Test Burden Index per Function).

*   <a id="LLR-WTB-02"></a>**LLR-WTB-02** — `calltree_burden` shall be a pure function of the three measurements it is given and shall read no graph, so that the figure the report prints and the figure the threshold catalogue bands are produced by one call and cannot diverge.

    A function whose cyclomatic complexity is recorded as zero — which no analysed function has, every path count being at least one — shall yield an index of zero rather than a special case, the multiplication doing the work without a guard.
    *Trace:* HLR-223.
