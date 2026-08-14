# elc User Manual

**Version:** 0.1 (Phase 0)
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

**What this build does:** parses its command line, reports usage, and returns
a meaningful exit status.

**What it does not do yet:** any analysis. Effective lines of code, cyclomatic
complexity, the System Dependence Graph, and the findings derived from it
arrive in later phases. Pointing this build at a target is accepted and
succeeds, but produces no report.

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
targets do not double-count.

### Options

| Option | Argument | Default | Effect |
| ------ | -------- | ------- | ------ |
| `-h`, `--help` | — | — | Print the usage summary to standard output and exit 0 |

Later phases add options for output format, output file, complexity
thresholds, architectural declarations, and custom rules. Each arrives
together with the behaviour it selects — an option is never added before it
does something.

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
```

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

**Nothing is printed for a valid target** — expected in this build. No
analysis stage exists yet, so a valid run succeeds silently.

## Getting more detail

- `man elc` — the reference form of this material
- `doc/PVD.md` — why `elc` exists and what it will and will not do
- `doc/SDP.md` — the phase plan, and what each phase adds
- `doc/HLRs.md` — the requirements, if you need the precise contract

## License

MIT. See `LICENSE`.
