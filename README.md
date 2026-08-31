# elocker (`elc`)
**Got a codebase and want to determine the scale of it, the quality of the code,
and where it can be improved?** `elc` parses your source instead of guessing at
it, and reports both: per-function metrics and whole-project architecture
analysis, for C, C++, Rust, and Python projects.

## What it does
Point `elc` at a file, a directory, or a Git repository and it will create a
a report with the following information about your source code:

**Which functions carry the code and the complexity.** Reports function name,
source file, line number, language, public/private, complexity, fan-in,
fan-out, and maintainability index.

**How the system hangs together.** By stitching the per-file syntax trees into
a project-wide **System Dependence Graph**, it answers the questions that
line counters cannot: what depends on what, where the dependency cycles are,
which components are architectural bottlenecks, how deep the call stack can
actually get, and which functions are provably unreachable.

**Which of it your build actually keeps.** Name a configuration with `-D` or a
linked image with `--elf`, and the report describes the program that ships
rather than the complete source it was built from. An image carrying debug information
answers two more questions the source cannot: which branch of an `#ifdef` you
never restated the build actually took, and where the functions a macro defines
are — an `ISR(...)` is a function to the compiler and an expression to a
grammar, and the image is what knows better. Both are reported as evidence,
counted apart from what you declared, and neither is ever required.

## Why another metrics tool

Most tools in this space report per *file* and per *language*, so a polyglot
repository needs several of them that disagree with each other, or one large
platform. `elc` is built on a few decisions that follow from that:

| Decision | Why |
| -------- | --- |
| **Per function, not per file** | A 2,000-line file tells you nothing about which of its forty functions nobody wants to touch. |
| **Parses, never guesses** | Every metric comes from a real syntax tree. Nested block comments, comment syntax inside string literals, and unusual comment forms are counted correctly — the cases where regex counters are silently wrong. |
| **One definition across every language** | The same notion of ELOC and complexity applies to C and to Python, so numbers from different parts of a repository are comparable. |
| **Deterministic output** | Identical input produces byte-identical output regardless of traversal order or filesystem. The results can be diffed, piped, estimated from, and compared between codebases or between versions of one. |
| **Architecture, not just size** | Dead code proven by graph reachability rather than guessed at by pattern matching — including the case that fools textual linters, where unused functions call one another. |
| **Measures, never lectures** | Findings are reported against *published* thresholds (MISRA C, Robert C. Martin's Instability metric, Henry–Kafura), each attributed to its source. `elc` proposes no fixes and holds no style opinions of its own. |
| **Small and self-contained** | One C11 binary, five libraries, a POSIX libc. No interpreter, no virtual machine, no network access, no plugin ecosystem, no server. |

## Adding a language costs no rebuild

Everything language-specific lives in data, never in the binary. A language is
a Tree-sitter grammar shared object plus a set of Scheme query files in a
runtime directory:

```text
runtime/
├── extensions.map          # "<ext> <lang>" per line
├── parsers/<lang>.so       # exports tree_sitter_<lang>
└── queries/<lang>/
    ├── comments.scm  functions.scm  complexity.scm
    ├── eloc.scm      calls.scm      globals.scm
    └── rules/*.scm         # your own coding standard, optional
```

No language name, file extension, or grammar node type appears anywhere in the
C source. Adding a language means adding a directory — and the same mechanism
is open to you: a team's own coding standard is expressed as `.scm` queries and
checked by the same engine that produces the built-in metrics.

**Planned initial support:** C, C++, Rust, and Python.

## What the output looks like

Reports render as an aligned table (default), CSV, XML, or GitHub-Flavored
Markdown, with a Graphviz `.dot` call tree written alongside. The XML form is a
complete record of a run, so a report can be regenerated later against a
different complexity threshold without re-analysing the source.

Here's an example of a markdown report.

> [!NOTE]
> Tables are rolled up by default to make navigating the information easier.

## Project summary

<details>
<summary>12 rows (click to expand)</summary>

| Metric         | Value |
|----------------|------: |
| Files          |    44 |
| Physical lines | 12488 |
| ELOC           |   438 |
| Functions      |    82 |
| Skipped        |     4 |
| Unparsed lines |     5 |
| Critical findings |     1 |
| Warnings       |     5 |
| Unresolved calls |    47 |
| Undecided regions |    56 |
| Files expanded |     0 |
| Measured as written |    44 |

</details>

## Findings

<details>
<summary>6 rows (click to expand)</summary>

| Severity | Measurement                | Subject                               | Detail                                                                                                                  | Source                                     |
| -------- | -------------------------- | ------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| critical | component dependency cycle | /home/john/Projects/avrOS/sys/queue.c | /home/john/Projects/avrOS/sys/queue.c -> /home/john/Projects/avrOS/sys/queue.h -> /home/john/Projects/avrOS/sys/queue.c | Martin, acyclic dependencies               |
| warning  | fan-out                    | sysInit                               | calls 13 distinct subroutines                                                                                           | Henry-Kafura                               |
| warning  | maintainability            | sysInit                               | maintainability index 61 of 100                                                                                         | elc heuristic — not a published standard |
| warning  | maintainability            | sysInitTick                           | maintainability index 64 of 100                                                                                         | elc heuristic — not a published standard |
| warning  | single-function global     | scanCycle                             | named by one function; belongs at block scope                                                                           | MISRA C Rule 8.9                           |
| warning  | single-function global     | sysTicksPending                       | named by one function; belongs at block scope                                                                           | MISRA C Rule 8.9                           |

</details>

## Callouts

<details>
<summary>2 rows (click to expand)</summary>

| What         | Value | Where                                                   |
| ------------ | ----: | ------------------------------------------------------- |
| Largest file |    96 | /home/john/Projects/avrOS/sys/fsm.c                     |
| Most complex |    10 | evntListRemove in /home/john/Projects/avrOS/sys/event.c |

</details>

## Discovery

<details>
<summary>4 rows (click to expand)</summary>

| Target                                      | Route      |
| ------------------------------------------- | ---------- |
| /home/john/Projects/avrOS/app/avrOS_example | repository |
| /home/john/Projects/avrOS/drv               | repository |
| /home/john/Projects/avrOS/srv               | repository |
| /home/john/Projects/avrOS/sys               | repository |

</details>

## Languages

<details>
<summary>1 row (click to expand)</summary>

| Language | Files | Lines | ELOC |
| -------- | ----: | ----: | ---: |
| c        |    44 | 12488 |  438 |

</details>

## Files

<details>
<summary>44 rows (click to expand)</summary>

| File                                                      | Language | Lines | ELOC | Functions |
| --------------------------------------------------------- | -------- | ----: | ---: | --------: |
| /home/john/Projects/avrOS/app/avrOS_example/avrOSConfig.h | c        |   140 |    0 |         0 |
| /home/john/Projects/avrOS/app/avrOS_example/main.c        | c        |   153 |   36 |         5 |
| /home/john/Projects/avrOS/drv/ac.h                        | c        |   336 |    0 |         0 |
| /home/john/Projects/avrOS/drv/adc.h                       | c        |   472 |    0 |         0 |
| /home/john/Projects/avrOS/drv/clk.h                       | c        |   348 |    8 |         7 |
| /home/john/Projects/avrOS/drv/cpu.c                       | c        |    99 |   18 |         2 |
| /home/john/Projects/avrOS/drv/cpu.h                       | c        |    83 |    0 |         0 |
| /home/john/Projects/avrOS/drv/dac.h                       | c        |   172 |    0 |         0 |
| /home/john/Projects/avrOS/drv/evt.h                       | c        |   206 |    0 |         0 |
| /home/john/Projects/avrOS/drv/gpio.c                      | c        |   325 |   32 |         6 |
| /home/john/Projects/avrOS/drv/gpio.h                      | c        |   218 |    0 |         0 |
| /home/john/Projects/avrOS/drv/int.h                       | c        |   259 |    0 |         0 |
| /home/john/Projects/avrOS/drv/mem.c                       | c        |   145 |   11 |         1 |
| /home/john/Projects/avrOS/drv/mem.h                       | c        |   168 |    8 |         8 |
| /home/john/Projects/avrOS/drv/nvm.h                       | c        |   227 |    0 |         0 |
| /home/john/Projects/avrOS/drv/pio.h                       | c        |   367 |   11 |        10 |
| /home/john/Projects/avrOS/drv/pmux.h                      | c        |   269 |    0 |         0 |
| /home/john/Projects/avrOS/drv/rst.h                       | c        |   118 |    0 |         0 |
| /home/john/Projects/avrOS/drv/rtc.h                       | c        |   604 |    0 |         0 |
| /home/john/Projects/avrOS/drv/slp.h                       | c        |   165 |    9 |         4 |
| /home/john/Projects/avrOS/drv/spi.h                       | c        |   319 |    0 |         0 |
| /home/john/Projects/avrOS/drv/tca.h                       | c        |   892 |    0 |         0 |
| /home/john/Projects/avrOS/drv/tcb.h                       | c        |   416 |    6 |         6 |
| /home/john/Projects/avrOS/drv/twi.h                       | c        |   643 |    0 |         0 |
| /home/john/Projects/avrOS/drv/uart.c                      | c        |   416 |   19 |         2 |
| /home/john/Projects/avrOS/drv/uart.h                      | c        |   633 |    4 |         4 |
| /home/john/Projects/avrOS/drv/vref.h                      | c        |   148 |    0 |         0 |
| /home/john/Projects/avrOS/drv/wdt.h                       | c        |   221 |    0 |         0 |
| /home/john/Projects/avrOS/drv/zcd.h                       | c        |   186 |    0 |         0 |
| /home/john/Projects/avrOS/srv/cli.c                       | c        |   396 |    5 |         0 |
| /home/john/Projects/avrOS/srv/cli.h                       | c        |   158 |    0 |         0 |
| /home/john/Projects/avrOS/srv/log.c                       | c        |    63 |    0 |         0 |
| /home/john/Projects/avrOS/srv/log.h                       | c        |   200 |    0 |         0 |
| /home/john/Projects/avrOS/srv/pcm.c                       | c        |   162 |    3 |         0 |
| /home/john/Projects/avrOS/sys/event.c                     | c        |   412 |   79 |         7 |
| /home/john/Projects/avrOS/sys/event.h                     | c        |   325 |    0 |         0 |
| /home/john/Projects/avrOS/sys/fio.h                       | c        |   150 |    0 |         0 |
| /home/john/Projects/avrOS/sys/fsm.c                       | c        |   542 |   96 |        10 |
| /home/john/Projects/avrOS/sys/fsm.h                       | c        |   378 |    0 |         0 |
| /home/john/Projects/avrOS/sys/list.h                      | c        |    52 |    0 |         0 |
| /home/john/Projects/avrOS/sys/queue.c                     | c        |   181 |   48 |         3 |
| /home/john/Projects/avrOS/sys/queue.h                     | c        |   363 |    5 |         3 |
| /home/john/Projects/avrOS/sys/sys.c                       | c        |   243 |   40 |         4 |
| /home/john/Projects/avrOS/sys/sys.h                       | c        |   115 |    0 |         0 |

</details>

## At or over a threshold (complexity listed at 15; complexity, fan-in, fan-out and maintainability banded)

<details>
<summary>2 rows (click to expand)</summary>

| File                                | Function    | Complexity | Fan-in | Fan-out | MI | Severity |
| ----------------------------------- | ----------- | ---------: | -----: | ------: | -: | -------- |
| /home/john/Projects/avrOS/sys/sys.c | sysInitTick |          2 |      1 |       7 | 64 | warning  |
| /home/john/Projects/avrOS/sys/sys.c | sysInit     |          1 |      1 |      13 | 61 | warning  |

</details>

## Linked-image filter

<details>
<summary>6 rows (click to expand)</summary>

| Property                         | Value          |
| -------------------------------- | -------------- |
| Image                            | build/main.elf |
| Unresolved linkage names         | 0              |
| ELOC outside any function        | 62             |
| Lines not compiled by this build | 339            |
| Files with no debug coverage     | 27             |
| Regions decided by this build    | 34             |

</details>

## Partially parsed files (measured except for these lines)

<details>
<summary>3 rows (click to expand)</summary>

| File                                  | Unparsed lines |
| ------------------------------------- | -------------: |
| /home/john/Projects/avrOS/drv/uart.c  |              3 |
| /home/john/Projects/avrOS/srv/cli.c   |              1 |
| /home/john/Projects/avrOS/sys/queue.c |              1 |

</details>

## Measured as written (macros not expanded)

<details>
<summary>44 rows (click to expand)</summary>

| File                                                      | Why                                |
| --------------------------------------------------------- | ---------------------------------- |
| /home/john/Projects/avrOS/app/avrOS_example/avrOSConfig.h | a condition in it is undecidable   |
| /home/john/Projects/avrOS/app/avrOS_example/main.c        | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/ac.h                        | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/adc.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/clk.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/cpu.c                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/cpu.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/dac.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/evt.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/gpio.c                      | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/gpio.h                      | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/int.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/mem.c                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/mem.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/nvm.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/pio.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/pmux.h                      | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/rst.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/rtc.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/slp.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/spi.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/tca.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/tcb.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/twi.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/uart.c                      | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/uart.h                      | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/vref.h                      | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/wdt.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/drv/zcd.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/srv/cli.c                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/srv/cli.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/srv/log.c                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/srv/log.h                       | a condition in it is undecidable   |
| /home/john/Projects/avrOS/srv/pcm.c                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/sys/event.c                     | the preprocessor rejected the file |
| /home/john/Projects/avrOS/sys/event.h                     | a condition in it is undecidable   |
| /home/john/Projects/avrOS/sys/fio.h                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/sys/fsm.c                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/sys/fsm.h                       | a condition in it is undecidable   |
| /home/john/Projects/avrOS/sys/list.h                      | a condition in it is undecidable   |
| /home/john/Projects/avrOS/sys/queue.c                     | the preprocessor rejected the file |
| /home/john/Projects/avrOS/sys/queue.h                     | the preprocessor rejected the file |
| /home/john/Projects/avrOS/sys/sys.c                       | the preprocessor rejected the file |
| /home/john/Projects/avrOS/sys/sys.h                       | a condition in it is undecidable   |

</details>

## Repaired regions (rewritten in elc's buffer to be measured; the files are untouched)

<details>
<summary>9 rows (click to expand)</summary>

| File                                               | Rule                       | Repairs |
| -------------------------------------------------- | -------------------------- | ------: |
| /home/john/Projects/avrOS/app/avrOS_example/main.c | macro as a declarator      |       1 |
| /home/john/Projects/avrOS/drv/gpio.c               | macro adjacent to a string |       1 |
| /home/john/Projects/avrOS/drv/mem.c                | macro adjacent to a string |      13 |
| /home/john/Projects/avrOS/drv/uart.c               | macro adjacent to a string |       2 |
| /home/john/Projects/avrOS/sys/event.c              | macro adjacent to a string |       2 |
| /home/john/Projects/avrOS/sys/event.c              | macro before a declaration |       3 |
| /home/john/Projects/avrOS/sys/fsm.c                | macro adjacent to a string |       5 |
| /home/john/Projects/avrOS/sys/queue.c              | macro adjacent to a string |       1 |
| /home/john/Projects/avrOS/sys/sys.c                | macro adjacent to a string |       5 |

</details>

## Standard-library dependence

<details>
<summary>5 rows (click to expand)</summary>

| File                                  | Library | Headers | Which                                        |
| ------------------------------------- | ------- | ------: | -------------------------------------------- |
| /home/john/Projects/avrOS/srv/log.h   | C       |       3 | stdio.h stddef.h stdarg.h                    |
| /home/john/Projects/avrOS/sys/event.h | C       |       5 | stdint.h wchar.h stddef.h stdbool.h stdlib.h |
| /home/john/Projects/avrOS/sys/fsm.h   | C       |       5 | stdint.h wchar.h stdbool.h stdlib.h stddef.h |
| /home/john/Projects/avrOS/sys/list.h  | C       |       3 | stdint.h wchar.h stddef.h                    |
| /home/john/Projects/avrOS/sys/sys.h   | C       |       3 | stdint.h wchar.h stdbool.h                   |

</details>

## Skipped files (no language module)

<details>
<summary>4 rows (click to expand)</summary>

| File                                                 |
| ---------------------------------------------------- |
| /home/john/Projects/avrOS/app/avrOS_example/avrOS.x  |
| /home/john/Projects/avrOS/app/avrOS_example/makefile |
| /home/john/Projects/avrOS/srv/btn.c_                 |
| /home/john/Projects/avrOS/srv/btn.h_                 |

</details>

## Nothing to report

4 tables above were empty and omitted:

- Layering (omitted: no architectural strata declared, see --stratum)
- Architecture conformance (omitted: no architectural strata declared, see --stratum)
- Cross-scope access (omitted: no execution scopes declared, see --scope)
- Conditional-compilation definitions (0)

## Contributing

The project is in its implementation phases; see [SDP.md](doc/SDP.md) for what
is being built and in what order. Because it is specified before it is built,
a behaviour change is a change to [`doc/Project.xml`](doc/Project.xml) as well
as to the source — a pull request that alters behaviour without a requirement
and a test tracing to it is incomplete by design.

## License

[MIT](LICENSE)
