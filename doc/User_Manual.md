# elc User Manual

**Version:** 0.4 (Phase 3)
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

**What this build does:** finds the source files in your targets, parses each
one, and reports **effective lines of code** per function, per file, and per
language — alongside each function's line range and each file's physical line
count. C is the language it ships with.

**What it does not do yet:** cyclomatic complexity, the System Dependence
Graph, and the findings derived from it. Those arrive in later phases.

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

## Effective lines of code

ELOC is the number of lines that carry an executable statement. A line counts
if it assigns or operates on data, directs control flow, calls something,
returns, or handles an exception. It does not count if it is blank, holds
nothing but a brace, declares without initialising, is a preprocessor
directive, or is a comment.

```c
int total = 0;          // counts — initialises
int scratch;            // does not — declares and nothing else
for (i = 0; i < n; i++) // counts — control flow
{                       // does not — a brace
	total += i;         // counts — an operation
}
```

That is five lines of source and three of ELOC.

### Layout does not move the number

A statement spread over several lines counts **once**, at the line it starts
on:

```c
printf("%d items in %s\n",     // one statement...
       count, name);           // ...so one line of ELOC
```

And two statements on one line also count once — that is one line to read:

```c
int a = 1; int b = 2;          // one line of ELOC, not two
```

Reformatting a file therefore cannot change its ELOC. That is the point: the
number describes the work the code does, not how it is laid out, so two
versions of the same logic are comparable and a diff of two reports shows real
change.

### A nested function's work belongs to it

Where a language allows a named function inside another, `elc` reports both —
and each statement counts for the **innermost** function containing it:

```c
int outer(void)
{
	int total = 1;              // outer

	int inner(int x)
	{
		return x * 2;           // inner, not outer
	}

	return inner(total);        // outer
}
```

`outer` is 2, `inner` is 1. An enclosing function is never credited with what
its nested functions do, so a function that merely contains others does not
look large.

### Comments cannot fool it

Comments come from the parsed syntax tree, never from matching text, so the
awkward cases are not special cases:

```c
return "/* this opens nothing */";   // a string, and it counts
/* a comment containing " a quote    // opens no string
   and // inline syntax              // is not a second comment
*/
int n = 1;   /* a trailing note */   // still a line of code
```

The last one is worth knowing: a comment at the end of a line of code does not
stop that line counting.

### Where the numbers may surprise you

**A file's ELOC is not always the sum of its functions'.** It can be higher,
when a statement sits outside every function — an initialised global counts
for the file and for no function. It can be lower, when two functions share a
line: the file counts that line once and each function counts it too.

**Code inside `#if 0` still counts.** `elc` parses; it does not run the
preprocessor, so a disabled block is ordinary source to it.

**A declaration counts only if it initialises.** `int x = f();` does work.
`int x;` reserves a name.

### The report

```
Project summary
  Files             2
  Physical lines   42
  ELOC             18
  Functions         3
  Skipped           1

Languages
  Language  Files  Lines  ELOC
  --------  -----  -----  ----
  c             2     42    18

Files
  File                  Language  Lines  ELOC  Functions
  --------------------  --------  -----  ----  ---------
  /home/u/proj/src/a.c  c            18    12          2
  /home/u/proj/src/b.c  c            24     6          1

Functions
  File                  Function    Lines  ELOC
  --------------------  ----------  -----  ----
  /home/u/proj/src/a.c  parse        5-19     9
  /home/u/proj/src/a.c  emit        21-24     3
  /home/u/proj/src/b.c  main         3-11     6

Skipped files (no language module)
  /home/u/proj/src/notes.md
```

Five sections: the project totals, those totals broken down by language, one
row per file, one row per function, and whatever was skipped. Paths are
canonical and absolute, and each column is padded to its longest value.

The `Languages` section is a partition of the totals, not a second count of
them: with one language present its row equals the summary exactly.

A function's range runs from its signature to the end of its body — where you
would point if asked where it starts. A function declared inside another is
reported in its own right, not folded into the one containing it.

Every section is printed whether or not it has anything in it. A heading with
nothing under it tells you there was nothing; a missing heading would leave
you wondering.

Files appear in ascending byte order, and functions within a file in
start-line order. That order comes from the report, not
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
empty tables, and exits 0. Silence would be indistinguishable from a crash.

## Languages

`elc` works out each file's language from its extension and loads the parser
for it on first use. Nothing about any language is built into the binary:

```sh
elc src/main.c      # .c → the C module in runtime/
```

This build ships C. C++, Rust, Python, and Ada arrive in a later phase — as
data, with no change to the executable.

### When a file has no language

A file whose extension `elc` has no module for is **skipped**, not failed:

```
elc: /home/u/proj/README.md: no usable language module; skipped
```

It is named on standard error, listed in the report's skipped section, and it
does not change the exit status. That is deliberate — a repository is full of
files that are not source, and a tool that failed on each would be useless.
The report accounts for every file it discovered, so nothing disappears
silently.

### When a language module is broken

If a module is present but unusable — its parser is missing, exposes no entry
point, or one of its query files will not compile — `elc` says so once, drops
that language, and carries on with the rest:

```
elc: c: functions.scm: no such node type in this grammar at byte 41
```

The run still exits 0: one broken language is a degraded run, not a failed
one. Only a runtime directory that yields *no* language at all is fatal, and
then `elc` stops before reading a single file rather than producing a report
covering nothing.

### When a file does not parse

A file that fails to parse is skipped whole and the run exits 1:

```
elc: /home/u/proj/src/broken.c: parse error; file skipped
```

Whole, not partly. A syntax error anywhere means the rest of the tree may be
misread, and metrics from a damaged tree look exactly like sound ones once
they are in a table. Discarding the file is the conservative choice: a
visibly skipped file is better than a quietly wrong number.

### Adding a language

Drop a grammar and six query files into `runtime/` and add one line to
`runtime/extensions.map`. No rebuild, no patch, no upstream release to wait
for. The contract a module must satisfy — the file names, the capture names,
and what each means — is `runtime/queries/README.md` in the distribution.

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

This build ships the C module. C++, Rust, Python, and Ada arrive in Phase 6.

`runtime/queries/README.md` is the contract: read it before writing a module,
and treat its file names and capture names as fixed — renaming one breaks
every grammar anyone has shipped.

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

**The `Lines` column against a file looks too big** — it is the physical line
count, blanks and comments included. `ELOC` is the column beside it.

**A file's ELOC does not equal the sum of its functions'** — expected. See
*Where the numbers may surprise you* above.

**`elc: <path>: no usable language module; skipped`** — the extension is not
in `runtime/extensions.map`, or the module for it could not be loaded. Look
above the message for a line naming the language, which says which. Exit
status is unaffected.

**`elc: <lang>: <file>.scm: ... at byte N`** — a query file will not compile
against that grammar. The language is dropped and the run continues. The byte
offset is into the query file.

**`elc: <path>: parse error; file skipped`** — the file did not parse. The run
continues and exits 1.

**Everything is skipped and the exit status is 0** — the runtime directory
was found but its modules are not loading. The first diagnostic names the
language and the reason.

**`elc: <path>: No such file or directory` naming a runtime directory, exit
2** — `ELC_RUNTIME_DIR` points somewhere that is not there, or `elc` was run
from a location with no `runtime/` beside it.

## Getting more detail

- `man elc` — the reference form of this material
- `doc/PVD.md` — why `elc` exists and what it will and will not do
- `doc/SDP.md` — the phase plan, and what each phase adds
- `doc/HLRs.md` — the requirements, if you need the precise contract

## License

MIT. See `LICENSE`.
