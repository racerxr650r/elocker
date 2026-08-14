# Software Test Plan

**Version:** 0.1
**Date:** 2026-08-12
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

**Bats** (Bash Automated Testing System) drives the integration, fixture-conformance, and instrumented levels, with `bats-support` and `bats-assert`. These levels invoke `build/elc` as a user would, assert on its output and exit status, and observe its process and link line — all of which is shell work, and none of which needs to call a C function.

Both harnesses emit TAP, so `make test` produces one merged report despite the split.

Every test writes its scratch files to `$BATS_TEST_TMPDIR`, or for Criterion to a per-test temporary directory created in setup and removed in teardown. No test writes into `test/fixtures/` or anywhere in the working tree, and no test depends on the execution order of any other.

**Platform degradation.** The instrumented level rests on facilities that are not universal: `/proc`, `strace`, `unshare`, and read-only bind mounts are Linux conveniences. Where one is unavailable, the affected test **skips explicitly**, reporting through the harness both that it skipped and which requirement thereby went unverified on that platform. A silent non-run is treated as a suite failure rather than a pass, so that a CI target quietly shedding its memory-safety, single-thread, or single-parse coverage is visible rather than assumed.

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
*   The suite is not considered passing until it has passed **three times**: once as an ordinary build, once under `make asan`, and once under `make valgrind`. A change may be merged only when all three are clean.
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

Snapshot: **44 test(s)** across
**5 file(s)**.

### 3.1. [test/unit/cli.c](../test/unit/cli.c)

Role: **unit**. **11 test(s).**

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
| 11 | <a id="options_free_is_safe_on_null"></a>`options_free_is_safe_on_null` | — | Releasing a null options structure does not fault, so teardown is safe on every path. |

### 3.2. [test/integration/cli.bats](../test/integration/cli.bats)

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
| 15 | <a id="a run producing no report writes nothing to stdout"></a>`a run producing no report writes nothing to stdout` | — | Nothing but results reaches the results stream. |
| 16 | <a id="a decoy dotfile in the working directory changes nothing"></a>`a decoy dotfile in the working directory changes nothing` | — | Configuration-like files planted beside the invocation produce byte-identical output to their absence. |

### 3.3. [test/integration/docs.bats](../test/integration/docs.bats)

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

### 3.4. [test/instrumented/environment.bats](../test/instrumented/environment.bats)

Role: **instrumented**. **7 test(s).**

| # | Test | Verifies | Purpose |
| - | ---- | -------- | ------- |
| 1 | <a id="HLR-040: the binary links no interpreter or virtual machine"></a>`HLR-040: the binary links no interpreter or virtual machine` | — | The link line is checked against an allowlist; a language runtime appearing there is what the requirement forbids. |
| 2 | <a id="HLR-040: elc runs identically with no network available"></a>`HLR-040: elc runs identically with no network available` | — | Running inside an empty network namespace produces identical output. |
| 3 | <a id="HLR-041: elc links no threading library"></a>`HLR-041: elc links no threading library` | — | No threading library appears on the link line. |
| 4 | <a id="HLR-041: elc references no thread-creation symbol"></a>`HLR-041: elc references no thread-creation symbol` | — | No thread-creation symbol is referenced by the binary. |
| 5 | <a id="HLR-041: the build passes no threading flag"></a>`HLR-041: the build passes no threading flag` | — | The build never passes -pthread, which would silently license a future thread. |
| 6 | <a id="HLR-043: elc does not modify the tree it analyses"></a>`HLR-043: elc does not modify the tree it analyses` | — | The analysed tree checksums identically before and after a run. |
| 7 | <a id="HLR-043: elc runs against a read-only directory"></a>`HLR-043: elc runs against a read-only directory` | — | A run succeeds against a directory with write permission removed. |

### 3.5. [test/fixtures/smoke.bats](../test/fixtures/smoke.bats)

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
| `LLR-MAIN-16` | `main` | `HLR-125`, `HLR-036` | **(no direct test)** |
| `LLR-CLI-01` | `cli_parse` | `HLR-071`, `HLR-063` | `missing_target_is_a_usage_error`, `single_target_is_collected`, `several_targets_are_collected_in_order` |
| `LLR-CLI-02` | `cli_parse` | `HLR-027`, `HLR-028`, `HLR-054`, `HLR-029` | **(no direct test)** |
| `LLR-CLI-03` | `cli_parse` | `HLR-030` | **(no direct test)** |
| `LLR-CLI-04` | `cli_parse` | `HLR-022` | **(no direct test)** |
| `LLR-CLI-05` | `cli_parse` | `HLR-081` | **(no direct test)** |
| `LLR-CLI-06` | `cli_parse` | `HLR-103` | **(no direct test)** |
| `LLR-CLI-07` | `cli_parse` | `HLR-106`, `HLR-119` | **(no direct test)** |
| `LLR-CLI-08` | `cli_parse` | `HLR-095` | **(no direct test)** |
| `LLR-CLI-09` | `cli_parse` | `HLR-107`, `HLR-063` | **(no direct test)** |
| `LLR-CLI-10` | `cli_parse` | `HLR-055`, `HLR-122` | **(no direct test)** |
| `LLR-CLI-11` | `cli_parse` | `HLR-057` | **(no direct test)** |
| `LLR-CLI-12` | `cli_parse` | `HLR-063` | `unrecognised_option_is_a_usage_error`, `missing_target_is_a_usage_error` |
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
| `LLR-DSC-01` | `discover_targets` | `HLR-062` | **(no direct test)** |
| `LLR-DSC-02` | `discover_targets` | `HLR-062` | **(no direct test)** |
| `LLR-DSC-03` | `discover_targets` | `HLR-071`, `HLR-001`, `HLR-126` | **(no direct test)** |
| `LLR-DSC-04` | `discover_targets` | `HLR-001` | **(no direct test)** |
| `LLR-DSC-05` | `discover_targets` | `HLR-002`, `HLR-004` | **(no direct test)** |
| `LLR-DSC-06` | `discover_targets` | `HLR-069` | **(no direct test)** |
| `LLR-DSC-07` | `discover_targets` | `HLR-072` | **(no direct test)** |
| `LLR-DSC-08` | `discover_targets` | `HLR-033` | **(no direct test)** |
| `LLR-DSC-10` | `discover_targets` | `HLR-127` | **(no direct test)** |
| `LLR-DSC-09` | `discover_targets` | `HLR-035` | **(no direct test)** |
| `LLR-GIT-01` | `walk_git_tree` | `HLR-002`, `HLR-126` | **(no direct test)** |
| `LLR-GIT-04` | `walk_git_tree` | `HLR-002`, `HLR-004` | **(no direct test)** |
| `LLR-GIT-02` | `walk_git_tree` | `HLR-003` | **(no direct test)** |
| `LLR-GIT-03` | `walk_git_tree` | `HLR-003` | **(no direct test)** |
| `LLR-FTS-01` | `walk_filesystem` | `HLR-004` | **(no direct test)** |
| `LLR-FTS-02` | `walk_filesystem` | `HLR-005` | **(no direct test)** |
| `LLR-FTS-03` | `walk_filesystem` | `HLR-005` | **(no direct test)** |
| `LLR-FTS-04` | `walk_filesystem` | `HLR-069` | **(no direct test)** |
| `LLR-EXT-01` | `is_excluded_extension` | `HLR-005`, `HLR-060` | **(no direct test)** |
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
| `LLR-ANL-02` | `analyze_file` | `HLR-043` | **(no direct test)** |
| `LLR-ANL-03` | `analyze_file` | `HLR-076` | **(no direct test)** |
| `LLR-ANL-04` | `analyze_file` | `HLR-020` | **(no direct test)** |
| `LLR-ANL-05` | `analyze_file` | `HLR-013` | **(no direct test)** |
| `LLR-ANL-06` | `analyze_file` | `HLR-019` | **(no direct test)** |
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
| `LLR-RPT-01` | `report_assemble` | `HLR-024` | **(no direct test)** |
| `LLR-RPT-02` | `report_assemble` | `HLR-025` | **(no direct test)** |
| `LLR-RPT-03` | `report_assemble` | `HLR-026` | **(no direct test)** |
| `LLR-RPT-04` | `report_assemble` | `HLR-026`, `HLR-033` | **(no direct test)** |
| `LLR-RPT-05` | `report_assemble` | `HLR-021` | **(no direct test)** |
| `LLR-RPT-06` | `report_assemble` | `HLR-023`, `HLR-100` | **(no direct test)** |
| `LLR-RPT-07` | `report_assemble` | `HLR-012` | **(no direct test)** |
| `LLR-RPT-08` | `report_assemble` | `HLR-077` | **(no direct test)** |
| `LLR-RPT-09` | `report_assemble` | `HLR-115` | **(no direct test)** |
| `LLR-RPT-10` | `report_assemble` | `HLR-033`, `HLR-032` | **(no direct test)** |
| `LLR-RPT-11` | `report_assemble` | `HLR-033` | **(no direct test)** |
| `LLR-RPT-12` | `report_assemble` | `HLR-066` | **(no direct test)** |
| `LLR-RPT-13` | `report_assemble` | `HLR-006` | **(no direct test)** |
| `LLR-RPT-14` | `report_assemble` | `HLR-109` | **(no direct test)** |
| `LLR-RPT-15` | `report_assemble` | `HLR-080`, `HLR-082`, `HLR-085`, `HLR-031` | **(no direct test)** |
| `LLR-RPT-17` | `report_assemble` | `HLR-127` | **(no direct test)** |
| `LLR-RPT-16` | `report_assemble` | `HLR-124`, `HLR-125` | **(no direct test)** |
| `LLR-TBL-01` | `format_table` | `HLR-027` | **(no direct test)** |
| `LLR-TBL-02` | `format_table` | `HLR-027` | **(no direct test)** |
| `LLR-TBL-03` | `format_table` | `HLR-038` | **(no direct test)** |
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
| `traversal/` | [test/fixtures/traversal/](../test/fixtures/traversal/) | Hidden directories; binary extensions; a self-referential directory symlink; a symlink named directly as a target; overlapping targets naming one file twice | The analysed file set, each file exactly once | HLR-004, HLR-005, HLR-069, HLR-071, HLR-072 |
| `runtime/` | [test/fixtures/runtime/](../test/fixtures/runtime/) | An absent runtime directory; one with no valid module; a module missing its entry point; a module with an unparseable query; an invalid custom rule, both CLI-named and runtime-located | Expected diagnostic text and exit status per case | HLR-036, HLR-059, HLR-070, HLR-116, HLR-120 |
| `escaping/` | [test/fixtures/escaping/](../test/fixtures/escaping/) | Identifiers containing commas, quotes, ampersands, and angle brackets — C++ template signatures being the natural source | CSV parsed back to the original field count; XML and GraphML parsed without error | HLR-064, HLR-065 |
| `determinism/` | [test/fixtures/determinism/](../test/fixtures/determinism/) | A tree analysed twice; reached via differing target order; with decoy `.elcrc` and dotfiles planted in the working directory, the target, and an ancestor | Byte-identical output in every case | HLR-032, HLR-033, HLR-039 |
| `regeneration/` | [test/fixtures/regeneration/](../test/fixtures/regeneration/) | A saved XML record; the same record with an unsupported version; a malformed record; a well-formed but structurally foreign document | Markdown byte-identical to direct analysis at the same threshold; rejection with exit 2 for the rest | HLR-055 – HLR-058, HLR-061, HLR-122 |
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
| `strace` | HLR-076 — single parse | `-e trace=openat`; each source file opened exactly once per run |
| `ldd` | HLR-040, HLR-113 — dependency constraints | Link line checked against an allowlist and for the graph library |
| `unshare` | HLR-040 — no network access | `unshare -n` must produce output identical to a normal run |
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
