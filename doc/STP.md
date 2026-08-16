# Software Test Plan

**Version:** 0.8
**Date:** 2026-08-21
**Author(s):** John Anderson

## 1. Introduction

### 1.1 Purpose
This Software Test Plan defines how every requirement in [HLRs.md](HLRs.md) and [LLRs.md](LLRs.md) is verified. It establishes the test levels, the harness, the fixture strategy, the pass/fail criteria, and the convention by which a test declares which requirements it verifies, so that [Traceability.md](Traceability.md) can report coverage without any separate bookkeeping.

### 1.2 Scope
This plan covers the verification of the `elc` executable and the runtime data it ships: the per-function metrics, the System Dependence Graph and every analysis over it, the output formats, the failure and exit-status behaviour, and the non-functional constraints on dependencies and execution model.

It does **not** cover the correctness of the third-party Tree-sitter grammars themselves. A grammar is upstream data; what this plan verifies is that `elc` draws the right conclusions from whatever a grammar reports. Where a metric depends on a language's `.scm` classification, the query file's conformance is verified by fixture (§5) while the C-side arithmetic that consumes it is verified by unit test.

### 1.3 Related Documents
*   [PVD.md](PVD.md) — the vision and the Appendix A threshold catalogue the findings are measured against.
*   [HLRs.md](HLRs.md) — the 122 externally observable requirements this plan verifies.
*   [LLRs.md](LLRs.md) — the 233 per-function contracts bound to unit tests.
*   [SDD.md](SDD.md) — the module and function structure the unit level mirrors.
*   [Traceability.md](Traceability.md) — the generated coverage report and the gap list this plan exists to close.

## 2. Test Strategy

### 2.1 Test Levels

| Level | Source | Driver | Style |
| ----- | ------ | ------ | ----- |
| Unit | [test/unit/](../test/unit/) | Criterion — one test binary per `src/` module | White-box, per-function, bound to LLRs |
| Integration | [test/integration/](../test/integration/) | Bats, driving `build/elc` | Black-box over the command line, bound to HLRs |
| Fixture conformance | [test/fixtures/](../test/fixtures/) | Bats, comparing output against hand-counted values | Black-box, per language, bound to HLR-034 and the ELOC and complexity HLRs |
| Instrumented | [test/instrumented/](../test/instrumented/) | Bats, observing the process and the binary | Environmental — process, syscall, link, and filesystem observation |
| Sanitized | The whole suite, rebuilt | `make asan`, then `make valgrind` | Not a separate suite — every level above re-run under instrumentation, bound to HLR-124 and HLR-125 |

### 2.2 Test Framework
Two harnesses, chosen for the two kinds of thing being tested.

**Criterion** drives the unit level. It is a C-native framework, so a test calls the function under test directly rather than through a shell boundary, and it registers tests automatically — there is no hand-written `main`, no registry to keep in step with the test list, and therefore no way for a test to be written and silently never run. A test is declared in place:

```c
/* Verifies LLR-MRG-02: overlapping and nested comment spans are
   coalesced before their lines are excluded from ELOC. */
Test(analyze, merge_nested_spans)
{
    cr_assert_eq(merge_comment_spans(&spans), 4);
}
```

Its decisive property for this project is **per-test process isolation**: each test runs in its own process, so a segmentation fault or an assertion in library code is reported as that one test failing rather than terminating the run. For code that spends its life dereferencing syntax-tree nodes, graph adjacency, and `mmap`'d buffers, that is the difference between a diagnosis and a silent stop. It also makes the deliberate crash cases — a query compiled against the wrong grammar, a teardown-order violation — testable rather than merely avoidable.

One Criterion binary is built per `src/` module and linked against that module, mirroring the SDD's structure so that the failing binary names the module.

**Mocking is done with the linker, not with the source.** Where a unit test must isolate its subject from a dependency — a syscall that would touch the filesystem, a Tree-sitter or graph-library call whose real behaviour is not what is under test, or a failure that cannot otherwise be provoked — the dependency is intercepted with GNU ld's `--wrap` flag:

```
LDFLAGS += -Wl,--wrap=mmap -Wl,--wrap=realloc -Wl,--wrap=dlopen
```

Each wrapped symbol `f` is then defined in the test as `__wrap_f`, which may consult the real implementation through `__real_f` when it wants to pass the call through. Nothing in `src/` changes: there are no injected function pointers, no `#ifdef TESTING`, and no seam maintained purely for the benefit of tests. The production build links exactly the code that shipped, which is the property that makes a unit test's result mean something about the delivered binary.

This is what makes the failure paths testable at the unit level rather than only by contrivance. A wrapped `realloc` returning `NULL` exercises the checked-growth contracts of LLR-ANL-34 and LLR-RPT-16; a wrapped `mmap` returning `MAP_FAILED` exercises `analyze_file`'s read-failure path; a wrapped `dlopen` or `dlsym` returning failure exercises the malformed-module tolerance of HLR-070 without needing a deliberately corrupt `.so` on disk. Wrapping is confined to the unit level: the integration, fixture, and instrumented levels link and run the real binary, unwrapped.

The wrapped symbols are listed **per module**, in the build as well as in the design. A `--wrap` applies to every object linked into the binary, and every unit binary links every module, so a symbol wrapped for one module's tests would oblige every other unit test file to define a `__wrap_` for it whether or not it mocks anything.

**Not everything worth mocking should be mocked.** `stat(2)` is the example: every invalid target class the requirement names — absent, unreadable, a FIFO, a device node — can be created on a real filesystem in three lines of test, and a test that creates one verifies the requirement where a test that wraps `stat` verifies the wrapper. The wrapper earns its place where the failure cannot otherwise be produced: a `realloc` that fails, a `realpath` that fails on a path that exists.

**Link-time interception and `ptrace` do not compose.** LeakSanitizer stops the world at exit through a `clone`d tracer and `ptrace`, which collides with `strace`'s own attachment and aborts the process. Under the sanitized pass that abort truncates the trace, so an instrumented test asserting "this syscall never appears" would pass because the process died before reaching the interesting part — passing for the wrong reason, which is worse than failing. Leak detection is therefore disabled for a traced run and for that run only; every other run in the same pass still has it on, so HLR-125 stays verified.

**Every arm/disarm flag guarding a wrapper must be `volatile`.** The compiler treats the allocation and I/O functions as builtins that cannot read the caller's globals, so in the natural shape of such a test —

```c
armed = 1;  p = malloc(n);  armed = 0;
```

— it judges the first store dead, overwritten before anything could observe it, and removes it. The wrapper then runs with the flag still clear, the interception silently does not happen, and the test fails while appearing to be about something else. Phase 0 hit exactly this. Declaring the flag `volatile` forces the store and costs nothing.

**Bats** (Bash Automated Testing System) drives the integration, fixture-conformance, and instrumented levels, with `bats-support` and `bats-assert`. These levels invoke `build/elc` as a user would, assert on its output and exit status, and observe its process and link line — all of which is shell work, and none of which needs to call a C function.

Both harnesses emit TAP, so `make test` produces one merged report despite the split.

Every test writes its scratch files to `$BATS_TEST_TMPDIR`, or for Criterion to a per-test temporary directory created in setup and removed in teardown. No test writes into `test/fixtures/` or anywhere in the working tree, and no test depends on the execution order of any other.

**Platform degradation.** The instrumented level rests on facilities that are not universal: `/proc`, `strace`, and read-only bind mounts are Linux conveniences. Where one is unavailable, the affected test **skips explicitly**, reporting through the harness both that it skipped and which requirement thereby went unverified on that platform. A silent non-run is treated as a suite failure rather than a pass, so that a CI target quietly shedding its memory-safety, single-thread, or single-parse coverage is visible rather than assumed.

Because CI enforces that policy — a skip on a Linux runner fails the build — an instrumented test may not rest on a facility a *container* withholds, as distinct from one a *platform* lacks. Unprivileged user namespaces are the case that established this: `unshare -rn` is present on any modern Linux and works when run locally, but GitHub's runners disable it, so the test that used it to verify HLR-040 skipped there and turned an unavailable facility into a red build. The property is now observed with `strace -e trace=%network`, which needs no privilege. Prefer observing the syscalls a requirement forbids over constructing an environment in which the requirement cannot be violated: the direct observation is more portable, and it is the stronger claim — that `elc` never *attempts* the forbidden thing, not merely that it survives being denied it.

### 2.3 Build and Execution
All tests are built and executed by `make test`. The target:

1. Builds `build/elc` and the `build/runtime` symlink, so that the binary under test resolves its grammars without an install step.
2. Builds one Criterion test binary per `src/` module under `test/unit/`, linking each against the module it exercises.
3. Runs the Criterion binaries with TAP output, then the Bats integration, fixture-conformance, and instrumented suites, merging both TAP streams into a single report.
4. Exports the runtime-location environment variable so that every suite tests against the in-tree `runtime/` rather than any installed copy.

`make unit`, `make integration`, `make fixtures`, and `make instrumented` run the levels individually.

`make asan` rebuilds the binary and every Criterion module with `-fsanitize=address,undefined -fno-omit-frame-pointer -g` and re-runs the entire suite. It is not an additional set of tests: it is the same tests executed against an instrumented build, so that every input the suite already exercises is also checked for memory error. This is what makes HLR-124 and HLR-125 verifiable — neither can be asserted from `elc`'s output, only from how it behaves while producing it.

The sanitized run sets `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:strict_string_checks=1` and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`, so that a diagnostic terminates the offending test rather than being reported and passed over. Criterion's per-test process isolation is what keeps that granular: an abort under sanitizer fails one test, not the run.

`make valgrind` re-runs the integration and fixture levels under `valgrind --leak-check=full --errors-for-leak-kinds=all`. Valgrind and AddressSanitizer are run in separate passes and never combined, since the two instrumentations are mutually incompatible. They are both kept because they do not find the same things: ASan catches out-of-bounds and use-after-free precisely and cheaply, while Valgrind sees uninitialised reads that ASan does not and needs no rebuild, making it the check that runs against the binary users will actually get.

### 2.4 Pass/Fail Criteria
*   A test passes when every assertion holds and the exercised code writes to no file outside its scratch directory.
*   **A sanitizer diagnostic is a failure, whatever the assertions did.** A test whose assertions all hold but which reports an out-of-bounds access, a use-after-free, an invalid free, or undefined behaviour has failed, and the defect is in `elc` rather than in the test.
*   **A leak is a failure.** With leak detection enabled, any allocation, mapping, or handle outstanding at exit fails the test that provoked it — including on runs that end in a usage error, an invalid target, or a rejected saved record, since HLR-125 covers error paths as well as the success path.
*   The suite is not considered passing until it has passed **three times**: once as an ordinary build, once under `make asan`, and once under `make valgrind`. A change may be merged only when all three are clean. The instrumented passes must leave no artefacts behind: an ASan-built binary left in place makes valgrind refuse to run, so a single sanitizer failure would otherwise cascade into a wall of unrelated valgrind failures and obscure its own cause.
*   The suite passes when every test passes. There is no tolerated-failure list and no quarantine; a test that cannot be made reliable is removed along with a note in [notes.md](notes.md) recording why.
*   A requirement is **verified** when at least one passing test traces to it. A requirement with no tracing test is a coverage gap, reported in [Traceability.md §6](Traceability.md#6-coverage-gaps) rather than silently tolerated.
*   A change to a hand-counted fixture value is a deliberate act: the fixture header states the expected numbers and the reasoning, so altering a number requires editing the reasoning that justifies it. Regenerating a fixture's expected values from `elc`'s own output is never acceptable.
*   Determinism failures are not flakes. A test that passes intermittently indicates a violation of HLR-032 or HLR-033 and is investigated as a product defect, never retried or skipped.
*   Requirements marked in §6 as verified by review rather than by execution are satisfied when the review is recorded; they do not count toward automated coverage and are listed explicitly so the distinction is never lost.

### 2.5 Traceability Convention
Every test function carries a doc comment naming the requirements it verifies, by identifier:

```c
/* Verifies LLR-MRG-02: overlapping and nested comment spans are
   coalesced before their lines are excluded from ELOC. */
Test(analyze, merge_nested_spans)
```

A Criterion suite name matches the `src/` module it exercises, so the
suite, the module, and the LLR prefix line up when a failure is read.

The same identifiers appear in the `<traces>` block of the matching `<test>` entry in [Project.xml](Project.xml), which is what [Traceability.md](Traceability.md) is generated from. The doc comment and the `<traces>` block must agree; the comment is the reviewable form, the XML is the machine-readable one.

Unit tests trace to **LLRs**, since an LLR is a per-function contract. Integration, fixture, and instrumented tests trace to **HLRs**, since an HLR is externally observable behaviour. A test that genuinely verifies both may declare both.

The sanitized gate of §2.1 needs stating separately, because it would otherwise fall outside this scheme altogether. Re-running an existing suite produces no new catalogued test, so HLR-124, HLR-125, and the memory-safety LLRs would never acquire a verifying test however diligently the sanitized build was run — they would sit permanently in the gap list while being, in fact, thoroughly exercised. `test/instrumented/sanitized.bats` therefore carries catalogued tests of its own: one per instrumented pass over the fixture corpus, tracing to HLR-124 and HLR-125, and one covering the sanitized unit run, tracing to the memory-safety LLRs. The re-run is the mechanism; the catalogued test is the record that the mechanism was applied and came back clean.

## 3. Test Catalogue

Snapshot: **357 test(s)** across
**24 file(s)**.

### 3.1. [test/unit/cli.c](../test/unit/cli.c)

Role: **unit**. **26 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="wrap_passes_through_when_not_armed"></a>`wrap_passes_through_when_not_armed` | `LLR-BLD-10` | An unarmed link-time wrapper delegates to the real implementation, so wrapping does not perturb tests that do not use it. |
| 2 | <a id="wrap_intercepts_when_armed"></a>`wrap_intercepts_when_armed` | `LLR-BLD-10` | An armed wrapper intercepts the call, proving the mechanism by which later phases provoke allocation failure. |
| 3 | <a id="help_short_option_reports_help"></a>`help_short_option_reports_help` | `LLR-CLI-13` | `-h` is reported as a help request rather than parsed as an ordinary option. |
| 4 | <a id="help_long_option_reports_help"></a>`help_long_option_reports_help` | `LLR-CLI-13` | `--help` is reported as a help request. |
| 5 | <a id="unrecognised_option_is_a_usage_error"></a>`unrecognised_option_is_a_usage_error` | `LLR-CLI-12` | An unrecognised option is rejected as a usage error. |
| 6 | <a id="missing_target_is_a_usage_error"></a>`missing_target_is_a_usage_error` | `LLR-CLI-12`, `LLR-CLI-01` | An invocation with no target is rejected as a usage error. |
| 7 | <a id="single_target_is_collected"></a>`single_target_is_collected` | `LLR-CLI-01` | A single target argument is collected into the options structure. |
| 8 | <a id="several_targets_are_collected_in_order"></a>`several_targets_are_collected_in_order` | `LLR-CLI-01` | Several targets are collected in argument order, files and directories intermixed. |
| 9 | <a id="help_takes_precedence_over_a_target"></a>`help_takes_precedence_over_a_target` | `LLR-CLI-13` | A help request is reported without validating the remaining arguments. |
| 10 | <a id="mode_defaults_to_analyse"></a>`mode_defaults_to_analyse` | — | The run mode defaults to analysis when no mode-selecting option is given. |
| 11 | <a id="the_output_destination_defaults_to_standard_output"></a>`the_output_destination_defaults_to_standard_output` | `LLR-CLI-03` | A null output path records standard output as the destination when none was supplied. |
| 12 | <a id="an_output_path_is_collected"></a>`an_output_path_is_collected` | `LLR-CLI-03` | An output file path is collected into the options, and its argument is not mistaken for a target. |
| 13 | <a id="the_short_output_option_behaves_as_the_long_one"></a>`the_short_output_option_behaves_as_the_long_one` | `LLR-CLI-03` | The short form of the redirection option is parsed as the long form is. |
| 14 | <a id="an_output_option_without_its_argument_is_a_usage_error"></a>`an_output_option_without_its_argument_is_a_usage_error` | `LLR-CLI-12` | An option requiring an argument and given none is a usage error. |
| 15 | <a id="the_complexity_threshold_defaults_to_fifteen"></a>`the_complexity_threshold_defaults_to_fifteen` | `LLR-CLI-04` | The threshold defaults to the documented value when none is supplied. |
| 16 | <a id="a_complexity_threshold_is_collected"></a>`a_complexity_threshold_is_collected` | `LLR-CLI-04` | A threshold given as a long option is collected, and its argument is not mistaken for a target. |
| 17 | <a id="the_short_threshold_option_behaves_as_the_long_one"></a>`the_short_threshold_option_behaves_as_the_long_one` | `LLR-CLI-04` | The short form is parsed as the long form is. |
| 18 | <a id="a_threshold_of_zero_is_accepted"></a>`a_threshold_of_zero_is_accepted` | `LLR-CLI-04` | Zero lists every function, which is a legitimate request and must not be read as absent. |
| 19 | <a id="a_malformed_threshold_is_a_usage_error"></a>`a_malformed_threshold_is_a_usage_error` | `LLR-CLI-16`, `LLR-CLI-12` | A sign, a trailing tail, leading whitespace, an empty string, and a hexadecimal literal are each rejected — every one of which a permissive conversion would accept, silently yielding a threshold the user did not write. |
| 20 | <a id="options_free_is_safe_on_null"></a>`options_free_is_safe_on_null` | — | Releasing a null options structure does not fault, so teardown is safe on every path. |
| 21 | <a id="a_scope_declaration_is_parsed_into_a_name_and_patterns"></a>`a_scope_declaration_is_parsed_into_a_name_and_patterns` | `LLR-SCP-01` | A name:glob[,glob…] declaration yields the scope name and each component pattern separately. |
| 22 | <a id="scope_declarations_accumulate"></a>`scope_declarations_accumulate` | `LLR-SCP-01` | The option is repeatable and each declaration adds a scope rather than replacing the last. |
| 23 | <a id="a_malformed_scope_declaration_is_rejected"></a>`a_malformed_scope_declaration_is_rejected` | `LLR-SCP-02` | A scope with no name, a name with no components, and an empty pattern between separators are each a usage error rather than a scope matching nothing — which would report nothing and read as a clean result. |
| 24 | <a id="the_scope_option_reaches_the_options_structure"></a>`the_scope_option_reaches_the_options_structure` | `LLR-SCP-01` | --scope is parsed by cli_parse into the options structure the analyses read. |
| 25 | <a id="a_malformed_scope_option_is_a_usage_error"></a>`a_malformed_scope_option_is_a_usage_error` | `LLR-SCP-02` | A malformed --scope argument fails the parse rather than being ignored. |
| 26 | <a id="no_scope_declared_leaves_the_list_empty"></a>`no_scope_declared_leaves_the_list_empty` | `LLR-STA-02` | An empty scope list means the analysis is omitted with a stated reason, not that every file belongs to one scope. |

### 3.2. [test/unit/registry.c](../test/unit/registry.c)

Role: **unit**. **15 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the_environment_variable_names_the_runtime_location"></a>`the_environment_variable_names_the_runtime_location` | `LLR-ROP-01`, `LLR-ROP-02` | The environment variable names the runtime location, and wins over the path adjacent to the executable — which for a unit binary does not exist at all. |
| 2 | <a id="an_absent_runtime_location_is_fatal"></a>`an_absent_runtime_location_is_fatal` | `LLR-ROP-04` | A runtime location that does not exist ends the run: no analysis is possible without one. |
| 3 | <a id="a_runtime_location_that_is_a_file_is_fatal"></a>`a_runtime_location_that_is_a_file_is_fatal` | `LLR-ROP-04` | A runtime location that is a regular file is the same unusable state as a missing one, and is treated the same way. |
| 4 | <a id="an_extension_map_naming_no_language_is_fatal"></a>`an_extension_map_naming_no_language_is_fatal` | `LLR-ROP-04` | An extension map naming no language leaves nothing that can be routed to a module, which is the state HLR-036 calls fatal. |
| 5 | <a id="the_extension_map_is_runtime_data"></a>`the_extension_map_is_runtime_data` | `LLR-ROP-03` | Pointing an unrelated extension at a language resolves, and an extension the data does not name does not — so the mapping is read from runtime data rather than compiled in. |
| 6 | <a id="the_extension_map_tolerates_comments_and_bare_extensions"></a>`the_extension_map_tolerates_comments_and_bare_extensions` | `LLR-ROP-03` | Comments and blank lines are not entries, an extension written without its leading period still matches, and matching ignores case. |
| 7 | <a id="a_module_is_loaded_on_first_use_of_its_extension"></a>`a_module_is_loaded_on_first_use_of_its_extension` | `LLR-RFP-01`, `LLR-RFP-03`, `LLR-RFP-08` | A file's language comes from its extension alone, and first use loads the grammar and compiles all six queries. |
| 8 | <a id="a_language_is_loaded_at_most_once"></a>`a_language_is_loaded_at_most_once` | `LLR-RFP-02` | A second lookup returns the cached module, so a language is loaded at most once per run and a mixed-language target needs one pass. |
| 9 | <a id="an_unmapped_extension_yields_no_module"></a>`an_unmapped_extension_yields_no_module` | `LLR-RFP-05` | An extension the map does not name, and a file name with no extension at all, both yield no module — so the caller skips rather than fails. |
| 10 | <a id="an_absent_grammar_makes_the_language_unusable"></a>`an_absent_grammar_makes_the_language_unusable` | `LLR-RFP-06` | A module whose grammar is missing is excluded rather than fatal, and opening the registry does not load any grammar up front. |
| 11 | <a id="a_missing_query_file_makes_the_language_unusable"></a>`a_missing_query_file_makes_the_language_unusable` | `LLR-RFP-06`, `LLR-RFP-08` | A module omitting one of the six required query files is handled as unusable, not as undefined behaviour. |
| 12 | <a id="an_invalid_query_makes_the_language_unusable"></a>`an_invalid_query_makes_the_language_unusable` | `LLR-RFP-06` | A query file that will not compile makes its language unusable rather than crashing the run or being silently ignored. |
| 13 | <a id="an_unusable_language_is_not_retried"></a>`an_unusable_language_is_not_retried` | `LLR-RFP-06`, `LLR-RFP-07` | A language that failed to load is recorded once, so the diagnostic is emitted once rather than once per file that would have used it. |
| 14 | <a id="no_particular_language_is_required"></a>`no_particular_language_is_required` | `LLR-ROP-05` | The registry opens over whatever the runtime location holds and verifies no particular language up front. |
| 15 | <a id="close_is_safe_on_null_and_on_a_zeroed_registry"></a>`close_is_safe_on_null_and_on_a_zeroed_registry` | `LLR-RCL-01` | Teardown is safe on a null and on a never-opened registry, so every error path can release unconditionally. |

### 3.3. [test/unit/analyze.c](../test/unit/analyze.c)

Role: **unit**. **41 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="physical_lines_are_counted"></a>`physical_lines_are_counted` | `LLR-ANL-06` | The physical line count comes from the mapped contents. |
| 2 | <a id="an_unterminated_final_line_counts"></a>`an_unterminated_final_line_counts` | `LLR-ANL-06` | A trailing fragment with no terminating newline is a line the reader sees, and is counted as one. |
| 3 | <a id="a_zero_length_file_reports_zero_without_error"></a>`a_zero_length_file_reports_zero_without_error` | `LLR-ANL-04` | A file of zero length reports zero metrics without error, rather than being mapped — mmap of an empty file fails with EINVAL. |
| 4 | <a id="an_unreadable_file_is_a_failure_without_metrics"></a>`an_unreadable_file_is_a_failure_without_metrics` | `LLR-ANL-02` | A file that cannot be read yields a failure and no metrics for the caller to release, so the run continues leak-free. |
| 5 | <a id="the_metrics_carry_the_path_and_language"></a>`the_metrics_carry_the_path_and_language` | `LLR-ANL-02` | The returned metrics carry the path measured and the language resolved for it. |
| 6 | <a id="a_function_is_reported_with_its_name_and_line_range"></a>`a_function_is_reported_with_its_name_and_line_range` | `LLR-ANL-07`, `LLR-ANL-35` | A function is reported with its name and its line range, the span beginning at the signature rather than at the body's opening brace. |
| 7 | <a id="a_nested_function_is_reported_in_its_own_right"></a>`a_nested_function_is_reported_in_its_own_right` | `LLR-ANL-09` | A named function declared inside another is reported as a function of its own, with its own name and line range, rather than folded into its enclosing function. |
| 8 | <a id="a_prototype_is_not_a_function"></a>`a_prototype_is_not_a_function` | `LLR-ANL-08`, `LLR-ANL-36` | A declaration without a body contributes no function: what counts as one is the query file's decision, and it supplies no body capture for a prototype. |
| 9 | <a id="a_function_name_outlives_the_mapping"></a>`a_function_name_outlives_the_mapping` | `LLR-ANL-07` | A name is still readable after analyze_file returns, so it was copied out of the mapping rather than pointed into it. |
| 10 | <a id="a_file_that_fails_to_parse_is_a_failure"></a>`a_file_that_fails_to_parse_is_a_failure` | `LLR-ANL-01` | A file whose tree contains an error node is skipped whole and reported as a failure, rather than reported from a partially valid tree. |
| 11 | <a id="an_unmapped_extension_is_a_skip_not_a_failure"></a>`an_unmapped_extension_is_a_skip_not_a_failure` | `LLR-ANL-37` | A file whose extension maps to no language is a skip, a distinct outcome from a failure, so the exit status stays 0. |
| 12 | <a id="spans_are_sorted_before_merging"></a>`spans_are_sorted_before_merging` | `LLR-MRG-01` | Spans arriving out of order are sorted before anything is merged, and disjoint spans stay separate. |
| 13 | <a id="overlapping_spans_coalesce"></a>`overlapping_spans_coalesce` | `LLR-MRG-02` | Two spans that overlap become one covering both, so their shared lines are counted once. |
| 14 | <a id="a_nested_span_is_absorbed_not_counted_twice"></a>`a_nested_span_is_absorbed_not_counted_twice` | `LLR-MRG-02`, `LLR-MRG-03` | A span wholly inside another is absorbed and contributes nothing of its own — the canonical case of a block comment carrying inline comment syntax, where subtracting per capture counts its lines twice. |
| 15 | <a id="a_shared_line_is_counted_once"></a>`a_shared_line_is_counted_once` | `LLR-MRG-03`, `LLR-MRG-05` | A line shared by three comments is one line, however many spans touch it. Counting per span is what drives ELOC below zero. |
| 16 | <a id="coalescing_a_trailing_run_stays_in_bounds"></a>`coalescing_a_trailing_run_stays_in_bounds` | `LLR-MRG-04` | Coalescing a run of spans that reaches the last element stops there. The bound is the whole of the requirement: without it the loop reads one past the final span whenever the trailing run is longer than one. |
| 17 | <a id="merging_an_empty_span_list_is_zero"></a>`merging_an_empty_span_list_is_zero` | `LLR-MRG-04` | A file with no comments merges to nothing without touching an empty array. |
| 18 | <a id="the_narrowest_enclosing_function_wins"></a>`the_narrowest_enclosing_function_wins` | `LLR-INN-01` | A statement inside a nested function is attributed to that function rather than to the one enclosing it. |
| 19 | <a id="the_narrowest_wins_whatever_order_the_ranges_are_in"></a>`the_narrowest_wins_whatever_order_the_ranges_are_in` | `LLR-INN-01`, `LLR-INN-02` | The answer does not depend on the order the ranges were discovered in — an implementation returning the first containing range would also break determinism. |
| 20 | <a id="an_offset_outside_every_function_has_no_owner"></a>`an_offset_outside_every_function_has_no_owner` | `LLR-INN-02` | An offset outside every function belongs to none, so file-scope code contributes to the file alone; the start offset is inclusive and the end offset exclusive. |
| 21 | <a id="an_empty_range_index_owns_nothing"></a>`an_empty_range_index_owns_nothing` | `LLR-INN-01` | A file defining no function attributes nothing, rather than indexing an empty array. |
| 22 | <a id="a_multi_line_statement_counts_once"></a>`a_multi_line_statement_counts_once` | `LLR-ANL-11` | The same expression written across three lines and on one line yields the same ELOC, so layout cannot move the number. |
| 23 | <a id="only_statements_count_toward_eloc"></a>`only_statements_count_toward_eloc` | `LLR-ANL-10` | A directive, a lone brace, a bare declaration, and a blank line contribute nothing, while an initialising declaration and a return each contribute one. |
| 24 | <a id="two_statements_on_one_line_count_once"></a>`two_statements_on_one_line_count_once` | `LLR-ANL-38` | Two statements sharing a line are one line of code. Counting captures instead would make the same two statements worth twice as much on one line as on two. |
| 25 | <a id="a_nested_functions_statements_are_not_counted_twice"></a>`a_nested_functions_statements_are_not_counted_twice` | `LLR-INN-02`, `LLR-ANL-40` | A nested function's statements contribute to it alone, the enclosing function keeps only its own, and the file counts each line once. |
| 26 | <a id="file_scope_code_counts_for_the_file_only"></a>`file_scope_code_counts_for_the_file_only` | `LLR-ANL-40` | An initialised global contributes to the file's ELOC and to no function's. |
| 27 | <a id="a_file_with_nothing_executable_reports_zero_eloc"></a>`a_file_with_nothing_executable_reports_zero_eloc` | `LLR-ANL-04`, `LLR-ANL-10` | A file of comments, directives, and bare declarations reports zero ELOC without error. |
| 28 | <a id="a_trailing_comment_does_not_remove_its_line"></a>`a_trailing_comment_does_not_remove_its_line` | `LLR-ANL-39` | A line of code carrying a trailing comment is still a line of code. The exclusion is byte-granular for this reason; a line-granular one deletes the statement. |
| 29 | <a id="a_function_that_never_branches_is_one"></a>`a_function_that_never_branches_is_one` | `LLR-ANL-21` | A function with no decision point reports 1. A query capturing the function itself would report 2, which is the failure this asserts against. |
| 30 | <a id="each_decision_point_adds_one"></a>`each_decision_point_adds_one` | `LLR-ANL-21` | Two decision points make three, so the base and the increment are both applied exactly once. |
| 31 | <a id="a_nested_functions_decisions_are_not_counted_twice"></a>`a_nested_functions_decisions_are_not_counted_twice` | `LLR-ANL-22`, `LLR-INN-02` | A nested named function owns its decision points and the function enclosing it gains none of them; running the query against the enclosing body without attribution would report one more. |
| 32 | <a id="an_unreported_scope_attributes_to_the_named_function_around_it"></a>`an_unreported_scope_attributes_to_the_named_function_around_it` | `LLR-ANL-22` | An offset inside a scope that is not a reported function resolves to the nearest named function containing it. This is the anonymous-callable rule, whose language-level observable waits for a language with lambdas; the mechanism it constrains is verified here. |
| 33 | <a id="a_file_scope_decision_belongs_to_no_function"></a>`a_file_scope_decision_belongs_to_no_function` | `LLR-ANL-41` | A conditional in a file-scope initialiser is charged to no function rather than to an arbitrary one. |
| 34 | <a id="filemetrics_free_is_safe_on_null"></a>`filemetrics_free_is_safe_on_null` | `LLR-ANL-02` | Releasing a null metrics structure does not fault, so teardown is safe on every path. |
| 35 | <a id="statements_after_a_terminator_are_recorded"></a>`statements_after_a_terminator_are_recorded` | `LLR-DED-01` | The named siblings following a captured terminator are recorded as unreachable, with the after-terminator cause. |
| 36 | <a id="a_label_after_a_terminator_is_not_recorded"></a>`a_label_after_a_terminator_is_not_recorded` | `LLR-DED-01` | A goto label following a return is a sibling of it and is reachable. Recording it would be a false claim inviting the deletion of a live branch target — the single case that decides whether this analysis can be trusted. |
| 37 | <a id="a_branch_guarded_by_a_variable_is_never_recorded"></a>`a_branch_guarded_by_a_variable_is_never_recorded` | `LLR-DED-03` | A condition holding a variable is not claimed dead however evidently its value could be inferred: deciding it needs data flow, and elc performs none. |
| 38 | <a id="a_literal_branch_is_recorded_with_its_whole_span"></a>`a_literal_branch_is_recorded_with_its_whole_span` | `LLR-DED-02` | The branch a literal condition excludes is recorded as its full line range, because the span is what the reader deletes. |
| 39 | <a id="a_predicate_that_does_not_hold_rejects_the_match"></a>`a_predicate_that_does_not_hold_rejects_the_match` | `LLR-ANL-46` | A query predicate is evaluated rather than ignored. The parser library returns predicates as data, so without evaluation every conditional would match the literal-condition pattern. |
| 40 | <a id="a_dead_span_is_attributed_to_the_innermost_function"></a>`a_dead_span_is_attributed_to_the_innermost_function` | `LLR-DED-04` | Each recorded span belongs to the reported function containing it, by the rule ELOC and complexity already use. |
| 41 | <a id="a_language_with_no_dead_code_query_is_unanalysed_not_clean"></a>`a_language_with_no_dead_code_query_is_unanalysed_not_clean` | `LLR-DED-05` | A language module supplying no dead-code query records that the analysis was not performed, rather than recording that no dead code was found. |

### 3.4. [test/unit/calltree.c](../test/unit/calltree.c)

Role: **unit**. **16 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="fan_out_counts_distinct_callees"></a>`fan_out_counts_distinct_callees` | `LLR-CTR-01` | Four call sites to two functions give a fan-out of two, so the measure is of subroutines invoked and not of invocations. |
| 2 | <a id="fan_out_ignores_global_edges"></a>`fan_out_ignores_global_edges` | `LLR-CTR-07` | A function that writes a global another reads has a fan-out of zero: coupling through shared state is not invocation. |
| 3 | <a id="direct_recursion_is_detected"></a>`direct_recursion_is_detected` | `LLR-CTR-02` | A function calling itself is reported as a one-member cycle. |
| 4 | <a id="mutual_recursion_is_detected"></a>`mutual_recursion_is_detected` | `LLR-CTR-02` | A mutually recursive pair is one cycle of two members rather than two cycles of one. |
| 5 | <a id="a_straight_line_program_has_no_cycles"></a>`a_straight_line_program_has_no_cycles` | `LLR-CTR-02` | An acyclic call graph reports no recursion, so the trivial single-node components every decomposition produces are not mistaken for self-calls. |
| 6 | <a id="a_global_cycle_is_not_recursion"></a>`a_global_cycle_is_not_recursion` | `LLR-CTR-07` | Two functions each writing a global the other reads form a cycle in the SDG and no recursion at all, which is the case that would otherwise produce a false critical MISRA C Rule 17.2 finding on ordinary code. |
| 7 | <a id="depth_is_the_length_of_the_deepest_chain"></a>`depth_is_the_length_of_the_deepest_chain` | `LLR-CTR-05`, `LLR-LPD-04` | A three-function chain from a declared entry point measures three, the entry point counting as the first layer. |
| 8 | <a id="the_deepest_chain_is_an_ordered_sequence"></a>`the_deepest_chain_is_an_ordered_sequence` | `LLR-LPD-01`, `LLR-LPD-03` | With a shallow branch listed first and a deep one second, the reported chain is the deep one in order — a search taking the first edge would give the wrong path and the wrong length. |
| 9 | <a id="depth_measures_from_the_deepest_entry_point"></a>`depth_measures_from_the_deepest_entry_point` | `LLR-CTR-05` | Given two entry points of different depths, the reported figure and chain are the worst case across them. |
| 10 | <a id="recursion_yields_no_depth_and_terminates"></a>`recursion_yields_no_depth_and_terminates` | `LLR-CTR-04` | A recursive call graph reports its cycle, no depth figure, and no chain — and the run terminates, which a longest-path search over a cyclic graph would not. |
| 11 | <a id="no_entry_points_omits_depth_with_a_reason"></a>`no_entry_points_omits_depth_with_a_reason` | `LLR-CTR-03`, `LLR-CTR-09` | With no declaration the depth analysis is omitted rather than failed, `main` is not inferred despite being present, and the measurements needing no declaration are still produced. |
| 12 | <a id="an_entry_point_matching_nothing_is_a_distinct_omission"></a>`an_entry_point_matching_nothing_is_a_distinct_omission` | `LLR-CTR-08` | A declared entry point naming no analysed function yields a different omission from declaring none, because the two call for different actions. |
| 13 | <a id="depth_carries_the_unresolved_count"></a>`depth_carries_the_unresolved_count` | `LLR-CTR-06` | The unresolved-call count travels with the depth, since a chain through an unresolved call is not followed and the figure is a lower bound. |
| 14 | <a id="a_chain_does_not_travel_along_a_global_edge"></a>`a_chain_does_not_travel_along_a_global_edge` | `LLR-CTR-07` | A chain stops at the last function actually called, rather than continuing to one that merely reads a global the callee wrote. |
| 15 | <a id="an_empty_project_measures_nothing_without_failing"></a>`an_empty_project_measures_nothing_without_failing` | `LLR-CTR-01` | A run that analysed nothing produces an empty but valid set of measurements. |
| 16 | <a id="tree_results_free_is_safe_on_null_and_twice"></a>`tree_results_free_is_safe_on_null_and_twice` | `LLR-CTR-01` | Releasing a null or already-released result set does not fault, so teardown is unconditional on every path. |

### 3.5. [test/unit/discover.c](../test/unit/discover.c)

Role: **unit**. **27 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="excluded_extension_matches_the_runtime_list"></a>`excluded_extension_matches_the_runtime_list` | `LLR-EXT-01` | An extension present in the runtime list is excluded and one absent from it is not, the list having been read from the runtime location rather than from any table in the binary. |
| 2 | <a id="excluded_extension_ignores_case"></a>`excluded_extension_ignores_case` | `LLR-EXT-02` | One entry covers every spelling of an extension, so a list maintained in lower case still excludes an upper-case file name. |
| 3 | <a id="a_name_without_an_extension_is_not_excluded"></a>`a_name_without_an_extension_is_not_excluded` | `LLR-EXT-01` | A dot occurring in a directory component, and a name that is nothing but a suffix, are both correctly read as having no extension to test. |
| 4 | <a id="an_empty_exclusion_list_excludes_nothing"></a>`an_empty_exclusion_list_excludes_nothing` | `LLR-EXT-03` | An empty list excludes nothing, which is the state discovery runs in when the runtime file is absent. |
| 5 | <a id="extension_list_skips_comments_and_blank_lines"></a>`extension_list_skips_comments_and_blank_lines` | `LLR-EXT-02` | Comment and blank lines are not entries, and an entry written without its leading period still matches, so the data file can be maintained in either style. |
| 6 | <a id="a_missing_extension_list_is_not_fatal"></a>`a_missing_extension_list_is_not_fatal` | `LLR-EXT-03` | An absent binary-extension file yields an empty list rather than aborting the run, so discovery still completes. |
| 7 | <a id="a_regular_file_target_is_appended_directly"></a>`a_regular_file_target_is_appended_directly` | `LLR-DSC-04` | A target that is a regular file is appended to the file list with no directory traversal performed for it. |
| 8 | <a id="a_missing_target_is_rejected_with_an_empty_list"></a>`a_missing_target_is_rejected_with_an_empty_list` | `LLR-DSC-02` | A target that does not exist is rejected, and no partial file list survives the rejection. |
| 9 | <a id="a_target_that_is_neither_file_nor_directory_is_rejected"></a>`a_target_that_is_neither_file_nor_directory_is_rejected` | `LLR-DSC-02` | A FIFO named as a target is rejected, covering the class of targets that exist but are neither regular file nor directory. |
| 10 | <a id="every_target_is_validated_before_any_is_walked"></a>`every_target_is_validated_before_any_is_walked` | `LLR-DSC-01` | A valid target given alongside an invalid one is not walked, so a report can never cover fewer targets than the user named. |
| 11 | <a id="a_file_reached_through_two_targets_appears_once"></a>`a_file_reached_through_two_targets_appears_once` | `LLR-DSC-07` | A file named explicitly and also contained in a named directory is retained once, canonicalisation having preceded de-duplication. |
| 12 | <a id="the_file_list_is_sorted_into_byte_order"></a>`the_file_list_is_sorted_into_byte_order` | `LLR-DSC-08` | The completed list is in ascending byte order whatever order the filesystem enumerated it in. |
| 13 | <a id="hidden_entries_and_binary_extensions_are_excluded"></a>`hidden_entries_and_binary_extensions_are_excluded` | `LLR-FTS-02`, `LLR-FTS-03` | A hidden file, a hidden directory's contents, and a file carrying an excluded extension are all absent from the walk's result. |
| 14 | <a id="the_walk_descends_into_subdirectories"></a>`the_walk_descends_into_subdirectories` | `LLR-FTS-01` | The traversal is recursive: a file several directories below the target is found. |
| 15 | <a id="a_hidden_directory_named_as_the_target_is_traversed"></a>`a_hidden_directory_named_as_the_target_is_traversed` | `LLR-FTS-06` | The hidden-entry exclusion applies below the target and not to the target itself, so a hidden directory named on the command line is walked. |
| 16 | <a id="files_and_directories_are_classified_independently"></a>`files_and_directories_are_classified_independently` | `LLR-DSC-03` | Each target is routed on its own type, so a file and a directory given together are both handled correctly. |
| 17 | <a id="a_cyclic_directory_symlink_terminates"></a>`a_cyclic_directory_symlink_terminates` | `LLR-FTS-04`, `LLR-FTS-05` | A self-referential directory link does not produce an unbounded traversal, and the linked directory is not descended into. Reaching the assertion is itself the result. |
| 18 | <a id="a_symbolic_link_named_as_a_target_is_resolved"></a>`a_symbolic_link_named_as_a_target_is_resolved` | `LLR-DSC-06` | A link named directly as a target is resolved to its referent, the other half of the symbolic-link policy from the one the walk applies. |
| 19 | <a id="a_path_that_cannot_be_canonicalised_is_a_per_file_failure"></a>`a_path_that_cannot_be_canonicalised_is_a_per_file_failure` | `LLR-DSC-09` | A canonicalisation failure, provoked through a link-time wrapper, drops that path and records a per-file failure rather than ending the run. |
| 20 | <a id="git_walk_yields_the_tracked_files"></a>`git_walk_yields_the_tracked_files` | `LLR-GIT-02` | The walk enumerates the files committed at `HEAD` and not a file that exists in the working tree but was never added, which is the exclusion the route exists to perform. |
| 21 | <a id="git_walk_is_scoped_to_the_target"></a>`git_walk_is_scoped_to_the_target` | `LLR-GIT-01` | Naming a subdirectory enumerates that subdirectory alone, though the tree at `HEAD` is the whole repository. |
| 22 | <a id="git_walk_declines_a_directory_in_no_repository"></a>`git_walk_declines_a_directory_in_no_repository` | `LLR-GIT-04` | A directory outside any repository is declined rather than failed: nothing is appended, no failure is recorded, and the negative return is what sends the caller to the filesystem walk. |
| 23 | <a id="git_walk_declines_a_target_the_repository_does_not_track"></a>`git_walk_declines_a_target_the_repository_does_not_track` | `LLR-GIT-04` | A directory inside a work tree that `HEAD` knows nothing about — a build directory, or a home directory under version control — is declined, so the target is analysed by traversal instead of reported as empty. |
| 24 | <a id="a_tracked_directory_of_excluded_files_stays_on_the_git_route"></a>`a_tracked_directory_of_excluded_files_stays_on_the_git_route` | `LLR-GIT-04` | A tracked directory whose every tracked file is excluded yields nothing and is still judged applicable, so the run does not fall back and analyse the untracked files beside them. Distinguishes "the repository tracks this directory" from "the repository yielded a file". |
| 25 | <a id="a_route_is_recorded_for_each_target"></a>`a_route_is_recorded_for_each_target` | `LLR-DSC-10` | Each target's route is recorded in the order given, naming the same target twice records it once, and releasing the list leaves it usable rather than stale. |
| 26 | <a id="the_route_list_owns_the_target_it_records"></a>`the_route_list_owns_the_target_it_records` | `LLR-DSC-10` | Mutating the caller's buffer after recording does not change the record, so the report can outlive the strings discovery was given. |
| 27 | <a id="filelist_free_is_safe_on_null"></a>`filelist_free_is_safe_on_null` | `LLR-DSC-01` | Releasing a null file list or extension list does not fault, so teardown is safe on every path. |

### 3.6. [test/unit/state.c](../test/unit/state.c)

Role: **unit**. **19 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="roots_are_entry_points_and_address_taken_functions"></a>`roots_are_entry_points_and_address_taken_functions` | `LLR-RTS-01` | The root set is the union of the two and nothing else: a function neither declared nor address-taken is not a root. |
| 2 | <a id="a_declared_symbol_naming_nothing_is_skipped_not_fatal"></a>`a_declared_symbol_naming_nothing_is_skipped_not_fatal` | `LLR-STA-01` | A declared entry point matching no analysed function is skipped rather than failing the run, since analysing one directory of a project whose entry point lives in another is ordinary. |
| 3 | <a id="a_root_declared_twice_is_one_root"></a>`a_root_declared_twice_is_one_root` | `LLR-RTS-01` | The root set is de-duplicated, so a symbol declared twice and also address-taken contributes one root. |
| 4 | <a id="a_clique_of_unused_functions_is_unreachable"></a>`a_clique_of_unused_functions_is_unreachable` | `LLR-RCH-03` | Two unused functions calling one another are both reported: each has a caller, so a textual rule finds neither, and no path reaches the pair from any root. |
| 5 | <a id="an_address_taken_function_and_its_callees_are_reachable"></a>`an_address_taken_function_and_its_callees_are_reachable` | `LLR-RTS-02` | An address-taken function is a root rather than an exemption: what it calls is reached through it. An implementation that merely excluded it from the report would report its callee dead. |
| 6 | <a id="reachability_does_not_travel_along_a_global_edge"></a>`reachability_does_not_travel_along_a_global_edge` | `LLR-STA-03` | A function reachable only by reading an object another function wrote is still unreachable: control never travels along a state edge, and following one would rescue genuinely dead code from the report. |
| 7 | <a id="with_no_entry_points_nothing_is_unreachable"></a>`with_no_entry_points_nothing_is_unreachable` | `LLR-STA-01` | With no declaration the analysis is omitted with its reason, and no function is reported unreachable. |
| 8 | <a id="a_declaration_matching_nothing_is_its_own_omission"></a>`a_declaration_matching_nothing_is_its_own_omission` | `LLR-STA-01` | Declaring an entry point that matches nothing is a different omission from declaring none, because the two call for different actions from the reader. |
| 9 | <a id="a_global_touched_by_one_function_is_a_scope_reduction_candidate"></a>`a_global_touched_by_one_function_is_a_scope_reduction_candidate` | `LLR-GLB-02`, `LLR-SDG-16` | An object one function writes and reads is flagged, and the graph carries no edge for it at all — which is why the finding is computed from the access records rather than from the edge table. |
| 10 | <a id="a_global_spanning_disconnected_regions_is_a_hidden_channel"></a>`a_global_spanning_disconnected_regions_is_a_hidden_channel` | `LLR-GLB-03` | An object written in one region of the call graph and read in another that never calls it is flagged, with the region count establishing the disconnection. |
| 11 | <a id="shared_state_within_one_region_is_ordinary"></a>`shared_state_within_one_region_is_ordinary` | `LLR-GLB-05` | The same sharing between two functions joined by a call edge carries no finding. Without this distinction the warning would fire on every shared variable. |
| 12 | <a id="an_undeclared_identifier_is_not_global_state"></a>`an_undeclared_identifier_is_not_global_state` | `LLR-SDG-13` | An identifier no file declares at file scope is not global state, however the over-broad read and write patterns captured it. |
| 13 | <a id="a_global_touched_only_by_dead_functions_is_unreachable"></a>`a_global_touched_only_by_dead_functions_is_unreachable` | `LLR-UGL-01` | An object every one of whose accessors is unreachable is itself reported unreachable. |
| 14 | <a id="a_global_no_function_touches_is_not_claimed_dead"></a>`a_global_no_function_touches_is_not_claimed_dead` | `LLR-UGL-02` | An object no analysed function accesses is not claimed: it may be touched from file scope or from a translation unit outside the target. |
| 15 | <a id="an_edge_crossing_a_declared_boundary_is_reported"></a>`an_edge_crossing_a_declared_boundary_is_reported` | `LLR-ISO-01` | A call from a function in one declared scope to a function in another is reported as a crossing. |
| 16 | <a id="a_shared_global_crossing_a_boundary_is_reported"></a>`a_shared_global_crossing_a_boundary_is_reported` | `LLR-ISO-01` | A scope that never calls into another but writes a variable the other reads has not been isolated. An implementation checking call edges alone would report the arrangement clean. |
| 17 | <a id="a_component_matching_no_declaration_is_outside_the_partition"></a>`a_component_matching_no_declaration_is_outside_the_partition` | `LLR-ISO-02` | An edge touching a file no declaration names is not a crossing: inventing a boundary would report violations against a division nobody drew. |
| 18 | <a id="with_no_scopes_declared_the_analysis_is_omitted"></a>`with_no_scopes_declared_the_analysis_is_omitted` | `LLR-STA-02` | With no execution scopes declared the analysis is omitted with its reason rather than reporting an empty result. |
| 19 | <a id="an_empty_graph_analyses_without_incident"></a>`an_empty_graph_analyses_without_incident` | `LLR-STA-04` | A run that analysed nothing produces empty state results rather than faulting. |

### 3.7. [test/unit/graph.c](../test/unit/graph.c)

Role: **unit**. **16 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="every_discovered_function_becomes_a_node"></a>`every_discovered_function_becomes_a_node` | `LLR-SDG-01` | Every function across every analysed file receives a node, so the graph spans the project rather than any one file. |
| 2 | <a id="node_identifiers_follow_sorted_file_order"></a>`node_identifiers_follow_sorted_file_order` | `LLR-SDG-09` | Files handed over in unsorted order still number their functions by sorted path, so node identifiers are a property of the source tree and not of discovery order. |
| 3 | <a id="a_call_resolves_across_files"></a>`a_call_resolves_across_files` | `LLR-SDG-02` | A call whose definition lies in another file resolves to an edge, which is the cross-file resolution the graph exists to perform. |
| 4 | <a id="repeated_calls_collapse_to_one_edge_with_a_count"></a>`repeated_calls_collapse_to_one_edge_with_a_count` | `LLR-SDG-04`, `LLR-SDG-14` | Three call sites between one pair of functions produce one edge carrying a count of three, so out-degree is distinct callees rather than call sites. |
| 5 | <a id="an_unresolvable_call_is_counted_and_does_not_abort"></a>`an_unresolvable_call_is_counted_and_does_not_abort` | `LLR-SDG-07`, `LLR-SDG-08` | A call into a library is counted, produces no edge, and does not fail the build — no destination is invented for it, which is what keeps a later dead-code proof sound. |
| 6 | <a id="a_call_from_file_scope_has_no_caller_node"></a>`a_call_from_file_scope_has_no_caller_node` | `LLR-SDG-14` | A call outside every reported function draws no edge and is counted as unresolved, because the graph does not represent it and the reader is judging completeness. |
| 7 | <a id="a_global_links_its_writer_to_its_reader"></a>`a_global_links_its_writer_to_its_reader` | `LLR-SDG-03`, `LLR-SDG-12` | A global written in one file and read in another produces an edge between the two functions, and that edge names the object through the graph's own copy of the string. |
| 8 | <a id="an_undeclared_name_is_not_global_state"></a>`an_undeclared_name_is_not_global_state` | `LLR-SDG-13` | An identifier written in one function and read in another, declared nowhere at file scope, produces no edge — the over-broad capture is resolved away rather than believed. |
| 9 | <a id="two_objects_between_one_pair_are_two_edges"></a>`two_objects_between_one_pair_are_two_edges` | `LLR-SDG-14` | Global edges are per object and do not collapse the way call edges do, since merging them would lose which shared state couples the two functions. |
| 10 | <a id="an_address_taken_function_is_marked"></a>`an_address_taken_function_is_marked` | `LLR-SDG-13` | A function assigned without being called is marked as a reachability root, captured in this phase though Phase 10 is what consumes it. |
| 11 | <a id="an_address_taken_name_that_is_not_a_function_is_discarded"></a>`an_address_taken_name_that_is_not_a_function_is_discarded` | `LLR-SDG-13` | An ordinary variable captured by the deliberately over-broad address-taken patterns resolves to nothing and marks no node, which is what allows the query files not to decide. |
| 12 | <a id="the_component_projection_is_file_level"></a>`the_component_projection_is_file_level` | `LLR-SDG-10` | A call between functions in two files yields one component edge between those files, a component being a source file. |
| 13 | <a id="a_call_within_one_file_is_no_component_dependency"></a>`a_call_within_one_file_is_no_component_dependency` | `LLR-SDG-10` | A call inside one file adds no component edge, so a file is not recorded as depending on itself. |
| 14 | <a id="an_empty_project_builds_an_empty_graph"></a>`an_empty_project_builds_an_empty_graph` | `LLR-SDG-01` | A run that analysed nothing builds a valid empty graph, including the library structure, rather than failing or leaving a null handle. |
| 15 | <a id="the_library_returns_errors_instead_of_aborting"></a>`the_library_returns_errors_instead_of_aborting` | `LLR-SDG-15` | Asking a cyclic graph for a topological ordering returns an error rather than aborting the process, so the library's default abort-on-error handler has been replaced. Reaching the assertion is itself the result: without the handler the test dies rather than fails. |
| 16 | <a id="graph_free_is_safe_on_null_and_twice"></a>`graph_free_is_safe_on_null_and_twice` | `LLR-SDG-12` | Releasing a null or an already-released graph does not fault, so teardown is unconditional on every exit path. |

### 3.8. [test/unit/format_graph.c](../test/unit/format_graph.c)

Role: **unit**. **7 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="graphml_is_off_unless_asked_for"></a>`graphml_is_off_unless_asked_for` | `LLR-GML-02` | With an output path but no request, no export is warranted: the export is opt-in, unlike the .dot companion. |
| 2 | <a id="graphml_needs_an_output_path"></a>`graphml_needs_an_output_path` | `LLR-GML-03` | With the export requested but the report going to standard output, nothing is warranted — there is no path to derive the companion's name from. |
| 3 | <a id="graphml_is_written_when_asked_for_and_named"></a>`graphml_is_written_when_asked_for_and_named` | `LLR-GML-02` | Both conditions together warrant the export. |
| 4 | <a id="the_companion_replaces_the_extension"></a>`the_companion_replaces_the_extension` | `LLR-GML-03` | report.md yields report.graphml rather than report.md.graphml: the extension is substituted, not appended. |
| 5 | <a id="a_path_without_an_extension_gains_one"></a>`a_path_without_an_extension_gains_one` | `LLR-GML-03` | An output path with no extension gains the companion's rather than being left unnamed. |
| 6 | <a id="a_dot_in_a_directory_is_not_an_extension"></a>`a_dot_in_a_directory_is_not_an_extension` | `LLR-GML-03` | A dot in a directory name is not mistaken for the file's extension, which would otherwise truncate the path at the directory. |
| 7 | <a id="a_directory_dot_with_an_extension_still_substitutes"></a>`a_directory_dot_with_an_extension_still_substitutes` | `LLR-GML-03` | A dotted directory alongside a real extension substitutes the extension and leaves the directory alone. |

### 3.9. [test/unit/report.c](../test/unit/report.c)

Role: **unit**. **6 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="totals_sum_across_every_file"></a>`totals_sum_across_every_file` | `LLR-RPT-01` | The project totals are the sum of the per-file physical line counts across every analysed file. |
| 2 | <a id="files_are_presented_in_ascending_path_order"></a>`files_are_presented_in_ascending_path_order` | `LLR-RPT-10`, `LLR-RPT-11` | Files are ordered by path in the model, so presentation order is a property of the report rather than of the order the files were discovered. |
| 3 | <a id="an_empty_run_yields_a_complete_model_with_zero_totals"></a>`an_empty_run_yields_a_complete_model_with_zero_totals` | `LLR-RPT-12` | A run in which no file was analysed still produces a complete model with zero totals, which renders normally. |
| 4 | <a id="assembly_leaves_the_accumulator_empty"></a>`assembly_leaves_the_accumulator_empty` | `LLR-RPT-18` | Ownership of the per-file metrics moves into the report, so releasing both the accumulator and the report cannot free the same metrics twice. |
| 5 | <a id="a_failed_growth_leaves_the_accumulator_intact"></a>`a_failed_growth_leaves_the_accumulator_intact` | `LLR-RPT-16` | A reallocation failure, provoked through a link-time wrapper, is reported and leaves the original allocation intact rather than overwriting it with a null pointer. |
| 6 | <a id="free_is_safe_on_null"></a>`free_is_safe_on_null` | `LLR-RPT-16` | Releasing a null report or accumulator does not fault, so teardown is safe on every path. |

### 3.10. [test/unit/format_text.c](../test/unit/format_text.c)

Role: **unit**. **4 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the_table_carries_the_summary_and_every_file"></a>`the_table_carries_the_summary_and_every_file` | `LLR-TBL-01` | The rendered table contains the project summary tier and one row for each file in the model. |
| 2 | <a id="columns_are_aligned_on_the_longest_path"></a>`columns_are_aligned_on_the_longest_path` | `LLR-TBL-01` | Every file row is rendered to the same width, the path column having been sized from the longest path in the model. |
| 3 | <a id="an_empty_report_still_renders_a_table"></a>`an_empty_report_still_renders_a_table` | `LLR-TBL-01` | A model with no files still renders its headings and column rule, so an empty run is distinguishable from a crash. |
| 4 | <a id="a_write_failure_is_reported"></a>`a_write_failure_is_reported` | `LLR-TBL-03` | A stream that cannot absorb the report yields a non-zero return, so a truncated report is never reported as success. |

### 3.11. [test/integration/cli.bats](../test/integration/cli.bats)

Role: **integration**. **16 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="--help exits 0"></a>`--help exits 0` | — | Requesting help succeeds; it is not an error. |
| 2 | <a id="-h exits 0"></a>`-h exits 0` | — | The short form of the help option behaves as the long form. |
| 3 | <a id="--help writes the summary to stdout, not stderr"></a>`--help writes the summary to stdout, not stderr` | — | The help summary goes to the results stream and nothing goes to the diagnostic stream. |
| 4 | <a id="--help lists every option elc accepts"></a>`--help lists every option elc accepts` | — | The usage summary names each accepted option, making it the reference the documentation is checked against. |
| 5 | <a id="--help documents the exit-status scheme"></a>`--help documents the exit-status scheme` | — | The usage summary describes what each exit status means. |
| 6 | <a id="--help is reported without validating other arguments"></a>`--help is reported without validating other arguments` | — | A help request short-circuits argument validation. |
| 7 | <a id="an unrecognised long option exits 2"></a>`an unrecognised long option exits 2` | — | An unrecognised long option terminates with the fatal status. |
| 8 | <a id="an unrecognised short option exits 2"></a>`an unrecognised short option exits 2` | — | An unrecognised short option terminates with the fatal status. |
| 9 | <a id="no target exits 2"></a>`no target exits 2` | — | An invocation with no target terminates with the fatal status. |
| 10 | <a id="a usage error writes to stderr, not stdout"></a>`a usage error writes to stderr, not stdout` | — | A usage error writes its diagnostic and summary to the diagnostic stream, leaving the results stream empty. |
| 11 | <a id="a usage error names the offending option"></a>`a usage error names the offending option` | — | The diagnostic identifies which option was rejected. |
| 12 | <a id="no target is diagnosed explicitly"></a>`no target is diagnosed explicitly` | — | The missing-target case is diagnosed in its own words rather than as a generic failure. |
| 13 | <a id="a single target is accepted"></a>`a single target is accepted` | — | A single file target is accepted. |
| 14 | <a id="several targets are accepted, files and directories intermixed"></a>`several targets are accepted, files and directories intermixed` | — | Several targets are accepted in one invocation, files and directories freely mixed. |
| 15 | <a id="an accepted invocation writes its report to stdout"></a>`an accepted invocation writes its report to stdout` | — | Nothing but results reaches the results stream. |
| 16 | <a id="a decoy dotfile in the working directory changes nothing"></a>`a decoy dotfile in the working directory changes nothing` | — | Configuration-like files planted beside the invocation produce byte-identical output to their absence. |

### 3.12. [test/integration/docs.bats](../test/integration/docs.bats)

Role: **integration**. **8 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the man page exists"></a>`the man page exists` | — | The project delivers a man page. |
| 2 | <a id="the user manual exists"></a>`the user manual exists` | — | The project delivers a user manual. |
| 3 | <a id="the man page renders without diagnostic"></a>`the man page renders without diagnostic` | — | The delivered man page is well-formed roff. |
| 4 | <a id="the usage summary advertises at least one option"></a>`the usage summary advertises at least one option` | — | The reference the documentation is checked against is non-empty. |
| 5 | <a id="every option in the usage summary appears in the man page"></a>`every option in the usage summary appears in the man page` | — | No accepted option is undocumented in the man page. |
| 6 | <a id="every option in the usage summary appears in the user manual"></a>`every option in the usage summary appears in the user manual` | — | No accepted option is undocumented in the user manual. |
| 7 | <a id="every long option the man page documents is accepted by elc"></a>`every long option the man page documents is accepted by elc` | — | No documented option is unimplemented. |
| 8 | <a id="both documents describe the exit-status scheme"></a>`both documents describe the exit-status scheme` | — | Both documents describe the exit-status classes. |

### 3.13. [test/integration/discovery.bats](../test/integration/discovery.bats)

Role: **integration**. **21 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="a directory target prints a table of its files"></a>`a directory target prints a table of its files` | — | A directory target produces the aligned table, listing the files found beneath it. |
| 2 | <a id="the table reports physical line counts"></a>`the table reports physical line counts` | — | The project total is the sum of the physical line counts of the files discovered. |
| 3 | <a id="a single file target reports that file alone"></a>`a single file target reports that file alone` | — | A regular-file target reports that file and performs no traversal for it. |
| 4 | <a id="files and directories may be intermixed on one command line"></a>`files and directories may be intermixed on one command line` | — | Files and directories given together are each classified on their own type and combined into one report. |
| 5 | <a id="the output shape does not depend on the type of the target"></a>`the output shape does not depend on the type of the target` | — | A file target and a directory target produce the same headings and the same columns, so results from different target types are directly comparable. |
| 6 | <a id="a file named alongside a directory containing it is counted once"></a>`a file named alongside a directory containing it is counted once` | — | A file reached through two overlapping targets appears exactly once in the report and once in the totals. |
| 7 | <a id="two runs over the same target are byte-identical"></a>`two runs over the same target are byte-identical` | — | Repeating an unmodified run produces identical bytes. |
| 8 | <a id="targets given in a different order produce identical output"></a>`targets given in a different order produce identical output` | — | The order the targets were named in does not show through into the report. |
| 9 | <a id="a target that does not exist exits 2"></a>`a target that does not exist exits 2` | — | An absent target ends the run with the status reserved for a run that never happened. |
| 10 | <a id="a target that is neither a file nor a directory exits 2"></a>`a target that is neither a file nor a directory exits 2` | — | A FIFO named as a target is rejected, covering the class of targets that exist but are of the wrong type. |
| 11 | <a id="an unreadable target exits 2"></a>`an unreadable target exits 2` | — | A target that exists but cannot be read is rejected before any analysis, rather than becoming a per-file failure part-way through. |
| 12 | <a id="an invalid target produces no report at all"></a>`an invalid target produces no report at all` | — | A valid target given alongside an invalid one yields no output, so no report can silently cover fewer targets than were named. |
| 13 | <a id="an invalid target is diagnosed on stderr, naming it"></a>`an invalid target is diagnosed on stderr, naming it` | — | The diagnostic identifies the offending target and reaches the diagnostic stream, not the results stream. |
| 14 | <a id="a target with no files reports zero totals and exits 0"></a>`a target with no files reports zero totals and exits 0` | — | A run that found nothing to analyse still emits a well-formed report with zero totals and succeeds. |
| 15 | <a id="the report goes to stdout and nothing else does"></a>`the report goes to stdout and nothing else does` | — | A successful run writes its results to standard output and writes nothing to standard error. |
| 16 | <a id="--output writes the report to the named file"></a>`--output writes the report to the named file` | `LLR-MAIN-17` | The report is written to the named file, and standard output is left empty. |
| 17 | <a id="-o is the short form of --output"></a>`-o is the short form of --output` | — | The short form of the redirection option behaves as the long form. |
| 18 | <a id="a redirected report is byte-identical to the one on stdout"></a>`a redirected report is byte-identical to the one on stdout` | — | Redirection changes where the report goes, not what it says. |
| 19 | <a id="an output file that cannot be opened exits 2"></a>`an output file that cannot be opened exits 2` | `LLR-MAIN-17` | A destination that cannot be opened ends the run with the status reserved for a run that produced no report. |
| 20 | <a id="an output file that cannot be opened is diagnosed on stderr"></a>`an output file that cannot be opened is diagnosed on stderr` | `LLR-MAIN-17` | The diagnostic names the destination and reaches the diagnostic stream. |
| 21 | <a id="an unreadable file inside a target degrades the run to 1"></a>`an unreadable file inside a target degrades the run to 1` | — | A file within a target that cannot be read is a per-file failure: the report still covers the files that succeeded, and the exit status says so. |

### 3.14. [test/integration/language.bats](../test/integration/language.bats)

Role: **integration**. **27 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-007: the language is determined from the extension, unprompted"></a>`HLR-007: the language is determined from the extension, unprompted` | — | A file's language is detected from its extension with no user declaration, and is shown in the report. |
| 2 | <a id="HLR-007: a header is detected as well as a source file"></a>`HLR-007: a header is detected as well as a source file` | — | A header maps to a language and its definitions are reported, so a project's headers are not silently uncovered. |
| 3 | <a id="HLR-008: files sharing a language are analysed in one invocation"></a>`HLR-008: files sharing a language are analysed in one invocation` | — | Several files are analysed in a single invocation and a single pass, with no per-language re-invocation. |
| 4 | <a id="HLR-014: each function is reported with its name and line range"></a>`HLR-014: each function is reported with its name and line range` | — | Every function discovered is reported with its name and its start and end lines. |
| 5 | <a id="HLR-014: the line range starts at the signature, not the brace"></a>`HLR-014: the line range starts at the signature, not the brace` | — | The reported span begins where a reader says the function begins, rather than at the body's opening delimiter. |
| 6 | <a id="HLR-033: functions are presented in start-line order"></a>`HLR-033: functions are presented in start-line order` | — | Functions appear in start-line order rather than in the order the query happened to match them. |
| 7 | <a id="HLR-012: a file with no language module is listed as skipped"></a>`HLR-012: a file with no language module is listed as skipped` | — | A file whose extension maps to no language appears in the report's skipped list, so the report accounts for every discovered file. |
| 8 | <a id="HLR-012: a skipped file is also reported on stderr"></a>`HLR-012: a skipped file is also reported on stderr` | — | The skip is reported through both observables the requirement names: the list and a diagnostic. |
| 9 | <a id="HLR-037: a skip does not make the exit status non-zero"></a>`HLR-037: a skip does not make the exit status non-zero` | — | A skipped file is not a failure and does not by itself make the status non-zero. |
| 10 | <a id="HLR-012: a skipped file does not contribute to the totals"></a>`HLR-012: a skipped file does not contribute to the totals` | — | A skipped file is accounted for without being counted: the project totals are unchanged by its presence. |
| 11 | <a id="HLR-035: a file that fails to parse does not abort the run"></a>`HLR-035: a file that fails to parse does not abort the run` | — | A file that fails to parse leaves the rest of the run intact and the report covers the files that succeeded. |
| 12 | <a id="HLR-120: a parse failure degrades the run to 1"></a>`HLR-120: a parse failure degrades the run to 1` | — | A parse failure produces the status reserved for a run that completed but did not process every file. |
| 13 | <a id="HLR-035: a parse failure names the file on stderr"></a>`HLR-035: a parse failure names the file on stderr` | — | The diagnostic identifies the file that failed and reaches the diagnostic stream. |
| 14 | <a id="HLR-035: a file with a syntax error is skipped whole, not partially reported"></a>`HLR-035: a file with a syntax error is skipped whole, not partially reported` | — | A file carrying an error node is discarded entirely rather than reported from the sound part of its tree, since metrics from a damaged tree are indistinguishable from sound ones once rendered. |
| 15 | <a id="HLR-019: each file reports its own line and function counts"></a>`HLR-019: each file reports its own line and function counts` | — | Each file's row carries its own physical line count and the number of functions it defines. |
| 16 | <a id="HLR-066: a target of only skipped files still reports zero totals"></a>`HLR-066: a target of only skipped files still reports zero totals` | — | A run in which nothing could be analysed still emits a well-formed report with zero totals, and names what it skipped. |
| 17 | <a id="HLR-006: the report has the same sections whatever the target type"></a>`HLR-006: the report has the same sections whatever the target type` | — | A file target and a directory target produce the same sections in the same order. |
| 18 | <a id="HLR-015: each function reports its own ELOC"></a>`HLR-015: each function reports its own ELOC` | — | A function's own ELOC appears beside its line range. |
| 19 | <a id="HLR-024: the project summary carries a combined ELOC total"></a>`HLR-024: the project summary carries a combined ELOC total` | — | The summary reports the combined ELOC across every analysed file. |
| 20 | <a id="HLR-025: the totals are broken down by language"></a>`HLR-025: the totals are broken down by language` | — | The report carries a per-language section, so each language's contribution is separately visible. |
| 21 | <a id="HLR-025: the per-language totals sum to the project totals"></a>`HLR-025: the per-language totals sum to the project totals` | — | With one language present, its row equals the summary exactly — the breakdown is a partition of the totals, not a second count of them. |
| 22 | <a id="HLR-019: a header of declarations only reports zero ELOC"></a>`HLR-019: a header of declarations only reports zero ELOC` | — | A file's own line and ELOC counts are reported per file, and a header whose only statement is one return reports one. |
| 23 | <a id="HLR-032: two runs over a parsed tree are byte-identical"></a>`HLR-032: two runs over a parsed tree are byte-identical` | — | Repeating a run that parses produces identical bytes, so the new sections are as deterministic as the old ones. |
| 24 | <a id="HLR-011: five languages are detected from their extensions"></a>`HLR-011: five languages are detected from their extensions` | — | One target holding a file of each delivered language yields all five in the per-language breakdown. |
| 25 | <a id="HLR-008: a mixed-language target is analysed in one invocation"></a>`HLR-008: a mixed-language target is analysed in one invocation` | — | Four languages are analysed in a single pass, with every function found — no invocation per language. |
| 26 | <a id="HLR-025: each language's contribution is separately visible"></a>`HLR-025: each language's contribution is separately visible` | — | A two-language target reports both languages, in name order. |
| 27 | <a id="HLR-011: elc requires no particular language to be present"></a>`HLR-011: elc requires no particular language to be present` | — | A target of one language runs exactly as a mixed one does; nothing verifies that the other four are installed. |

### 3.15. [test/integration/complexity.bats](../test/integration/complexity.bats)

Role: **integration**. **13 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-017: complexity is reported per function"></a>`HLR-017: complexity is reported per function` | — | Each function's complexity appears beside its ELOC: one for a function that never branches, four for one that branches three times. |
| 2 | <a id="HLR-026: the summary names the most complex function and the largest file"></a>`HLR-026: the summary names the most complex function and the largest file` | — | The report carries both project-wide callouts, naming the function and the file. |
| 3 | <a id="HLR-022: the threshold defaults to 15"></a>`HLR-022: the threshold defaults to 15` | — | With no option given, the listing is built at the documented default. |
| 4 | <a id="HLR-021: a function at or over the threshold is listed for its file"></a>`HLR-021: a function at or over the threshold is listed for its file` | — | A function meeting the threshold appears in the per-file listing, against the file that defines it. |
| 5 | <a id="HLR-021: the listing is at-or-over, not strictly over"></a>`HLR-021: the listing is at-or-over, not strictly over` | — | A function whose complexity equals the threshold is listed; one below it is not. The boundary is the requirement's word, and an off-by-one here is invisible in any other test. |
| 6 | <a id="HLR-022: a lower threshold lists more"></a>`HLR-022: a lower threshold lists more` | — | Lowering the threshold enlarges the listing, so the option reaches the listing at all. |
| 7 | <a id="HLR-063: a malformed threshold is a usage error"></a>`HLR-063: a malformed threshold is a usage error` | — | An argument that is not a number is rejected with the usage-error status rather than silently becoming some other threshold. |
| 8 | <a id="HLR-023: the threshold does not affect the exit status"></a>`HLR-023: the threshold does not affect the exit status` | — | A threshold every function breaches and one no function breaches both exit 0. Findings are data; deciding what a number warrants is the caller's. |
| 9 | <a id="HLR-023: the threshold changes the listing and nothing else"></a>`HLR-023: the threshold changes the listing and nothing else` | — | Everything above the listing section is byte-identical at two very different thresholds, so no total, callout, or ordering moves with it. |
| 10 | <a id="HLR-023: the threshold does not change the totals or the callouts"></a>`HLR-023: the threshold does not change the totals or the callouts` | — | A threshold no function meets leaves the function count and the callouts as they were — the report still describes the code. |
| 11 | <a id="HLR-026: a tie for most complex resolves the same way every run"></a>`HLR-026: a tie for most complex resolves the same way every run` | — | Two functions of equal complexity resolve to the one sorting first under the presentation order, identically on every run. |
| 12 | <a id="HLR-032: two runs with a threshold are byte-identical"></a>`HLR-032: two runs with a threshold are byte-identical` | — | Repeating a run with a threshold produces identical bytes. |
| 13 | <a id="HLR-066: an empty run still renders the callouts and the listing"></a>`HLR-066: an empty run still renders the callouts and the listing` | — | A run that analysed nothing renders both new sections with no rows, rather than omitting them and changing the report's shape. |

### 3.16. [test/unit/format_csv.c](../test/unit/format_csv.c)

Role: **unit**. **11 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="an_ordinary_field_is_not_quoted"></a>`an_ordinary_field_is_not_quoted` | `LLR-FLD-01` | A field needing no escape is emitted bare, so the common case stays readable rather than uniformly quoted. |
| 2 | <a id="a_field_containing_a_comma_is_quoted"></a>`a_field_containing_a_comma_is_quoted` | `LLR-FLD-01` | A comma is what separates fields, so a value carrying one is quoted — the template-signature case, where one identifier looks like two fields. |
| 3 | <a id="a_quote_is_doubled_not_backslashed"></a>`a_quote_is_doubled_not_backslashed` | `LLR-FLD-01` | RFC 4180 escapes a quote by doubling it. A backslash escape parses without error and carries the wrong text, which is worse than failing. |
| 4 | <a id="a_field_containing_a_newline_is_quoted"></a>`a_field_containing_a_newline_is_quoted` | `LLR-FLD-01` | A line break inside a field ends the record unless the field is quoted. |
| 5 | <a id="a_field_containing_a_carriage_return_is_quoted"></a>`a_field_containing_a_carriage_return_is_quoted` | `LLR-FLD-01` | Records are CRLF-terminated, so a carriage return ends one as surely as a newline. |
| 6 | <a id="a_field_needing_every_escape_survives"></a>`a_field_needing_every_escape_survives` | `LLR-FLD-01` | A comma, a quote, and a newline in one field are handled together rather than one rule cancelling another. |
| 7 | <a id="an_empty_field_is_emitted_empty"></a>`an_empty_field_is_emitted_empty` | `LLR-FLD-01` | An empty value is an empty field. |
| 8 | <a id="a_null_field_is_emitted_empty"></a>`a_null_field_is_emitted_empty` | `LLR-FLD-01` | A missing value is an empty field rather than a fault, so a record with an absent language still has the right shape. |
| 9 | <a id="the_header_row_is_written"></a>`the_header_row_is_written` | `LLR-CSV-01`, `LLR-FLD-02` | The header names the columns each record carries, and passes through the same emission path as every record. |
| 10 | <a id="an_empty_report_is_a_header_alone"></a>`an_empty_report_is_a_header_alone` | `LLR-CSV-01` | A run with no functions produces the header and nothing else, rather than no output at all. |
| 11 | <a id="a_write_failure_is_reported"></a>`a_write_failure_is_reported` | `LLR-CSV-01` | A stream that cannot absorb the document yields a non-zero return, so a truncated file is never reported as success. |

### 3.17. [test/unit/format_xml.c](../test/unit/format_xml.c)

Role: **unit**. **17 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="an_ampersand_is_escaped"></a>`an_ampersand_is_escaped` | `LLR-ESC-01` | An unescaped ampersand begins an entity reference and makes the document unparseable. |
| 2 | <a id="angle_brackets_are_escaped"></a>`angle_brackets_are_escaped` | `LLR-ESC-01` | An angle bracket would open or close a tag — the template-signature case, which carries both. |
| 3 | <a id="quotation_marks_are_escaped"></a>`quotation_marks_are_escaped` | `LLR-ESC-01` | A quotation mark inside an attribute value ends the attribute early, leaving the document unparseable from that point. |
| 4 | <a id="an_ampersand_in_an_entity_is_escaped_once"></a>`an_ampersand_in_an_entity_is_escaped_once` | `LLR-ESC-01` | Escaping is not recursive: text that already looks like an entity is encoded once, not doubled. |
| 5 | <a id="ordinary_text_is_unchanged"></a>`ordinary_text_is_unchanged` | `LLR-ESC-01` | A value needing no escape passes through untouched. |
| 6 | <a id="escaping_null_emits_nothing"></a>`escaping_null_emits_nothing` | `LLR-ESC-01` | A missing value emits nothing rather than faulting. |
| 7 | <a id="the_record_carries_its_format_version"></a>`the_record_carries_its_format_version` | `LLR-XWR-03`, `LLR-XWR-04` | The root element carries a format-version identifier, which is what lets a consumer decide whether it understands the structure before interpreting it. |
| 8 | <a id="an_empty_report_is_still_a_complete_record"></a>`an_empty_report_is_still_a_complete_record` | `LLR-XWR-01`, `LLR-XWR-04` | A run that analysed nothing still writes a complete, well-formed record rather than nothing. |
| 9 | <a id="a_write_failure_is_reported"></a>`a_write_failure_is_reported` | `LLR-XWR-04` | A truncated record is not well-formed and a consumer would reject it with no idea why, so a write failure is reported instead. |
| 10 | <a id="input_that_is_not_xml_is_rejected"></a>`input_that_is_not_xml_is_rejected` | `LLR-XRD-03` | A file that is not XML at all is rejected. |
| 11 | <a id="well_formed_but_foreign_input_is_rejected"></a>`well_formed_but_foreign_input_is_rejected` | `LLR-XRD-04`, `LLR-XRD-06` | A well-formed document of some other shape is rejected outright, with nothing reconstructed from it. |
| 12 | <a id="an_unsupported_format_version_is_rejected"></a>`an_unsupported_format_version_is_rejected` | `LLR-XRD-05` | A record of a version this build does not read is rejected rather than interpreted optimistically. |
| 13 | <a id="a_record_without_a_version_is_rejected"></a>`a_record_without_a_version_is_rejected` | `LLR-XRD-04` | A record with no version identifier is rejected: the identifier is what makes the structure knowable. |
| 14 | <a id="the_threshold_supplied_now_is_the_one_applied"></a>`the_threshold_supplied_now_is_the_one_applied` | `LLR-XRD-07` | One record read at two thresholds yields two different listings, so the threshold is the one supplied at conversion time and the record carries none. |
| 15 | <a id="the_model_is_reconstructed_from_the_record"></a>`the_model_is_reconstructed_from_the_record` | `LLR-XRD-01`, `LLR-XRD-09` | The model is rebuilt from the record alone, ordered by the same code that orders a live one, with the totals recomputed rather than read. |
| 16 | <a id="a_truncated_record_is_rejected"></a>`a_truncated_record_is_rejected` | `LLR-XRD-03`, `LLR-XRD-06` | A record whose root never closes is not well-formed and is rejected rather than read as far as it goes. |
| 17 | <a id="an_absent_record_is_rejected"></a>`an_absent_record_is_rejected` | `LLR-XRD-03` | A record that cannot be opened is a rejection rather than a crash. |

### 3.18. [test/integration/formats.bats](../test/integration/formats.bats)

Role: **integration**. **16 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-027: table is the format when none is selected"></a>`HLR-027: table is the format when none is selected` | — | The aligned table is what a run with no format option produces. |
| 2 | <a id="HLR-027: table may also be selected explicitly"></a>`HLR-027: table may also be selected explicitly` | — | Selecting the default explicitly produces the same bytes as not selecting it. |
| 3 | <a id="HLR-028: csv produces one record per function"></a>`HLR-028: csv produces one record per function` | — | CSV carries a header and a record naming each function. |
| 4 | <a id="HLR-029: md produces GitHub-Flavored Markdown"></a>`HLR-029: md produces GitHub-Flavored Markdown` | — | Markdown carries headings and pipe tables rather than the aligned form. |
| 5 | <a id="HLR-054: xml produces a record with a version"></a>`HLR-054: xml produces a record with a version` | — | XML carries a closed root element with a format-version identifier. |
| 6 | <a id="HLR-063: an unknown format is a usage error naming the choices"></a>`HLR-063: an unknown format is a usage error naming the choices` | — | An unrecognised format is rejected, and the diagnostic names what would have been accepted. |
| 7 | <a id="HLR-063: a format option without its argument is a usage error"></a>`HLR-063: a format option without its argument is a usage error` | — | An option requiring an argument and given none is rejected. |
| 8 | <a id="HLR-031: table and Markdown present the same tiers"></a>`HLR-031: table and Markdown present the same tiers` | — | The two formats emit the same headings in the same order, which is the observable form of their sharing one traversal. |
| 9 | <a id="HLR-031: a tier with no rows appears in both"></a>`HLR-031: a tier with no rows appears in both` | — | A tier that happens to be empty is still present in both formats, so the report's shape does not vary with its content. |
| 10 | <a id="the format changes the rendering and not the analysis"></a>`the format changes the rendering and not the analysis` | — | Every format names the same discovered function, so the formats are views of one run rather than four separate reports. |
| 11 | <a id="HLR-028: csv is unfiltered by the threshold"></a>`HLR-028: csv is unfiltered by the threshold` | — | Two very different thresholds produce identical CSV: the threshold governs a listing tier CSV does not have. |
| 12 | <a id="HLR-054: xml is unfiltered by the threshold"></a>`HLR-054: xml is unfiltered by the threshold` | — | The record stores what was measured, so the threshold does not change it. |
| 13 | <a id="HLR-038: every format writes results to stdout alone"></a>`HLR-038: every format writes results to stdout alone` | — | Each format leaves the diagnostic stream carrying only diagnostics. |
| 14 | <a id="HLR-030: every format honours --output"></a>`HLR-030: every format honours --output` | — | Redirection applies to every format, not only the default one. |
| 15 | <a id="HLR-032: every format is byte-identical across runs"></a>`HLR-032: every format is byte-identical across runs` | — | Repeating a run in any format produces identical bytes. |
| 16 | <a id="HLR-066: every format renders an empty run"></a>`HLR-066: every format renders an empty run` | — | A run that analysed nothing still produces output in every format. |

### 3.19. [test/fixtures/eloc.bats](../test/fixtures/eloc.bats)

Role: **fixture**. **20 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the hand-counted category totals match"></a>`the hand-counted category totals match` | — | The physical line count, the file's ELOC, and the function count match the values counted line by line in the fixture's header. |
| 2 | <a id="HLR-015: the function's ELOC excludes the file-scope statement"></a>`HLR-015: the function's ELOC excludes the file-scope statement` | — | The function reports eighteen of the file's nineteen statement lines; the nineteenth is an initialised global outside it. |
| 3 | <a id="HLR-019: the file's ELOC includes code outside any function"></a>`HLR-019: the file's ELOC includes code outside any function` | — | The file's ELOC accounts for every qualifying line, including the one that belongs to no function. |
| 4 | <a id="HLR-049 – HLR-052: blanks, braces, bare declarations and directives are excluded"></a>`HLR-049 – HLR-052: blanks, braces, bare declarations and directives are excluded` | — | The fixture holds two directives, three bare declarations, five lone braces and six blank lines; counting any of them would exceed the hand count. |
| 5 | <a id="HLR-017: the hand-counted complexity matches"></a>`HLR-017: the hand-counted complexity matches` | — | The function's complexity matches the value counted by hand in the fixture's header: one plus five decision points. |
| 6 | <a id="HLR-017: a straight-line function is one"></a>`HLR-017: a straight-line function is one` | — | A function with no branch reports one, not two — the base is added once, and the query does not capture the function itself. |
| 7 | <a id="HLR-017: a short-circuit operator is a decision point"></a>`HLR-017: a short-circuit operator is a decision point` | — | A `&&` inside a condition adds a decision, so a function built from one compound condition does not score as though it had none. |
| 8 | <a id="HLR-017: a default label and a goto are not decisions"></a>`HLR-017: a default label and a goto are not decisions` | — | A `default` label adds no path that was not already counted and a `goto` chooses nothing, so neither raises the complexity. |
| 9 | <a id="HLR-044: an assignment or operation counts"></a>`HLR-044: an assignment or operation counts` | — | An initialising declaration, a plain assignment, and a compound operation each contribute a line. |
| 10 | <a id="HLR-046: a call counts whether or not its result is used"></a>`HLR-046: a call counts whether or not its result is used` | — | A call whose result is discarded counts as surely as one whose result is used; the prototypes beside them declare and do nothing. |
| 11 | <a id="HLR-047: a return counts, with or without a value"></a>`HLR-047: a return counts, with or without a value` | — | A bare return and a return carrying a value each contribute a line. |
| 12 | <a id="HLR-045: else-if on one line counts once"></a>`HLR-045: else-if on one line counts once` | — | A line that is both an else and an if contributes one line, since ELOC counts lines rather than captures. |
| 13 | <a id="HLR-020: a file with nothing executable reports zero, without error"></a>`HLR-020: a file with nothing executable reports zero, without error` | — | A file of comments, directives, and declarations reports zero ELOC and succeeds. |
| 14 | <a id="HLR-011: C++ matches its hand-counted categories"></a>`HLR-011: C++ matches its hand-counted categories` | — | C++'s ELOC and complexity match the values counted line by line in the group's header, over a fixture holding one instance of every category the language has. |
| 15 | <a id="HLR-011: Python matches its hand-counted categories"></a>`HLR-011: Python matches its hand-counted categories` | — | Python's figures match its hand count, including the decisions the query file records: `pass` and `import` excluded, the module docstring counted as the expression statement it is. |
| 16 | <a id="HLR-011: Rust matches its hand-counted categories"></a>`HLR-011: Rust matches its hand-counted categories` | — | Rust's figures match its hand count, including a counted `static` against an excluded `const`, and a tail expression counted where the language has no `return`. |
| 17 | <a id="HLR-011: Ada matches its hand-counted categories"></a>`HLR-011: Ada matches its hand-counted categories` | — | Ada's figures match its hand count, including short-circuit `and then` counted as a decision where plain `and` is not. |
| 18 | <a id="HLR-048: C++ counts exception handling toward ELOC"></a>`HLR-048: C++ counts exception handling toward ELOC` | — | The one ELOC category C cannot express. `try`, `catch`, and `throw` each contribute a line, which no earlier phase could verify in any shipped language. |
| 19 | <a id="HLR-048: a catch clause is a decision point"></a>`HLR-048: a catch clause is a decision point` | — | A handler is a path out of the guarded block and raises complexity; `try` and `throw` choose nothing and do not. |
| 20 | <a id="HLR-011: a language with no exception construct still reports"></a>`HLR-011: a language with no exception construct still reports` | — | C has no exception handling and analysing it is not thereby an error — no language is required to have every category. |

### 3.20. [test/fixtures/comments.bats](../test/fixtures/comments.bats)

Role: **fixture**. **7 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the hand-counted comment totals match"></a>`the hand-counted comment totals match` | — | The physical line count, the file's ELOC, and the function count match the fixture's hand-counted header. |
| 2 | <a id="HLR-013: a block-comment opener inside a string opens nothing"></a>`HLR-013: a block-comment opener inside a string opens nothing` | — | A string containing a block-comment opener is a string. A textual matcher would open a comment there and swallow the rest of the file. |
| 3 | <a id="HLR-013: a line-comment opener inside a string discards nothing"></a>`HLR-013: a line-comment opener inside a string discards nothing` | — | A string containing a line-comment opener is a string. A textual matcher would discard the rest of that line, losing a statement. |
| 4 | <a id="HLR-013: a quote inside a comment opens no string"></a>`HLR-013: a quote inside a comment opens no string` | — | An unbalanced quote inside a comment opens nothing. A matcher tracking strings textually would mis-read everything after it — the case no regular expression survives, and the reason HLR-013's verification is bound to this group. |
| 5 | <a id="HLR-016: a comment sharing a line with code does not remove that line"></a>`HLR-016: a comment sharing a line with code does not remove that line` | — | A statement carrying a trailing comment still counts. Excluding by line rather than by byte deletes it, which an earlier implementation did. |
| 6 | <a id="HLR-016: inline syntax inside a block comment excludes no line twice"></a>`HLR-016: inline syntax inside a block comment excludes no line twice` | — | Comment-like openers inside a block comment do not cause any line to be excluded more than once. |
| 7 | <a id="HLR-020: a file of only comments reports zero ELOC"></a>`HLR-020: a file of only comments reports zero ELOC` | — | A file with nothing but comments reports zero, without error. |

### 3.21. [test/fixtures/nesting.bats](../test/fixtures/nesting.bats)

Role: **fixture**. **17 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the hand-counted nesting totals match"></a>`the hand-counted nesting totals match` | — | The physical line count, the file's ELOC, and the function count match the fixture's hand-counted header. |
| 2 | <a id="HLR-068: the innermost function owns its own statements"></a>`HLR-068: the innermost function owns its own statements` | — | Each nested function reports the statements written inside it. |
| 3 | <a id="HLR-068: the innermost function owns its own decision points"></a>`HLR-068: the innermost function owns its own decision points` | — | Each nested function reports the branches written inside it; running the query against each body without attribution would give an enclosing function everything its nested functions branch on. |
| 4 | <a id="HLR-017: complexity is one plus the decision points"></a>`HLR-017: complexity is one plus the decision points` | — | The outermost function scores one plus its two decision points — the `if` and the `&&` short-circuiting inside its condition. |
| 5 | <a id="HLR-068: an enclosing function gains none of the nested one's lines"></a>`HLR-068: an enclosing function gains none of the nested one's lines` | — | The outermost function reports its own three statements and none of the five in the file. Attributing to the outermost enclosing function would report five; attributing to every enclosing one would report five, two, and one. |
| 6 | <a id="HLR-067: all three functions are reported in their own right"></a>`HLR-067: all three functions are reported in their own right` | — | Three levels of nesting yield three reported functions, not one. |
| 7 | <a id="HLR-019: the file counts each statement line once"></a>`HLR-019: the file counts each statement line once` | — | The file's ELOC is the number of distinct lines carrying a statement, however those statements are attributed. |
| 8 | <a id="HLR-032: nested attribution is deterministic across runs"></a>`HLR-032: nested attribution is deterministic across runs` | — | Repeating the run produces identical bytes, so the attribution does not depend on the order the query matched. |
| 9 | <a id="HLR-067: Ada reports a nested subprogram in its own right"></a>`HLR-067: Ada reports a nested subprogram in its own right` | — | Three subprograms nested three deep are three reported functions, which is the shape HLR-067 was written for. |
| 10 | <a id="HLR-068: an Ada subprogram gains none of its nested ones' work"></a>`HLR-068: an Ada subprogram gains none of its nested ones' work` | — | Each of the three owns exactly its own ELOC and complexity; attributing to the outermost would give the outer one everything and the others nothing. |
| 11 | <a id="HLR-018: a Rust closure is not reported as a function"></a>`HLR-018: a Rust closure is not reported as a function` | — | A closure has no name to report, so the report names the two `fn` items and not the closure beside them. |
| 12 | <a id="HLR-018: a Rust closure's decision point lands on the enclosing function"></a>`HLR-018: a Rust closure's decision point lands on the enclosing function` | — | The enclosing function's complexity includes the closure's branch. Without the rule that branch would belong to nothing at all, since an unreported callable is not a function for it to land on — it would vanish from the report. |
| 13 | <a id="HLR-018: a Python lambda is not reported as a function"></a>`HLR-018: a Python lambda is not reported as a function` | — | A lambda is unnamed and so is absent from the reported function set. |
| 14 | <a id="HLR-018: a Python lambda's conditional lands on the enclosing function"></a>`HLR-018: a Python lambda's conditional lands on the enclosing function` | — | The conditional expression inside a lambda raises the enclosing function's complexity. |
| 15 | <a id="HLR-018: a C++ lambda is not reported as a function"></a>`HLR-018: a C++ lambda is not reported as a function` | — | A lambda is unnamed and so is absent from the reported function set. |
| 16 | <a id="HLR-018: a C++ lambda's conditional lands on the enclosing function"></a>`HLR-018: a C++ lambda's conditional lands on the enclosing function` | — | The conditional expression inside a lambda raises the enclosing function's complexity. |
| 17 | <a id="HLR-067: a nested named function is reported where a lambda is not"></a>`HLR-067: a nested named function is reported where a lambda is not` | — | The distinction the two requirements draw, in one language and one function body: Rust's nested `fn` is reported and the closure beside it is not. |

### 3.22. [test/fixtures/escaping.bats](../test/fixtures/escaping.bats)

Role: **fixture**. **13 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-064: a path containing a comma does not split a record"></a>`HLR-064: a path containing a comma does not split a record` | — | Every row of the document parses to the same field count under an independent CSV reader, which a split field would break. The hostile value is a path rather than an identifier, because C cannot yet produce an identifier carrying a comma. |
| 2 | <a id="HLR-064: the path survives the round trip intact"></a>`HLR-064: the path survives the round trip intact` | — | The value read back out of the document is the value that went in, comma and quotes included. |
| 3 | <a id="HLR-064: a hostile path changes the field count of nothing"></a>`HLR-064: a hostile path changes the field count of nothing` | — | A record over a hostile path has the same number of fields as one over an ordinary path, so quoting preserved the structure rather than merely surviving it. |
| 4 | <a id="HLR-065: XML over a hostile path is well-formed"></a>`HLR-065: XML over a hostile path is well-formed` | — | An independent parser accepts the document, so the escaping is correct rather than merely present. |
| 5 | <a id="HLR-065: the structural characters are escaped, not emitted raw"></a>`HLR-065: the structural characters are escaped, not emitted raw` | — | The angle brackets, ampersand, and quotation marks appear as entities in the document. |
| 6 | <a id="HLR-065: the path arrives intact after unescaping"></a>`HLR-065: the path arrives intact after unescaping` | — | An independent parser recovers the original value, so escaping and the document's meaning agree. |
| 7 | <a id="HLR-056: a hostile path survives a record round trip"></a>`HLR-056: a hostile path survives a record round trip` | — | Regeneration over a hostile path matches a direct run. An asymmetry — escaped on the way out and not unescaped on the way in — passes a well-formedness check and still corrupts the report. |
| 8 | <a id="HLR-027: the table renders a hostile path unchanged"></a>`HLR-027: the table renders a hostile path unchanged` | — | The aligned table escapes nothing and must not: it is read by a person, and a path is what it is. |
| 9 | <a id="HLR-014: a template specialisation is reported under its full name"></a>`HLR-014: a template specialisation is reported under its full name` | — | An explicit specialisation names itself with its template arguments, so the reported name carries a comma and two angle brackets — an identifier the analyser produces rather than one the suite constructs. |
| 10 | <a id="HLR-064: an identifier containing a comma stays one CSV field"></a>`HLR-064: an identifier containing a comma stays one CSV field` | — | Every row parses to the same field count under an independent reader, and the name reads back whole. This is the value HLR-064 was written for, reachable only once C++ shipped. |
| 11 | <a id="HLR-065: an identifier containing angle brackets is escaped"></a>`HLR-065: an identifier containing angle brackets is escaped` | — | The angle brackets appear as entities, never raw, so no identifier can open or close a tag. |
| 12 | <a id="HLR-065: XML carrying such an identifier is well-formed"></a>`HLR-065: XML carrying such an identifier is well-formed` | — | An independent parser accepts the document, so the escaping is correct rather than merely present. |
| 13 | <a id="HLR-056: such an identifier survives a record round trip"></a>`HLR-056: such an identifier survives a record round trip` | — | Regeneration matches a direct run, so escaping on the way out and reading on the way in agree about the identifier. |

### 3.23. [test/fixtures/regeneration.bats](../test/fixtures/regeneration.bats)

Role: **fixture**. **16 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the hand-counted subject totals match"></a>`the hand-counted subject totals match` | — | The subject's totals match the values in the fixture's header, so a later failure is about the round trip rather than about the subject having changed. |
| 2 | <a id="HLR-056: regenerated Markdown is byte-identical to a direct run"></a>`HLR-056: regenerated Markdown is byte-identical to a direct run` | — | The report rebuilt from a record is identical to the one a direct analysis produces at the same threshold — not similar, identical, so a diff of the two is empty. |
| 3 | <a id="HLR-057: the threshold supplied now is the one applied"></a>`HLR-057: the threshold supplied now is the one applied` | — | Regeneration at a given threshold matches a direct run at that threshold, so the threshold reaches the rebuilt report. |
| 4 | <a id="HLR-057: the same record at two thresholds gives two listings"></a>`HLR-057: the same record at two thresholds gives two listings` | — | Two thresholds over one record produce different reports. Without this, the test above would pass for a build that ignored the threshold entirely. |
| 5 | <a id="HLR-055: regeneration reads no source file"></a>`HLR-055: regeneration reads no source file` | — | A report is produced from the record with the working directory elsewhere and the source unreachable. |
| 6 | <a id="HLR-054: the record carries every tier the report presents"></a>`HLR-054: the record carries every tier the report presents` | — | Every tier the report has today appears in the record — the totals, the languages, the files and their functions, and the skipped list. A tier missing here regenerates as a report that looks complete and is not. |
| 7 | <a id="HLR-061: the record carries a format-version identifier"></a>`HLR-061: the record carries a format-version identifier` | — | The record identifies the structure it uses, exactly once. |
| 8 | <a id="HLR-058: input that is not XML is rejected with no output"></a>`HLR-058: input that is not XML is rejected with no output` | — | A file that is not XML is rejected with the fatal status and nothing on the results stream. |
| 9 | <a id="HLR-058: a well-formed document of another shape is rejected"></a>`HLR-058: a well-formed document of another shape is rejected` | — | Being parseable is not being an elc record; no best-effort partial conversion is attempted. |
| 10 | <a id="HLR-058: an unsupported format version is rejected, naming it"></a>`HLR-058: an unsupported format version is rejected, naming it` | — | A bumped version is rejected, and the diagnostic names both the version found and the version this build reads. |
| 11 | <a id="HLR-058: a truncated record is rejected"></a>`HLR-058: a truncated record is rejected` | — | A record whose root never closes is rejected rather than read as far as it goes. |
| 12 | <a id="HLR-058: an absent record is rejected"></a>`HLR-058: an absent record is rejected` | — | A record that cannot be opened is a rejection rather than a crash. |
| 13 | <a id="HLR-055: regeneration defaults to Markdown without being asked"></a>`HLR-055: regeneration defaults to Markdown without being asked` | — | The mode's output format is its default, so it is usable without a redundant option. |
| 14 | <a id="HLR-063: a format other than Markdown is rejected in regeneration mode"></a>`HLR-063: a format other than Markdown is rejected in regeneration mode` | — | A saved record carries the findings of a run rather than the graph, so another format is a usage error rather than a silently ignored request. |
| 15 | <a id="HLR-063: an explicit Markdown selection is accepted"></a>`HLR-063: an explicit Markdown selection is accepted` | — | Saying the default out loud is not an error; only asking for something else is. |
| 16 | <a id="HLR-063: a target alongside --from-xml is a usage error"></a>`HLR-063: a target alongside --from-xml is a usage error` | — | The record is the input; a target would name a second source of truth for one report. |

### 3.24. [test/fixtures/runtime.bats](../test/fixtures/runtime.bats)

Role: **fixture**. **15 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="an intact runtime analyses the subject"></a>`an intact runtime analyses the subject` | — | The control the rest of the group is read against: an unbroken runtime directory analyses the subject and reports its function. |
| 2 | <a id="HLR-036: an absent runtime directory is fatal before any file is read"></a>`HLR-036: an absent runtime directory is fatal before any file is read` | — | A missing runtime location ends the run with no report, since no analysis is possible in that state. |
| 3 | <a id="HLR-036: a runtime location that is a file is fatal"></a>`HLR-036: a runtime location that is a file is fatal` | — | The same unusable state reached differently is treated the same way. |
| 4 | <a id="HLR-036: an absent extension map is fatal"></a>`HLR-036: an absent extension map is fatal` | — | Without the extension map nothing can be routed to a language, which is the fatal state. |
| 5 | <a id="HLR-036: an extension map naming no language is fatal"></a>`HLR-036: an extension map naming no language is fatal` | — | A map present but naming nothing is as unusable as one that is missing. |
| 6 | <a id="HLR-036: a fatal runtime failure is diagnosed on stderr"></a>`HLR-036: a fatal runtime failure is diagnosed on stderr` | — | The fatal diagnostic names the location and reaches the diagnostic stream. |
| 7 | <a id="HLR-070: an absent grammar degrades that language only"></a>`HLR-070: an absent grammar degrades that language only` | — | A language whose grammar is missing is excluded, the run completes, the status stays 0, and the affected file is listed as skipped. |
| 8 | <a id="HLR-070: a grammar exposing no entry point degrades that language only"></a>`HLR-070: a grammar exposing no entry point degrades that language only` | — | A shared object that loads but exposes no grammar entry point is unusable rather than fatal. |
| 9 | <a id="HLR-070: a grammar that is not a shared object degrades that language only"></a>`HLR-070: a grammar that is not a shared object degrades that language only` | — | A parser file that cannot be loaded at all is unusable rather than fatal. |
| 10 | <a id="HLR-121: a module missing a required query file is unusable, not undefined"></a>`HLR-121: a module missing a required query file is unusable, not undefined` | — | A module omitting one of the six required query files is handled under HLR-070 rather than producing undefined behaviour. |
| 11 | <a id="HLR-070: an unparseable query degrades that language only"></a>`HLR-070: an unparseable query degrades that language only` | — | A query file that will not compile excludes its language and leaves the run otherwise intact. |
| 12 | <a id="HLR-070: the diagnostic names the language and the reason"></a>`HLR-070: the diagnostic names the language and the reason` | — | The diagnostic identifies the language, the query file, and the reason in words rather than as a numeric code. |
| 13 | <a id="HLR-070: an unusable module is reported once, not once per file"></a>`HLR-070: an unusable module is reported once, not once per file` | — | The load failure is diagnosed once for the language rather than once for every file that would have used it. |
| 14 | <a id="HLR-059: the environment variable takes precedence over the adjacent runtime"></a>`HLR-059: the environment variable takes precedence over the adjacent runtime` | — | Pointing the environment variable at a broken runtime degrades the run even though a working one sits beside the executable, which it can only do if the variable was preferred. |
| 15 | <a id="HLR-059: the runtime adjacent to the executable is used when the variable is unset"></a>`HLR-059: the runtime adjacent to the executable is used when the variable is unset` | — | With the variable unset, the runtime beside the executable is found and used. |

### 3.25. [test/fixtures/traversal.bats](../test/fixtures/traversal.bats)

Role: **fixture**. **11 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the walk yields exactly the three source files"></a>`the walk yields exactly the three source files` | — | The analysed file set for the hand-counted traversal tree is exactly the three files its header states, and nothing else. |
| 2 | <a id="the hand-counted traversal totals match"></a>`the hand-counted traversal totals match` | — | The file count and physical line total match the values counted by hand in the fixture's header. |
| 3 | <a id="HLR-005: a binary extension is excluded"></a>`HLR-005: a binary extension is excluded` | — | A file whose extension appears in the runtime exclusion list is absent from the walk's result. |
| 4 | <a id="HLR-005: a hidden directory is excluded"></a>`HLR-005: a hidden directory is excluded` | — | The contents of a hidden directory are absent from the walk's result. |
| 5 | <a id="HLR-005: a hidden file is excluded"></a>`HLR-005: a hidden file is excluded` | — | A hidden file in the target is absent from the walk's result, which is what makes a configuration-like file planted there unable to change the output. |
| 6 | <a id="HLR-069: a symbolic link is not followed during the walk"></a>`HLR-069: a symbolic link is not followed during the walk` | — | A link to a file already inside the tree is not followed, so the file it names is not contributed a second time. |
| 7 | <a id="HLR-069: a cyclic directory link does not hang the walk"></a>`HLR-069: a cyclic directory link does not hang the walk` | — | A self-referential directory link is not descended into and the walk terminates with the expected file set. |
| 8 | <a id="HLR-069: a symbolic link named as a target is resolved"></a>`HLR-069: a symbolic link named as a target is resolved` | — | A link named directly as a target is resolved and its referent analysed, the other half of the symbolic-link policy. |
| 9 | <a id="HLR-072: naming a file and its directory analyses it once"></a>`HLR-072: naming a file and its directory analyses it once` | — | Overlapping targets over the fixture tree yield the same file set as the directory alone. |
| 10 | <a id="HLR-071: several targets combine into one report"></a>`HLR-071: several targets combine into one report` | — | Two targets produce a single report spanning both. |
| 11 | <a id="HLR-043: the fixture tree is unchanged by a run"></a>`HLR-043: the fixture tree is unchanged by a run` | — | Every file in the fixture tree checksums identically before and after a run. |

### 3.26. [test/fixtures/deadcode.bats](../test/fixtures/deadcode.bats)

Role: **fixture**. **22 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-137: statements after a return are reported"></a>`HLR-137: statements after a return are reported` | `LLR-DED-01` | The statements following a return in the same block are reported, with their line ranges. |
| 2 | <a id="HLR-138: a label following a return is NOT reported"></a>`HLR-138: a label following a return is NOT reported` | `LLR-DED-01` | A labeled_statement is a sibling of the return and is reachable. Reporting it would invite deleting a live goto target, and the count is taken against the extracted rows rather than the whole output so the assertion cannot pass vacuously. |
| 3 | <a id="HLR-137: statements after a break are reported"></a>`HLR-137: statements after a break are reported` | `LLR-DED-01` | A break terminates its block for the sibling walk exactly as a return does. |
| 4 | <a id="HLR-137: statements after a continue are reported"></a>`HLR-137: statements after a continue are reported` | `LLR-DED-01` | A continue terminates its block for the sibling walk exactly as a return does. |
| 5 | <a id="LLR-DED-01: the walk does not leave the terminator's own block"></a>`LLR-DED-01: the walk does not leave the terminator's own block` | `LLR-DED-01` | A statement after the loop containing the break is live: the walk visits siblings within one parent and does not climb. |
| 6 | <a id="HLR-138: a switch arm does not leak into the next"></a>`HLR-138: a switch arm does not leak into the next` | `LLR-DED-01` | In this grammar a case construct contains the statements of its arm, so a return in one arm has no sibling to leak into and the following case is not reported. |
| 7 | <a id="LLR-DED-01: a statement following two terminators is reported once"></a>`LLR-DED-01: a statement following two terminators is reported once` | `LLR-DED-09` | A statement the walk reaches from two terminators appears once, so the report does not depend on how many ways it was found. |
| 8 | <a id="HLR-137: the consequence of if (0) and the else of if (1) are reported"></a>`HLR-137: the consequence of if (0) and the else of if (1) are reported` | `LLR-DED-02` | Both literal-condition shapes are found, each as its whole span. |
| 9 | <a id="HLR-137: the body of a literally false loop is reported"></a>`HLR-137: the body of a literally false loop is reported` | `LLR-DED-02` | A loop whose condition is written as a false literal has an unreachable body. |
| 10 | <a id="HLR-138: a do-while(0) body is NOT reported"></a>`HLR-138: a do-while(0) body is NOT reported` | `LLR-DED-02` | A do-while body runs exactly once, and it is one of the commonest idioms in C. Reporting it would be a false claim against ordinary code. |
| 11 | <a id="HLR-138: a branch guarded by a variable is NOT reported"></a>`HLR-138: a branch guarded by a variable is NOT reported` | `LLR-DED-03` | Neither a plain variable nor a const one holding zero makes its branch dead: deciding either needs data flow, and elc performs none. |
| 12 | <a id="LLR-DED-03: a zero the source did not write as one is undecided"></a>`LLR-DED-03: a zero the source did not write as one is undecided` | `LLR-DED-03` | Hexadecimal and octal zeroes fall through as undecided rather than being judged, while the decimal zero beside them is reported — the contrast being what shows the query is narrow rather than merely quiet. |
| 13 | <a id="HLR-137: the two causes are distinguished"></a>`HLR-137: the two causes are distinguished` | `LLR-DED-01` | A statement after a terminator and a branch under a literal condition are reported with different causes, because the reader's next action differs. |
| 14 | <a id="HLR-016: a comment is not dead code"></a>`HLR-016: a comment is not dead code` | `LLR-DED-07` | A comment is a named sibling, so without the merged-comment exclusion the walk would report the trailing note on the terminator's own line. The exact row count pins it. |
| 15 | <a id="HLR-139: a language supplying a dead-code query reports every language analysed"></a>`HLR-139: a language supplying a dead-code query reports every language analysed` | `LLR-DED-05` | The section heading states that nothing was left unanalysed, so an empty table means none found. |
| 16 | <a id="HLR-139: a language with no dead-code query is reported not analysed"></a>`HLR-139: a language with no dead-code query is reported not analysed` | `LLR-DED-05` | Ada ships without one on purpose, and the heading names it rather than leaving a clean-looking table. |
| 17 | <a id="HLR-139: removing a query file makes that language unanalysed, not clean"></a>`HLR-139: removing a query file makes that language unanalysed, not clean` | `LLR-RFP-11` | The same distinction against a language that does ship one: with the file removed the findings vanish and the heading says the analysis was not performed. |
| 18 | <a id="HLR-070: a module missing an optional query is still usable"></a>`HLR-070: a module missing an optional query is still usable` | `LLR-RFP-11` | Every other measurement survives the absence of an optional query file, which is the whole point of the file being optional. |
| 19 | <a id="HLR-070: an optional query that will not compile is a defect, not a choice"></a>`HLR-070: an optional query that will not compile is a defect, not a choice` | `LLR-RFP-12` | An optional file that is present and will not compile is diagnosed like a required one. Omitting a file is a decision the contract allows; writing a broken one is not. |
| 20 | <a id="HLR-137: dead statements inside an unreachable function are still reported"></a>`HLR-137: dead statements inside an unreachable function are still reported` | `LLR-DED-06` | Neither dead-code analysis suppresses the other: the function is reported unreachable by traversal and its dead statement is reported by syntax in the same run. |
| 21 | <a id="HLR-139: dead code is found in every language supplying a query"></a>`HLR-139: dead code is found in every language supplying a query` | `LLR-DED-01` | Python, Rust and C++ each find the statement after a terminator, so the mechanism is not C's alone. |
| 22 | <a id="HLR-138: no language claims a branch guarded by a variable"></a>`HLR-138: no language claims a branch guarded by a variable` | `LLR-DED-03` | The restraint holds in Python and Rust as it does in C: no language module is permitted to infer a value. |

### 3.27. [test/fixtures/reachability.bats](../test/fixtures/reachability.bats)

Role: **fixture**. **24 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-097: a clique of unused functions calling one another is reported"></a>`HLR-097: a clique of unused functions calling one another is reported` | `LLR-RCH-03` | The case textual linters get wrong. Each of the pair has a caller, so a no-caller rule finds neither; traversal reaches neither from any root. |
| 2 | <a id="HLR-096: a function reachable only through an address-taken pointer is NOT reported"></a>`HLR-096: a function reachable only through an address-taken pointer is NOT reported` | `LLR-RTS-02` | A function appearing only as an initialiser of a vector table is a root, and reporting it dead would tell a user to delete an interrupt handler. |
| 3 | <a id="LLR-RTS-02: an address-taken function is a root, not merely an exception"></a>`LLR-RTS-02: an address-taken function is a root, not merely an exception` | `LLR-RTS-02` | A function called only from an address-taken one is reachable through it. An implementation that excluded address-taken functions from the report without traversing from them would report it dead. |
| 4 | <a id="HLR-096: functions reached from the declared entry point are not reported"></a>`HLR-096: functions reached from the declared entry point are not reported` | `LLR-RCH-01` | The declared root and what it calls are absent from the unreachable list. |
| 5 | <a id="HLR-115: with no entry points declared, nothing is reported unreachable"></a>`HLR-115: with no entry points declared, nothing is reported unreachable` | `LLR-STA-01` | The list is empty and the heading states the omission and its reason, rather than reporting the whole program dead. |
| 6 | <a id="HLR-115: a declared entry point matching nothing says so differently"></a>`HLR-115: a declared entry point matching nothing says so differently` | `LLR-STA-01` | The two absences carry different headings, because they call for different actions from the reader. |
| 7 | <a id="LLR-CTR-09: omitting reachability does not omit its neighbours"></a>`LLR-CTR-09: omitting reachability does not omit its neighbours` | `LLR-STA-04` | A run with no entry points still reports its global-access map. |
| 8 | <a id="HLR-096: a global touched only by unreachable functions is unreachable"></a>`HLR-096: a global touched only by unreachable functions is unreachable` | `LLR-UGL-01` | The storage goes the way of the code that touches it. |
| 9 | <a id="LLR-UGL-01: a global a reachable function touches is not condemned"></a>`LLR-UGL-01: a global a reachable function touches is not condemned` | `LLR-UGL-01` | One live accessor is enough to keep an object out of the unreachable list. |
| 10 | <a id="HLR-115: with no entry points, no global is reported unreachable either"></a>`HLR-115: with no entry points, no global is reported unreachable either` | `LLR-STA-01` | The data half of the analysis is omitted with the function half, rather than condemning every object. |
| 11 | <a id="HLR-091: every global reports its writers and its readers"></a>`HLR-091: every global reports its writers and its readers` | `LLR-GLB-01` | Both sets are present for every object, whether or not the object carries a finding. |
| 12 | <a id="HLR-092: a global touched by one function is flagged for scope reduction"></a>`HLR-092: a global touched by one function is flagged for scope reduction` | `LLR-GLB-02` | The finding appears with its published source, for the object no state edge exists for. |
| 13 | <a id="HLR-093: a global spanning disconnected regions is a hidden channel"></a>`HLR-093: a global spanning disconnected regions is a hidden channel` | `LLR-GLB-03` | The finding names the disconnected participants, grouped by region, because the grouping is the finding. |
| 14 | <a id="HLR-093: shared state within one call-connected region is not a channel"></a>`HLR-093: shared state within one call-connected region is not a channel` | `LLR-GLB-05` | An object shared across a call edge carries no finding at all, asserted on the row rather than on the output — which does carry both phrases for the other objects. |
| 15 | <a id="LLR-GLB-04: both findings carry their published source"></a>`LLR-GLB-04: both findings carry their published source` | `LLR-GLB-04` | Each of the two verdicts is attributed to MISRA C Rule 8.9 in the report. |
| 16 | <a id="HLR-094: a call crossing a declared scope boundary is reported"></a>`HLR-094: a call crossing a declared scope boundary is reported` | `LLR-ISO-01` | A call from one declared scope into another appears as a crossing naming both ends. |
| 17 | <a id="HLR-094: a shared global crossing a boundary is reported too"></a>`HLR-094: a shared global crossing a boundary is reported too` | `LLR-ISO-01` | A scope sharing a variable with another without calling it has not been isolated, and the crossing names the object. |
| 18 | <a id="HLR-094: exactly the two crossings are reported"></a>`HLR-094: exactly the two crossings are reported` | `LLR-ISO-01` | The count in the heading matches the rows, so neither kind is reported twice nor an intra-scope edge counted. |
| 19 | <a id="HLR-115: with no scopes declared the analysis is omitted with a reason"></a>`HLR-115: with no scopes declared the analysis is omitted with a reason` | `LLR-STA-02` | The heading states the omission rather than leaving an empty table to be read as isolation verified. |
| 20 | <a id="HLR-094: an edge within one scope is not a crossing"></a>`HLR-094: an edge within one scope is not a crossing` | `LLR-ISO-01` | The same calls and the same shared variable, with both files in one declared scope, produce nothing. |
| 21 | <a id="HLR-094: a file matching no declaration is outside the partition"></a>`HLR-094: a file matching no declaration is outside the partition` | `LLR-ISO-02` | A file no declaration names lies outside the partition rather than in a scope of its own. |
| 22 | <a id="HLR-063: a malformed scope declaration is a usage error"></a>`HLR-063: a malformed scope declaration is a usage error` | `LLR-SCP-02` | A declaration that cannot be parsed ends the run with the fatal status and a diagnostic, rather than becoming a scope matching nothing. |
| 23 | <a id="HLR-032: the state sections survive a record round trip byte-identically"></a>`HLR-032: the state sections survive a record round trip byte-identically` | `LLR-XWR-11` | A report regenerated from the saved record is byte-identical to the live one, with the reachability and global-state sections included. |
| 24 | <a id="HLR-032: a global-state finding survives the round trip"></a>`HLR-032: a global-state finding survives the round trip` | `LLR-XWR-12` | The verdict and its participants are carried in the record, and the citation is derived from the verdict on both paths rather than stored. |

### 3.28. [test/fixtures/calltree.bats](../test/fixtures/calltree.bats)

Role: **fixture**. **20 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-085: fan-out is exact at every band boundary"></a>`HLR-085: fan-out is exact at every band boundary` | — | Fan-out is correct for 2, 3, 7, 8, 10, 11, 15 and 16 distinct callees — the values Phase 12 will band, asserted individually so a failure names the boundary that moved. |
| 2 | <a id="HLR-085: a repeated call does not raise fan-out"></a>`HLR-085: a repeated call does not raise fan-out` | — | Every caller in the fixture invokes one helper twice; the figures are unchanged, which is what the whole boundary table rests on. |
| 3 | <a id="HLR-085: a function that calls nothing has fan-out zero"></a>`HLR-085: a function that calls nothing has fan-out zero` | — | A leaf reports zero rather than being omitted. |
| 4 | <a id="HLR-085: every function appears, not only the interesting ones"></a>`HLR-085: every function appears, not only the interesting ones` | — | All twenty-four functions are listed: this is a measurement table, not a finding list. |
| 5 | <a id="HLR-087: the hand-counted depth matches"></a>`HLR-087: the hand-counted depth matches` | — | The four-layer chain measures four. |
| 6 | <a id="HLR-088: the deepest chain is reported in full, in order"></a>`HLR-088: the deepest chain is reported in full, in order` | — | The ordered sequence from entry point to deepest leaf is reported, not merely its length. |
| 7 | <a id="HLR-088: the deepest branch is taken, not the first"></a>`HLR-088: the deepest branch is taken, not the first` | — | The entry point calls the shallow branch first; the reported chain is the deep one, so the traversal is a longest-path search and not a first-path one. |
| 8 | <a id="HLR-087: the depth is presented with the unresolved count"></a>`HLR-087: the depth is presented with the unresolved count` | — | The depth heading states the unresolved count, so the figure is read as the lower bound it is. |
| 9 | <a id="HLR-089: direct and mutual recursion are both reported"></a>`HLR-089: direct and mutual recursion are both reported` | — | One decomposition finds both kinds, and labels which is which. |
| 10 | <a id="HLR-089: the recursive functions are named"></a>`HLR-089: the recursive functions are named` | — | The members of each cycle are named, since MISRA C Rule 17.2 is about which functions are involved. |
| 11 | <a id="HLR-090: recursion yields no depth figure, and the run terminates"></a>`HLR-090: recursion yields no depth figure, and the run terminates` | — | A recursive graph reports unbounded depth with no chain, and the run completes — a longest-path search over it would not. |
| 12 | <a id="HLR-090: no finite depth is invented for a recursive graph"></a>`HLR-090: no finite depth is invented for a recursive graph` | — | No layer count appears anywhere in the report for a recursive graph. |
| 13 | <a id="HLR-115: with no entry points, depth is omitted with its reason"></a>`HLR-115: with no entry points, depth is omitted with its reason` | — | The run succeeds and the report states that no entry points were declared, rather than reporting an empty or zero result. |
| 14 | <a id="HLR-115: an omitted depth does not omit the other measurements"></a>`HLR-115: an omitted depth does not omit the other measurements` | — | Fan-out is still reported when the depth analysis is omitted; omitting one analysis does not silence its neighbours. |
| 15 | <a id="HLR-095: main is not inferred, however obvious it looks"></a>`HLR-095: main is not inferred, however obvious it looks` | — | A file containing an obvious entry point still omits the analysis, because entry points are declared and never guessed at. |
| 16 | <a id="HLR-115: an entry point matching nothing is a distinct omission"></a>`HLR-115: an entry point matching nothing is a distinct omission` | — | A declared symbol matching no analysed function produces its own omission message, distinct from having declared none. |
| 17 | <a id="an unmatched entry point is diagnosed on stderr"></a>`an unmatched entry point is diagnosed on stderr` | — | The unmatched symbol is named on standard error, and the run still succeeds. |
| 18 | <a id="HLR-032: two runs over the same tree agree"></a>`HLR-032: two runs over the same tree agree` | — | Two runs produce identical reports. |
| 19 | <a id="HLR-033: the targets may be given in either order"></a>`HLR-033: the targets may be given in either order` | — | The report does not depend on the order the targets were named in. |
| 20 | <a id="HLR-056: the measurements survive a record round trip"></a>`HLR-056: the measurements survive a record round trip` | — | A report regenerated from a record is byte-identical and still carries the depth, since none of the call-tree measurements can be recomputed without the source. |

### 3.29. [test/fixtures/graph.bats](../test/fixtures/graph.bats)

Role: **fixture**. **16 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-106: the exported graph matches the expected topology exactly"></a>`HLR-106: the exported graph matches the expected topology exactly` | — | The emitted GraphML is compared node for node and edge for edge against a file written from the source, so a change in what elc believes about the code appears as a diff rather than as a number nobody questions. |
| 2 | <a id="HLR-106: the export is off unless asked for"></a>`HLR-106: the export is off unless asked for` | — | No GraphML file appears without the option, whatever the output path. |
| 3 | <a id="HLR-106: no companion is written when the report goes to stdout"></a>`HLR-106: no companion is written when the report goes to stdout` | — | Requesting the export while the report goes to standard output writes no file, since there is no output path from which to derive its name. |
| 4 | <a id="LLR-GML-03: the companion is named from the report by substitution"></a>`LLR-GML-03: the companion is named from the report by substitution` | — | An output of analysis.md yields analysis.graphml and not analysis.md.graphml. |
| 5 | <a id="HLR-085: repeated calls are one edge carrying a call-site count"></a>`HLR-085: repeated calls are one edge carrying a call-site count` | — | Two calls from one function to one helper produce a single edge with a count of two, so fan-out is distinct callees. |
| 6 | <a id="HLR-074: a global links its writer to its reader across files"></a>`HLR-074: a global links its writer to its reader across files` | — | A writer in one file and a reader in another are joined by an edge naming the object, though neither names the other. |
| 7 | <a id="HLR-074: call edges and global edges are distinguishable"></a>`HLR-074: call edges and global edges are distinguishable` | — | The export carries the two kinds separately and never merged, so an analysis can tell coupling through state from coupling through calls. |
| 8 | <a id="HLR-096: a function whose address is taken is marked"></a>`HLR-096: a function whose address is taken is marked` | — | A function assigned to a pointer without being called is marked a reachability root, and the function that installs it is not. |
| 9 | <a id="HLR-077: an unresolvable call is counted, not fatal"></a>`HLR-077: an unresolvable call is counted, not fatal` | — | A library call and an ambiguous Ada construct are counted as unresolved and the run still succeeds. |
| 10 | <a id="HLR-077: the unresolved count reaches the report"></a>`HLR-077: the unresolved count reaches the report` | — | The project summary states the count, so a reader can judge the graph's completeness without reading the export. |
| 11 | <a id="HLR-077: no destination is invented for an unresolved call"></a>`HLR-077: no destination is invented for an unresolved call` | — | The edge count is exactly what the fixture header accounts for; a tool guessing at an unresolved target would have more. |
| 12 | <a id="the Ada array-index ambiguity is unresolved, not a spurious edge"></a>`the Ada array-index ambiguity is unresolved, not a spurious edge` | — | An Ada array index that the grammar cannot distinguish from a call resolves against no subprogram and lands in the unresolved count rather than becoming an edge — pinning behaviour that is better than doc/notes.md §2.2 predicted. |
| 13 | <a id="an Ada call resolves like any other"></a>`an Ada call resolves like any other` | — | An unambiguous Ada call still produces its edge, so the language's ambiguity costs precision on the ambiguous construct alone. |
| 14 | <a id="HLR-033: node identifiers follow sorted file order"></a>`HLR-033: node identifiers follow sorted file order` | — | The exported node order is the sorted-path order of the files, which is not the order a directory walk yields them in. |
| 15 | <a id="HLR-032: two runs over the same tree produce identical GraphML"></a>`HLR-032: two runs over the same tree produce identical GraphML` | — | The export is byte-identical across runs, so no container's internal enumeration reaches the output. |
| 16 | <a id="HLR-076: the graph is built without reopening a source file"></a>`HLR-076: the graph is built without reopening a source file` | — | Each source is opened exactly once under strace while the graph is built, so cross-file resolution uses the facts of the single parse rather than re-reading. |

### 3.30. [test/fixtures/repo.bats](../test/fixtures/repo.bats)

Role: **fixture**. **19 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-002: the repository route yields exactly the tracked source files"></a>`HLR-002: the repository route yields exactly the tracked source files` | — | The analysed set of a repository target is exactly the tracked, non-binary, non-hidden source files, matching the set counted by hand in the fixture's header. |
| 2 | <a id="the hand-counted repository totals match"></a>`the hand-counted repository totals match` | — | The file count and physical line total for the repository target match the values counted by hand in the fixture's header. |
| 3 | <a id="HLR-003: an untracked file is excluded"></a>`HLR-003: an untracked file is excluded` | — | A perfectly ordinary source file that the filesystem walk would find is absent from the result, its only disqualification being that it is not in the tree at `HEAD`. |
| 4 | <a id="HLR-003: a gitignored build directory is excluded"></a>`HLR-003: a gitignored build directory is excluded` | — | Generated output under an ignored directory is excluded, which is where a filesystem walk goes wrong by the largest margin. |
| 5 | <a id="HLR-003: .gitignore is never read to decide this"></a>`HLR-003: .gitignore is never read to decide this` | — | Deleting `.gitignore` changes nothing about the result, so the exclusion follows from what git tracks rather than from elc parsing the ignore rules — which it would then have to keep in agreement with git across nested files, negations, and `core.excludesFile`. |
| 6 | <a id="HLR-003: a tracked file with binary content is excluded despite its extension"></a>`HLR-003: a tracked file with binary content is excluded despite its extension` | — | A tracked file named `.c` whose content is binary is excluded, and the extension list is shown not to contain `.c`, so git's own content check is what excluded it. |
| 7 | <a id="HLR-005: the exclusions of the filesystem walk apply to this route too"></a>`HLR-005: the exclusions of the filesystem walk apply to this route too` | — | A tracked file with an excluded extension, and tracked hidden entries, are absent from the repository route's result, so the two routes agree about exclusions git is happy to report. |
| 8 | <a id="HLR-126: naming a subdirectory analyses that subdirectory and nothing above it"></a>`HLR-126: naming a subdirectory analyses that subdirectory and nothing above it` | — | A subdirectory target yields the files beneath it alone, though the tree at `HEAD` is the whole repository. |
| 9 | <a id="HLR-126: the scoped totals are the subdirectory's, not the repository's"></a>`HLR-126: the scoped totals are the subdirectory's, not the repository's` | — | The scoped file count and line total differ from the whole repository's, so a missing scope test fails here rather than inflating a figure nobody questions. |
| 10 | <a id="HLR-126: a directory denotes the same files by either route"></a>`HLR-126: a directory denotes the same files by either route` | — | The same directory enumerated from the repository and then, with the repository removed, traversed from the filesystem, yields the same file set — the property the scoping requirement exists to guarantee, asserted against both routes rather than inferred from one. |
| 11 | <a id="HLR-004: a directory in no repository is walked at the filesystem level"></a>`HLR-004: a directory in no repository is walked at the filesystem level` | — | A directory outside any repository reports the filesystem route. |
| 12 | <a id="HLR-002: an enclosing repository that does not track the target is disregarded"></a>`HLR-002: an enclosing repository that does not track the target is disregarded` | — | A gitignored build directory inside a work tree reports the filesystem route and yields its contents, rather than reporting an empty result indistinguishable from a directory with no source in it. |
| 13 | <a id="HLR-002: a directory tracked by no repository above it falls back"></a>`HLR-002: a directory tracked by no repository above it falls back` | — | A freshly created directory inside a work tree — the version-controlled home directory reduced to its essential shape — reports the filesystem route and is analysed. |
| 14 | <a id="HLR-002: a repository with no commits falls back rather than failing"></a>`HLR-002: a repository with no commits falls back rather than failing` | — | A repository whose `HEAD` resolves to nothing is treated as inapplicable rather than as an error, since a freshly initialised repository is an ordinary state. |
| 15 | <a id="HLR-127: the route is reported for each directory target"></a>`HLR-127: the route is reported for each directory target` | — | A run over a repository target and a plain directory reports a Discovery section naming each target with the route applied to it. |
| 16 | <a id="HLR-072: one directory named twice is one row, canonically spelled"></a>`HLR-072: one directory named twice is one row, canonically spelled` | — | Two spellings of one directory produce a single Discovery row naming its canonical path, so the section reports directories rather than arguments and agrees with a Files section that has already collapsed them. |
| 17 | <a id="HLR-033: the discovery section does not depend on the order of the targets"></a>`HLR-033: the discovery section does not depend on the order of the targets` | — | The same two targets in either order produce identical reports, so the Discovery section is ordered by a key rather than by the order the user typed. |
| 18 | <a id="HLR-006: a repository target produces the same report shape as any other"></a>`HLR-006: a repository target produces the same report shape as any other` | — | A repository target and a plain directory target produce the same section headings, completing a claim the man page has made since Phase 5 of which only the file and plain-directory halves were tested. |
| 19 | <a id="HLR-056: the record carries the route, so regeneration is the same report"></a>`HLR-056: the record carries the route, so regeneration is the same report` | — | A report regenerated from a record is byte-identical to a direct run, and exactly one line of it names the target with its route — so the routes survived the round trip rather than both reports being equally empty. |

### 3.31. [test/fixtures/determinism.bats](../test/fixtures/determinism.bats)

Role: **fixture**. **7 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the hand-counted determinism totals match"></a>`the hand-counted determinism totals match` | — | The file count and physical line total match the values counted by hand in the fixture's header. |
| 2 | <a id="HLR-033: files are presented in byte order, not creation order"></a>`HLR-033: files are presented in byte order, not creation order` | — | The reported paths are in ascending byte order, though the tree was created in a different one. |
| 3 | <a id="HLR-032: two runs over the same target are byte-identical"></a>`HLR-032: two runs over the same target are byte-identical` | — | Repeating an unmodified run over the fixture tree produces identical bytes. |
| 4 | <a id="HLR-033: two directory targets in either order agree"></a>`HLR-033: two directory targets in either order agree` | — | Two overlapping directory targets produce the same report whichever order they are named in. |
| 5 | <a id="HLR-033: a file target and a directory target in either order agree"></a>`HLR-033: a file target and a directory target in either order agree` | — | The independence of target order holds across the two classification routes, not only within one of them. |
| 6 | <a id="HLR-039: decoys in the working directory, the target, and an ancestor change nothing"></a>`HLR-039: decoys in the working directory, the target, and an ancestor change nothing` | — | Configuration-like files planted in all three locations produce output byte-identical to their absence. |
| 7 | <a id="HLR-039: a decoy does not change the file count either"></a>`HLR-039: a decoy does not change the file count either` | — | A decoy planted in the target does not appear in the report as a discovered file. |

### 3.32. [test/instrumented/environment.bats](../test/instrumented/environment.bats)

Role: **instrumented**. **21 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-040: the binary links no interpreter or virtual machine"></a>`HLR-040: the binary links no interpreter or virtual machine` | — | The link line is checked against an allowlist; a language runtime appearing there is what the requirement forbids. |
| 2 | <a id="HLR-040: elc makes no network syscall"></a>`HLR-040: elc makes no network syscall` | — | `strace -e trace=%network` over a full run logs no network syscall. This observes the syscalls directly rather than inferring the property from survival inside an isolated network namespace: it proves `elc` never *attempts* network access, and it runs in containers where unprivileged user namespaces are unavailable. |
| 3 | <a id="HLR-041: /proc reports a single thread while elc runs"></a>`HLR-041: /proc reports a single thread while elc runs` | — | The running process reports exactly one thread. Redirecting the report into a FIFO removes the race that made this observation impossible before: opening a FIFO for writing blocks until a reader arrives, so the sample is taken while elc is certainly alive. |
| 4 | <a id="HLR-041: elc issues no thread-creating syscall"></a>`HLR-041: elc issues no thread-creating syscall` | — | No clone or clone3 syscall is observed for the whole run, which is stronger than sampling a thread count because it holds for every instant rather than the one sampled. |
| 5 | <a id="HLR-041: elc links no threading library"></a>`HLR-041: elc links no threading library` | — | No threading library appears on the link line. |
| 6 | <a id="HLR-041: elc references no thread-creation symbol"></a>`HLR-041: elc references no thread-creation symbol` | — | No thread-creation symbol is referenced by the binary. |
| 7 | <a id="the build's required flags survive an overridden CFLAGS"></a>`the build's required flags survive an overridden CFLAGS` | `LLR-BLD-11` | The language standard, the warning set, and the header-dependency generation all appear in the compile command when CFLAGS is overridden from the command line, so a build invoked with an added flag is compiled under the same rules as one invoked with none. |
| 8 | <a id="HLR-041: the build passes no threading flag"></a>`HLR-041: the build passes no threading flag` | — | The build never passes -pthread, which would silently license a future thread. |
| 9 | <a id="the help block and the real target set agree"></a>`the help block and the real target set agree` | `LLR-BLD-13` | Every declared target appears in the makefile's help block and every name in that block is a real target, so the hand-maintained summary cannot drift from what the build actually offers. |
| 10 | <a id="make help prints the block from the file's header"></a>`make help prints the block from the file's header` | `LLR-BLD-13` | The help target prints the header block itself, marker stripped, so a reader who opens the file and a reader who runs it see one text. |
| 11 | <a id="every grammar is linked with the scanner it requires"></a>`every grammar is linked with the scanner it requires` | `LLR-BLD-16` | No delivered grammar has an unresolved external-scanner symbol. The rule located an optional scanner with a construct make expands before running the recipe, so it looked for a file the fetch above it had not unpacked and silently found none — invisible while C, which has no scanner, was the only grammar. |
| 12 | <a id="the grammar build takes its owner and reference as parameters"></a>`the grammar build takes its owner and reference as parameters` | `LLR-BLD-15` | The rule for a grammar hosted outside the parsing library's organisation reaches that owner, and fetches by commit rather than by tag because its upstream cuts no releases. Both would be impossible with either value hardcoded. |
| 13 | <a id="every grammar the build declares is one the build can produce"></a>`every grammar the build declares is one the build can produce` | `LLR-BLD-15` | Each name the build lists has a rule behind it. A missing one fails only on a clean tree, which is the tree nobody builds on. |
| 14 | <a id="check-prereqs reports every grammar against upstream"></a>`check-prereqs reports every grammar against upstream` | `LLR-BLD-17` | Each delivered language appears in the dependency report, so a pin that is immutable is not thereby invisible when it falls behind. |
| 15 | <a id="check-prereqs survives an unreachable upstream"></a>`check-prereqs survives an unreachable upstream` | `LLR-BLD-17` | An unreachable upstream reports "unknown" rather than failing or hanging: the report is a diagnostic a person runs, not a gate. |
| 16 | <a id="every runtime data file the build does not produce is tracked"></a>`every runtime data file the build does not produce is tracked` | — | Every file under the runtime location that the build does not itself produce is tracked by the repository, so that a version-control ignore rule cannot silently exclude product data. `.gitignore` carries `*.map` for linker map files, which also matched the extension table: it worked locally, was absent from the clone CI made, and every parsing test then failed naming the missing file rather than the rule that hid it. |
| 17 | <a id="HLR-043: elc does not modify the tree it analyses"></a>`HLR-043: elc does not modify the tree it analyses` | — | The analysed tree checksums identically before and after a run. |
| 18 | <a id="HLR-043: elc opens nothing for writing"></a>`HLR-043: elc opens nothing for writing` | — | No syscall capable of modifying a file — an open carrying a writing mode, a creat, an unlink, a truncate, or a rename — is observed for the whole run. This is direct evidence of read-only operation, where comparing checksums afterwards is only circumstantial. |
| 19 | <a id="HLR-076: each source file is opened exactly once"></a>`HLR-076: each source file is opened exactly once` | — | No source file is opened twice for the whole run, which is the observable form of the single-parse rule: a second open would mean some stage re-read it. |
| 20 | <a id="HLR-009: the grammar is loaded from the runtime location, not linked"></a>`HLR-009: the grammar is loaded from the runtime location, not linked` | — | No grammar appears among the binary's link-time dependencies, and the grammar file is opened during the run — language support is loaded at run time rather than compiled in. |
| 21 | <a id="HLR-043: elc runs against a read-only directory"></a>`HLR-043: elc runs against a read-only directory` | — | A run succeeds against a directory with write permission removed. |

### 3.33. [test/fixtures/smoke.bats](../test/fixtures/smoke.bats)

Role: **fixture**. **2 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the fixture level is wired and elc is runnable"></a>`the fixture level is wired and elc is runnable` | — | The fixture-conformance level is wired and green before the first real fixture is written. |
| 2 | <a id="every expected-value file has a fixture header beside it"></a>`every expected-value file has a fixture header beside it` | — | Guards the convention that expected values are hand-counted, never generated from elc's own output. |

## 4. LLR Coverage Matrix

Every LLR in [LLRs.md](LLRs.md) and the test(s) that verify it.
LLRs marked **(no direct test)** are exercised transitively or
verified by code review — see
[Traceability.md](Traceability.md) for the per-LLR justification.

| LLR | Function | HLR(s) | Verifying Test(s) |
| --- | -------- | ------ | ----------------- |
| `LLR-MAIN-01` | `main` | `HLR-063` | **(no direct test)** |
| `LLR-MAIN-02` | `main` | `HLR-117` | **(no direct test)** |
| `LLR-MAIN-03` | `main` | `HLR-055` | **(no direct test)** |
| `LLR-MAIN-04` | `main` | `HLR-122` | **(no direct test)** |
| `LLR-MAIN-05` | `main` | `HLR-036` | **(no direct test)** |
| `LLR-MAIN-06` | `main` | `HLR-076` | **(no direct test)** |
| `LLR-MAIN-07` | `main` | `HLR-035` | **(no direct test)** |
| `LLR-MAIN-08` | `main` | `HLR-037`, `HLR-012` | **(no direct test)** |
| `LLR-MAIN-09` | `main` | `HLR-037`, `HLR-120` | **(no direct test)** |
| `LLR-MAIN-10` | `main` | `HLR-120`, `HLR-062`, `HLR-036`, `HLR-058` | **(no direct test)** |
| `LLR-MAIN-11` | `main` | `HLR-100`, `HLR-023` | **(no direct test)** |
| `LLR-MAIN-12` | `main` | `HLR-038`, `HLR-030` | **(no direct test)** |
| `LLR-MAIN-13` | `main` | `HLR-036` | **(no direct test)** |
| `LLR-MAIN-14` | `main` | `HLR-041` | **(no direct test)** |
| `LLR-MAIN-15` | `main` | `HLR-103`, `HLR-104` | **(no direct test)** |
| `LLR-MAIN-17` | `main` | `HLR-030`, `HLR-038`, `HLR-120` | `--output writes the report to the named file`, `an output file that cannot be opened exits 2`, `an output file that cannot be opened is diagnosed on stderr` |
| `LLR-MAIN-18` | `main` | `HLR-012`, `HLR-037` | **(no direct test)** |
| `LLR-MAIN-16` | `main` | `HLR-125`, `HLR-036` | **(no direct test)** |
| `LLR-CLI-01` | `cli_parse` | `HLR-071`, `HLR-063` | `missing_target_is_a_usage_error`, `single_target_is_collected`, `several_targets_are_collected_in_order` |
| `LLR-CLI-02` | `cli_parse` | `HLR-027`, `HLR-028`, `HLR-054`, `HLR-029` | **(no direct test)** |
| `LLR-CLI-18` | `cli_parse` | `HLR-095`, `HLR-039` | **(no direct test)** |
| `LLR-CLI-03` | `cli_parse` | `HLR-030` | `the_output_destination_defaults_to_standard_output`, `an_output_path_is_collected`, `the_short_output_option_behaves_as_the_long_one` |
| `LLR-CLI-04` | `cli_parse` | `HLR-022` | `the_complexity_threshold_defaults_to_fifteen`, `a_complexity_threshold_is_collected`, `the_short_threshold_option_behaves_as_the_long_one`, `a_threshold_of_zero_is_accepted` |
| `LLR-CLI-05` | `cli_parse` | `HLR-081` | **(no direct test)** |
| `LLR-CLI-06` | `cli_parse` | `HLR-103` | **(no direct test)** |
| `LLR-CLI-07` | `cli_parse` | `HLR-106`, `HLR-119` | **(no direct test)** |
| `LLR-CLI-08` | `cli_parse` | `HLR-095` | **(no direct test)** |
| `LLR-CLI-09` | `cli_parse` | `HLR-107`, `HLR-063` | **(no direct test)** |
| `LLR-CLI-10` | `cli_parse` | `HLR-055`, `HLR-122` | **(no direct test)** |
| `LLR-CLI-11` | `cli_parse` | `HLR-057` | **(no direct test)** |
| `LLR-CLI-12` | `cli_parse` | `HLR-063` | `unrecognised_option_is_a_usage_error`, `missing_target_is_a_usage_error`, `an_output_option_without_its_argument_is_a_usage_error`, `a_malformed_threshold_is_a_usage_error` |
| `LLR-CLI-13` | `cli_parse` | `HLR-117` | `help_short_option_reports_help`, `help_long_option_reports_help`, `help_takes_precedence_over_a_target` |
| `LLR-CLI-14` | `cli_parse` | `HLR-039` | **(no direct test)** |
| `LLR-CLI-16` | `cli_parse` | `HLR-063`, `HLR-022` | `a_malformed_threshold_is_a_usage_error` |
| `LLR-CLI-17` | `cli_parse` | `HLR-055`, `HLR-063` | **(no direct test)** |
| `LLR-CLI-19` | `cli_parse` | `HLR-131` | **(no direct test)** |
| `LLR-CLI-20` | `cli_parse` | `HLR-131`, `HLR-063` | **(no direct test)** |
| `LLR-CLI-21` | `cli_parse` | `HLR-136`, `HLR-063` | **(no direct test)** |
| `LLR-CLI-15` | `cli_parse` | `HLR-122`, `HLR-063` | **(no direct test)** |
| `LLR-USG-01` | `cli_usage` | `HLR-117` | **(no direct test)** |
| `LLR-USG-02` | `cli_usage` | `HLR-117`, `HLR-063`, `HLR-038` | **(no direct test)** |
| `LLR-STR-01` | `parse_stratum` | `HLR-078` | **(no direct test)** |
| `LLR-STR-02` | `parse_stratum` | `HLR-078`, `HLR-118` | **(no direct test)** |
| `LLR-STR-03` | `parse_stratum` | `HLR-063`, `HLR-078` | **(no direct test)** |
| `LLR-SCP-01` | `parse_scope` | `HLR-094` | `a_scope_declaration_is_parsed_into_a_name_and_patterns`, `scope_declarations_accumulate`, `the_scope_option_reaches_the_options_structure` |
| `LLR-SCP-02` | `parse_scope` | `HLR-063`, `HLR-094` | `a_malformed_scope_declaration_is_rejected`, `a_malformed_scope_option_is_a_usage_error`, `HLR-063: a malformed scope declaration is a usage error` |
| `LLR-SCP-03` | `parse_scope` | `HLR-094`, `HLR-125` | **(no direct test)** |
| `LLR-DSC-01` | `discover_targets` | `HLR-062` | `every_target_is_validated_before_any_is_walked`, `filelist_free_is_safe_on_null` |
| `LLR-DSC-02` | `discover_targets` | `HLR-062` | `a_missing_target_is_rejected_with_an_empty_list`, `a_target_that_is_neither_file_nor_directory_is_rejected` |
| `LLR-DSC-03` | `discover_targets` | `HLR-071`, `HLR-001`, `HLR-126` | `files_and_directories_are_classified_independently` |
| `LLR-DSC-04` | `discover_targets` | `HLR-001` | `a_regular_file_target_is_appended_directly` |
| `LLR-DSC-05` | `discover_targets` | `HLR-002`, `HLR-004` | **(no direct test)** |
| `LLR-DSC-06` | `discover_targets` | `HLR-069` | `a_symbolic_link_named_as_a_target_is_resolved` |
| `LLR-DSC-07` | `discover_targets` | `HLR-072` | `a_file_reached_through_two_targets_appears_once` |
| `LLR-DSC-08` | `discover_targets` | `HLR-033` | `the_file_list_is_sorted_into_byte_order` |
| `LLR-DSC-10` | `discover_targets` | `HLR-127` | `a_route_is_recorded_for_each_target`, `the_route_list_owns_the_target_it_records` |
| `LLR-DSC-09` | `discover_targets` | `HLR-035` | `a_path_that_cannot_be_canonicalised_is_a_per_file_failure` |
| `LLR-GIT-01` | `walk_git_tree` | `HLR-002`, `HLR-126` | `git_walk_is_scoped_to_the_target` |
| `LLR-GIT-04` | `walk_git_tree` | `HLR-002`, `HLR-004` | `git_walk_declines_a_directory_in_no_repository`, `git_walk_declines_a_target_the_repository_does_not_track`, `a_tracked_directory_of_excluded_files_stays_on_the_git_route` |
| `LLR-GIT-02` | `walk_git_tree` | `HLR-003` | `git_walk_yields_the_tracked_files` |
| `LLR-GIT-03` | `walk_git_tree` | `HLR-003` | **(no direct test)** |
| `LLR-FTS-01` | `walk_filesystem` | `HLR-004` | `the_walk_descends_into_subdirectories` |
| `LLR-FTS-02` | `walk_filesystem` | `HLR-005` | `hidden_entries_and_binary_extensions_are_excluded` |
| `LLR-FTS-03` | `walk_filesystem` | `HLR-005` | `hidden_entries_and_binary_extensions_are_excluded` |
| `LLR-FTS-04` | `walk_filesystem` | `HLR-069` | `a_cyclic_directory_symlink_terminates` |
| `LLR-FTS-05` | `walk_filesystem` | `HLR-069`, `HLR-072` | `a_cyclic_directory_symlink_terminates` |
| `LLR-FTS-06` | `walk_filesystem` | `HLR-005` | `a_hidden_directory_named_as_the_target_is_traversed` |
| `LLR-EXT-01` | `is_excluded_extension` | `HLR-005`, `HLR-060` | `excluded_extension_matches_the_runtime_list`, `a_name_without_an_extension_is_not_excluded` |
| `LLR-EXT-02` | `is_excluded_extension` | `HLR-005`, `HLR-060` | `excluded_extension_ignores_case`, `extension_list_skips_comments_and_blank_lines` |
| `LLR-EXT-03` | `is_excluded_extension` | `HLR-005`, `HLR-038` | `an_empty_exclusion_list_excludes_nothing`, `a_missing_extension_list_is_not_fatal` |
| `LLR-ROP-01` | `registry_open` | `HLR-059` | `the_environment_variable_names_the_runtime_location` |
| `LLR-ROP-02` | `registry_open` | `HLR-059` | `the_environment_variable_names_the_runtime_location` |
| `LLR-ROP-03` | `registry_open` | `HLR-060` | `the_extension_map_is_runtime_data`, `the_extension_map_tolerates_comments_and_bare_extensions` |
| `LLR-ROP-04` | `registry_open` | `HLR-036` | `an_absent_runtime_location_is_fatal`, `a_runtime_location_that_is_a_file_is_fatal`, `an_extension_map_naming_no_language_is_fatal` |
| `LLR-ROP-05` | `registry_open` | `HLR-011` | `no_particular_language_is_required` |
| `LLR-ROP-07` | `registry_open` | `HLR-059`, `HLR-005` | **(no direct test)** |
| `LLR-ROP-06` | `registry_open` | `HLR-039` | **(no direct test)** |
| `LLR-RFP-01` | `registry_for_path` | `HLR-007` | `a_module_is_loaded_on_first_use_of_its_extension` |
| `LLR-RFP-02` | `registry_for_path` | `HLR-008` | `a_language_is_loaded_at_most_once` |
| `LLR-RFP-03` | `registry_for_path` | `HLR-009` | `a_module_is_loaded_on_first_use_of_its_extension` |
| `LLR-RFP-04` | `registry_for_path` | `HLR-010` | **(no direct test)** |
| `LLR-RFP-05` | `registry_for_path` | `HLR-012` | `an_unmapped_extension_yields_no_module` |
| `LLR-RFP-06` | `registry_for_path` | `HLR-070` | `an_absent_grammar_makes_the_language_unusable`, `a_missing_query_file_makes_the_language_unusable`, `an_invalid_query_makes_the_language_unusable`, `an_unusable_language_is_not_retried` |
| `LLR-RFP-07` | `registry_for_path` | `HLR-070` | `an_unusable_language_is_not_retried` |
| `LLR-RFP-09` | `registry_for_path` | `HLR-070`, `HLR-038` | **(no direct test)** |
| `LLR-RFP-10` | `registry_for_path` | `HLR-134`, `HLR-121`, `HLR-070` | **(no direct test)** |
| `LLR-RFP-11` | `registry_for_path` | `HLR-139`, `HLR-121`, `HLR-070` | `HLR-139: removing a query file makes that language unanalysed, not clean`, `HLR-070: a module missing an optional query is still usable` |
| `LLR-RFP-12` | `registry_for_path` | `HLR-070`, `HLR-121` | `HLR-070: an optional query that will not compile is a defect, not a choice` |
| `LLR-RFP-08` | `registry_for_path` | `HLR-121` | `a_module_is_loaded_on_first_use_of_its_extension`, `a_missing_query_file_makes_the_language_unusable` |
| `LLR-RLR-01` | `registry_load_rules` | `HLR-107` | **(no direct test)** |
| `LLR-RLR-02` | `registry_load_rules` | `HLR-107` | **(no direct test)** |
| `LLR-RLR-03` | `registry_load_rules` | `HLR-107`, `HLR-070` | **(no direct test)** |
| `LLR-RLR-04` | `registry_load_rules` | `HLR-108` | **(no direct test)** |
| `LLR-RLR-05` | `registry_load_rules` | `HLR-110`, `HLR-039` | **(no direct test)** |
| `LLR-RLR-06` | `registry_load_rules` | `HLR-116`, `HLR-063` | **(no direct test)** |
| `LLR-RLR-07` | `registry_load_rules` | `HLR-116`, `HLR-070` | **(no direct test)** |
| `LLR-RCL-01` | `registry_close` | `HLR-124`, `HLR-125`, `HLR-009` | `close_is_safe_on_null_and_on_a_zeroed_registry` |
| `LLR-ANL-01` | `analyze_file` | `HLR-013` | `a_file_that_fails_to_parse_is_a_failure` |
| `LLR-ANL-02` | `analyze_file` | `HLR-043` | `an_unreadable_file_is_a_failure_without_metrics`, `the_metrics_carry_the_path_and_language`, `filemetrics_free_is_safe_on_null` |
| `LLR-ANL-03` | `analyze_file` | `HLR-076` | **(no direct test)** |
| `LLR-ANL-04` | `analyze_file` | `HLR-020` | `a_zero_length_file_reports_zero_without_error`, `a_file_with_nothing_executable_reports_zero_eloc` |
| `LLR-ANL-05` | `analyze_file` | `HLR-013` | **(no direct test)** |
| `LLR-ANL-06` | `analyze_file` | `HLR-019` | `physical_lines_are_counted`, `an_unterminated_final_line_counts` |
| `LLR-ANL-07` | `analyze_file` | `HLR-014` | `a_function_is_reported_with_its_name_and_line_range`, `a_function_name_outlives_the_mapping` |
| `LLR-ANL-08` | `analyze_file` | `HLR-014` | `a_prototype_is_not_a_function` |
| `LLR-ANL-09` | `analyze_file` | `HLR-067` | `a_nested_function_is_reported_in_its_own_right` |
| `LLR-ANL-10` | `analyze_file` | `HLR-015` | `only_statements_count_toward_eloc`, `a_file_with_nothing_executable_reports_zero_eloc` |
| `LLR-ANL-11` | `analyze_file` | `HLR-053` | `a_multi_line_statement_counts_once` |
| `LLR-ANL-12` | `analyze_file` | `HLR-044` | **(no direct test)** |
| `LLR-ANL-13` | `analyze_file` | `HLR-045` | **(no direct test)** |
| `LLR-ANL-14` | `analyze_file` | `HLR-046` | **(no direct test)** |
| `LLR-ANL-15` | `analyze_file` | `HLR-047` | **(no direct test)** |
| `LLR-ANL-16` | `analyze_file` | `HLR-048` | **(no direct test)** |
| `LLR-ANL-17` | `analyze_file` | `HLR-049` | **(no direct test)** |
| `LLR-ANL-18` | `analyze_file` | `HLR-050` | **(no direct test)** |
| `LLR-ANL-19` | `analyze_file` | `HLR-051` | **(no direct test)** |
| `LLR-ANL-20` | `analyze_file` | `HLR-052` | **(no direct test)** |
| `LLR-ANL-21` | `analyze_file` | `HLR-017` | `a_function_that_never_branches_is_one`, `each_decision_point_adds_one` |
| `LLR-ANL-22` | `analyze_file` | `HLR-018` | `a_nested_functions_decisions_are_not_counted_twice`, `an_unreported_scope_attributes_to_the_named_function_around_it` |
| `LLR-ANL-23` | `analyze_file` | `HLR-019` | **(no direct test)** |
| `LLR-ANL-24` | `analyze_file` | `HLR-020` | **(no direct test)** |
| `LLR-ANL-25` | `analyze_file` | `HLR-073`, `HLR-074`, `HLR-096` | **(no direct test)** |
| `LLR-ANL-26` | `analyze_file` | `HLR-109` | **(no direct test)** |
| `LLR-ANL-27` | `analyze_file` | `HLR-109` | **(no direct test)** |
| `LLR-ANL-28` | `analyze_file` | `HLR-035` | **(no direct test)** |
| `LLR-ANL-29` | `analyze_file` | `HLR-035` | **(no direct test)** |
| `LLR-ANL-30` | `analyze_file` | `HLR-014` | **(no direct test)** |
| `LLR-ANL-31` | `analyze_file` | `HLR-043` | **(no direct test)** |
| `LLR-ANL-32` | `analyze_file` | `HLR-034` | **(no direct test)** |
| `LLR-ANL-33` | `analyze_file` | `HLR-124` | **(no direct test)** |
| `LLR-ANL-35` | `analyze_file` | `HLR-014` | `a_function_is_reported_with_its_name_and_line_range` |
| `LLR-ANL-36` | `analyze_file` | `HLR-014`, `HLR-121` | `a_prototype_is_not_a_function` |
| `LLR-ANL-37` | `analyze_file` | `HLR-012`, `HLR-035`, `HLR-037` | `an_unmapped_extension_is_a_skip_not_a_failure` |
| `LLR-ANL-38` | `analyze_file` | `HLR-015`, `HLR-053` | `two_statements_on_one_line_count_once` |
| `LLR-ANL-39` | `analyze_file` | `HLR-016`, `HLR-015` | `a_trailing_comment_does_not_remove_its_line` |
| `LLR-ANL-40` | `analyze_file` | `HLR-019`, `HLR-068` | `a_nested_functions_statements_are_not_counted_twice`, `file_scope_code_counts_for_the_file_only` |
| `LLR-ANL-41` | `analyze_file` | `HLR-017`, `HLR-018` | `a_file_scope_decision_belongs_to_no_function` |
| `LLR-ANL-42` | `analyze_file` | `HLR-132`, `HLR-135` | **(no direct test)** |
| `LLR-ANL-43` | `analyze_file` | `HLR-132` | **(no direct test)** |
| `LLR-ANL-44` | `analyze_file` | `HLR-133`, `HLR-135`, `HLR-013` | **(no direct test)** |
| `LLR-ANL-45` | `analyze_file` | `HLR-131`, `HLR-032` | **(no direct test)** |
| `LLR-ANL-46` | `analyze_file` | `HLR-121`, `HLR-138`, `HLR-013` | `a_predicate_that_does_not_hold_rejects_the_match` |
| `LLR-ANL-47` | `analyze_file` | `HLR-121`, `HLR-138` | **(no direct test)** |
| `LLR-ANL-34` | `analyze_file` | `HLR-124`, `HLR-125` | **(no direct test)** |
| `LLR-DED-01` | `collect_dead_code` | `HLR-137` | `statements_after_a_terminator_are_recorded`, `a_label_after_a_terminator_is_not_recorded`, `HLR-137: statements after a return are reported`, `HLR-138: a label following a return is NOT reported`, `HLR-137: statements after a break are reported`, `HLR-137: statements after a continue are reported`, `LLR-DED-01: the walk does not leave the terminator's own block`, `HLR-138: a switch arm does not leak into the next`, `HLR-137: the two causes are distinguished`, `HLR-139: dead code is found in every language supplying a query` |
| `LLR-DED-02` | `collect_dead_code` | `HLR-137` | `a_literal_branch_is_recorded_with_its_whole_span`, `HLR-137: the consequence of if (0) and the else of if (1) are reported`, `HLR-137: the body of a literally false loop is reported`, `HLR-138: a do-while(0) body is NOT reported` |
| `LLR-DED-03` | `collect_dead_code` | `HLR-138`, `HLR-013` | `a_branch_guarded_by_a_variable_is_never_recorded`, `HLR-138: a branch guarded by a variable is NOT reported`, `LLR-DED-03: a zero the source did not write as one is undecided`, `HLR-138: no language claims a branch guarded by a variable` |
| `LLR-DED-04` | `collect_dead_code` | `HLR-137`, `HLR-068` | `a_dead_span_is_attributed_to_the_innermost_function` |
| `LLR-DED-05` | `collect_dead_code` | `HLR-139`, `HLR-121` | `a_language_with_no_dead_code_query_is_unanalysed_not_clean`, `HLR-139: a language supplying a dead-code query reports every language analysed`, `HLR-139: a language with no dead-code query is reported not analysed` |
| `LLR-DED-06` | `collect_dead_code` | `HLR-137`, `HLR-096` | `HLR-137: dead statements inside an unreachable function are still reported` |
| `LLR-DED-07` | `collect_dead_code` | `HLR-137`, `HLR-138`, `HLR-016` | `HLR-016: a comment is not dead code` |
| `LLR-DED-08` | `collect_dead_code` | `HLR-137` | **(no direct test)** |
| `LLR-DED-09` | `collect_dead_code` | `HLR-137`, `HLR-032` | `LLR-DED-01: a statement following two terminators is reported once` |
| `LLR-MRG-01` | `merge_comment_spans` | `HLR-016` | `spans_are_sorted_before_merging` |
| `LLR-MRG-02` | `merge_comment_spans` | `HLR-016` | `overlapping_spans_coalesce`, `a_nested_span_is_absorbed_not_counted_twice` |
| `LLR-MRG-03` | `merge_comment_spans` | `HLR-016`, `HLR-034` | `a_nested_span_is_absorbed_not_counted_twice`, `a_shared_line_is_counted_once` |
| `LLR-MRG-05` | `merge_comment_spans` | `HLR-016` | `a_shared_line_is_counted_once` |
| `LLR-MRG-04` | `merge_comment_spans` | `HLR-124` | `coalescing_a_trailing_run_stays_in_bounds`, `merging_an_empty_span_list_is_zero` |
| `LLR-INN-01` | `innermost_enclosing` | `HLR-068` | `the_narrowest_enclosing_function_wins`, `the_narrowest_wins_whatever_order_the_ranges_are_in`, `an_empty_range_index_owns_nothing` |
| `LLR-INN-02` | `innermost_enclosing` | `HLR-068`, `HLR-067` | `the_narrowest_wins_whatever_order_the_ranges_are_in`, `an_offset_outside_every_function_has_no_owner`, `a_nested_functions_statements_are_not_counted_twice`, `a_nested_functions_decisions_are_not_counted_twice` |
| `LLR-SDG-01` | `graph_build` | `HLR-073` | `every_discovered_function_becomes_a_node`, `an_empty_project_builds_an_empty_graph` |
| `LLR-SDG-02` | `graph_build` | `HLR-073` | `a_call_resolves_across_files` |
| `LLR-SDG-03` | `graph_build` | `HLR-074` | `a_global_links_its_writer_to_its_reader` |
| `LLR-SDG-04` | `graph_build` | `HLR-085` | `repeated_calls_collapse_to_one_edge_with_a_count` |
| `LLR-SDG-05` | `graph_build` | `HLR-075`, `HLR-071` | **(no direct test)** |
| `LLR-SDG-06` | `graph_build` | `HLR-076` | **(no direct test)** |
| `LLR-SDG-07` | `graph_build` | `HLR-077` | `an_unresolvable_call_is_counted_and_does_not_abort` |
| `LLR-SDG-08` | `graph_build` | `HLR-077` | `an_unresolvable_call_is_counted_and_does_not_abort` |
| `LLR-SDG-09` | `graph_build` | `HLR-033` | `node_identifiers_follow_sorted_file_order` |
| `LLR-SDG-10` | `graph_build` | `HLR-114` | `the_component_projection_is_file_level`, `a_call_within_one_file_is_no_component_dependency` |
| `LLR-SDG-12` | `graph_build` | `HLR-124`, `HLR-074` | `a_global_links_its_writer_to_its_reader`, `graph_free_is_safe_on_null_and_twice` |
| `LLR-SDG-13` | `graph_build` | `HLR-074`, `HLR-096`, `HLR-121` | `an_undeclared_identifier_is_not_global_state`, `an_undeclared_name_is_not_global_state`, `an_address_taken_function_is_marked`, `an_address_taken_name_that_is_not_a_function_is_discarded` |
| `LLR-SDG-14` | `graph_build` | `HLR-085`, `HLR-074`, `HLR-077` | `repeated_calls_collapse_to_one_edge_with_a_count`, `a_call_from_file_scope_has_no_caller_node`, `two_objects_between_one_pair_are_two_edges` |
| `LLR-SDG-16` | `graph_build` | `HLR-091`, `HLR-092`, `HLR-032` | `a_global_touched_by_one_function_is_a_scope_reduction_candidate` |
| `LLR-SDG-15` | `graph_build` | `HLR-124`, `HLR-113`, `HLR-120` | `the_library_returns_errors_instead_of_aborting` |
| `LLR-SDG-11` | `graph_build` | `HLR-124`, `HLR-077` | **(no direct test)** |
| `LLR-ARC-01` | `arch_analyse` | `HLR-081` | **(no direct test)** |
| `LLR-ARC-02` | `arch_analyse` | `HLR-081`, `HLR-099` | **(no direct test)** |
| `LLR-ARC-03` | `arch_analyse` | `HLR-115` | **(no direct test)** |
| `LLR-ARC-04` | `arch_analyse` | `HLR-078` | **(no direct test)** |
| `LLR-CPL-01` | `compute_coupling` | `HLR-080` | **(no direct test)** |
| `LLR-CPL-02` | `compute_coupling` | `HLR-080` | **(no direct test)** |
| `LLR-CPL-03` | `compute_coupling` | `HLR-114` | **(no direct test)** |
| `LLR-INS-01` | `instability` | `HLR-082` | **(no direct test)** |
| `LLR-INS-02` | `instability` | `HLR-082` | **(no direct test)** |
| `LLR-INS-03` | `instability` | `HLR-099`, `HLR-082` | **(no direct test)** |
| `LLR-CYC-01` | `find_cycles` | `HLR-083` | **(no direct test)** |
| `LLR-CYC-02` | `find_cycles` | `HLR-083` | **(no direct test)** |
| `LLR-CYC-03` | `find_cycles` | `HLR-083`, `HLR-089` | **(no direct test)** |
| `LLR-CYC-04` | `find_cycles` | `HLR-084`, `HLR-123` | **(no direct test)** |
| `LLR-LAY-01` | `check_strata` | `HLR-079` | **(no direct test)** |
| `LLR-LAY-02` | `check_strata` | `HLR-118` | **(no direct test)** |
| `LLR-LAY-03` | `check_strata` | `HLR-079`, `HLR-118` | **(no direct test)** |
| `LLR-CTR-01` | `calltree_analyse` | `HLR-085` | `fan_out_counts_distinct_callees`, `an_empty_project_measures_nothing_without_failing`, `tree_results_free_is_safe_on_null_and_twice` |
| `LLR-CTR-02` | `calltree_analyse` | `HLR-089` | `direct_recursion_is_detected`, `mutual_recursion_is_detected`, `a_straight_line_program_has_no_cycles` |
| `LLR-CTR-03` | `calltree_analyse` | `HLR-115`, `HLR-087` | `no_entry_points_omits_depth_with_a_reason` |
| `LLR-CTR-04` | `calltree_analyse` | `HLR-090` | `recursion_yields_no_depth_and_terminates` |
| `LLR-CTR-07` | `calltree_analyse` | `HLR-085`, `HLR-089`, `HLR-074` | `fan_out_ignores_global_edges`, `a_global_cycle_is_not_recursion`, `a_chain_does_not_travel_along_a_global_edge` |
| `LLR-CTR-08` | `calltree_analyse` | `HLR-115`, `HLR-095` | `an_entry_point_matching_nothing_is_a_distinct_omission` |
| `LLR-CTR-09` | `calltree_analyse` | `HLR-115`, `HLR-089` | `no_entry_points_omits_depth_with_a_reason` |
| `LLR-CTR-05` | `calltree_analyse` | `HLR-087` | `depth_is_the_length_of_the_deepest_chain`, `depth_measures_from_the_deepest_entry_point` |
| `LLR-CTR-06` | `calltree_analyse` | `HLR-087`, `HLR-077` | `depth_carries_the_unresolved_count` |
| `LLR-LPD-01` | `longest_path_dag` | `HLR-087` | `the_deepest_chain_is_an_ordered_sequence` |
| `LLR-LPD-04` | `longest_path_dag` | `HLR-087`, `HLR-032` | `depth_is_the_length_of_the_deepest_chain` |
| `LLR-LPD-02` | `longest_path_dag` | `HLR-088` | **(no direct test)** |
| `LLR-LPD-03` | `longest_path_dag` | `HLR-088` | `the_deepest_chain_is_an_ordered_sequence` |
| `LLR-STA-01` | `state_analyse` | `HLR-115`, `HLR-096` | `a_declared_symbol_naming_nothing_is_skipped_not_fatal`, `with_no_entry_points_nothing_is_unreachable`, `a_declaration_matching_nothing_is_its_own_omission`, `HLR-115: with no entry points declared, nothing is reported unreachable`, `HLR-115: a declared entry point matching nothing says so differently`, `HLR-115: with no entry points, no global is reported unreachable either` |
| `LLR-STA-02` | `state_analyse` | `HLR-115`, `HLR-094` | `no_scope_declared_leaves_the_list_empty`, `with_no_scopes_declared_the_analysis_is_omitted`, `HLR-115: with no scopes declared the analysis is omitted with a reason` |
| `LLR-STA-03` | `state_analyse` | `HLR-096`, `HLR-097`, `HLR-074` | `reachability_does_not_travel_along_a_global_edge` |
| `LLR-STA-04` | `state_analyse` | `HLR-115`, `HLR-091` | `an_empty_graph_analyses_without_incident`, `LLR-CTR-09: omitting reachability does not omit its neighbours` |
| `LLR-GLB-01` | `classify_globals` | `HLR-091` | `HLR-091: every global reports its writers and its readers` |
| `LLR-GLB-02` | `classify_globals` | `HLR-092` | `a_global_touched_by_one_function_is_a_scope_reduction_candidate`, `HLR-092: a global touched by one function is flagged for scope reduction` |
| `LLR-GLB-03` | `classify_globals` | `HLR-093` | `a_global_spanning_disconnected_regions_is_a_hidden_channel`, `HLR-093: a global spanning disconnected regions is a hidden channel` |
| `LLR-GLB-04` | `classify_globals` | `HLR-099`, `HLR-092`, `HLR-093` | `LLR-GLB-04: both findings carry their published source` |
| `LLR-GLB-05` | `classify_globals` | `HLR-093`, `HLR-074` | `shared_state_within_one_region_is_ordinary`, `HLR-093: shared state within one call-connected region is not a channel` |
| `LLR-RTS-01` | `collect_roots` | `HLR-096`, `HLR-095` | `roots_are_entry_points_and_address_taken_functions`, `a_root_declared_twice_is_one_root` |
| `LLR-RTS-02` | `collect_roots` | `HLR-096`, `HLR-097` | `an_address_taken_function_and_its_callees_are_reachable`, `HLR-096: a function reachable only through an address-taken pointer is NOT reported`, `LLR-RTS-02: an address-taken function is a root, not merely an exception` |
| `LLR-RCH-01` | `reachability` | `HLR-096` | `HLR-096: functions reached from the declared entry point are not reported` |
| `LLR-RCH-02` | `reachability` | `HLR-097` | **(no direct test)** |
| `LLR-RCH-03` | `reachability` | `HLR-097` | `a_clique_of_unused_functions_is_unreachable`, `HLR-097: a clique of unused functions calling one another is reported` |
| `LLR-UGL-01` | `unreachable_globals` | `HLR-096` | `a_global_touched_only_by_dead_functions_is_unreachable`, `HLR-096: a global touched only by unreachable functions is unreachable`, `LLR-UGL-01: a global a reachable function touches is not condemned` |
| `LLR-UGL-02` | `unreachable_globals` | `HLR-096`, `HLR-138` | `a_global_no_function_touches_is_not_claimed_dead` |
| `LLR-ISO-01` | `check_scopes` | `HLR-094` | `an_edge_crossing_a_declared_boundary_is_reported`, `a_shared_global_crossing_a_boundary_is_reported`, `HLR-094: a call crossing a declared scope boundary is reported`, `HLR-094: a shared global crossing a boundary is reported too`, `HLR-094: exactly the two crossings are reported`, `HLR-094: an edge within one scope is not a crossing` |
| `LLR-ISO-02` | `check_scopes` | `HLR-094`, `HLR-115` | `a_component_matching_no_declaration_is_outside_the_partition`, `HLR-094: a file matching no declaration is outside the partition` |
| `LLR-THR-01` | `thresholds_apply` | `HLR-098` | **(no direct test)** |
| `LLR-THR-02` | `thresholds_apply` | `HLR-099` | **(no direct test)** |
| `LLR-THR-03` | `thresholds_apply` | `HLR-123` | **(no direct test)** |
| `LLR-THR-04` | `thresholds_apply` | `HLR-123` | **(no direct test)** |
| `LLR-THR-05` | `thresholds_apply` | `HLR-086` | **(no direct test)** |
| `LLR-THR-06` | `thresholds_apply` | `HLR-087` | **(no direct test)** |
| `LLR-THR-07` | `thresholds_apply` | `HLR-089`, `HLR-099` | **(no direct test)** |
| `LLR-THR-08` | `thresholds_apply` | `HLR-098` | **(no direct test)** |
| `LLR-THR-09` | `thresholds_apply` | `HLR-101` | **(no direct test)** |
| `LLR-THR-10` | `thresholds_apply` | `HLR-111` | **(no direct test)** |
| `LLR-RPT-01` | `report_assemble` | `HLR-024` | `totals_sum_across_every_file` |
| `LLR-RPT-02` | `report_assemble` | `HLR-025` | **(no direct test)** |
| `LLR-RPT-03` | `report_assemble` | `HLR-026` | **(no direct test)** |
| `LLR-RPT-04` | `report_assemble` | `HLR-026`, `HLR-033` | **(no direct test)** |
| `LLR-RPT-05` | `report_assemble` | `HLR-021` | **(no direct test)** |
| `LLR-RPT-06` | `report_assemble` | `HLR-023`, `HLR-100` | **(no direct test)** |
| `LLR-RPT-07` | `report_assemble` | `HLR-012` | **(no direct test)** |
| `LLR-RPT-08` | `report_assemble` | `HLR-077` | **(no direct test)** |
| `LLR-RPT-09` | `report_assemble` | `HLR-115` | **(no direct test)** |
| `LLR-RPT-10` | `report_assemble` | `HLR-033`, `HLR-032` | `files_are_presented_in_ascending_path_order` |
| `LLR-RPT-11` | `report_assemble` | `HLR-033` | `files_are_presented_in_ascending_path_order` |
| `LLR-RPT-23` | `report_assemble` | `HLR-077`, `HLR-024` | **(no direct test)** |
| `LLR-RPT-24` | `report_assemble` | `HLR-085`, `HLR-088`, `HLR-089` | **(no direct test)** |
| `LLR-RPT-26` | `report_assemble` | `HLR-137`, `HLR-139`, `HLR-033` | **(no direct test)** |
| `LLR-RPT-25` | `report_assemble` | `HLR-115`, `HLR-090` | **(no direct test)** |
| `LLR-RPT-12` | `report_assemble` | `HLR-066` | `an_empty_run_yields_a_complete_model_with_zero_totals` |
| `LLR-RPT-13` | `report_assemble` | `HLR-006` | **(no direct test)** |
| `LLR-RPT-14` | `report_assemble` | `HLR-109` | **(no direct test)** |
| `LLR-RPT-15` | `report_assemble` | `HLR-080`, `HLR-082`, `HLR-085`, `HLR-031` | **(no direct test)** |
| `LLR-RPT-17` | `report_assemble` | `HLR-033`, `HLR-127` | **(no direct test)** |
| `LLR-RPT-18` | `report_assemble` | `HLR-124`, `HLR-125` | `assembly_leaves_the_accumulator_empty` |
| `LLR-RPT-19` | `report_assemble` | `HLR-025`, `HLR-033` | **(no direct test)** |
| `LLR-RPT-20` | `report_assemble` | `HLR-021`, `HLR-023` | **(no direct test)** |
| `LLR-RPT-21` | `report_assemble` | `HLR-026`, `HLR-032`, `HLR-033` | **(no direct test)** |
| `LLR-RPT-27` | `report_assemble` | `HLR-136`, `HLR-133`, `HLR-033` | **(no direct test)** |
| `LLR-RPT-28` | `report_assemble` | `HLR-137`, `HLR-068`, `HLR-032` | **(no direct test)** |
| `LLR-RPT-29` | `report_assemble` | `HLR-032`, `HLR-033` | **(no direct test)** |
| `LLR-RPT-16` | `report_assemble` | `HLR-124`, `HLR-125` | `a_failed_growth_leaves_the_accumulator_intact`, `free_is_safe_on_null` |
| `LLR-TBL-01` | `format_table` | `HLR-027` | `the_table_carries_the_summary_and_every_file`, `columns_are_aligned_on_the_longest_path`, `an_empty_report_still_renders_a_table` |
| `LLR-TBL-02` | `format_table` | `HLR-027` | **(no direct test)** |
| `LLR-TBL-03` | `format_table` | `HLR-038` | `a_write_failure_is_reported` |
| `LLR-MKD-01` | `format_markdown` | `HLR-029` | **(no direct test)** |
| `LLR-SUM-01` | `render_summary` | `HLR-031`, `HLR-127`, `HLR-012`, `HLR-115` | **(no direct test)** |
| `LLR-SUM-03` | `render_summary` | `HLR-031` | **(no direct test)** |
| `LLR-SUM-04` | `render_summary` | `HLR-027`, `HLR-032` | **(no direct test)** |
| `LLR-SUM-05` | `render_summary` | `HLR-136`, `HLR-031` | **(no direct test)** |
| `LLR-SUM-02` | `render_summary` | `HLR-031` | **(no direct test)** |
| `LLR-CSV-01` | `format_csv` | `HLR-028` | `the_header_row_is_written`, `an_empty_report_is_a_header_alone`, `a_write_failure_is_reported` |
| `LLR-CSV-02` | `format_csv` | `HLR-028`, `HLR-031` | **(no direct test)** |
| `LLR-FLD-01` | `write_field` | `HLR-064` | `an_ordinary_field_is_not_quoted`, `a_field_containing_a_comma_is_quoted`, `a_quote_is_doubled_not_backslashed`, `a_field_containing_a_newline_is_quoted`, `a_field_containing_a_carriage_return_is_quoted`, `a_field_needing_every_escape_survives`, `an_empty_field_is_emitted_empty`, `a_null_field_is_emitted_empty` |
| `LLR-FLD-02` | `write_field` | `HLR-064` | `the_header_row_is_written` |
| `LLR-XWR-01` | `xml_write_report` | `HLR-054` | `an_empty_report_is_still_a_complete_record` |
| `LLR-XWR-02` | `xml_write_report` | `HLR-054` | **(no direct test)** |
| `LLR-XWR-03` | `xml_write_report` | `HLR-061` | `the_record_carries_its_format_version` |
| `LLR-XWR-05` | `xml_write_report` | `HLR-061`, `HLR-054` | **(no direct test)** |
| `LLR-XWR-06` | `xml_write_report` | `HLR-054`, `HLR-056`, `HLR-127` | **(no direct test)** |
| `LLR-XWR-07` | `xml_write_report` | `HLR-054`, `HLR-056`, `HLR-077` | **(no direct test)** |
| `LLR-XWR-09` | `xml_write_report` | `HLR-054`, `HLR-056`, `HLR-137` | **(no direct test)** |
| `LLR-XWR-08` | `xml_write_report` | `HLR-054`, `HLR-056` | **(no direct test)** |
| `LLR-XWR-10` | `xml_write_report` | `HLR-136`, `HLR-054` | **(no direct test)** |
| `LLR-XWR-11` | `xml_write_report` | `HLR-054`, `HLR-056`, `HLR-096`, `HLR-137` | `HLR-032: the state sections survive a record round trip byte-identically` |
| `LLR-XWR-12` | `xml_write_report` | `HLR-139`, `HLR-056`, `HLR-099` | `HLR-032: a global-state finding survives the round trip` |
| `LLR-XWR-04` | `xml_write_report` | `HLR-065` | `the_record_carries_its_format_version`, `an_empty_report_is_still_a_complete_record`, `a_write_failure_is_reported` |
| `LLR-ESC-01` | `write_escaped` | `HLR-065` | `an_ampersand_is_escaped`, `angle_brackets_are_escaped`, `quotation_marks_are_escaped`, `an_ampersand_in_an_entity_is_escaped_once`, `ordinary_text_is_unchanged`, `escaping_null_emits_nothing` |
| `LLR-ESC-02` | `write_escaped` | `HLR-065` | **(no direct test)** |
| `LLR-XRD-01` | `xml_read_report` | `HLR-055` | `the_model_is_reconstructed_from_the_record` |
| `LLR-XRD-02` | `xml_read_report` | `HLR-055` | **(no direct test)** |
| `LLR-XRD-03` | `xml_read_report` | `HLR-058` | `input_that_is_not_xml_is_rejected`, `a_truncated_record_is_rejected`, `an_absent_record_is_rejected` |
| `LLR-XRD-04` | `xml_read_report` | `HLR-058` | `well_formed_but_foreign_input_is_rejected`, `a_record_without_a_version_is_rejected` |
| `LLR-XRD-05` | `xml_read_report` | `HLR-058`, `HLR-061` | `an_unsupported_format_version_is_rejected` |
| `LLR-XRD-09` | `xml_read_report` | `HLR-056`, `HLR-032` | `the_model_is_reconstructed_from_the_record` |
| `LLR-XRD-10` | `xml_read_report` | `HLR-058` | **(no direct test)** |
| `LLR-XRD-12` | `xml_read_report` | `HLR-055`, `HLR-056`, `HLR-127` | **(no direct test)** |
| `LLR-XRD-13` | `xml_read_report` | `HLR-136`, `HLR-056` | **(no direct test)** |
| `LLR-XRD-06` | `xml_read_report` | `HLR-058` | `well_formed_but_foreign_input_is_rejected`, `a_truncated_record_is_rejected` |
| `LLR-XRD-07` | `xml_read_report` | `HLR-057` | `the_threshold_supplied_now_is_the_one_applied` |
| `LLR-XRD-08` | `xml_read_report` | `HLR-056` | **(no direct test)** |
| `LLR-WAR-01` | `graph_dot_warranted` | `HLR-103` | **(no direct test)** |
| `LLR-WAR-02` | `graph_dot_warranted` | `HLR-104` | **(no direct test)** |
| `LLR-WAR-03` | `graph_dot_warranted` | `HLR-122` | **(no direct test)** |
| `LLR-DOT-01` | `graph_write_dot` | `HLR-102` | **(no direct test)** |
| `LLR-DOT-02` | `graph_write_dot` | `HLR-102` | **(no direct test)** |
| `LLR-DOT-03` | `graph_write_dot` | `HLR-119` | **(no direct test)** |
| `LLR-DOT-04` | `graph_write_dot` | `HLR-032`, `HLR-033` | **(no direct test)** |
| `LLR-DOT-05` | `graph_write_dot` | `HLR-035` | **(no direct test)** |
| `LLR-STY-01` | `node_style` | `HLR-105` | **(no direct test)** |
| `LLR-STY-02` | `node_style` | `HLR-105` | **(no direct test)** |
| `LLR-GML-01` | `graph_write_graphml` | `HLR-106` | **(no direct test)** |
| `LLR-GML-02` | `graph_write_graphml` | `HLR-106` | `graphml_is_off_unless_asked_for`, `graphml_is_written_when_asked_for_and_named` |
| `LLR-GML-03` | `graph_write_graphml` | `HLR-106`, `HLR-119` | `graphml_needs_an_output_path`, `the_companion_replaces_the_extension`, `a_path_without_an_extension_gains_one`, `a_dot_in_a_directory_is_not_an_extension`, `a_directory_dot_with_an_extension_still_substitutes` |
| `LLR-GML-04` | `graph_write_graphml` | `HLR-065`, `HLR-106` | **(no direct test)** |
| `LLR-BLD-01` | `build_configuration` | `HLR-040` | **(no direct test)** |
| `LLR-BLD-02` | `build_configuration` | `HLR-040` | **(no direct test)** |
| `LLR-BLD-03` | `build_configuration` | `HLR-040` | **(no direct test)** |
| `LLR-BLD-04` | `build_configuration` | `HLR-112` | **(no direct test)** |
| `LLR-BLD-05` | `build_configuration` | `HLR-113` | **(no direct test)** |
| `LLR-BLD-06` | `build_configuration` | `HLR-112`, `HLR-113` | **(no direct test)** |
| `LLR-BLD-07` | `build_configuration` | `HLR-011` | **(no direct test)** |
| `LLR-BLD-08` | `build_configuration` | `HLR-121`, `HLR-010` | **(no direct test)** |
| `LLR-BLD-10` | `build_configuration` | `HLR-113`, `HLR-124` | `wrap_passes_through_when_not_armed`, `wrap_intercepts_when_armed` |
| `LLR-BLD-11` | `build_configuration` | `HLR-124` | `the build's required flags survive an overridden CFLAGS` |
| `LLR-BLD-12` | `build_configuration` | `HLR-040`, `HLR-011`, `HLR-009` | **(no direct test)** |
| `LLR-BLD-13` | `build_configuration` | `HLR-128` | `the help block and the real target set agree`, `make help prints the block from the file's header` |
| `LLR-BLD-14` | `build_configuration` | `HLR-035`, `HLR-013` | **(no direct test)** |
| `LLR-BLD-15` | `build_configuration` | `HLR-010`, `HLR-011` | `the grammar build takes its owner and reference as parameters`, `every grammar the build declares is one the build can produce` |
| `LLR-BLD-16` | `build_configuration` | `HLR-009`, `HLR-011` | `every grammar is linked with the scanner it requires` |
| `LLR-BLD-17` | `build_configuration` | `HLR-011` | `check-prereqs reports every grammar against upstream`, `check-prereqs survives an unreachable upstream` |
| `LLR-BLD-09` | `build_configuration` | `HLR-124`, `HLR-125` | **(no direct test)** |
| `LLR-DOC-01` | `user_documentation` | `HLR-128` | **(no direct test)** |
| `LLR-DOC-02` | `user_documentation` | `HLR-128` | **(no direct test)** |
| `LLR-DOC-03` | `user_documentation` | `HLR-128` | **(no direct test)** |
| `LLR-DOC-04` | `user_documentation` | `HLR-129` | **(no direct test)** |
| `LLR-DOC-05` | `user_documentation` | `HLR-129` | **(no direct test)** |
| `LLR-DOC-06` | `user_documentation` | `HLR-130` | **(no direct test)** |

## 5. Integration Test Environment

Fixtures are grouped by the property they exist to exercise. Each group's directory holds the inputs, and each fixture file carries a header stating its expected values and the reasoning behind them.

The adversarial fixtures are the ones that matter: they are chosen so that an implementation taking a shortcut the requirements forbid — scanning text instead of the syntax tree, subtracting comment spans without merging them, resolving only direct calls — fails visibly rather than drifting quietly.

| Fixture | Source | Inputs | Expected | Verifies |
| ------- | ------ | --- | --- | --- |
| `eloc/` | [test/fixtures/eloc/](../test/fixtures/eloc/) | One source file per language, per ELOC category | `expected.tsv` — hand-counted ELOC and complexity per function | HLR-015, HLR-019, HLR-020, HLR-044 – HLR-053 |
| `comments/` | [test/fixtures/comments/](../test/fixtures/comments/) | Nested block comments; comment syntax inside string literals; string delimiters inside comments; a block comment containing inline comment syntax | `expected.tsv`, with the merge arithmetic shown in each file header | HLR-016, HLR-034 — and, through them, HLR-013, which has no direct observable |
| `nesting/` | [test/fixtures/nesting/](../test/fixtures/nesting/) | Ada nested subprograms; lambdas and closures; methods and constructors; a nested function inside a lambda | `expected.tsv` — per-function attribution showing no statement counted twice | HLR-014, HLR-018, HLR-067, HLR-068 |
| `deadcode/` | [test/fixtures/deadcode/](../test/fixtures/deadcode/) | Statements after a `return`, a `break`, and a `continue`; a `goto` label following a `return`, which is reachable and must not be reported; a switch whose arms each end in `return`; `if (0)`, `if (1) … else`, and a loop with a literally false condition; a variable holding a constant used as a condition, which must **not** be reported; dead statements inside an unreachable function; a file in a language whose module supplies no dead-code query | The exact set of unreachable statements with their line ranges and causes — and, as importantly, the exact set of constructs that are *not* reported. The false-positive cases carry the weight: a missed statement costs a cleanup, a false claim invites deleting code that runs | HLR-013, HLR-096, HLR-121, HLR-137, HLR-138, HLR-139 |
| `calltree/` | [test/fixtures/calltree/](../test/fixtures/calltree/) | One function per fan-out band boundary — 2, 3, 7, 8, 10, 11, 15, 16 — each also calling one helper twice; a four-deep call chain with a shallower branch listed first; direct and mutual recursion in their smallest forms | The exact fan-out at every boundary, the deepest chain as an ordered sequence, both recursive cycles by kind and members, and the stated reason wherever a depth figure is absent. Phase 12 adds `expected-findings.tsv`, the band each boundary value falls in — which is why the values are pinned here first | HLR-032, HLR-033, HLR-038, HLR-056, HLR-077, HLR-085, HLR-087 – HLR-090, HLR-095, HLR-115 |
| `graph/` | [test/fixtures/graph/](../test/fixtures/graph/) | **Phase 8**: a repeated call between one pair of functions; a global written in one file and read in another; a function assigned without being called; a call into a library; and, in Ada, a construct the grammar cannot separate from a call. **Phases 9–11 add**: mutual recursion within one file and across two; a clique of unused functions calling one another; a component dependency cycle; a global read and written by a single function; a global read by unreachable functions only | `expected.graphml`, compared node for node and edge for edge; `expected-findings.tsv` joins it when there are findings to compare. The ambiguous-call case pins what the grammar yields, so that the imprecision is a recorded decision rather than a later surprise — and so that a *false cycle* arising from it would be visible in the fixture rather than in a user's report. As of Phase 8 the Ada ambiguity resolves against no subprogram and lands in the unresolved count rather than becoming an edge, which is better than predicted and is pinned as such | Phase 8: HLR-010, HLR-032, HLR-033, HLR-073 – HLR-077, HLR-085, HLR-096, HLR-104, HLR-106, HLR-114. Later phases add HLR-083, HLR-084, HLR-089, HLR-091 – HLR-093, HLR-097 |
| `arch/` | [test/fixtures/arch/](../test/fixtures/arch/) | A layered tree with declared strata and execution scopes; a call skipping a layer; a call inverting the declared direction; a component with high fan-in and fan-out; components at each end of the instability range; a run with no strata declared at all | `expected-findings.tsv`, with the hand-computed `Ca`, `Ce`, and instability table, and the omission notice for the undeclared run | HLR-078 – HLR-082, HLR-094, HLR-114, HLR-115, HLR-118 |
| `rules/` | [test/fixtures/rules/](../test/fixtures/rules/) | A valid rule file with several named captures, supplied both from the runtime location and from the command line; a rule naming a language with no module | Each match reported with its identity as basename plus capture name, and the file and line range | HLR-107 – HLR-111 |
| `traversal/` | [test/fixtures/traversal/](../test/fixtures/traversal/) | Hidden files and hidden directories; binary extensions; a self-referential directory symlink; a symlink to a file inside the tree; a symlink named directly as a target; overlapping targets naming one file twice | The analysed file set, each file exactly once | HLR-004, HLR-005, HLR-043, HLR-069, HLR-071, HLR-072 |
| `repo/` | [test/fixtures/repo/](../test/fixtures/repo/) | A repository built into the test's own temporary directory, holding tracked source, an untracked file, a gitignored build directory, a tracked blob with binary content, a tracked file with an excluded extension, and tracked hidden entries; a subdirectory target; a repository with no commits; targets an enclosing repository does not track | The analysed file set and the route reported for each target; the scoped totals; the same file set by either route | HLR-002, HLR-003, HLR-004, HLR-005, HLR-033, HLR-055, HLR-056, HLR-126, HLR-127 |
| `runtime/` | [test/fixtures/runtime/](../test/fixtures/runtime/) | An absent runtime directory; one with no valid module; a module missing its entry point; a module with an unparseable query; an invalid custom rule, both CLI-named and runtime-located | Expected diagnostic text and exit status per case | HLR-036, HLR-059, HLR-070, HLR-116, HLR-120 |
| `conditional/` | [test/fixtures/conditional/](../test/fixtures/conditional/) | A C source with `#if 0`, `#ifdef`, `#ifndef`, and a nested conditional; the same logic under a Rust `cfg` attribute; a condition depending on a symbol no definition names; a run with no definitions at all | Hand-counted ELOC and complexity for each configuration; identical figures to a run made without the option when no definition is supplied; both branches counted and the region reported undecided where the condition names an unknown symbol | HLR-131 – HLR-136 |
| `escaping/` | [test/fixtures/escaping/](../test/fixtures/escaping/) | Identifiers containing commas, quotes, ampersands, and angle brackets — C++ template signatures being the natural source | CSV parsed back to the original field count; XML and GraphML parsed without error | HLR-064, HLR-065 |
| `determinism/` | [test/fixtures/determinism/](../test/fixtures/determinism/) | A tree analysed twice; reached via differing target order; with decoy `.elcrc` and dotfiles planted in the working directory, the target, and an ancestor | Byte-identical output in every case | HLR-032, HLR-033, HLR-039 |
| `regeneration/` | [test/fixtures/regeneration/](../test/fixtures/regeneration/) | A saved XML record; the same record with an unsupported version; a malformed record; a well-formed but structurally foreign document | Markdown byte-identical to direct analysis at the same threshold; rejection with exit 2 for the rest | HLR-055 – HLR-058, HLR-061, HLR-122 |
**Where a fixture group lives.** The hand-counted data of a group lives in the directory named above; the Bats suite that asserts against it lives *beside* that directory, as `test/fixtures/<group>.bats`, rather than inside it. The reason is specific rather than stylistic: recursive discovery in Bats enumerates suites with `find -L`, which follows symbolic links, and the `traversal/` group deliberately contains a self-referential one. Keeping the suites flat means the harness never has to walk a tree built to defeat walking.

**The fixture table is not the whole coverage plan.** A large group of requirements is verified at the integration level instead, because what they constrain is the command line and the shape of the report rather than the analysis of any particular source: the report formats and their uniform composition, companion-artefact naming and the GraphML default, the threshold listing and its default, help and usage handling, skipped-file reporting, the severity vocabulary, and the attribution strings. These need a target to run against, not a specially constructed one, and so are exercised by the integration suites over the simplest fixture available.

**The GraphML export carries the graph test suite.** The rendered findings report conclusions, not topology — they say a cycle exists, not which edges the graph holds. GraphML is therefore the only channel through which a test can assert that the SDG itself was built correctly, which makes HLR-106 a dependency of this plan and not merely a user-facing feature. The `graph/` fixture group asserts against `expected.graphml` for exactly this reason.

## 6. Tooling and Dependencies

| Component | Required For | Notes |
| --------- | ------------ | ----- |
| `criterion` | The unit level | C-native framework with automatic test registration and per-test process isolation; TAP output merged with the Bats stream |
| GNU `ld` `--wrap` | Unit-level mocking | Intercepts a dependency at link time via `__wrap_`/`__real_` symbols, so no test seam is carried in `src/`. GNU ld or LLVM lld; not available with every linker |
| `bats` | The integration, fixture, and instrumented levels | With `bats-support` and `bats-assert`; vendored or provided by the platform |
| `gcc` or `clang` | Building `elc` and the unit drivers | C11, with `-Wall -Wextra -Wpedantic`; warnings are treated as defects |
| `make` | Building and running the suite | GNU make; `make test` is the single entry point |
| AddressSanitizer + LeakSanitizer | HLR-124, HLR-125 — `make asan` | `-fsanitize=address` with `detect_leaks=1`; catches out-of-bounds, use-after-free, invalid free, and leaks. Requires an instrumented rebuild |
| UndefinedBehaviorSanitizer | HLR-124 — `make asan` | `-fsanitize=undefined` with `halt_on_error=1`; catches signed overflow, misaligned and null dereference, and invalid shifts |
| `valgrind` | HLR-124, HLR-125 — `make valgrind` | `--leak-check=full --errors-for-leak-kinds=all` over a single file, a directory, and a repository target. Run in a separate pass from ASan, never combined; finds uninitialised reads ASan does not, and needs no rebuild |
| `strace` | HLR-040 — no network access; HLR-076 — single parse | `-e trace=%network` must log no network syscall over a full run; `-e trace=openat` must show each source file opened exactly once. Preferred over `unshare -rn` for the network property, which containers — GitHub's runners included — commonly withhold by disabling unprivileged user namespaces |
| `ldd` | HLR-040, HLR-113 — dependency constraints | Link line checked against an allowlist and for the graph library |
| `git` | Repository-target fixtures | Repositories are built per-test with pinned `user.name` and `user.email` |
| `graphviz` | Validating `.dot` output | `dot -Tsvg -o /dev/null` must parse the emitted file; never linked by `elc` |
| An XML parser | Validating XML and GraphML output | `xmllint --noout` or equivalent; well-formedness is asserted, not assumed |

## 7. Maintenance

**Every behavioural change ships with its test in the same change.** A new or altered HLR or LLR without a bound test leaves a gap that [Traceability.md §6](Traceability.md#6-coverage-gaps) will report, and the gap is the signal that the change is incomplete.

**Adding a `src/` module** adds one Criterion binary and one `make` rule; because registration is automatic, adding a test to an existing module adds nothing but the test itself.

**Adding a language** adds a fixture, not a test module. A new grammar and its query files bring a directory under `test/fixtures/eloc/` and `test/fixtures/nesting/` with hand-counted expected values; no C code and no `.bats` file changes. If supporting a language requires either, the extensibility model has been broken and the defect is in `src/`, not in the test suite.

**Requirements verified by review rather than execution.** A small number cannot be settled by any run of the binary: HLR-101 and HLR-111 (that `elc` offers no advice and holds no opinion of its own) are argued from the output's content, and HLR-121's cross-release stability clause is a property of the release process. Each has a test asserting what *can* be asserted — that findings carry a measurement and an attribution and no imperative text — and the residue is recorded as a review item. §2's pass/fail criteria state that these do not count toward automated coverage.

**A test seam never enters `src/`.** If a unit test seems to need a hook — a function pointer to swap, a conditional compile, an extra parameter existing only for tests — the answer is a `--wrap` on the dependency, not a change to the module under test. A seam maintained for tests is production code that no user exercises, and it drifts.

**Memory-safety regressions are caught by the suite you already have.** Because `make asan` re-runs every level rather than adding tests of its own, a new test broadens memory-safety coverage as a side effect of covering its own requirement. Conversely, a requirement with no test has no memory-safety coverage either — which is a second reason the gap list in [Traceability.md §6](Traceability.md#6-coverage-gaps) matters.

**Fixture expected values are inputs, never outputs.** They are counted by hand and justified in the fixture header. When `elc` disagrees with a fixture, the default assumption is that `elc` is wrong.
