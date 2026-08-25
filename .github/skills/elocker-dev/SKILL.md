---
name: elocker-dev
description: "Use when writing, reviewing, debugging, or extending the elocker (`elc`) C source code — the C11 tree-sitter code-metrics and architecture-analysis CLI specified in doc/SDD.md. USE FOR: any change under src/, include/, or test/; CLI parsing, target discovery (stat/libgit2/fts), dynamic language loading (dlopen/dlsym), tree-sitter parsing, ELOC or cyclomatic-complexity calculation, comment-span merging; System Dependence Graph construction, coupling, instability, dependency cycles, call-tree depth, dead-code reachability, threshold evaluation; output formatters (table, CSV, XML, Markdown, .dot, GraphML); adding a language to runtime/; writing or fixing .scm queries; Makefile and build changes; writing or fixing Criterion or Bats tests; diagnosing segfaults, leaks, or valgrind/ASan findings; questions about memory ownership, mmap/munmap, TSTree/TSQuery/TSParser lifetimes, or dlclose ordering."
---

# elocker (`elc`) — AI Development Rules

`elc` is a POSIX-compliant **C11** command-line tool that computes
**Effective Lines of Code (ELOC)** and **Cyclomatic Complexity** per function,
builds a project-wide **System Dependence Graph** from the same parse, and
reports architectural findings against published thresholds — all using
`libtree-sitter` and runtime-loaded grammars.

The specification stack is authoritative, in this order:

| Document | Answers |
| -------- | ------- |
| [`doc/PVD.md`](../../../doc/PVD.md) | Why it exists; the tie-breaker principles; Appendix A's thresholds |
| [`doc/HLRs.md`](../../../doc/HLRs.md) | What it must do — 124 requirements |
| [`doc/SDD.md`](../../../doc/SDD.md) | **How it is structured** — read this before any non-trivial change |
| [`doc/LLRs.md`](../../../doc/LLRs.md) | The per-function contract you are implementing — 240 requirements |
| [`doc/STP.md`](../../../doc/STP.md) | How your change gets verified |

`doc/Project.xml` is the single source of truth for all five; the `.md` files
are generated from it by `tools/render_doc.py`. **Never hand-edit a generated
document.** This skill is the working guide for turning the SDD and LLRs into
correct C — it does not replace them. Where this skill and the spec disagree,
the spec wins, and the disagreement is a defect in one of them: say so rather
than silently picking a side.

## The Three Pillars (tie-breakers)

Every design decision must survive these. Cite them when rejecting an approach.

1. **Target agnosticism.** A single file, a bare directory, and a Git
   repository are all valid targets and produce the same shape of output.
2. **Dynamic extensibility.** Adding a language means dropping a `.so` and a
   query directory into `runtime/`. **Never** hard-code a language, a grammar
   symbol, or a node type in C. If you are typing `"if_statement"` into a
   `.c` file, you are in the wrong file — it belongs in a `.scm` query.
3. **AST querying only.** Metrics come from tree-sitter queries. No regex
   line-counting, no brace matching, no heuristics over raw text.

## Single-Threaded by Design

`elc` is **single-threaded** — HLR-041 requires the entire run, graph analysis
included, on one thread. `mmap` + tree-sitter is fast enough, and a pool would
buy throughput at the cost of a work queue, a lock around the language
registry, and a class of race conditions that are expensive to test. No
performance target is committed (PVD §9), so there is nothing to optimise
against.

**Do not add threads, a work queue, or `pthread_*` calls.** If a change
appears to need concurrency, that is a conversation to have, not a decision to
make in code.

Two habits are worth keeping regardless, because they cost nothing:

- **No global mutable state, no static scratch buffers.** Everything a
  function needs arrives through its arguments.

  **One exception exists, in `src/diag.c`, and it is argued rather than
  assumed** (SDD §25). The debug companion's file handle is a module-static,
  because seventy-nine functions across thirteen modules diagnose and
  threading a sink to each would put a parameter on every caller between
  `main` and a parse error. It is safe because it is *write-only* — nothing
  reads it back, so no measurement, finding or exit status can depend on it,
  which is what this rule protects — and because `elc` is single-threaded by
  requirement. Do not take it as licence for a second: a new global needs the
  same two properties and the same argument in the design document.
- Use `_r`-suffixed library variants where the choice is free (`strtok_r`).

That keeps the door open without paying for it today.

## Repository Layout

```text
elocker/
├── Makefile                 # GNU make; see "Build"
├── doc/                     # Project.xml + the five generated specs
├── include/                 # one public header per src module
├── src/                     # 16 modules, a one-way pipeline (SDD §2.1)
│   ├── main.c               # sequencing, exit status
│   ├── cli.c                # the only reader of argv
│   ├── discover.c           # stat / libgit2 / fts routing, dedup, ordering
│   ├── registry.c           # runtime location, dlopen, queries, custom rules
│   ├── analyze.c            # THE single parse: metrics + graph facts
│   ├── graph.c              # SDG construction, component projection
│   ├── arch.c               # coupling, instability, cycles, layering
│   ├── calltree.c           # fan-out, depth, deepest stack, recursion
│   ├── state.c              # globals, scopes, reachability, dead code
│   ├── thresholds.c         # threshold catalogue, severity, attribution
│   ├── report.c             # the model — and every sort (see Determinism)
│   ├── format_text.c        # table + markdown
│   ├── format_csv.c         # RFC 4180
│   ├── format_xml.c         # XML write + streaming read (regeneration)
│   ├── format_graph.c       # .dot + GraphML
│   └── elfsyms.c            # the linked image's function set   (HLR-140)
├── build/                   # objects, .d, elc, runtime symlink (gitignored)
├── runtime/
│   ├── extensions.map       # "<ext> <lang>" per line          (HLR-060)
│   ├── binary.exts          # excluded extensions              (HLR-005)
│   ├── parsers/<lang>.so    # exports tree_sitter_<lang>
│   └── queries/<lang>/
│       ├── comments.scm functions.scm complexity.scm
│       ├── eloc.scm calls.scm globals.scm
│       └── rules/*.scm      # optional custom rules            (HLR-107)
└── test/
    ├── unit/                # one Criterion binary per src module
    ├── integration/         # Bats, drives build/elc
    ├── instrumented/        # Bats: strace, /proc, ldd, unshare, sanitized
    ├── fixtures/            # hand-counted expected values, by property
    └── helpers/             # bats-support, bats-assert, shared setup
```

`src/` is a strict one-way pipeline: each stage consumes the previous stage's
output and no stage reaches backwards. **Six queries per language, not three**
— `eloc.scm`, `calls.scm`, and `globals.scm` supply the ELOC classification
and the graph facts.

**The `runtime/` adjacency requirement is real.** The binary looks for
`runtime/` beside itself unless `ELC_RUNTIME_DIR` is set, which takes
precedence (HLR-059). The Makefile creates `build/runtime` as a symlink so
`build/elc` works without an install step; tests set `ELC_RUNTIME_DIR`.

## Build

**GNU make.** Non-recursive — one top-level `Makefile`, no `make -C` into
subdirectories.

### The help text lives in the file's header

The Makefile documents itself, and `help` is the **default goal**. `make`
with no arguments prints the usage summary; `make all` builds.

The summary is a comment block at the top of the Makefile, marked `#>`, and
`make help` prints that block with the marker stripped:

```makefile
.DEFAULT_GOAL := help

#>Usage:
#>  make <target>
#>
#>Build:
#>  all             Build elc, the grammars, and the runtime symlink
#>  ...

.PHONY: help
help:
	@printf '\n'
	@sed -n 's/^#>//p' $(firstword $(MAKEFILE_LIST))
	@printf '\n'
```

**One text, two ways of reading it.** Most people meet a Makefile by opening
it, and the previous mechanism — reconstructing the list by `awk`-ing a
`## <description>` off each target declaration — gave that reader nothing: the
help existed only as a side effect of a regular expression over the source.

`#>` is the marker because it cannot occur in ordinary prose, so no comment
joins the help by accident.

**Every target you add MUST appear in the help block**, and every name in the
block must be a real target. That is not a convention anyone has to remember:
an instrumented test compares the block against the `.PHONY` declarations and
fails on either kind of drift. Internal helper targets are named with a
leading underscore, which is what excludes them.

The old `## <description>` annotations are gone. Do not reintroduce them —
two mechanisms for one text is how the two come to disagree.

### Makefile conventions

- **Tabs** for recipe indentation — a Makefile requirement, not a style
  preference.
- `?=` for every overridable variable, so it can be overridden from the
  command line or environment. `CC`, `CFLAGS`, `LDFLAGS`, `DESTDIR`, and
  `PREFIX` must all stay overridable — append in the recipes rather than
  assigning over the user's value.
- `$(VARIABLE)`, never `${VARIABLE}`.
- `@` prefix on echo/print recipes to avoid duplicated output.
- `.PHONY` declared for every non-file target, grouped consistently with the
  rest of the file.
- Do **not** remove or rename an existing target without asking first.
- Read the existing Makefile before adding to it; preserve its ordering and
  grouping, and put new targets next to related ones.

### Build rules

- Auto-generate header dependencies with `-MMD -MP` and `-include` the
  resulting `.d` files. (`.gitignore` already excludes `*.d`.)
- Objects go to `build/`, never beside sources. Mirror the `src/` tree.
- Baseline flags: `-std=c11 -Wall -Wextra -Wpedantic`.
- Feature-test macros (`_XOPEN_SOURCE=700`, `_DEFAULT_SOURCE`) are set in
  `CPPFLAGS` in the Makefile, not scattered across `.c` files.
- Discover `libgit2` and `libtree-sitter` with `pkg-config` where available,
  with an overridable fallback. Never hard-code `/usr/lib` paths.

### Required targets

Each row is also the line in the help block — keep them in sync.

| Target | Help-block description |
| ------ | ---------------------- |
| `help` (default goal) | Display this help message |
| `all` | Build elc and the runtime symlink |
| `test` | Run every test level |
| `unit` | Build and run the Criterion unit binaries |
| `integration` | Run the CLI-level Bats suites |
| `fixtures` | Run the fixture-conformance suites |
| `instrumented` | Run the environment-observing suites |
| `debug` | Build with `-O0 -g3 -DDEBUG` |
| `asan` | Rebuild with ASan and UBSan and re-run the whole suite |
| `valgrind` | Re-run integration and fixtures under valgrind |
| `spec` | Validate Project.xml and check the rendered documents are current |
| `coverage` | Fail if verification coverage has regressed |
| `clean` | Remove build artifacts |
| `install` | Install elc and runtime under `$(DESTDIR)$(PREFIX)` |

Keep `all` warning-clean; CI adds `-Werror`.

## Testing

**Criterion** is the unit harness and **Bats (Bash Automated Testing
System)** the integration harness. Every behaviour change ships with a test
in the same change.

**Integration tests** invoke `build/elc` and assert on its output:

- Use `run` and assert on `$status` and `$output`; prefer `assert_success`,
  `assert_failure`, and `assert_output` from `bats-assert`.
- Set `ELC_RUNTIME_DIR` in `setup()` to point at the repo's `runtime/`.
- Scratch files go in `$BATS_TEST_TMPDIR` — never write into `test/fixtures/`
  or the working tree, and never depend on test execution order.
- Cover all three target types (single file, plain directory, Git repo) and
  all four report formats (`table`, `-f csv`, `-f xml`, `-f md`), plus the
  `.dot` companion and the GraphML export.
- Git-repo tests must construct their repo in `$BATS_TEST_TMPDIR` with
  `git init` plus a pinned `user.name`/`user.email`, not depend on the
  checkout they are running inside.

**Unit tests** use **Criterion**, one binary per `src/` module under
`test/unit/`, linked against that module. Tests register automatically —
there is no `main` to maintain and no way to write a test that never runs:

```c
Test(analyze, merge_nested_spans) { cr_assert_eq(merge_comment_spans(&s), 4); }
```

Criterion runs each test in its own process, so a segfault in pointer-heavy
code is a reported failure rather than a dead suite. Do not hand-roll driver
executables and do not try to call C functions from shell.

**Mock with `--wrap`, and make the arm flag `volatile`:**

```c
static volatile int malloc_should_fail;   /* volatile is load-bearing */
extern void *__real_malloc(size_t);
void *__wrap_malloc(size_t n)
{ return malloc_should_fail ? NULL : __real_malloc(n); }
```

Without `volatile` the compiler removes the `armed = 1` store — it treats
malloc as a builtin that cannot read your globals, so a store overwritten
before any *visible* read looks dead. The wrapper then never fires and the
test fails for a reason that looks unrelated.

**Fixtures** carry hand-counted expected values. Every fixture needs a
comment header stating its expected numbers and the reasoning, so a change
that alters one forces a deliberate decision rather than a silent golden-file
update. Never regenerate expected values from `elc`'s own output — a fixture
that agrees with the implementation by construction asserts nothing.

**The suite must pass three times** before a change is done: ordinary,
`make asan`, and `make valgrind`. A sanitizer diagnostic or a leak is a
failure whatever the assertions did (HLR-124, HLR-125).

## Canonical Data Structures

`ElcOptions`, `LanguageModule`, `FunctionMetric`, `FileMetrics`, `FileFacts`,
`Sdg`, `Finding`, and `Report` are defined in the
[SDD Data Dictionary](../../../doc/SDD.md). **Do not redefine, reorder, or
rename fields without updating `doc/Project.xml` and re-rendering.**

Ownership and teardown order:

| Field | Owner | Released |
| ----- | ----- | -------- |
| `FileMetrics.path`, `.functions` | `FileMetrics` | file teardown |
| `FunctionMetric.name` | `FunctionMetric` | file teardown |
| `Report.*` collections | `Report` | after rendering |
| `Sdg.graph`, `.nodes`, `.symbols` | `Sdg` | after every analysis |
| `LanguageModule.queries` | registry | exit, **before** `dlclose` |
| `LanguageModule.dl_handle` | registry | exit, **last** |

Every dynamic array grows by doubling, and **the `realloc` result goes into a
temporary** that is checked before overwriting the original. Never
`x = realloc(x, …)` — a failed growth then loses the allocation and leaves a
dangling pointer, which is an HLR-125 violation ASan will find.

## Execution Pipeline

Follow the flow in [`doc/SDD.md` §2.1](../../../doc/SDD.md) stage by stage.
The condensed map:

| Stage | Does | Key calls |
| ----- | ---- | --------- |
| `cli.c` | argv → validated options | `getopt_long`, `fnmatch` |
| `discover.c` | Classify targets, build the file list | `stat`, `git_repository_open_ext`, `git_tree_walk`, `fts_open`/`fts_read`, `realpath` |
| `registry.c` | Lazy, cached grammar loading | `dlopen`, `dlerror`, `dlsym`, `ts_query_new` |
| `analyze.c` | Per file: metrics **and** graph facts, one parse | `mmap`, `ts_parser_parse_string`, `ts_query_cursor_exec` |
| `graph.c` | Cross-file resolution into the SDG | symbol table, graph library |
| `arch/calltree/state.c` | Analyses over the SDG | SCC, topological order, reachability |
| `thresholds.c` | Bands, severity, attribution | — |
| `report.c` | Assemble and **sort everything** | — |
| `format_*.c` | Render | `printf`-family |

Discovery picks exactly one traversal strategy per target: regular file → direct; directory
with a Git root → `libgit2` (which gives `.gitignore` handling for free);
directory without one → `fts(3)` with explicit binary-extension and
hidden-directory filtering. Do not run both traversals over the same tree.
Call `git_libgit2_init()` once before any libgit2 use and
`git_libgit2_shutdown()` once at exit.

Grammar loading is **lazy and cached**: look up the extension in the registry
first and return the cached module on hit. A language is loaded at most once
per process.

Renderers run over a fully assembled `Report`. They are pure consumers: no
recomputation, no re-parsing, no mutation, **and no sorting**.

**Output must be deterministic.** Neither `fts_read` nor `git_tree_walk`
guarantees a sorted order, and neither does a graph library's internal
enumeration — so **every sort lives in `report.c`** (LLR-RPT-10/11). That one
file is the audit point for HLR-032/033. The two exceptions are the graph
writers, which walk the `Sdg` directly and must therefore impose ascending
node-id and target-id order themselves (LLR-DOT-04). A determinism failure is
never a flaky test; it is a product defect.

## Resource Reuse

`TSParser` and `TSQueryCursor` allocation is expensive. Allocate **one of
each for the whole run**, reuse them across every file, and destroy them at
exit. Never allocate per file, and never per function.

*(Reuse requires only `ts_parser_set_language()` per file; `ts_parser_reset()`
is for resuming a parse cancelled by timeout or cancellation flag, which `elc`
never does.)*

## Correctness Rules That Are Easy To Get Wrong

These are the places the spec calls out as critical, plus the ones the C will
bite you on.

### Comment-span merging (ELOC)

Captured comment spans **overlap and nest** — a block comment can contain
inline-comment syntax. The required algorithm: collect all spans, **sort by
start byte, merge overlapping spans**, then subtract the merged line count
from the physical line count. Subtracting per-capture double-counts and
produces negative ELOC on nested comments — that is the canonical bug here.

A comment sharing a line with code is a judgement call the query file owns;
whatever you choose, encode it once, in one place, and test it.

### Complexity attribution

`complexity.scm` is executed **against the `@function.body` node**, not the
root. Complexity is `1 + number of captures`. Captures inside a **nested
lambda or closure belong to the enclosing function**, unless that language's
query file explicitly excludes them. Do not "fix" this in C — fix the `.scm`.

### Line numbers

`ts_node_start_point()` / `ts_node_end_point()` return `TSPoint.row` that is
**0-based**. `start_line` / `end_line` in `FunctionMetric` are what a user
reads in an editor. Convert once, at the boundary, and never twice.

### `mmap`'d buffers are not NUL-terminated

`ts_parser_parse_string()` takes an explicit `uint32_t` length — pass the
`st_size` from `fstat`, never `strlen`. Any identifier extracted from the
mapping must be `memcpy`'d into a fresh, explicitly NUL-terminated allocation
(`ts_node_*_byte` gives the range) — the name outlives the mapping.

**`mmap` of a zero-length file fails with `EINVAL`.** Short-circuit empty
files to zero metrics before mapping.

### `dlsym` and function pointers

ISO C forbids converting `void *` to a function pointer directly. Use the
POSIX-sanctioned form:

```c
const TSLanguage *(*lang_fn)(void);
*(void **)(&lang_fn) = dlsym(handle, symbol);
```

Clear `dlerror()` before the call and check it after — `dlsym` returning
`NULL` is a legal success for a NULL symbol. Build the symbol name from the
language name (`"tree_sitter_" + name`) with a bounds-checked `snprintf`.

### `dlclose` ordering

`TSQuery` and `TSTree` objects reference code and data **inside** the loaded
grammar. Tearing down in the wrong order dereferences unmapped memory, and
the crash surfaces at exit with a useless backtrace. Correct order:

```
ts_tree_delete → ts_query_delete → ts_query_cursor_delete
              → ts_parser_delete → dlclose
```

### Feature-test macros

`fts(3)` is not in POSIX. On glibc it requires `_XOPEN_SOURCE 700` **and**
`_DEFAULT_SOURCE`, set in `CPPFLAGS`. Note that `fts` is unavailable on musl
— if portability there is ever required, that is a spec-level decision, not
something to paper over in code.

## Coding Standards

- **C11**, `-std=c11 -Wall -Wextra -Wpedantic`. Treat warnings as defects.
- **Every allocation and every syscall is checked.** `malloc`, `realloc`,
  `strdup`, `open`, `mmap`, `dlopen`, `ts_query_new` — all of them fail.
- **Errors go to `stderr`, results go to `stdout`.** Never interleave. A
  parse failure on one file is a warning and the run continues; a failure to
  load the runtime directory is fatal. Return a non-zero exit status when any
  file failed.
- **`snprintf` for all path construction**, with the return value checked for
  truncation. No `strcpy`/`strcat`/`sprintf`.
- **Single exit path per function** for anything that acquires a resource —
  `goto cleanup:` is idiomatic and preferred over duplicated frees.
- Fixed-size struct fields (`extension[16]`, `language_name[32]`) are hard
  limits. Validate against them; do not silently truncate.
- Static-scope anything not in a public header. Every `src/x.c` with public
  symbols has exactly one `include/x.h`.
- `munmap` in the same function that mapped, on every path.

## Adding a Language

No C changes should be required. If a language addition needs a C change, the
extensibility pillar is broken — stop and fix that instead.

1. Build/obtain the grammar as `runtime/parsers/<name>.so` exporting
   `tree_sitter_<name>`.
2. Create `runtime/queries/<name>/` with `comments.scm`, `functions.scm`,
   and `complexity.scm`.
3. `functions.scm` must capture `@function.name` and `@function.body`.
   Those capture names are a **contract** with Phase C.
4. `complexity.scm` captures decision points only — the `1 +` base is added
   in C, so do not capture the function itself.
5. Register the extension → language-name mapping (`.py` → `python`) in the
   data-driven table, not in a branch.
6. Add a fixture under `test/fixtures/` with hand-counted ELOC and
   complexity, plus a Bats case asserting on it.

## Definition of Done

- [ ] `make all` is clean at `-Wall -Wextra -Wpedantic`, no new warnings.
- [ ] `make test` passes — both unit and integration suites.
- [ ] Any new Makefile target appears in the `#>` help block at the top of
      the Makefile, with `.PHONY` set if it is not a file.
- [ ] New or changed behaviour has a Bats test; new languages have a fixture.
- [ ] `make asan` clean.
- [ ] `valgrind --leak-check=full` clean on a single file, a directory, and a
      Git repo target.
- [ ] Every acquired resource released on **every** path, including error
      paths, in the documented teardown order.
- [ ] Output is byte-identical across repeated runs.
- [ ] No language name, file extension, or grammar node type hard-coded in C.
- [ ] No threads introduced.
- [ ] Errors on `stderr`, exit status reflects failure.
- [ ] `make asan` and `make valgrind` both clean — a diagnostic or a leak is
      a failure whatever the assertions did (HLR-124, HLR-125).
- [ ] Behaviour change is reflected in `doc/Project.xml` and the affected
      documents re-rendered; a new behaviour has an HLR, an LLR, and a test.

## Open Items — Ask, Don't Invent

The spec settles nearly everything; [`doc/notes.md`](../../../doc/notes.md)
§3 lists the judgements that were made deliberately and could reasonably go
the other way. Two are worth knowing before you touch the code they govern:

- **Component = source file** (HLR-114). Coarse for C++/Rust where
  several classes or packages share a file. Changing it is a change to
  HLR-114 alone; everything else references it.
- **Any error node skips the whole file** (HLR-035). One syntax error
  discards a 5,000-line file. `analyze.c`'s error handling is the single
  place to relax this — do not scatter tolerance elsewhere.

If a genuine gap appears, raise it rather than inventing an answer: an
undocumented behaviour has no HLR, no LLR, and therefore no test.
