# elc User Manual

**Version:** 0.2 (Phase 1)
**Applies to:** the `elc` build shipped alongside this file

This manual describes the version it ships with. Every option `elc` accepts is
documented here, and every option documented here is accepted — the
documentation test in `test/integration/docs.bats` enforces both directions,
so this file cannot silently drift from the tool.

For the terse reference form, see `man elc`.

---

## What `elc` is for

`elc` answers two kinds of question about a codebase:

- **Which functions carry the code and the complexity** — effective lines of
  code and cyclomatic complexity, reported per function rather than aggregated
  per file, where the problem function hides inside a large one.
- **How the system hangs together** — what depends on what, where the
  dependency cycles are, how deep the call chains run, and which functions are
  provably unreachable.

Numbers are produced by parsing, not by pattern matching, and the same
definitions apply to every language, so results from different parts of a
repository are comparable and results from different runs can be diffed.

## Current state — read this first

`elc` is under active development, and this manual grows with it.

**What this build does:** finds the source files in your targets and prints a
table of them with their physical line counts, plus the project totals. It
walks directories, resolves overlapping targets, and writes the report to
standard output or to a file you name.

**What it does not do yet:** parse anything. Effective lines of code,
cyclomatic complexity, the System Dependence Graph, and the findings derived
from it arrive in later phases. The `Lines` column counts physical lines —
every line in the file, blank and comment alike — which is not the same thing
as ELOC and is not meant to be.

`doc/SDP.md` lists what each phase delivers. Metrics land in Phases 3–4, the
call graph in Phase 8, and the architectural analyses in Phases 9–13.

## Installing

```sh
make all       # builds build/elc
make install   # installs under /usr/local by default
```

`make install` honours `PREFIX` and `DESTDIR`:

```sh
make install PREFIX=/opt/elc
make install DESTDIR=/tmp/staging PREFIX=/usr
```

It installs four things: the `elc` binary, the `runtime/` directory holding
language grammars and queries, the `elc(1)` man page, and this manual.

Run `make` with no arguments to see every available target.

## Running `elc`

```
elc [OPTION]... TARGET...
elc --help
```

A **target** is a source file or a directory. You may give several, in any
combination of files and directories:

```sh
elc src/                       # a directory
elc src/main.c                 # a single file
elc src/main.c src/ include/   # several, intermixed
```

A file reached through more than one target is analysed once, so overlapping
targets do not double-count: `elc src/main.c src/` reports `main.c` once, not
twice.

### What gets discovered

A target that is a regular file is analysed directly. A target that is a
directory is walked recursively, and the walk leaves out:

- **hidden files and hidden directories** — anything whose name starts with a
  dot, below the target. A hidden directory named *as* the target is walked:
  naming it is explicit.
- **files with a binary extension** — the list lives in
  `runtime/binary.exts`, one extension per line. It is data, so you can add to
  it without rebuilding.
- **anything reached through a symbolic link** — a linked directory is never
  descended into, which is what stops a self-referential link from walking for
  ever, and stops a linked directory's files being counted twice.

A symbolic link named *directly* as a target is the exception: it is resolved
and its referent analysed, because naming it says which file you mean.

Every target is checked before any of them is walked. If one is missing,
unreadable, or is something other than a file or a directory — a socket, a
FIFO, a device node — `elc` says so and stops with status 2. You never get a
report that quietly covers fewer targets than you asked for.

### The report

```
Project summary
  Files             3
  Physical lines   42

Files
  File                       Lines
  -------------------------  -----
  /home/u/proj/src/a.c          18
  /home/u/proj/src/b.c          21
  /home/u/proj/src/sub/c.c       3
```

Two tiers: the project summary, then one row per file. Paths are canonical and
absolute, and the column is padded to the longest of them.

Files appear in ascending byte order. That order comes from the report, not
from the filesystem, which is why two runs over the same tree produce
byte-identical output — and so do two runs naming the same targets in a
different order. That is what makes the output worth diffing:

```sh
elc src/ > before.txt
# ... change something ...
elc src/ > after.txt
diff before.txt after.txt
```

A target containing no files still produces the report, with zero totals and
an empty table, and exits 0. Silence would be indistinguishable from a crash.

The report has the same shape whatever the target was — a single file, a
directory, or a repository — so results from different targets are directly
comparable.

### Options

| Option | Argument | Default | Effect |
| ------ | -------- | ------- | ------ |
| `-o`, `--output` | `FILE` | standard output | Write the report to `FILE` |
| `-h`, `--help` | — | — | Print the usage summary to standard output and exit 0 |

```sh
elc --output report.txt src/     # results in the file, stdout empty
elc -o report.txt src/           # the same, short form
```

Redirecting with `--output` and redirecting with the shell produce the same
bytes; the option exists so that a caller that has no shell around it can
still separate results from diagnostics. If the file cannot be opened, `elc`
says so on standard error and exits 2 without writing a partial report.

Later phases add options for output format, complexity thresholds,
architectural declarations, and custom rules. Each arrives together with the
behaviour it selects — an option is never added before it does something.

### Exit status

The three classes are distinct so that a script can tell a **degraded run**
from a run that **never happened**:

| Status | Meaning |
| ------ | ------- |
| `0` | Every discovered file was processed, or skipped because no language module was available for it. A skipped file is not a failure. |
| `1` | The run completed and produced a report, but at least one file failed to be read or parsed. The report covers the files that succeeded. |
| `2` | The run did not complete and no report was produced: a usage error, an invalid target, a fatal runtime-location failure, or a rejected saved record. |

A finding — however severe — never changes the exit status. Findings are data;
deciding what a finding warrants is yours. If you want a build to fail on a
threshold breach, act on the report rather than expecting `elc` to exit
non-zero.

```sh
elc src/ ; echo "exit: $?"
```

### Output streams

Results go to standard output. Diagnostics go to standard error. Nothing else
is written to the results stream, so this is always safe:

```sh
elc src/ > results.txt      # results only
elc src/ 2> problems.txt    # diagnostics only
elc -o results.txt src/     # the same split, without the shell
```

This holds for `--output` too: the named file receives results and nothing
else, and every diagnostic still goes to standard error.

## Configuration

There is none, deliberately.

`elc`'s behaviour is determined entirely by its command line and the contents
of its runtime directory. It reads no configuration file and no dotfile, and
discovers nothing from the working directory, the analysis target, or any
ancestor of either. Two people running the same command on the same tree get
the same answer — which is what makes the output worth diffing and comparing.

### `ELC_RUNTIME_DIR`

The one environment variable `elc` reads. It names the directory holding
language grammars and their query files:

```sh
ELC_RUNTIME_DIR=/opt/elc/runtime elc src/
```

When unset, `elc` looks for a `runtime/` directory beside the executable. When
set, it takes precedence.

## The runtime directory

Everything language-specific lives here as data, never in the binary:

```text
runtime/
├── extensions.map          # "<ext> <lang>", one pair per line
├── binary.exts             # extensions excluded from analysis
├── parsers/<lang>.so       # Tree-sitter grammar, exports tree_sitter_<lang>
└── queries/<lang>/
    ├── comments.scm  functions.scm  complexity.scm
    ├── eloc.scm      calls.scm      globals.scm
    └── rules/*.scm         # your own coding standard, optional
```

Adding a language means adding a directory here — no rebuild, no patch, no
upstream release to wait for. The same mechanism is open to you: a team's own
coding standard is expressed as `.scm` queries and checked by the same engine
that produces the built-in metrics.

Language support is populated by Phases 2 and 6. This build ships the
structure, not yet the grammars.

## Troubleshooting

**`elc: no target given`** — `elc` needs at least one file or directory. Exit
status 2.

**`elc: unrecognised option '--foo'`** — the option does not exist in this
build. Run `elc --help` for the current list; an option documented for a later
phase is not accepted until that phase ships. Exit status 2.

**`elc: <path>: No such file or directory`** — a target does not exist. All
targets are validated before any is walked, so nothing was analysed. Exit
status 2.

**`elc: <path>: not a regular file or directory`** — the target is a socket, a
FIFO, or a device node. Exit status 2.

**A file you expected is missing from the table** — check the three exclusion
rules under *What gets discovered*: it may be hidden, carry an extension
listed in `runtime/binary.exts`, or be reachable only through a symbolic
link. Naming it directly as a target analyses it regardless of the first and
third.

**The run printed a table but exited 1** — a file inside a target could not be
read. The diagnostic naming it is on standard error, and the report covers
everything that succeeded.

**The `Lines` column looks too big** — it is the physical line count, blanks
and comments included. Effective lines of code arrives in Phase 3.

## Getting more detail

- `man elc` — the reference form of this material
- `doc/PVD.md` — why `elc` exists and what it will and will not do
- `doc/SDP.md` — the phase plan, and what each phase adds
- `doc/HLRs.md` — the requirements, if you need the precise contract

## License

MIT. See `LICENSE`.
