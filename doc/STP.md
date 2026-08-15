# Software Test Plan

**Version:** 0.2
**Date:** 2026-08-15
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

Snapshot: **128 test(s)** across
**12 file(s)**.

### 3.1. [test/unit/cli.c](../test/unit/cli.c)

Role: **unit**. **15 test(s).**

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
| 15 | <a id="options_free_is_safe_on_null"></a>`options_free_is_safe_on_null` | — | Releasing a null options structure does not fault, so teardown is safe on every path. |

### 3.2. [test/unit/analyze.c](../test/unit/analyze.c)

Role: **unit**. **7 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="physical_lines_are_counted"></a>`physical_lines_are_counted` | `LLR-ANL-06` | The physical line count comes from the mapped contents. |
| 2 | <a id="an_unterminated_final_line_counts"></a>`an_unterminated_final_line_counts` | `LLR-ANL-06` | A trailing fragment with no terminating newline is a line the reader sees, and is counted as one. |
| 3 | <a id="a_zero_length_file_reports_zero_without_error"></a>`a_zero_length_file_reports_zero_without_error` | `LLR-ANL-04` | A file of zero length reports zero metrics without error, rather than being mapped — mmap of an empty file fails with EINVAL. |
| 4 | <a id="the_metrics_carry_the_path"></a>`the_metrics_carry_the_path` | `LLR-ANL-06` | The returned metrics carry the path they were measured from, which the report is later ordered by. |
| 5 | <a id="an_unreadable_file_is_reported_without_metrics"></a>`an_unreadable_file_is_reported_without_metrics` | `LLR-ANL-02` | A file that cannot be read yields a non-zero return and no metrics for the caller to release, so the run can continue leak-free. |
| 6 | <a id="a_file_in_an_unwritable_directory_is_still_measured"></a>`a_file_in_an_unwritable_directory_is_still_measured` | `LLR-ANL-02` | Measurement succeeds where nothing may be written, which is the observable consequence of opening read-only. |
| 7 | <a id="filemetrics_free_is_safe_on_null"></a>`filemetrics_free_is_safe_on_null` | `LLR-ANL-02` | Releasing a null metrics structure does not fault, so teardown is safe on every path. |

### 3.3. [test/unit/discover.c](../test/unit/discover.c)

Role: **unit**. **20 test(s).**

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
| 20 | <a id="filelist_free_is_safe_on_null"></a>`filelist_free_is_safe_on_null` | `LLR-DSC-01` | Releasing a null file list or extension list does not fault, so teardown is safe on every path. |

### 3.4. [test/unit/report.c](../test/unit/report.c)

Role: **unit**. **6 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="totals_sum_across_every_file"></a>`totals_sum_across_every_file` | `LLR-RPT-01` | The project totals are the sum of the per-file physical line counts across every analysed file. |
| 2 | <a id="files_are_presented_in_ascending_path_order"></a>`files_are_presented_in_ascending_path_order` | `LLR-RPT-10`, `LLR-RPT-11` | Files are ordered by path in the model, so presentation order is a property of the report rather than of the order the files were discovered. |
| 3 | <a id="an_empty_run_yields_a_complete_model_with_zero_totals"></a>`an_empty_run_yields_a_complete_model_with_zero_totals` | `LLR-RPT-12` | A run in which no file was analysed still produces a complete model with zero totals, which renders normally. |
| 4 | <a id="assembly_leaves_the_accumulator_empty"></a>`assembly_leaves_the_accumulator_empty` | `LLR-RPT-18` | Ownership of the per-file metrics moves into the report, so releasing both the accumulator and the report cannot free the same metrics twice. |
| 5 | <a id="a_failed_growth_leaves_the_accumulator_intact"></a>`a_failed_growth_leaves_the_accumulator_intact` | `LLR-RPT-16` | A reallocation failure, provoked through a link-time wrapper, is reported and leaves the original allocation intact rather than overwriting it with a null pointer. |
| 6 | <a id="free_is_safe_on_null"></a>`free_is_safe_on_null` | `LLR-RPT-16` | Releasing a null report or accumulator does not fault, so teardown is safe on every path. |

### 3.5. [test/unit/format_text.c](../test/unit/format_text.c)

Role: **unit**. **4 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the_table_carries_the_summary_and_every_file"></a>`the_table_carries_the_summary_and_every_file` | `LLR-TBL-01` | The rendered table contains the project summary tier and one row for each file in the model. |
| 2 | <a id="columns_are_aligned_on_the_longest_path"></a>`columns_are_aligned_on_the_longest_path` | `LLR-TBL-01` | Every file row is rendered to the same width, the path column having been sized from the longest path in the model. |
| 3 | <a id="an_empty_report_still_renders_a_table"></a>`an_empty_report_still_renders_a_table` | `LLR-TBL-01` | A model with no files still renders its headings and column rule, so an empty run is distinguishable from a crash. |
| 4 | <a id="a_write_failure_is_reported"></a>`a_write_failure_is_reported` | `LLR-TBL-03` | A stream that cannot absorb the report yields a non-zero return, so a truncated report is never reported as success. |

### 3.6. [test/integration/cli.bats](../test/integration/cli.bats)

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

### 3.7. [test/integration/docs.bats](../test/integration/docs.bats)

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

### 3.8. [test/integration/discovery.bats](../test/integration/discovery.bats)

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

### 3.9. [test/fixtures/traversal.bats](../test/fixtures/traversal.bats)

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

### 3.10. [test/fixtures/determinism.bats](../test/fixtures/determinism.bats)

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

### 3.11. [test/instrumented/environment.bats](../test/instrumented/environment.bats)

Role: **instrumented**. **11 test(s).**

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
| 9 | <a id="HLR-043: elc does not modify the tree it analyses"></a>`HLR-043: elc does not modify the tree it analyses` | — | The analysed tree checksums identically before and after a run. |
| 10 | <a id="HLR-043: elc opens nothing for writing"></a>`HLR-043: elc opens nothing for writing` | — | No syscall capable of modifying a file — an open carrying a writing mode, a creat, an unlink, a truncate, or a rename — is observed for the whole run. This is direct evidence of read-only operation, where comparing checksums afterwards is only circumstantial. |
| 11 | <a id="HLR-043: elc runs against a read-only directory"></a>`HLR-043: elc runs against a read-only directory` | — | A run succeeds against a directory with write permission removed. |

### 3.12. [test/fixtures/smoke.bats](../test/fixtures/smoke.bats)

Role: **fixture**. **2 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="the fixture level is wired and elc is runnable"></a>`the fixture level is wired and elc is runnable` | — | The fixture-conformance level is wired and green before the first real fixture is written. |
| 2 | <a id="fixture directories carry no generated expected values yet"></a>`fixture directories carry no generated expected values yet` | — | Guards the convention that expected values are hand-counted, never generated from elc's own output. |

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
| `LLR-MAIN-16` | `main` | `HLR-125`, `HLR-036` | **(no direct test)** |
| `LLR-CLI-01` | `cli_parse` | `HLR-071`, `HLR-063` | `missing_target_is_a_usage_error`, `single_target_is_collected`, `several_targets_are_collected_in_order` |
| `LLR-CLI-02` | `cli_parse` | `HLR-027`, `HLR-028`, `HLR-054`, `HLR-029` | **(no direct test)** |
| `LLR-CLI-03` | `cli_parse` | `HLR-030` | `the_output_destination_defaults_to_standard_output`, `an_output_path_is_collected`, `the_short_output_option_behaves_as_the_long_one` |
| `LLR-CLI-04` | `cli_parse` | `HLR-022` | **(no direct test)** |
| `LLR-CLI-05` | `cli_parse` | `HLR-081` | **(no direct test)** |
| `LLR-CLI-06` | `cli_parse` | `HLR-103` | **(no direct test)** |
| `LLR-CLI-07` | `cli_parse` | `HLR-106`, `HLR-119` | **(no direct test)** |
| `LLR-CLI-08` | `cli_parse` | `HLR-095` | **(no direct test)** |
| `LLR-CLI-09` | `cli_parse` | `HLR-107`, `HLR-063` | **(no direct test)** |
| `LLR-CLI-10` | `cli_parse` | `HLR-055`, `HLR-122` | **(no direct test)** |
| `LLR-CLI-11` | `cli_parse` | `HLR-057` | **(no direct test)** |
| `LLR-CLI-12` | `cli_parse` | `HLR-063` | `unrecognised_option_is_a_usage_error`, `missing_target_is_a_usage_error`, `an_output_option_without_its_argument_is_a_usage_error` |
| `LLR-CLI-13` | `cli_parse` | `HLR-117` | `help_short_option_reports_help`, `help_long_option_reports_help`, `help_takes_precedence_over_a_target` |
| `LLR-CLI-14` | `cli_parse` | `HLR-039` | **(no direct test)** |
| `LLR-CLI-15` | `cli_parse` | `HLR-122`, `HLR-063` | **(no direct test)** |
| `LLR-USG-01` | `cli_usage` | `HLR-117` | **(no direct test)** |
| `LLR-USG-02` | `cli_usage` | `HLR-117`, `HLR-063`, `HLR-038` | **(no direct test)** |
| `LLR-STR-01` | `parse_stratum` | `HLR-078` | **(no direct test)** |
| `LLR-STR-02` | `parse_stratum` | `HLR-078`, `HLR-118` | **(no direct test)** |
| `LLR-STR-03` | `parse_stratum` | `HLR-063`, `HLR-078` | **(no direct test)** |
| `LLR-SCP-01` | `parse_scope` | `HLR-094` | **(no direct test)** |
| `LLR-SCP-02` | `parse_scope` | `HLR-063`, `HLR-094` | **(no direct test)** |
| `LLR-DSC-01` | `discover_targets` | `HLR-062` | `every_target_is_validated_before_any_is_walked`, `filelist_free_is_safe_on_null` |
| `LLR-DSC-02` | `discover_targets` | `HLR-062` | `a_missing_target_is_rejected_with_an_empty_list`, `a_target_that_is_neither_file_nor_directory_is_rejected` |
| `LLR-DSC-03` | `discover_targets` | `HLR-071`, `HLR-001`, `HLR-126` | `files_and_directories_are_classified_independently` |
| `LLR-DSC-04` | `discover_targets` | `HLR-001` | `a_regular_file_target_is_appended_directly` |
| `LLR-DSC-05` | `discover_targets` | `HLR-002`, `HLR-004` | **(no direct test)** |
| `LLR-DSC-06` | `discover_targets` | `HLR-069` | `a_symbolic_link_named_as_a_target_is_resolved` |
| `LLR-DSC-07` | `discover_targets` | `HLR-072` | `a_file_reached_through_two_targets_appears_once` |
| `LLR-DSC-08` | `discover_targets` | `HLR-033` | `the_file_list_is_sorted_into_byte_order` |
| `LLR-DSC-10` | `discover_targets` | `HLR-127` | **(no direct test)** |
| `LLR-DSC-09` | `discover_targets` | `HLR-035` | `a_path_that_cannot_be_canonicalised_is_a_per_file_failure` |
| `LLR-GIT-01` | `walk_git_tree` | `HLR-002`, `HLR-126` | **(no direct test)** |
| `LLR-GIT-04` | `walk_git_tree` | `HLR-002`, `HLR-004` | **(no direct test)** |
| `LLR-GIT-02` | `walk_git_tree` | `HLR-003` | **(no direct test)** |
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
| `LLR-ROP-01` | `registry_open` | `HLR-059` | **(no direct test)** |
| `LLR-ROP-02` | `registry_open` | `HLR-059` | **(no direct test)** |
| `LLR-ROP-03` | `registry_open` | `HLR-060` | **(no direct test)** |
| `LLR-ROP-04` | `registry_open` | `HLR-036` | **(no direct test)** |
| `LLR-ROP-05` | `registry_open` | `HLR-011` | **(no direct test)** |
| `LLR-ROP-06` | `registry_open` | `HLR-039` | **(no direct test)** |
| `LLR-RFP-01` | `registry_for_path` | `HLR-007` | **(no direct test)** |
| `LLR-RFP-02` | `registry_for_path` | `HLR-008` | **(no direct test)** |
| `LLR-RFP-03` | `registry_for_path` | `HLR-009` | **(no direct test)** |
| `LLR-RFP-04` | `registry_for_path` | `HLR-010` | **(no direct test)** |
| `LLR-RFP-05` | `registry_for_path` | `HLR-012` | **(no direct test)** |
| `LLR-RFP-06` | `registry_for_path` | `HLR-070` | **(no direct test)** |
| `LLR-RFP-07` | `registry_for_path` | `HLR-070` | **(no direct test)** |
| `LLR-RFP-08` | `registry_for_path` | `HLR-121` | **(no direct test)** |
| `LLR-RLR-01` | `registry_load_rules` | `HLR-107` | **(no direct test)** |
| `LLR-RLR-02` | `registry_load_rules` | `HLR-107` | **(no direct test)** |
| `LLR-RLR-03` | `registry_load_rules` | `HLR-107`, `HLR-070` | **(no direct test)** |
| `LLR-RLR-04` | `registry_load_rules` | `HLR-108` | **(no direct test)** |
| `LLR-RLR-05` | `registry_load_rules` | `HLR-110`, `HLR-039` | **(no direct test)** |
| `LLR-RLR-06` | `registry_load_rules` | `HLR-116`, `HLR-063` | **(no direct test)** |
| `LLR-RLR-07` | `registry_load_rules` | `HLR-116`, `HLR-070` | **(no direct test)** |
| `LLR-RCL-01` | `registry_close` | `HLR-124`, `HLR-125`, `HLR-009` | **(no direct test)** |
| `LLR-ANL-01` | `analyze_file` | `HLR-013` | **(no direct test)** |
| `LLR-ANL-02` | `analyze_file` | `HLR-043` | `an_unreadable_file_is_reported_without_metrics`, `a_file_in_an_unwritable_directory_is_still_measured`, `filemetrics_free_is_safe_on_null` |
| `LLR-ANL-03` | `analyze_file` | `HLR-076` | **(no direct test)** |
| `LLR-ANL-04` | `analyze_file` | `HLR-020` | `a_zero_length_file_reports_zero_without_error` |
| `LLR-ANL-05` | `analyze_file` | `HLR-013` | **(no direct test)** |
| `LLR-ANL-06` | `analyze_file` | `HLR-019` | `physical_lines_are_counted`, `an_unterminated_final_line_counts`, `the_metrics_carry_the_path` |
| `LLR-ANL-07` | `analyze_file` | `HLR-014` | **(no direct test)** |
| `LLR-ANL-08` | `analyze_file` | `HLR-014` | **(no direct test)** |
| `LLR-ANL-09` | `analyze_file` | `HLR-067` | **(no direct test)** |
| `LLR-ANL-10` | `analyze_file` | `HLR-015` | **(no direct test)** |
| `LLR-ANL-11` | `analyze_file` | `HLR-053` | **(no direct test)** |
| `LLR-ANL-12` | `analyze_file` | `HLR-044` | **(no direct test)** |
| `LLR-ANL-13` | `analyze_file` | `HLR-045` | **(no direct test)** |
| `LLR-ANL-14` | `analyze_file` | `HLR-046` | **(no direct test)** |
| `LLR-ANL-15` | `analyze_file` | `HLR-047` | **(no direct test)** |
| `LLR-ANL-16` | `analyze_file` | `HLR-048` | **(no direct test)** |
| `LLR-ANL-17` | `analyze_file` | `HLR-049` | **(no direct test)** |
| `LLR-ANL-18` | `analyze_file` | `HLR-050` | **(no direct test)** |
| `LLR-ANL-19` | `analyze_file` | `HLR-051` | **(no direct test)** |
| `LLR-ANL-20` | `analyze_file` | `HLR-052` | **(no direct test)** |
| `LLR-ANL-21` | `analyze_file` | `HLR-017` | **(no direct test)** |
| `LLR-ANL-22` | `analyze_file` | `HLR-018` | **(no direct test)** |
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
| `LLR-ANL-34` | `analyze_file` | `HLR-124`, `HLR-125` | **(no direct test)** |
| `LLR-MRG-01` | `merge_comment_spans` | `HLR-016` | **(no direct test)** |
| `LLR-MRG-02` | `merge_comment_spans` | `HLR-016` | **(no direct test)** |
| `LLR-MRG-03` | `merge_comment_spans` | `HLR-016`, `HLR-034` | **(no direct test)** |
| `LLR-MRG-04` | `merge_comment_spans` | `HLR-124` | **(no direct test)** |
| `LLR-INN-01` | `innermost_enclosing` | `HLR-068` | **(no direct test)** |
| `LLR-INN-02` | `innermost_enclosing` | `HLR-068`, `HLR-067` | **(no direct test)** |
| `LLR-SDG-01` | `graph_build` | `HLR-073` | **(no direct test)** |
| `LLR-SDG-02` | `graph_build` | `HLR-073` | **(no direct test)** |
| `LLR-SDG-03` | `graph_build` | `HLR-074` | **(no direct test)** |
| `LLR-SDG-04` | `graph_build` | `HLR-085` | **(no direct test)** |
| `LLR-SDG-05` | `graph_build` | `HLR-075`, `HLR-071` | **(no direct test)** |
| `LLR-SDG-06` | `graph_build` | `HLR-076` | **(no direct test)** |
| `LLR-SDG-07` | `graph_build` | `HLR-077` | **(no direct test)** |
| `LLR-SDG-08` | `graph_build` | `HLR-077` | **(no direct test)** |
| `LLR-SDG-09` | `graph_build` | `HLR-033` | **(no direct test)** |
| `LLR-SDG-10` | `graph_build` | `HLR-114` | **(no direct test)** |
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
| `LLR-CTR-01` | `calltree_analyse` | `HLR-085` | **(no direct test)** |
| `LLR-CTR-02` | `calltree_analyse` | `HLR-089` | **(no direct test)** |
| `LLR-CTR-03` | `calltree_analyse` | `HLR-115`, `HLR-087` | **(no direct test)** |
| `LLR-CTR-04` | `calltree_analyse` | `HLR-090` | **(no direct test)** |
| `LLR-CTR-05` | `calltree_analyse` | `HLR-087` | **(no direct test)** |
| `LLR-CTR-06` | `calltree_analyse` | `HLR-087`, `HLR-077` | **(no direct test)** |
| `LLR-LPD-01` | `longest_path_dag` | `HLR-087` | **(no direct test)** |
| `LLR-LPD-02` | `longest_path_dag` | `HLR-088` | **(no direct test)** |
| `LLR-LPD-03` | `longest_path_dag` | `HLR-088` | **(no direct test)** |
| `LLR-STA-01` | `state_analyse` | `HLR-115`, `HLR-096` | **(no direct test)** |
| `LLR-STA-02` | `state_analyse` | `HLR-115`, `HLR-094` | **(no direct test)** |
| `LLR-GLB-01` | `classify_globals` | `HLR-091` | **(no direct test)** |
| `LLR-GLB-02` | `classify_globals` | `HLR-092` | **(no direct test)** |
| `LLR-GLB-03` | `classify_globals` | `HLR-093` | **(no direct test)** |
| `LLR-GLB-04` | `classify_globals` | `HLR-099`, `HLR-092`, `HLR-093` | **(no direct test)** |
| `LLR-RTS-01` | `collect_roots` | `HLR-096`, `HLR-095` | **(no direct test)** |
| `LLR-RTS-02` | `collect_roots` | `HLR-096`, `HLR-097` | **(no direct test)** |
| `LLR-RCH-01` | `reachability` | `HLR-096` | **(no direct test)** |
| `LLR-RCH-02` | `reachability` | `HLR-097` | **(no direct test)** |
| `LLR-RCH-03` | `reachability` | `HLR-097` | **(no direct test)** |
| `LLR-UGL-01` | `unreachable_globals` | `HLR-096` | **(no direct test)** |
| `LLR-ISO-01` | `check_scopes` | `HLR-094` | **(no direct test)** |
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
| `LLR-RPT-12` | `report_assemble` | `HLR-066` | `an_empty_run_yields_a_complete_model_with_zero_totals` |
| `LLR-RPT-13` | `report_assemble` | `HLR-006` | **(no direct test)** |
| `LLR-RPT-14` | `report_assemble` | `HLR-109` | **(no direct test)** |
| `LLR-RPT-15` | `report_assemble` | `HLR-080`, `HLR-082`, `HLR-085`, `HLR-031` | **(no direct test)** |
| `LLR-RPT-17` | `report_assemble` | `HLR-127` | **(no direct test)** |
| `LLR-RPT-18` | `report_assemble` | `HLR-124`, `HLR-125` | `assembly_leaves_the_accumulator_empty` |
| `LLR-RPT-16` | `report_assemble` | `HLR-124`, `HLR-125` | `a_failed_growth_leaves_the_accumulator_intact`, `free_is_safe_on_null` |
| `LLR-TBL-01` | `format_table` | `HLR-027` | `the_table_carries_the_summary_and_every_file`, `columns_are_aligned_on_the_longest_path`, `an_empty_report_still_renders_a_table` |
| `LLR-TBL-02` | `format_table` | `HLR-027` | **(no direct test)** |
| `LLR-TBL-03` | `format_table` | `HLR-038` | `a_write_failure_is_reported` |
| `LLR-MKD-01` | `format_markdown` | `HLR-029` | **(no direct test)** |
| `LLR-SUM-01` | `render_summary` | `HLR-031`, `HLR-127`, `HLR-012`, `HLR-115` | **(no direct test)** |
| `LLR-SUM-02` | `render_summary` | `HLR-031` | **(no direct test)** |
| `LLR-CSV-01` | `format_csv` | `HLR-028` | **(no direct test)** |
| `LLR-CSV-02` | `format_csv` | `HLR-028`, `HLR-031` | **(no direct test)** |
| `LLR-FLD-01` | `write_field` | `HLR-064` | **(no direct test)** |
| `LLR-FLD-02` | `write_field` | `HLR-064` | **(no direct test)** |
| `LLR-XWR-01` | `xml_write_report` | `HLR-054` | **(no direct test)** |
| `LLR-XWR-02` | `xml_write_report` | `HLR-054` | **(no direct test)** |
| `LLR-XWR-03` | `xml_write_report` | `HLR-061` | **(no direct test)** |
| `LLR-XWR-04` | `xml_write_report` | `HLR-065` | **(no direct test)** |
| `LLR-ESC-01` | `write_escaped` | `HLR-065` | **(no direct test)** |
| `LLR-ESC-02` | `write_escaped` | `HLR-065` | **(no direct test)** |
| `LLR-XRD-01` | `xml_read_report` | `HLR-055` | **(no direct test)** |
| `LLR-XRD-02` | `xml_read_report` | `HLR-055` | **(no direct test)** |
| `LLR-XRD-03` | `xml_read_report` | `HLR-058` | **(no direct test)** |
| `LLR-XRD-04` | `xml_read_report` | `HLR-058` | **(no direct test)** |
| `LLR-XRD-05` | `xml_read_report` | `HLR-058`, `HLR-061` | **(no direct test)** |
| `LLR-XRD-06` | `xml_read_report` | `HLR-058` | **(no direct test)** |
| `LLR-XRD-07` | `xml_read_report` | `HLR-057` | **(no direct test)** |
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
| `LLR-GML-02` | `graph_write_graphml` | `HLR-106` | **(no direct test)** |
| `LLR-GML-03` | `graph_write_graphml` | `HLR-106`, `HLR-119` | **(no direct test)** |
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
| `graph/` | [test/fixtures/graph/](../test/fixtures/graph/) | Mutual recursion within one file and across two; a function reachable only via an address-taken pointer; a clique of unused functions calling one another; a component dependency cycle; a global written in one region and read in another; a global read and written by a single function; a global read by unreachable functions only; and, for a language whose grammar cannot separate a call from an index, a construct that is ambiguous between the two | `expected.graphml` and `expected-findings.tsv`. The ambiguous-call case pins whichever edge the grammar yields, so that over-approximation is a recorded decision rather than a later surprise — and so that a *false cycle* arising from it is visible in the fixture rather than in a user's report | HLR-073 – HLR-077, HLR-083, HLR-084, HLR-089, HLR-091 – HLR-093, HLR-096, HLR-097, HLR-114 |
| `arch/` | [test/fixtures/arch/](../test/fixtures/arch/) | A layered tree with declared strata and execution scopes; a call skipping a layer; a call inverting the declared direction; a component with high fan-in and fan-out; components at each end of the instability range; a run with no strata declared at all | `expected-findings.tsv`, with the hand-computed `Ca`, `Ce`, and instability table, and the omission notice for the undeclared run | HLR-078 – HLR-082, HLR-094, HLR-114, HLR-115, HLR-118 |
| `calltree/` | [test/fixtures/calltree/](../test/fixtures/calltree/) | Functions with fan-out at each band boundary — 2, 3, 7, 8, 10, 11, 15, and 16; a chain of known depth; a chain continuing through an unresolved indirect call; a recursive cycle | `expected-findings.tsv` — the classification of every boundary value, the deepest chain in full, and the recursion report standing in place of a depth figure | HLR-085 – HLR-090, HLR-086's exhaustive bands in particular |
| `rules/` | [test/fixtures/rules/](../test/fixtures/rules/) | A valid rule file with several named captures, supplied both from the runtime location and from the command line; a rule naming a language with no module | Each match reported with its identity as basename plus capture name, and the file and line range | HLR-107 – HLR-111 |
| `repo/` | [test/fixtures/repo/](../test/fixtures/repo/) | A repository constructed in `$BATS_TEST_TMPDIR` by `git init` with pinned identity, holding ignored, untracked, and binary files | The tracked, non-binary subset | HLR-002, HLR-003, HLR-006 |
| `traversal/` | [test/fixtures/traversal/](../test/fixtures/traversal/) | Hidden files and hidden directories; binary extensions; a self-referential directory symlink; a symlink to a file inside the tree; a symlink named directly as a target; overlapping targets naming one file twice | The analysed file set, each file exactly once | HLR-004, HLR-005, HLR-043, HLR-069, HLR-071, HLR-072 |
| `runtime/` | [test/fixtures/runtime/](../test/fixtures/runtime/) | An absent runtime directory; one with no valid module; a module missing its entry point; a module with an unparseable query; an invalid custom rule, both CLI-named and runtime-located | Expected diagnostic text and exit status per case | HLR-036, HLR-059, HLR-070, HLR-116, HLR-120 |
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
