# elc User Manual

For the terse reference form, see `man elc`.

---

## Why elc

`elc` answers two questions about your codebase:

- **Which functions carry the code and the complexity** — effective lines of
  code and cyclomatic complexity, reported per function.
- **How the system hangs together** — what depends on what, where the
  dependency cycles are, how deep the call chains run, and which functions are
  unreachable.

The metrics are derived from parsing your code's syntax tree. 
The definitions apply uniformly across all supported languages. 
This allows you to directly compare results across different parts of a codebase and between successive runs.

## What `elc` does

It parses source files and reports two scopes of measurements:

- **Per-function metrics** — effective lines of code and cyclomatic
  complexity. It provides project totals, a per-language breakdown, and lists the most complex functions.
- **Whole-project architecture** — fan-out, fan-in, call depth, recursion, unreachable functions, dead code, file dependencies, and layering violations. These are extracted from a call graph built during the initial parse. Each measurement is checked against a threshold.

`elc` writes the report as a table, Markdown, CSV, or XML. It can rebuild a report
later from a saved XML file. It can also
draw the call graph as an annotated Graphviz diagram. It includes built-in support
for C, C++, Rust, and Python. You can
also point it at a specific build configuration using `-D`, or at a compiled
binary using `--elf`.

This manual describes the features and behavior of the `elc` application. `elc --help`
lists the available options.

### `elc` meets the standard it reports

`elc` is analysed by `elc`, and the result is part of the test suite rather than
a claim made here. Over its own `src/` the delivered binary reports no function
at or over the default complexity threshold of 15, no dependency cycle between
its own modules, no source file it cannot parse, and no call it cannot resolve
to exactly one definition.

The reason to say so is not modesty. A tool that flagged your function at 16
while its own stood at 30 would be telling you something true and giving you no
reason to act on it, and no wording fixes that. The check runs on every change,
so a release that stopped meeting it fails its own gate rather than shipping and
being discovered.

What this does **not** mean is that the thresholds are the last word. They are
published ones, cited to their sources, and `elc` names the source of each
precisely so that you can disagree with one — see *Findings: where a measurement
falls, and on whose authority*. It means only that `elc` holds itself to the
lines it draws for you.

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

A **target** is a source file or a directory. You can provide several targets in any combination:

```sh
elc src/                       # a directory
elc src/main.c                 # a single file
elc src/main.c src/ include/   # several, intermixed
```

A file reached through more than one target is analysed once. `elc src/main.c src/` reports `main.c` once.

### What gets discovered

A target that is a regular file is analysed directly. A target that is a
directory is discovered using one of two methods. `elc` indicates which method it used in its report.

#### In a Git repository

If the target directory is tracked by a Git repository, `elc` analyzes the files tracked by `git` at `HEAD`. 

- **A file you have not committed is not analyzed.** The analysis runs against the tree at `HEAD`, not your working directory.
- **Binary files are excluded by content**. A tracked blob that git reports as binary is omitted.
- **Naming a subdirectory analyzes only that subdirectory.**

Both discovery methods exclude hidden files and specific binary extensions.

#### Anywhere else

If the target is not in a Git repository, or is in one that does not track it, the directory is walked recursively. This walk excludes:

- **Hidden files and directories**. Anything starting with a period (`.`) below the target is ignored. A hidden directory explicitly named as the target *is* walked.
- **Files with a binary extension**. The list of extensions is in `runtime/binary.exts`. You can add to it.
- **Symbolic links**. Linked directories are never descended into. However, a symbolic link passed directly as a target *is* resolved and analyzed.

#### The discovery route

Every report names, for each directory target, which route was applied:

```text
Discovery
  Target  Route
  ------  ----------
  src/    repository
```

It is there so that a surprising result can be diagnosed rather than guessed
at. A count far smaller than you expected next to `repository` usually means
the files are not committed; a count far larger next to `filesystem` usually
means a build directory got swept in, and that the repository does not track
the directory you named.

Every target is checked before any of them is walked. If one is missing,
unreadable, or is something other than a file or a directory — a socket, a
FIFO (a named pipe), or a device node — `elc` says so and stops with status
2. You never get a report that quietly covers fewer targets than you asked
for.

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

Multiple statements on a single line will result in that line being counted multiple times.

```c
int a = 1; int b = 2;          // two ELOC
```

Reformatting a file therefore cannot change its ELOC count, only the physical lines it spans. That is the point: the
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

The report below is what `elc --verbose src/` prints. Without `--verbose` it
presents the *summary* tiers alone — the `Functions` table below is one of
the tiers the default omits. Which tiers belong to which is set out in
[Summary and verbose reports](#summary-and-verbose-reports).

```
Project summary
  Files               2
  Physical lines     42
  ELOC               18
  Functions           3
  Skipped             1
  Unresolved calls    4

Callouts
  What          Value  Where
  ------------  -----  ---------------------------------
  Largest file     12  /home/u/proj/src/a.c
  Most complex      7  parse in /home/u/proj/src/a.c

Discovery
  Target                Route
  --------------------  ----------
  /home/u/proj/src      repository

Languages
  Language  Files  Lines  ELOC
  --------  -----  -----  ----
  c             2     42    18

Files
  File                  Language  Lines  ELOC  Functions
  --------------------  --------  -----  ----  ---------
  /home/u/proj/src/a.c  c            18    12          2
  /home/u/proj/src/b.c  c            24     6          1

At or over a threshold (complexity listed at 5; complexity, fan-in, fan-out and maintainability banded)
  File                  Function  Complexity  Fan-in  Fan-out  MI  Severity
  --------------------  --------  ----------  ------  -------  --  --------
  /home/u/proj/src/a.c  parse              7       1        2  62

Functions
  File                     Language  Function  Visibility  Lines  ELOC  Complexity  Fan-in  Fan-out   MI
  -----------------------  --------  --------  ----------  -----  ----  ----------  ------  -------  ---
  /home/u/proj/src/a.c:5   c         parse     public         15     9           7       1        2   77
  /home/u/proj/src/a.c:21  c         emit      public          4     3           1       1        0   90
  /home/u/proj/src/b.c:3   c         main      public          9     6           2       0        1   83

Skipped files (no language module)
  /home/u/proj/src/notes.md

Nothing to report
  4 tables above were empty and omitted:
    - Component dependency cycles
    - Recursion
    - Deepest call chain (omitted: no entry points declared, see --entry)
    - Layering (omitted: no architectural strata declared, see --stratum)
```

That example is **abridged**: it shows only the sections covered so far in
this manual — the project totals, the callouts, the discovery route applied
to each directory target, those totals broken down by language, one row per
file, the functions a threshold named, one row per function, and whatever was
skipped. A real run also prints the sections covered later in this manual, all
derived from the call graph: recursion, the deepest call chain, coupling,
dependency cycles, layering, global state, dead code, cross-scope access, the
findings, the configuration in force, and any custom-rule matches. Paths are
canonical and absolute (the full path from the filesystem root, with no `..`
or symlinks left in it), and each column is padded to its longest value.

**Three things about the shape of that report are worth noticing now.**

The **findings** come first — right after the project summary, before every
table that supplies their evidence. The example above has none, so the section
is absent; a run with findings puts them second.

**A table with no rows is not printed.** Instead the report ends with a
`Nothing to report` statement naming every one that was empty, by its full
heading — which is how a section omitted for want of a `--stratum` or
`--entry` declaration still tells you why. The statement is there whether or
not anything was empty.

**There is one per-function table.** Its last two columns are the function's
fan-in and fan-out, explained under
[Fan-out and fan-in](#fan-out-and-fan-in). Earlier releases split the same
functions across three tables.

`Discovery` has a row per *directory* target only; a file named directly is
analysed with no traversal, so there is no route to report for it.

**What each summary row counts.** The rows are what the whole report is
summarising, so they are worth reading precisely:

| Row | Counts |
| --- | ------ |
| `Files` | Source files analysed. A skipped file is not among them |
| `Physical lines` | Every line in those files, blanks and comments included |
| `ELOC` | Effective lines of code, as defined above |
| `Functions` | Functions reported, nested named functions counted in their own right |
| `Skipped` | Files discovered but not analysed, for want of a language module |
| `Unparsed lines` | Lines the grammar could not follow; every other figure covers the rest of the file — see [When the parser cannot follow your code](#when-the-parser-cannot-follow-your-code) |
| `Critical findings`, `Warnings` | How many rows the **Findings** section holds at each severity. Neither reaches the exit status |
| `Unresolved calls` | Call sites with no definition in what you analysed — a measure of how complete the graph is, not a defect |
| `Undecided regions` | Conditional regions left counted because their condition could not be decided from the `-D` symbols you gave — nor, on a filtered run, from what the image shows the build compiled |

Two of those state the *completeness* of a measurement rather than measuring
anything: `Unresolved calls` and `Undecided regions`. They are printed beside
the figures they qualify because a number whose accuracy is unstated cannot be
acted on.

The `Languages` section splits the project totals by language; it is not a
second, separate count. With only one language present, its row matches the
project summary exactly.

A function's range runs from its signature to the end of its body — where you
would point if asked where it starts. A function declared inside another is
reported in its own right, not folded into the one containing it.

Every section is printed whether or not it has anything in it. A heading with
nothing under it tells you there was nothing; a missing heading would leave
you wondering.

Files appear in ascending byte order (alphabetical order, essentially), and
functions within a file appear in the order their definitions start. This
ordering is fixed by `elc` itself, not by the filesystem or the order you
named the targets in. As a result, two runs over the same tree always
produce byte-identical output, even if you list the same targets in a
different order. That is what makes the output worth diffing:

```sh
elc src/ > before.txt
# ... change something ...
elc src/ > after.txt
diff before.txt after.txt
```

A target containing no files still produces the report, with zero totals and
empty tables, and exits 0. Silence would be indistinguishable from a crash.

## Cyclomatic complexity

Cyclomatic complexity is a widely used measure of how tangled a function's
control flow is — roughly, how many independent paths a test suite would
need to exercise every branch at least once. `elc` computes it as **one
plus** the number of decision points in a function. A function that never
branches is 1.

```c
int simple(int n)          // complexity 1 — one path
{
	return n;
}

int branchy(int a, int b)  // complexity 4
{
	if (a && b)            // the if is one, the && is another
		return 1;
	while (a--)            // and the loop is a third
		b++;
	return b;
}
```

A decision point is a place execution can go more than one way: an `if`, a
loop, a `case` label, a conditional expression `? :`, and each `&&` or `||`.
The short-circuit operators count because each is a second place the
condition can be decided — without them, a function built from one long
compound condition would score the same as one with no condition at all.

Three things look like decisions and are not:

| Construct | Why not |
| --------- | ------- |
| a bare `else` | the branch was already counted at the `if` that owns it |
| `default:` | where control goes when no branch was taken |
| `goto` | moves control without choosing; the choice is in the `if` guarding it |

An `else if` counts once — for the `if` inside it.

As with ELOC, a nested named function owns its own decision points and the
function containing it gains none of them.

### What counts as a decision differs by language

The table above is C. Each language's `complexity.scm` decides what a decision
point is for that language, because the constructs genuinely differ:

| Language | Counted, beyond the common `if`, loop, and short-circuit operators |
| -------- | ---------------------------------------------------- |
| C | `case` labels, the `? :` conditional, `do` and `for` loops |
| C++ | the same, plus each `catch` clause and the range-based `for` |
| Rust | each `match` **arm**, the `?` operator, and `loop` |
| Python | each `elif` and `except` clause, `match` cases, and the `if` and `for` clauses *inside* a comprehension |

Two consequences follow. A Rust `match` scores one per arm, so it
behaves like a `switch` whose arms each end in a `case` rather than like a
single branch. And a Python comprehension carrying a filter is a decision
point, so a densely written comprehension scores where the loop it replaces
would have scored too.

The authoritative list for any language is its `complexity.scm` under
`runtime/queries/`, which is data you can read — and, if you disagree with it,
change without rebuilding `elc`.

## The complexity threshold

`elc` lists the functions whose complexity is **at or above** a threshold:

```sh
elc src/                # the default, 15
elc -c 10 src/          # list anything 10 or greater
elc -c 1 src/           # list everything
```

```
At or over a threshold (complexity listed at 10; complexity, fan-in, fan-out and maintainability banded)
  File                  Function  Complexity  Fan-in  Fan-out  MI  Severity
  --------------------  --------  ----------  ------  -------  --  --------
  /home/u/proj/src/a.c  parse             17       2        4  58  critical
```

That table holds two kinds of row. A function is listed because its complexity
met the threshold you set — or because one of its complexity, fan-in and
fan-out fell in a warning or critical band, whatever the threshold. The
**Severity** column is the higher of the bands that named it, and is blank for
a function the threshold alone put there. The bands are in
[The bands](#the-bands).

**The threshold changes what is listed and nothing else.** Not a total, not a
callout, not a severity, and — this is the part worth being clear about —
**not the exit status**:

```sh
elc -c 1 src/ ; echo $?     # lists everything, exits 0
elc -c 999 src/ ; echo $?   # lists only what a band named, exits 0
```

Findings are data. Deciding what a number warrants is yours, not `elc`'s. If
you want a build to fail when something crosses a line, act on the report —
`elc` will not do it for you, and a tool that did would make the number
political rather than descriptive.

## Callouts

The report names two things across the whole run:

```
Callouts
  What          Value  Where
  ------------  -----  ---------------------------------
  Largest file     12  /home/u/proj/src/a.c
  Most complex      7  parse in /home/u/proj/src/a.c
```

When two files or two functions tie, the one that sorts first in the report
wins — so the callout is the same on every run, and a diff of two reports
shows real change rather than a coin toss.

## Output formats

There are two ways to name a format, and which one you use depends on where
the report is going.

**Writing to a file, the extension names it.** Nothing else is needed:

```sh
elc -o report.txt src/   # an aligned table
elc -o report.md src/    # GitHub-Flavored Markdown
elc -o report.csv src/   # one record per function, RFC 4180
elc -o report.xml src/   # the complete record of the run
```

**Writing to standard output, `-f` names it.** Standard output has no
filename and so no extension, which is what keeps a machine-readable format
available to a caller that pipes rather than redirects:

```sh
elc src/                 # an aligned table, the default
elc -f md src/           # GitHub-Flavored Markdown
elc -f csv src/ | ...    # one record per function, RFC 4180
elc -f xml src/          # the complete record of the run
```

| Format | Extension | For | Notes |
| ------ | --------- | --- | ----- |
| `table` | `.txt` | reading | The default on standard output |
| `md` | `.md` | a pull request, a wiki | Same sections as the table, in the same order; each table folded behind a click-to-expand |
| `csv` | `.csv` | a spreadsheet, another tool | Complete dataset; the threshold does not filter it. The same columns the Functions table carries |
| `xml` | `.xml` | keeping | Complete record; what `--from-xml` reads back |
| `html` | `.html` | looking at the shape of it | One page drawing the graph as layers holding files holding functions, opened collapsed. Presents its information in the context of the drawing rather than as the same tiers; not available from `--from-xml` |

### The CSV record is the Functions table, loaded rather than read

They carry the same columns, in the same order:

```console
$ elc -f csv src/ | head -3
file,language,function,visibility,lines,eloc,complexity,fan_in,fan_out,mi
/home/you/src/measure.c:12,c,measure,public,9,4,2,3,1,84
/home/you/src/measure.c:24,c,scale,private,6,3,1,1,0,91
```

`file` is `path:line` — the function's first line, in the navigable form the
report prints — and `lines` is the number of lines the function occupies
rather than a range. An unknown visibility is the empty field and never
`public`: a language whose module supplies no visibility rule has not been
asked, and that is a different claim from having answered.

**If you are reading a CSV written by elc 0.29 or earlier**, the shape was
`file,language,function,start_line,end_line,eloc,complexity`. To move: take the
start line from the `file` field, and read `lines` where you computed
`end_line - start_line`. Every other field you read is still there, under the
same name and in the same relative order. The `xml` record is unchanged and
still carries a separate start and end line.

### The Markdown report folds its tables away

A verbose report over a real project runs to hundreds of rows, and on GitHub
that is a page nobody scrolls. So every table in the `md` format sits inside
an HTML `<details>` element, and its `<summary>` says how many rows are
behind it:

```markdown
## Functions

<details>
<summary>639 rows (click to expand)</summary>

| File                    | Language | Function | Visibility | Lines | ELOC | Complexity | Fan-in | Fan-out | MI |
| ----------------------- | -------- | -------- | ---------- | ----: | ---: | ---------: | -----: | ------: | -: |
| /home/u/proj/src/a.c:21 | c        | parse    | public     |    50 |   31 |          9 |      3 |       7 | 62 |

</details>
```

**The heading stays a heading.** It is what a renderer derives a section
anchor from, so a link to `#functions` still resolves and a generated table of
contents still lists the section. That is why the summary states the row count
rather than repeating the name above it — the count is the one thing the
heading does not already tell you, and it is what you want when deciding
whether to expand.

The aligned table has no disclosure to offer and gains none, and `csv` and
`xml` are parsed by their consumers rather than read, so neither carries any
HTML.

**An extension `elc` does not recognise is an error, not a guess.**

```sh
$ elc -o report.json src/
elc: '.json' is not a report format extension; expected .txt, .md, .csv, or .xml
```

Guessing would write one format under a name promising another, and quietly
defaulting to the table would leave you with a `report.json` holding no JSON.
A filename with no extension at all is rejected for the same reason.

**Naming the format twice is fine; naming it two different ways is not.**
`-f md -o report.md` is accepted, because nothing is ambiguous about saying a
thing twice. `-f csv -o report.md` is a usage error naming both — honouring
either one would leave your own command line disagreeing with the file it
produced:

```sh
$ elc -f csv -o report.md src/
elc: --format csv and an output file named 'report.md' disagree; the extension
already names the format, so name it once or name it the same twice
```

The extension picks the format and nothing else. The companion artefacts put
their own extension on the same path, so `-o report.md` still gives you
`report.dot`, `report.graphml`, and `report.dsm.csv`.

**`table` and `md` are the same report,** built by walking the same
underlying data once. A section cannot appear in one and be missing from the
other — only the formatting differs.

**`csv` and `xml` are unfiltered.** The complexity threshold only affects the
*listing* section of a human-readable report. CSV has no such section — it is
the complete, unfiltered dataset, and filtering is left to whatever tool you
pipe it into:

```sh
elc -f csv src/ | cut -d, -f3,7      # function names and complexities
```

CSV carries per-function metrics only. The architectural findings are excluded
by design: they are not expressible as one flat record set, which is what XML
is for.

Every field containing a comma, a quotation mark, or a line break is quoted,
so a name like `foo<int, long>` stays one field.

## Summary and verbose reports

A report comes at one of two verbosities. The default is the **summary**;
`-v` / `--verbose` gives you the **verbose** report, which is everything the
summary has plus the detail tiers it leaves out.

```sh
elc src/                 # the summary — what fits in a terminal
elc --verbose src/       # every tier, as elc printed before the summary existed
```

The default changed deliberately. The full report outgrew the length at which
it can be read in a terminal, and a default nobody reads is a default that
serves nobody. Nothing is lost: `--verbose` restores it exactly, and the two
complete formats are untouched.

### Which tiers are which

The rule is by *tier*, so the partition is a property of the report rather
than of how it happened to be printed. A tier presenting a project-level
aggregate, a file's own totals, or a finding you are expected to act on is a
summary tier. A tier enumerating one row per analysed entity — per function,
per global object, per unreachable statement, per graph edge, per custom-rule
match — is a detail tier.

| Tier | Summary | Verbose |
| ---- | ------- | ------- |
| Project summary, Findings | ✅ | ✅ |
| Callouts | ✅ | ✅ |
| Discovery | ✅ | ✅ |
| Languages | ✅ | ✅ |
| Files | ✅ | ✅ |
| At or over a threshold | ✅ | ✅ |
| An analysis omitted for want of a declaration, with its reason | ✅ | ✅ |
| Architecture conformance | ✅ | ✅ |
| Conditional-compilation definitions | ✅ | ✅ |
| Linked-image filter | ✅ | ✅ |
| Partially parsed files | ✅ | ✅ |
| Skipped files | ✅ | ✅ |
| Functions | — | ✅ |
| Recursion, Deepest call chain | — | ✅ |
| Component coupling, Component dependency cycles, Layering | — | ✅ |
| Dependency structure matrix | — | ✅ |
| Graph purification | — | ✅ |
| Global state, Unreachable globals | — | ✅ |
| Unreachable functions, Dead code within functions | — | ✅ |
| Cross-scope access | — | ✅ |
| Custom rule matches | — | ✅ |
| Functions the image places that the parse did not reach | — | ✅ |
| Functions the image does not define (**last**) | — | ✅ |

**The last row is last on purpose.** The functions a linked image does not
define is the longest table a filtered run produces, and it answers a question
you ask *after* reading the report rather than one you read the report to
answer — so it closes the report. The image itself stays in the summary, where
you meet it before the figures it qualifies.

**A tier reached but empty is named, not printed.** A table with no rows is
not printed at all, and the report closes with a `Nothing to report`
statement listing every one that was empty — by its full heading, so an
omitted analysis still states its reason there. A tier a summary run filters
out appears in neither place, which is how you tell "this run found nothing"
from "this verbosity did not look".

**The summary keeps the findings, and puts them first.** That is the point of
it. Every architectural measurement that crossed a published line becomes a
finding,
and the findings are in the summary — so the summary tells you what to act
on, and `--verbose` tells you every number behind it. A summary that dropped
the one section you act on would be shorter and useless.

**An omitted analysis still says so.** The sections carrying those notices
are detail tiers, but the notice itself is not: if you did not declare an
entry point, the summary still tells you that reachability was not measured,
rather than leaving an absence you might read as a clean bill of health.

```
Unreachable functions (omitted: no entry points declared, see --entry)
  File  Function  Line
  ----  --------  ----
```

### Verbosity changes presentation and nothing else

No measurement, no finding, no severity, and not the exit status. A value
absent from a summary is absent because it was not printed, never because it
was not computed.

`csv` and `xml` are defined as *complete* — one record per function, and
every element of a run — so there is no presentation for a verbosity to
select between. Both are byte-identical whichever way you ask:

```sh
elc -f xml src/ > a.xml
elc --verbose -f xml src/ > b.xml
diff a.xml b.xml            # no output
```

Asking for `--verbose` alongside `-f xml` or `-f csv` is **accepted**, not
rejected. Every other conflicting pair of options `elc` defines is a usage
error, so this one is worth stating: there is nothing contradictory about
asking a format that is already complete for more detail, and the request
simply has no effect.

Because the record is complete whatever the verbosity, one saved record
answers both questions, and answers each one identically to a direct run:

```sh
elc -f xml -o build/metrics.xml src/
elc --from-xml build/metrics.xml              # summary Markdown
elc --from-xml build/metrics.xml --verbose    # verbose Markdown
```

## Keeping a record

`-f xml` writes a record that is sufficient on its own to produce a report
later, without the source it describes:

```sh
elc -f xml -o build/metrics.xml src/     # today
elc --from-xml build/metrics.xml         # any time after
```

The regenerated Markdown is **byte-identical** to what a direct analysis
would have produced at the same threshold — not merely similar, but
identical byte for byte, so a diff between the two produces no output.

That is achievable because the record carries the *measurements* and nothing
derived from them. The totals, the callouts, the ordering, and the threshold
listing are all worked out when the report is assembled, by the same code on
both paths. There is no second implementation to drift.

The saved record also includes which discovery route was used — Git
repository or plain filesystem walk — for each target. That is something
the original run observed and cannot be recovered later: by the time you
regenerate the report, the tree may no longer be a Git repository, or may
not exist at all. So a regenerated report still tells you how its files
were originally found.

### The threshold is not in the record

The record stores what was measured; the threshold is what you decided about
it. So one record answers as many questions as you care to ask:

```sh
elc --from-xml build/metrics.xml -c 10   # what was over 10?
elc --from-xml build/metrics.xml -c 20   # and over 20?
```

Neither re-reads a source file. This is the point of keeping the record: the
tree may have moved on, and the questions may not have been thought of yet.

### What is rejected

A record that is not well-formed XML, that does not match `elc`'s own
structure, or that carries a format version this build does not read, is
rejected with exit status 2 and **no output at all**:

```
elc: build/metrics.xml: format version 7 is not supported; this build reads version 1
```

No best-effort partial conversion is attempted. A half-reconstructed report
looks exactly like a complete one once it is rendered, and you would have no
way to tell.

### What regeneration does not do

It takes no `TARGET`, and produces Markdown alone. A saved record carries the
findings of a run rather than the graph behind them, so asking for another
format is a usage error rather than a request quietly ignored.

## The dependence graph

`elc` builds a **System Dependence Graph**: a map of your whole project in
which each function is a point (a *node*), and each call from one function to
another is an arrow (an *edge*) joining them. Every analysis in the rest of
this section — fan-out, call depth, dead code, coupling, dependency cycles —
is really a different question asked of that one map.

Nothing is re-read to build it. The graph comes from the same single parse
that produced the per-function metrics, which is why analysing a project
costs one pass over each file, however many questions are asked of the
result.

### What is in it

**Nodes** are functions — the same ones the report lists.

**Call edges** join a caller to a callee, across files. Two calls to the same
helper are **one edge** carrying a count of two, not two edges. That is what
makes fan-out the number of distinct subroutines a function invokes: a
function calling one helper in a loop and again in its error path is coupled
to one thing, not two.

**Global edges** join a function that writes a global to every function that
reads it. Two functions can be tightly coupled without either naming the
other, and this is how that shows up. The two kinds of edge are kept separate
and never merged.

### Unresolved calls

The project summary reports a count:

```text
Project summary
  Files               3
  ...
  Unresolved calls    2
```

A call is unresolved when `elc` cannot find a definition for it in what you
asked it to analyse — a call into a library, a system call, an indirect call
through a pointer. **This is not an error and not a defect in your code.** A
project that calls `printf` has unresolved calls by definition.

It is reported because it tells you how complete the graph is. If you analyse
one directory of a larger project, most calls leave it, the count is high,
and the fan-out figures describe that directory rather than the program. If
you expect a small number and see a large one, you probably scoped the run
more narrowly than you meant to.

`elc` never guesses at a destination. An edge that does not exist would be
worse than a missing one: the [dead-code analysis](#dead-code-between-functions)
proves that nothing calls a function, and one invented edge would make that
proof wrong.

### Fan-out and fan-in

Every per-function figure `elc` measures is in one table — the **Functions**
table introduced in [The report](#the-report) — and its last two columns are
the function's two degrees:

```text
Functions
  File                     Language  Function  Visibility  Lines  ELOC  Complexity  Fan-in  Fan-out   MI
  -----------------------  --------  --------  ----------  -----  ----  ----------  ------  -------  ---
  /home/u/proj/src/a.c:5   c         main      public         15    12           3       0        4   84
  /home/u/proj/src/a.c:21  c         parse     public         50    31           9       3        7   62
  /home/u/proj/src/a.c:72  c         chomp     public          7     4           1       6        0   93
```

**Fan-out** is the number of *distinct subroutines a function invokes*.
Distinct, not call sites: a function that calls one helper in a loop body and
again in its error path is coupled to *one* thing, and reports a fan-out of 1.

**Fan-in** is the converse — how many *distinct functions call it* — and is
counted the same way. A caller that invokes `parse` at forty call sites
contributes **one**, and a function that merely *reads* a global `parse`
writes contributes **nothing**: that is coupling, and coupling is not a call.

A degree of 0 is a measurement and not a complaint. `main` above calls four
things and nothing calls it, which is what an entry point looks like; `chomp`
is called from six places and calls nothing, which is what a leaf looks like.
An exported API function and an interrupt handler reached from a vector table
also legitimately have no callers. Whether an absence of callers means the
function is *dead* is a different question, answered in
[Dead code between functions](#dead-code-between-functions).

`elc` reports both degrees for every function whether or not they are high
enough to flag. What counts as too many is covered in
[Findings](#findings-where-a-measurement-falls-and-on-whose-authority) below:
for fan-out, 10 or fewer draws no comment, 11 to 15 is a warning, and above 15
is critical; for fan-in, above 25 is a warning and there is no critical band.
A function's rating, where it has one, appears in the **Findings** section and
in the threshold listing rather than here — this table only measures.

> **The fan-in band is `elc`'s own.** No published source divides fan-in into
> accepted and unaccepted ranges, so the line at 25 is this project's
> judgement, and the finding says so where you read it. It is one of exactly
> two thresholds `elc` invented; the other is the
> [bottleneck](#component-coupling) heuristic.

One caveat specific to fan-in. Calls resolve by name, so where several files
define a `static` helper of the same name, every call resolves into one of
them: that definition collects every caller's fan-in and the others collect
none. Since fan-in is banded, an error of that shape can put a function over
the line or hide one that is. `elc` diagnoses duplicate definitions on
standard error; read the two together.

### The Adapted Maintainability Index

The last column of the Functions table is a single score out of a hundred,
combining everything else on the row:

```text
IF  = (Fan-In × Fan-Out)²
MI  = 171 − 5.2 ln(IF + 1) − 0.23 v(G) − 16.2 ln(ELOC)
MI′ = max(0, MI ÷ 171 × 100)
```

It is Coleman and Oman's Maintainability Index with one substitution: their
third term is the logarithm of a function's Halstead Volume, and `elc` uses
the information flow through it instead. That is what makes the score fall
when a function is *entangled* and not only when it is long or branchy — a
short function that forty things call and that calls twenty more is hard to
change, and no measure of its size says so.

Two details keep the arithmetic defined. **One is added to the information
flow** before the logarithm, so a function at either end of the call graph
scores on length and branching alone rather than on an infinity — an entry
point is not coupled by being an entry point. **A function with no effective
lines is taken as having one**, for the same reason: it has nothing to
maintain, so it sits at the top of the scale.

| Score | Meaning |
| ----- | ------- |
| 65–100 | No finding. |
| below 65 | **Warning** — moderate structural risk. |
| below 55 | **Critical** — a rigid, fragile monolith. |

> **These thresholds are `elc`'s own**, and it is the third of the three that
> are. The index is published and so are thresholds for it — the Software
> Engineering Institute's 85 and 65 — but those were calibrated against the
> Halstead term this adaptation replaces. Dropping it removes thirty to
> forty-five points of range, and the normalisation rescales what is left;
> carried across unchanged, the published numbers flag four functions in five.
> **A citation is not transitive.** Adapting a metric does not inherit the
> thresholds drawn for the original, so `elc` draws its own and says so.

This is the only measurement in `elc` where the **low** value is the bad one.
Everywhere else a finding means a number got too big.

> **One caveat, and it is the metric's rather than your code's.** The
> information-flow term squares the product of the two degrees, so a small,
> simple helper that many functions call scores badly *for being widely
> shared*. `elc`'s own `diag_printf` is ten effective lines with a complexity
> of two and scores 51, because seventy-nine functions call it — which is
> good factoring, not a fragile monolith.
>
> A related surprise: routing calls through one of your own functions rather
> than a library one *lowers* the scores of every caller, because a library
> call cannot be resolved into the graph and yours can. Centralising something
> can therefore make the number worse while making the code simpler.
>
> Read a low score as *a question worth asking*, not a verdict. That is why
> the finding says where the score fell and stops.

And as with every other finding, the row says where the score fell and who
drew the line. It does not tell you to refactor anything — what a low score
warrants is your call, and a metric whose name sounds like a verdict is the
last place `elc` would start giving advice.

> **The Henry–Kafura value is gone.** Earlier releases reported
> `HK = ELOC × (Fan-In × Fan-Out)²` per function and summed across the
> project, in a third table beside this one. No published source bands the
> figure, so `elc` reported it with no severity — and in practice a
> four-order-of-magnitude number with no threshold got read as a score
> anyway. The two degrees it was formed from are here, beside the ELOC and
> the complexity it was formed with, and each is banded on a stated
> authority. If you want the value, the report and the GraphML export both
> carry all three of its inputs.

### Recursion

```text
Recursion
  Kind    Functions
  ------  -----------
  direct  fact
  mutual  ping, pong
```

Direct recursion is a function that calls itself; mutual recursion is a group
of functions that call each other in a cycle. Both are found the same way,
and both matter for the same reason: recursion makes the worst-case stack
depth unpredictable, which on a target with only a few kilobytes of stack is
a crash waiting for the wrong input. This is why **MISRA C Rule 17.2** — part
of a widely used coding standard for safety-critical C — forbids recursion
outright.

The **Functions** column is a set, not a path. `elc` can tell you which
functions are mutually recursive; it does not claim a particular cycle through
them, because the analysis that finds them does not produce one. Breaking any
call among the listed functions breaks the recursion.

### The deepest call chain

```text
Deepest call chain (4 layers; a lower bound, 3 calls unresolved)
  Step  File                  Function
  ----  --------------------  --------
  1     /home/u/proj/src/a.c  main
  2     /home/u/proj/src/a.c  parse
  3     /home/u/proj/src/b.c  lex
  4     /home/u/proj/src/b.c  next_token
```

The chain itself, not just its length — because knowing the depth is 4 tells
you nothing about which path to shorten.

**This needs entry points, and `elc` will not guess at them:**

```sh
elc --entry main --entry timer_isr --entry usb_rx src/
```

Repeat `--entry` for each. There is no default, and none is inferred: `main`
is the right answer for an application, the wrong answer for a library, and
badly wrong for firmware whose interrupt handlers are reached from a vector
table and called by nothing.

Four things can appear in that heading, and each says exactly what happened:

| Heading | Meaning |
| ------- | ------- |
| `(N layers; a lower bound, M calls unresolved)` | measured |
| `(unbounded: the call graph is recursive)` | recursion makes depth infinite; see the Recursion section |
| `(omitted: no entry points declared, see --entry)` | you did not say where execution starts |
| `(omitted: no declared entry point matches an analysed function)` | you did, but the named functions are not in what you analysed |

**Why it is a lower bound.** A chain that continues through a call `elc` could
not resolve is not followed, so the true worst case may be deeper. That is why
the unresolved count sits in the heading: a depth of 4 with 0 unresolved calls
is a measurement, and a depth of 4 with 300 unresolved calls is a lower bound
you should not rely on.

**Why recursion gives no number.** On a cyclic call graph the longest path has
no finite answer. `elc` reports the cycle instead of a number, rather than
picking some finite value that would be wrong, or looping forever trying to
find one.

### Component coupling

```text
Component coupling (I = Ce/(Ce+Ca), Martin; bottleneck at Ca and Ce >= 5)
  Component             Ca  Ce  Instability  Finding
  --------------------  --  --  -----------  -------
  /home/u/proj/app.c     1   2         0.67
  /home/u/proj/drv.c     2   0         0.00
  /home/u/proj/util.c    0   0    undefined
```

A **component is a source file**, and that is the unit of measurement for
this section and the two after it. A dependency runs from file X to file Y
when any function in X calls any function in Y, or writes a global variable
that a function in Y reads.

`Ca` (**afferent coupling**, or "fan-in") counts how many other components
depend on this one. `Ce` (**efferent coupling**, or "fan-out" at the file
level) counts how many other components this one depends on. Both count
**components, not calls**: a file that calls another in forty different
places still depends on it once.

**Instability**, `I = Ce / (Ce + Ca)`, is a score from 0 to 1 that captures
how risky a file is to change. A value near 0 means many other files depend
on this one and it depends on few of them in return — it is *stable*, in
the sense that a change here is likely to ripple outward and break
something. A value near 1 means the opposite: little to nothing depends on
it, so it is free to change without wider consequences.

**Where both couplings are zero it is `undefined`, not `0.00`.** A file nothing
depends on that depends on nothing is entirely ordinary — a lone file in a
single-file target is exactly that — and reporting zero there would claim
maximum stability for a component that has no relationships at all.

A component whose `Ca` **and** `Ce` are each at or above the bottleneck
threshold is flagged: it is simultaneously depended upon widely and dependent
on much else, so it is both dangerous to change and hard to isolate for
testing. Both conditions have to hold, not just one — a file that is widely
used but depends on little itself is stable, not a bottleneck.

```sh
elc -b 3 src/     # lower the bar from the default 5
```

**This threshold is `elc`'s own heuristic and says so on every row it flags.**
It is one of exactly two that are; the other is the fan-in band. Everything
else `elc` bands comes from a published source, and presenting an invented
line beside McCabe, Henry–Kafura, Martin and MISRA without saying so would
lend it authority it has not got.

### Component dependency cycles

```text
Component dependency cycles
  Components                  Example loop
  --------------------------  ----------------------------------------
  /u/p/a.c, /u/p/b.c, /u/p/c.c  /u/p/a.c -> /u/p/b.c -> /u/p/c.c -> /u/p/a.c
```

Files that depend on each other, either directly or through a chain of
other files. Two columns are shown because one alone would be misleading:
the **group** is the full set of files that has to be untangled, and the
**loop** is one concrete example of a dependency cycle, showing exactly
which link you could cut. A group can contain many overlapping loops — far
too many to list — so `elc` shows one representative loop for you to act on.

**This is not the same finding as recursion, and the difference is the unit.**
Two mutually recursive functions in one file appear in the Recursion section
and *not* here, because a file does not depend on itself. Split that same pair
across two files and it is legitimately both: one statement says the stack
depth has no finite bound, the other says the two files cannot be built,
tested, or understood apart.

### Layering

You declare the layers; `elc` does not guess them:

```sh
elc --stratum 'app:src/app/*' --stratum 'hal:src/hal/*'     --stratum 'drv:src/drv/*' src/
```

```text
Layering (2)
  Kind        From  Function      To   Function    Layers
  ----------  ----  ------------  ---  ----------  ------
  skip-level  app   app_shortcut  drv  drv_poke         2
  inverted    hal   hal_callback  app  app_notify       1
```

**The order layers are first declared in is the permitted direction of
dependency**, topmost first. So above, `app` may depend on `hal`, and `hal`
on `drv`. State it explicitly instead if you prefer:

```sh
elc --stratum-order 'app>hal>drv' --stratum 'drv:src/drv/*' ...
```

`--stratum-order` may come before or after the layers it orders. Every declared
stratum must appear in it, and every name must be a declared stratum: a partial
order determines no direction, and a misspelt name would silently leave your
layering checked against something you did not write.

**Two findings, and they are independent.** They come out of one comparison of
the caller's layer against the callee's:

| | Bypasses a layer | Runs against the direction |
| --- | --- | --- |
| `app` → `drv` (down two) | **skip-level** | no |
| `hal` → `app` (up one) | no | **inverted** |
| `drv` → `app` (up two) | **skip-level** | **inverted** |
| `app` → `hal` (down one) | no | no |

The third row is reported **twice**, because both statements are true of it and
each has its own remedy — you can fix the direction without fixing the skip.
The fourth is reported not at all; that is the arrangement you declared.

A file that matches no `--stratum` pattern lies outside your declared layers
entirely, rather than belonging to a layer of its own, so a call touching it
triggers neither finding. And a layer whose pattern matches nothing is
reported on standard error and kept anyway — dropping it would renumber the
layers below it and change what everything else is compared against.

Only **calls** are checked here. A global two layers happen to share is a
different fact, with its own findings in Global state and `--scope`.

With no `--stratum` at all the section states that it was omitted. The coupling
table above it is still produced.

### How much of it conforms

The Layering section says *which* calls breach the declaration. The two
conformance indices say *how much of the code base* does not:

```text
Architecture conformance (over 6 inter-layer call edges; undefined where there are none)
  Index      Violating  Conforming  Of
  ---------  ---------  ----------  --
  Back-call     16.67%      83.33%   6
  Skip-call     16.67%      83.33%   6
```

Both are proportions of the same denominator: **the run's inter-layer call
edges** — the call edges joining two components that lie in *different*
declared layers. The tree above has six of them, one of which runs against the
declared direction and one of which bypasses a layer, so each index is one in
six.

**Three things are outside that denominator**, and each is worth knowing about
before you compare a figure against your own count:

* A call **within** one layer. It has no direction to invert, so it is not a
  candidate for either index.
* A call touching a file **no `--stratum` names**. It lies outside the
  partition, exactly as it does in the Layering section.
* A **shared global**. It is coupling, not invocation, and has its own
  findings in Global state.

A repeated call counts once, as everywhere else: one function calling another
in forty places is one edge, so the percentages are over the same figure the
tables beside them show.

**Where there is no inter-layer call at all, both read `undefined`** — not 0%,
and not 100% conforming:

```text
Architecture conformance (over 0 inter-layer call edges; undefined where there are none)
  Index      Violating   Conforming  Of
  ---------  ----------  ----------  --
  Back-call   undefined   undefined   0
  Skip-call   undefined   undefined   0
```

A project whose layers never call one another has not achieved perfect
conformance; it has demonstrated nothing either way. This is the same
convention Instability follows when both its couplings are zero, and
deliberately not the one the two degrees follow — a fan-in of zero is a
measured zero and prints `0`. The difference between `undefined` and `0` is
the difference between a question with no answer and an answer that is none.

**Do not add the two indices together.** A call ascending two layers is a
back-call and a skip-level call at once and is counted once in each, so a
combined score would count twice exactly the call most worth acting on — and
would name no remedy, where each index separately names one. `elc` reports no
combined score for that reason.

Both are counted from the layering findings listed above them, never
re-derived from the graph, so the percentage and the table cannot drift apart.

### The dependency matrix

The indices say how much conforms; the matrix says **where** the dependencies
are:

```text
Dependency structure matrix (declared layers)
  Rows are callers, columns callees, in ascending order. Above the diagonal: the declared direction. On it: within one subject. Below it: back-calls.
  caller \ callee  app  hal  drv
  ---------------  ---  ---  ---
  app                0    2    1
  hal                1    0    2
  drv                0    0    0
```

**Rows are callers and columns are callees**, both in ascending layer order.
That is what gives a cell's *position* a meaning:

* **Above** the diagonal — dependencies running the way you declared, from a
  layer to one below it. The `app → hal` cell of 2 is two such calls.
* **On** the diagonal — dependencies inside one subject, which no declared
  order constrains.
* **Below** the diagonal — the back-calls. The single `hal → app` cell is the
  one inverted call the Layering section lists, and the cells below the
  diagonal always total exactly that count.

The convention is printed with the matrix every time it renders. A grid whose
orientation you have to infer gives you the opposite answer half the time,
which is worse than no grid.

**You get a matrix even with nothing declared** — over the analysed
directories instead, ordered by path (the convention line is printed here too
and is elided below for width):

```text
Dependency structure matrix (directories: no strata declared, see --stratum)
  caller \ callee      /home/u/proj/src/app  /home/u/proj/src/drv  /home/u/proj/src/hal
  -------------------  --------------------  --------------------  --------------------
  /home/u/proj/src/app                    0                     1                     2
  /home/u/proj/src/drv                    0                     0                     0
  /home/u/proj/src/hal                    1                     2                     0
```

Same six edges, arranged two ways. The heading tells you which you are looking
at, and it matters: with directories the order is alphabetical rather than
architectural, so **no cell below this diagonal is a violation** — it is just a
dependency that happens to point at an earlier name.

The matrix counts call edges only, for the reason the layering analysis does. A
global object two subjects share is a different fact.

It is a detail tier, so it appears in `--verbose` reports. For a
machine-readable copy beside the report:

```sh
elc --dsm -o report.md src/     # writes report.md and report.dsm.csv
```

```text
"Rows are callers, columns callees, in ascending order. Above the diagonal: the declared direction. On it: within one subject. Below it: back-calls."
caller \ callee,app,hal,drv
app,0,2,1
hal,1,0,2
drv,0,0,0
```

The convention is the first record; the grid follows. Every field goes through
the same RFC 4180 quoting the CSV report uses, so a directory whose name
contains a comma is quoted rather than splitting the row. Like `--graphml`, the
companion is off unless you ask for it and needs `--output` to derive a name
from — but unlike `--graphml`, it works in regeneration mode too, because a
saved record carries the matrix where it carries no graph.

### Graph purification

A raw call graph rarely sorts into layers. A logger everything calls, and a
dispatcher that calls everything, each join parts of a program that have
nothing to do with one another; an ordering computed over such a graph
collapses into one tangled stratum that describes nothing.

So `elc` builds a second graph — a **recovery view** — with those functions
set aside, and reports every classification it made in doing so.

```text
Graph purification (recovery view only, no measurement above is taken over it; elc heuristic — not a published standard: sink at authority >= 90% and hub <= 10%, god object at betweenness >= 90% and hub >= 90%, peripheral below core depth 2; 9 functions retained, 12 call edges masked)
  File                    Function  Class         Metric       Value                                            Action
  ----------------------  --------  ------------  -----------  -----------------------------------------------  ----------------------
  /home/u/proj/app/d.c    dispatch  god object    betweenness  14.00, above 100% of functions (hub above 100%)   all edges masked
  /home/u/proj/feat/f.c   helper_c  peripheral    coreness     1, below the core depth of 2                      excluded from the view
  /home/u/proj/util/l.c   util_log  utility sink  authority    1.0000, above 100% of functions (hub above 0%)    incoming edges masked
```

**The view is a copy, and that is the property to hold on to.** Nothing else
in the report is computed over it. Fan-out, fan-in, call depth, recursion,
coupling, Instability, dependency cycles, reachability, the conformance
indices, and every cell of the matrix are each
exactly what they would be if purification had never run. `util_log` above has
every one of its incoming edges masked in the view and still reports a fan-in
of six in the Functions table. Change a purification threshold and no
number in the report moves but the ones in this one section.

#### The three classifications

A **utility sink** has high authority and a hub score near zero: many parts of
the program call it and it calls almost nothing back — a logger, a string
helper, an arithmetic routine. Its *incoming* edges are masked and its outgoing
edges are not. The asymmetry is the point: the fusion such a function causes is
between its *callers*, who are joined to one another through it, so its own
calls harm nothing. Masking the edges rather than the node is what removes the
fusion while leaving the function's position observable.

A **god object** has high betweenness *and* a high hub score: it lies on a
great many shortest paths and calls widely. It loses its edges in **both**
directions, because it short-circuits in both. The hub score is required beside
the betweenness because betweenness alone would not tell a monolithic
dispatcher from a genuine intermediary a layering ought to keep — a legitimate
waypoint need not call widely. A function that meets the utility-sink test as
well is reported as a god object: masking all its edges subsumes masking the
incoming ones, and the more specific claim is the more useful one to read.

A **peripheral** function lies below the configured core depth — outside the
mutually connected centre of the program. It is **excluded** from the view
rather than placed at the edge of it, and gets no recovered layer at all. A
function `elc` did not consider is not a function `elc` put at the bottom of
your architecture, and treating the two alike would drop every leaf into the
lowest layer.

#### Rank, not raw score

Each threshold is a **position in the ordered distribution** of a score, not
the score itself. A betweenness value means nothing on its own: it scales with
the size of the graph, so a fixed cut-off serviceable on a project whose
dispatcher lies on ten thousand shortest paths would classify nothing in a
nine-function tree, and one serviceable there would classify half the large
project. `above 100% of functions` in the table means the function outranks
every other in the run.

The scores come from an iterative computation, so a comparison at the boundary
is made to a defined tolerance, and functions whose scores agree within it hold
one position and classify alike. Ties in the ordering break by the same stable
identifier every other output is ordered by. Two runs over one tree therefore
classify identically, on any machine.

#### These are `elc`'s own heuristics

All five thresholds are `elc`'s judgement rather than a published standard, and
the report says so wherever a classification appears. Each is adjustable:

```sh
elc --verbose --core-depth 3 src/                     # strip more of the periphery
elc --verbose --sink-authority 95 --sink-hub 5 src/   # demand a stricter sink
elc --verbose --god-betweenness 80 --god-hub 80 src/  # cast a wider net for dispatchers
```

The four centrality thresholds are percentages of the *other* functions, so 100
is the ceiling and a larger figure is a usage error rather than a setting that
silently classifies nothing. `--core-depth` is a coreness and has no ceiling.

**No classification carries a severity, and none becomes a finding.** "God
object" says where a function sits in a graph. It is not a measurement banded
against an accepted range — no published source bands these — and `elc` says
nothing about whether the arrangement is right, only that it is what the graph
shows. That is why the section names the metric and the value behind every row:
masking you cannot inspect is a black box, and this table is what lets you
judge whether the right things were set aside.

#### Seeing what the masking did

Two drawings, on request, beside the report:

```sh
elc --purify-dot -o report.md src/   # writes report.raw.dot and report.purified.dot
```

The first is the call graph as built. The second is the recovery view, in which
the masked and excluded functions are **greyed, labelled with their class, and
left holding no edge** — never deleted. Seeing what purification did is what
lets you judge whether it did the right thing, and one drawing of the result
cannot show what it acted on. Neither replaces the annotated call tree: that
answers a different question and is written by default.

Like every companion, they take their names from `--output` and accept no path
of their own, so with the report on standard output nothing is written.

### The recovered layering

With the fusing functions set aside, `elc` orders what remains and folds the
order into layers — a description of the architecture your code already has,
for a reader who has declared none:

```text
Architecture recovery (a proposal, never the baseline conformance is measured against; 3 layers over 3 directories, 0 functions masked and 0 excluded)
  Layer  Directory or cycle       Functions
  -----  -----------------------  ---------
  0      /home/u/proj/app                 2
  1      /home/u/proj/svc                 3
  2      /home/u/proj/hal                 2

Architecture recovery — the proposal as arguments (elc never applies it; passing it back is what declares it)
  Adopt with
  ----------------------------------------------------------------------------------
  --stratum app:'/home/u/proj/app/*' --stratum hal:'/home/u/proj/hal/*' --stratum svc:'/home/u/proj/svc/*' --stratum-order 'app>svc>hal'
```

**This is a proposal and never a baseline.** Nothing in the report is measured
against it. The `--stratum` declarations remain the sole standard the layering
findings, the back-call index and the skip-call index are judged by, and with
none declared those analyses stay omitted with their reason stated — however
confidently a layering was recovered. `elc` measuring conformance against its
own proposal would be a tool marking its own homework: every code base would
conform, because the standard would have been read off the thing being judged.

So the proposal arrives as an **argument list**, not as prose. Read it, and if
you agree with it, paste it back:

```sh
elc --verbose src/            # proposes a layering; conformance stays omitted
elc --verbose --stratum app:'/home/u/proj/app/*' \
    --stratum hal:'/home/u/proj/hal/*' \
    --stratum svc:'/home/u/proj/svc/*' \
    --stratum-order 'app>svc>hal' src/   # now it is a declaration, and it is yours
```

The quoting is load-bearing in both halves: the patterns hold a `*` and the
order holds `>`, which a shell would otherwise read as a redirection. And the
declarations come out ordered by directory depth rather than by layer, on
purpose — `elc` takes the first declared layer whose pattern matches a file,
and `app/*` matches everything beneath `app/` as well as in it, so a parent
directory declared before its child would claim the child's files. The layer
order comes from `--stratum-order` beside them, which is why the declaration
order is free to be chosen for correctness.

**A topological order is not a layering.** It orders functions; an architecture
orders directories. So the order is folded by the directory each file belongs
to, and a directory's layer is fixed by *where the bulk of its edges point*
rather than by its earliest or latest member. One function reaching far down
the order does not drag its whole directory with it — a completion callback
called from the layer below its own is an ordinary shape, and it should not
turn a service layer upside down. Functions excluded from the recovery view get
no layer at all.

Where the recovery view is still cyclic, no ordering of it exists:

```text
Architecture recovery (omitted: the recovery view is cyclic, so no ordering exists; the mutually reachable groups below are reported in its place)
  Layer  Directory or cycle                       Functions
  -----  ---------------------------------------  ---------
  cycle  hal_init, hal_stop, svc_open, svc_close
```

The groups are reported instead of a layering, rather than the graph being
ordered arbitrarily — the same answer `elc` gives for a call depth over a
cyclic graph. A function that calls *itself* is not one of these: the edge runs
from a node to itself and orders nothing, the recursion section already says
so, and treating it as blocking would cost every project holding one recursive
function this whole analysis. A group is printed as its membership and not as a chain of
arrows: every member reaches every other, but the decomposition yields no
order, and arrows would assert a path that may not exist. Breaking any edge
among them is what would make a layering possible.

### When you disagree: the purification manifest

The classifications are heuristics, and heuristics have false positives. A
state machine's dispatcher legitimately lies on a great many shortest paths and
legitimately calls widely; nothing in the graph separates it from the monolith
the god-object test describes, and only you know which it is.

```sh
elc --write-manifest -o report.md src/    # also writes report.manifest.json
```

```json
{
  "manifest-version": 1,
  "classifications": [
    {
      "function": "dispatch",
      "file": "/home/u/proj/app/dispatch.c",
      "class": "god object",
      "mask": true
    }
  ]
}
```

Edit the statement you disagree with and hand the file back:

```sh
elc --verbose --manifest report.manifest.json src/
```

A statement **governs**: the class it names is the class `elc` uses, and `elc`
neither recomputes nor overrules it. Setting `"mask"` to `false` is the usual
correction — you agree the function is the dispatcher and disagree that it
should be set aside, so the classification stays in the report and the
function's edges stay in the recovery view, which changes the layering read off
it. Omitting `"file"` matches the function wherever it is defined; naming it is
the precise form and is what `elc` writes.

The report tells you which is which:

```text
  File                     Function  Class         Metric     Value                                 Action                  Source
  -----------------------  --------  ------------  ---------  ------------------------------------  ----------------------  --------
  /home/u/proj/app/d.c     dispatch  god object                                                     kept in the view        manifest
  /home/u/proj/util/l.c    util_log  utility sink  authority  1.0000, above 100% of functions       incoming edges masked   computed
```

Without that column a reader cannot tell the tool's assumptions from their own
team's, which is the whole reason the section exists.

**A manifest is read only when you name it.** It is never discovered from the
working directory, the analysis target, an ancestor of either, or a dotfile —
the same rule custom rule files follow, and the reason two people running the
same command on the same tree get the same answer.

A manifest that cannot be read, and one that is well-formed JSON but not a
manifest — no version, a version this build does not read, a class name it does
not know — both end the run with a diagnostic naming the fault, and the
diagnostic quotes the line and column of a syntax error so you know where you
broke it. You named the file, so the failure is yours to correct. A statement
naming a function no analysed file defines is a different matter: it is
reported and ignored, and the run continues, because analysing one directory of
a project whose manifest covers all of it is ordinary use.

### Dead code between functions

```text
Unreachable functions (3; from the declared entry points and every address-taken function)
  File                  Function   Line
  --------------------  ---------  ----
  /home/u/proj/src/a.c  clique_a     41
  /home/u/proj/src/a.c  clique_b     47
  /home/u/proj/src/c.c  orphan       12
```

`elc` traverses the call graph forward from a root set and reports everything
the traversal never reaches. This is the analysis a grep cannot do, and the
two properties below are why.

**A clique of unused functions is correctly reported.** A *clique* here means
a small group of functions that only call each other and nothing outside the
group calls in. If `clique_a` and `clique_b` call each other and nothing else
calls either one, a naive rule looking for "a function nothing calls" finds a
caller for each of them and reports neither as dead. A full traversal catches
both, because no path from any root leads into the pair at all.

**The root set is the declared entry points *together with* every function
whose address is taken**, and that second half is what makes the claim worth
acting on. A handler installed in an interrupt vector table, or a callback
stored in an array, is never called by name — it has no resolved caller
anywhere in the graph. Without it in the root set, `elc` would report your
interrupt handlers as provably dead. The asymmetry is deliberate: an extra
root can only shrink the unreachable set, whereas a missing one produces a
false claim that costs you working code.

Functions reached *through* an address-taken root are reachable too. It is a
root, not an exemption.

```sh
elc --entry main --entry timer_isr src/
```

**With no `--entry` at all, nothing is reported unreachable** — the section
says the analysis was omitted and why. `elc` never reports your whole program
as dead because you did not tell it where execution starts.

Storage goes the same way as the code:

```text
Unreachable globals (touched only by unreachable functions)
  Object
  ------------
  orphan_state
```

An object is reported unreachable when every function that touches it is
itself unreachable. An object *no* analysed function touches at all is
deliberately left unclaimed: it may be written to from outside any function,
or from a source file outside what you pointed `elc` at.

### Global state

```text
Global state
  Object       Writers   Readers     Finding
  -----------  --------  ----------  ------------------------------------------------------------
  channel      producer  consumer    hidden channel — {producer} {consumer} never call each other (MISRA C Rule 8.9)
  config       loader    loader      scope reduction — one function names it (MISRA C Rule 8.9)
  shared_flag  set_flag  check_flag
```

Every global object, with the functions that write it and the functions that
read it. Two arrangements carry a finding, both attributed to MISRA C Rule 8.9:

**Scope reduction.** Only one function names the object, so nothing is lost
by moving it from global scope to a local variable declared inside that one
function ("block scope," in C terms) — it was never really shared. Note
that this case produces no writer-to-reader edge in the graph at all, since
there is only one function involved; that's why this finding comes from
looking at *which* functions access the object, not from the graph's edges.

**Hidden channel.** The object is shared between functions that live in
*disconnected* parts of the call graph — parts that never call each other,
directly or indirectly. Nothing in the code says that one must run before
the other, and yet the program only works if it does. That is called
**temporal coupling**: a hidden dependency where *execution order* matters,
even though nothing in the code makes that dependency visible. It is a
failure mode that survives most code reviews, because no single file shows
the connection. The finding names the disconnected groups involved, since
identifying which parts of the program are secretly coupled is the point.

Sharing *within* one call-connected region is ordinary — `producer` calling
`consumer` and handing state through a variable is a design, not a defect —
and is reported as a measurement with no finding. Without that distinction the
warning would fire on every shared variable and mean nothing.

### Dead code within a function

```text
Dead code within functions (every language analysed)
  File                  Function  Lines  Cause
  --------------------  --------  -----  ------------------
  /home/u/proj/src/a.c  parse     88-88  after a terminator
  /home/u/proj/src/b.c  lex       12-14  literal condition
```

A different question from the one above, answered by different means, and
neither subsumes the other: a function reached from an entry point may contain
statements that cannot execute, and an unreachable function may contain none.
Both are reported.

Two classes are detected, from the syntax tree alone:

* **After a terminator.** Statements following a `return`, `break`, `continue`
  or unconditional transfer, in the same block, up to the first construct that
  can be entered without falling into it.
* **Literal condition.** The branch a condition *written as a literal*
  excludes — the body of `if (0)`, the `else` of `if (1)`, the body of a loop
  whose condition is literally false. The whole span is reported, because the
  span is what you delete.

**What `elc` deliberately will not tell you.** There is no **data-flow
analysis** (tracking how a value moves and changes through the code), no
**constant propagation** (substituting a variable's known fixed value into
the expressions that use it), and no evaluation of expressions at all.
`x = 0; if (x)` is therefore not reported, and neither is
`const int zero = 0; if (zero)`, however clearly you can see the answer by
eye. Nor is `if (0x0)` reported, because the query matches a decimal zero
and nothing else.

That is a deliberate trade and it runs one way: a missed statement costs you a
cleanup opportunity, and a false claim invites you to delete code that runs —
a defect this tool would have introduced. Where both cannot be had, `elc`
reports nothing.

A `goto` label following a `return` is *not* reported, for the same reason. It
is reachable, and a tool that flagged it would be telling you to delete a live
branch target.

**Support is per language, and its absence is stated.** A language module may
supply a dead-code query or not, and every language shipping today does. One
that writes its false literal as an ordinary identifier the grammar cannot
distinguish from one your program declared should not: guessing would risk
exactly the false claim above. When a language has no query the heading says
so:

```text
Dead code within functions (not analysed for: some_language)
```

*Not analysed* and *none found* are different claims. A reader who cannot tell
them apart has been told nothing, so `elc` never renders the first as the
second.

### Execution scopes

Some targets combine several programs that end up sharing one address space
and one set of global names at run time — a host-driven test harness calling
into firmware, say, or a bootloader running beside the application it starts.
Nothing in the source marks where one program ends and the next begins, so
you tell `elc` yourself:

```sh
elc --scope 'host:*/harness/*' --scope 'target:*/firmware/*' src/
```

```text
Cross-scope access (2)
  From  Function     To      Function      Via
  ----  -----------  ------  ------------  -------
  host  host_drives  target  target_entry  call
  host  host_writes  target  target_reads  mailbox
```

Every call and every shared global object by which one scope reaches another.
**Both kinds, and the second is the reason this exists** — a scope that never
calls into another but writes a variable the other reads has not been
isolated, and a check that looked only at calls would call the arrangement
clean.

A file matching no `--scope` declaration lies outside every declared scope
entirely, rather than belonging to a scope of its own: you said nothing
about it, and inventing a boundary would report violations against a
division nobody actually drew. With no `--scope` at all, the analysis is
omitted, with the reason stated.

### Exporting it

```sh
elc --graphml -o report.md src/     # writes report.md and report.graphml
```

GraphML is a standard file format for describing graphs. Tools built for
analysing or drawing graphs — igraph, NetworkX, Gephi, and yEd, among others
— can all read it directly, so you can explore or visualise the dependence
graph without writing your own reader for it. The export is off unless you
ask for it, and its filename is derived from `--output` by substituting the
extension; it takes no path of its own. With the report going to standard
output there is no filename to derive one from, so no file is written.

The node attributes are `name`, `file`, `line-start`, `line-end`,
`component`, `eloc`, `complexity`, `fan-out`, `fan-in` and `address-taken`;
edges carry `kind` (`call` or `global`), the object's name on a global edge,
and `call-sites` on a call edge.

No figure *derived* from those attributes is exported beside them. An
ingesting tool holding `eloc`, `complexity`, `fan-in` and `fan-out` can form
whatever it wants from them exactly; carrying a derived value as well would
put a second computation of it beside the one in the report, and two places
computing a figure are two places it can be computed differently.

### Looking at it

```sh
elc -o report.html \
    --stratum app:'*/app/*' --stratum hal:'*/hal/*' src/
```

The `.dot` and GraphML companions are exports: something else draws or loads
them. The `html` format *is* the drawing — one file that opens in a browser
when you double-click it, with no server to start and nothing to build.

**It is chosen the way every format is chosen: by the extension.** There is no
`--html` option, because `report.html` has already said what the format is, and
a flag saying it again is exactly the disagreement `elc` refuses when
`--format` and a filename contradict each other. For a report going to standard
output, where there is no filename to read an extension from, `-f html` names it
like any other format.

**Collapse all** in the top right closes every file and returns the drawing
to exactly the picture the page opened with — the same arrangement at the same
zoom, not a fresh fit that lands somewhere near it. There is deliberately no *expand
all*: opening every file at once is the whole call graph at function level,
which is the picture this format exists to save you from.

**Boxes never overlap.** Whatever moves them — opening a file, or dragging one
yourself — any boxes that end up on top of each other are pushed apart. A box
you dropped stays where you put it and the others move out from under it.

**A file opens where it sits.** Click a file and it stays exactly where it
was on screen; everything around it moves outward to make room. An open
file's box is solid, so lines between other files pass behind it rather than
across it, while every call that touches its functions — its own, and the call
paths in and out of it — is drawn on top. Opening a file never changes your
zoom. A file that
was left of it is still left of it afterwards, so you never have to hunt for
the box you just opened, and closing it puts the drawing back. Edges between
other files pass behind an opened box rather than across its face.

**It opens at the file level, inside the architecture.** What you see first is
one box per file, sitting inside the layer you declared it in with
`--stratum`. Click a file and it opens to the functions in it; click it again
and it closes. **Files are the only boxes that open** — a layer stands open
from the start, because it is context for the files inside it and context you
have to open is not context. That
default is the point of the artefact rather than a preference: a call graph of
a real project drawn at function level is a picture nobody can read, which is
the failing the `.dot` and GraphML companions have in common. You descend into
the part you care about instead of starting at maximum density.

With no `--stratum` declared, there are simply no layers to sit above the
files. `elc` does not invent one — a layering is something you state, never
something read off the directory tree (see [Declaring the
architecture](#declaring-the-architecture)) — and for the same reason a file
matching none of your stratum patterns is drawn beside the layers rather than
tucked inside one.

**It shows you what the run found, not just what calls what.** A function the
report warns about is drawn amber and one it calls critical is drawn red — the
same two colours the `.dot` companion uses, so you learn one scheme and can
read either drawing. A function in a recursive cycle gets a double border and
one no entry point reaches is dashed. Two findings about global objects change
the shape of the box instead: taking part in a hidden channel, and being the
only function that uses some global — which means that global could be a local
one. The key above the drawing *draws* each shape rather than naming it, so
you can match what you see to what it means.

**Point at any box and it tells you what was found about it** — the file and
line it is defined at, its ELOC and complexity, and each finding on its own
line. That is the same text the `.dot` companion puts in its tooltips, which
is where you go when the drawing says a function is critical and you want to
know which finding said so. The key sits above the drawing, so a
page you send to somebody else still explains itself.

These are the report's own judgements, placed by the same code that places them
on the `.dot` companion — the drawing never forms a view of its own about what
exceeds a threshold, so it cannot disagree with the tables in the report beside
it. Strip every colour and shape away and the same graph remains, with the same
boxes and the same arrows: a mark says something *about* the drawing and never
changes what is drawn.

**It is laid out in call order.** Callers sit above the functions they call, so
the drawing reads as a flow rather than as a mesh, and a layer you declared
stays a box around its own files. That needs a layout engine the viewer does
not ship, which is why the page fetches four scripts rather than two — see
*What "standalone" means here* below.

**A file with no functions of its own is not drawn.** A C header usually
declares functions rather than defining them, so it has nothing to put in the
drawing — no box, no arrows — and the `.dot` companion has never drawn one
either. The file is still discovered, measured and counted everywhere the
report counts a file; what is left out is a box that could only ever be empty.
The same thing happens under `--elf` when the image defines none of a file's
functions: the filter empties it and the drawing then omits it, on the
evidence of the build rather than on a rule of the drawing's own.

**A file is labelled by where it differs.** Every file box sheds the directory
prefix all of them share — on most projects, the path of the tree you analysed
— so a box reads `src/report.c` rather than repeating a prefix that is the
same on every box in the drawing. Nothing is lost by it: the full path is
carried on the node itself, in the embedded record, and the tables of every
other format are untouched.

**It is not the Markdown report with a picture attached.** The other
human-readable formats present the same tiers in the same order as each other;
this one presents its information *in the context of the drawing* — a figure
reached by opening the box that holds it, at the level you are looking at — so
it is not held to that uniformity, in the way `csv` and `xml` are not. Where it
shows you a measurement it is the measurement the report states; what differs is
the arrangement, never the content.

**Only calls between functions are drawn.** When you collapse a file, the lines
between the collapsed boxes are drawn by the viewer from the calls crossing
between them. `elc` does not emit them: a coupling figure drawn here by a rule
of its own could disagree with the Ca/Ce figures in the report's own tables,
and those are the measured ones.

**What "standalone" means here.** The file needs no web server and no build
step. It does need the network the first time you open it, because the drawing
library and its layout engine are fetched from a CDN — so a browser on a disconnected machine shows
the page and no diagram. `elc` itself never touches the network; this is a
property of viewing the artefact, not of producing it. If you need the page to
work offline, keep it beside a cached copy of the scripts named in its
`<head>`, or open it once while connected and let the browser cache them.

**A regenerated report cannot be written in this format.** A saved record
carries the findings of a run and not the graph they came from, so
`--from-xml` with an `.html` output is refused rather than answered with an
empty drawing.

### Where the graph is imprecise, and in which direction

`elc` resolves calls by matching names across the files you gave it, without
using a compiler's full type information. Two consequences follow from that,
and both affect how much weight you should put on a number:

- **A name defined more than once resolves to its first definition**, in
  sorted file order, and `elc` says so on standard error. Two `static`
  helper functions with the same name in two different files is ordinary C
  — but every call meant for the second one will be resolved to the first
  instead.
- **A method call is matched by method name alone, not by the type of the
  object it's called on.** Working out exactly which class's method a call
  lands on requires full type analysis, which a grammar-based tool like
  `elc` does not perform.

`elc` does not attempt to correct for either of these. The fix would have to
be built into the binary itself and would encode one language's rules for
name resolution there — exactly what the data-driven language design is
built to avoid.

**The first of these can produce a false "unreachable" claim, so it is worth
knowing about before you delete anything.** If two files each define a
`static` helper called `grow`, every call to either one resolves to the
first definition, and the second function ends up with no incoming call in
the graph — so reachability analysis reports it as dead when it is not.
This is the one place `elc` errs toward calling something *un*reachable
rather than reachable. It is a limitation of name-only resolution, not a
genuine finding about your code.

`elc` makes this visible with a diagnostic:

```text
elc: grow is defined 5 times; calls to it resolve to /home/u/proj/src/analyze.c:127
```

**Read standard error before acting on the unreachable list.** A function
named in a diagnostic like this and also reported unreachable is a
duplicate-name artefact, not genuinely dead code.

**The same problem reaches the component-coupling analyses too, and there
it is worse.** Every call to the duplicated name resolves into the file
with the winning definition, so that file gains afferent coupling
(dependents) it has not actually earned, while the other files lose credit
for depending on it. If the winning file already depends on one of the
losing files, this invented edge can **close a dependency cycle that does
not actually exist**. A false circular-dependency finding is a more
expensive mistake than a false dead-code claim, because it points at an
architecture problem rather than at a single line to delete.

Running `elc` on its own source code demonstrates both problems at once:
several of its files define a `static grow` helper, one definition wins,
and the report shows a dependency cycle between the winning file and one of
the others. The diagnostic naming the duplicate is the tell in every case.
The fix is either to give the helpers distinct names, or to read the
diagnostic and the report together and discount the false finding.

**These same errors also show up in the call-tree drawing** (below), which is
the part of the report most likely to be shared around — and least likely to
be shared with its caveats attached. A function wrongly reported unreachable
is drawn with a dashed outline, an invented dependency cycle colours two file
clusters red, and a recursive cycle is drawn onto every function sharing that
name. Check standard error alongside any picture before you show it to
someone else.

## The call tree

Graphviz is a widely used tool for drawing graphs from a plain-text
description, and `.dot` is the name of that text format. Whenever a report is
written to a named file, `elc` also writes the call graph as a `.dot` file
beside it, annotated with the findings that apply to each part of it. The
filename is derived from `--output` by substituting the extension:

```sh
elc --entry main -o report.md src/    # writes report.md and report.dot
dot -Tsvg report.dot -o report.svg    # Graphviz draws it; elc does not
```

`elc` writes the file and renders nothing. It neither links Graphviz nor runs
it, so the drawing is yours to make with whatever renderer you like — `dot`,
`neato`, an online viewer, or a script that reads the file for something else
entirely.

Unlike `--graphml`, this one is **on by default**: `--no-dot` declines it. With
the report going to standard output no file is written whether or not you
declined it, because there is no output path to derive a name from. A file that
cannot be written is a diagnostic and a failed exit status — never a reason to
withhold the report itself.

### What the drawing shows

Each source file becomes a labelled cluster and each function a node inside it.
The edges are calls. Coupling through a shared global object is *not* drawn as
an edge, because it is not a call — it shows up instead on the functions that
take part in it.

Every annotation is a Graphviz attribute a renderer may ignore. Ignore all of
them and the same tree remains, with the same nodes and the same edges:

| Annotation | Means |
| ---------- | ----- |
| filled amber | a warning-severity finding applies to the function |
| filled red | a critical-severity finding applies to it |
| double border | a member of a recursive cycle |
| octagon | takes part in a hidden channel |
| note shape | the only function naming some global object |
| dashed grey | no path reaches it from a declared entry point |
| thick blue | a step of the deepest call chain, on nodes and edges alike |
| coloured cluster | a finding applies to the source file as a whole |

The severities shown here are exactly the ones the **Findings** section
reports, judged against the same published thresholds. The drawing only
colours what Findings already decided — it does not apply a second, separate
judgment of its own.

Each node's tooltip carries its definition site and its findings in full, which
an SVG renderer will show on hover. The head of the file carries the same key
in a comment, along with any finding that belongs to no single function — the
depth of the call tree is the one that does.

Two annotations that apply at once ride two different attributes, so neither
hides the other: a function that both takes part in a hidden channel and is
unreachable is a dashed grey octagon.

### Two things worth declaring first

- **`--entry`**, or nothing is dashed. Reachability is measured from roots you
  declare, and with none declared the analysis is omitted rather than guessed
  at — so a drawing made without `--entry` shows no dead code, which is not the
  same claim as showing none exists.
- **`--stratum`**, if you want a layering or instability finding to appear on a
  cluster. Both need a declared architecture to compare against.

## Custom rules

`elc` is built on **Tree-sitter**, a parsing library that turns source code
into a syntax tree and lets you search that tree with small pattern-matching
programs called *queries*. A custom rule is simply a Tree-sitter query *you*
write, checked against your source by the exact same mechanism that produces
`elc`'s own built-in metrics — during the same parse, with the same handling
of query conditions ("predicates"). A rule is just a data file, so adding one
needs no rebuild and no change to `elc` itself.

```scheme
; house-style.scm
((call_expression function: (identifier) @allocation)
 (#eq? @allocation "malloc"))

(goto_statement) @jump
```

```sh
elc --rules c:house-style.scm src/
```

```text
Custom rule matches (2)
  Rule                     File                Lines
  -----------------------  ------------------  -----
  house-style.allocation   /home/u/src/a.c     4-4
  house-style.jump         /home/u/src/a.c     6-6
```

**`elc` reports what your rule matched and forms no opinion about it.** There
is no severity column and no source column, because there is nothing honest to
put in either: you decided the rule was worth writing, not `elc`. Matches get
a section of their own beside the **Findings** and never appear among them —
a finding is a measurement `elc` banded against a published threshold, and
those are different things.

### A rule's identity

A match is identified by the **basename** of the query file (its filename,
without the directory path) plus the **capture name** that matched — the
`@allocation`-style label in your query, shown above. So a single file can
express as many independently named rules as it has captures. The capture
name is visible in the report in a way it is not used anywhere else in
`elc`, so choose one that will make sense to whoever reads it.

### Where rules come from

Two places, and no others:

- **`<runtime>/queries/<lang>/rules/*.scm`** — bound to the language by the
  directory holding it, and used without being named. This is where a shared
  house standard belongs.
- **`--rules lang:path`** — bound by the argument. A query compiles against
  one specific grammar, which is why the language is named rather than
  guessed.

**No rule file is ever discovered from your working directory, your target, or
a dotfile.** Two people running the same command on the same tree must get the
same answer, and a rule picked up from a checkout would make that false — the
same reason `elc` reads no configuration file.

### When a rule is broken

Where it came from decides what happens, not what is wrong with it:

| Provenance | Unreadable or will not compile |
| ---------- | ------------------------------ |
| `--rules lang:path` | diagnosed, and the run stops before analysing anything |
| the runtime location | diagnosed, excluded, and the run continues |

A rule you just named is a mistake you can fix now, so `elc` stops and tells
you. A rule sitting in the runtime location is a malformed component, and is
handled like every other one: reported, skipped, survived.

A rule naming a language with no module is reported and skipped either way —
what is missing is the module, not the rule.

Writing the queries themselves is covered by `runtime/queries/README.md`, which
ships with the runtime and is the contract a rule is written against.

## Conditional compilation

**Conditional compilation** is when source code contains several possible
variants of itself, selected by directives like C's `#if` and `#ifdef`, or
Rust's `#[cfg(...)]` attribute — so which lines actually end up in a given
build depends on which symbols are defined. Source that uses it effectively
describes *several* programs at once. Measuring it without saying which one
you mean gives you the union of all of them: a number that describes no
build that actually exists, and overstates every figure drawn from it. Name
a configuration, and `elc` measures that configuration:

```sh
elc -DFEATURE_X -DTARGET=stm32 src/
```

**`elc` runs no preprocessor.** It invokes no `cpp` (the C preprocessor), no
compiler, and no build system, and it reads no file your source `#include`s.
A result that depended on which toolchain happened to be installed would not
be a property of your source code. Instead, `elc` decides each region using
only the syntax tree it has already parsed.

### What gets decided, and what does not

Exactly two things are decided:

- **A constant condition.** `#if 0` and `#if 1` mean the same in every
  configuration, so they prune whether or not you supplied any `-D`.
- **A definedness test over a symbol you named.** `#ifdef`, `#ifndef` and
  `#if defined(...)` are decided when the symbol appears in a `-D`.

Everything else is **undecidable rather than false**. `#if VERSION > 2` needs
macro values `elc` does not have, so both branches stay counted and the region
is added to the **Undecided regions** figure in the project summary.

That asymmetry is the whole safety argument, and it is worth understanding
before you trust a number. Treating an unrecognised condition as false would
silently delete code and hand you a report that is confidently wrong and looks
exactly like a correct one. Treating it as true over-counts — which is visible,
in a figure printed right beside the metrics.

### A symbol you did not name is not thereby undefined

`-D` can only say a symbol **is** defined. There is no `-U`. A symbol you never
mention might still be defined by a header, or by a build system `elc` will
never see, so `elc` calls such a condition undecidable rather than false.

Two consequences follow, and both are intentional:

- **Supplying no definitions prunes nothing** on that account, so adding the
  option to an existing invocation cannot change a figure you already trusted.
- **An `#ifndef` include guard is undecidable in every run.** A header-heavy
  project reports a large undecided count. That is honest, not a defect — `elc`
  genuinely cannot tell whether the guard is defined at that point.

### Which constructs count as conditional is data

The constructs that introduce a region, where the condition sits, and which
part is the alternative are declared in the language's `conditionals.scm`, not
in `elc`. A C preprocessor conditional and a Rust `#[cfg]` attribute are the
same mechanism, and a language whose module ships no such file simply has no
conditional compilation.

Rust is a special case. An attribute has no `#else`, so `#[cfg(X)]` can only be
*removed*, never swapped for something else — and since a symbol you did not
name is undecidable, `#[cfg(X)]` is pruned by nothing. It is `#[cfg(not(X))]`
with `-DX` that prunes.

### The configuration travels with the report

The definitions in force appear in the report and in the saved record, so a
report always states which configuration it describes — and one regenerated
from a record still does. Because pruning happens when a file is measured
rather than when a report is rendered, combining `-D` with `--from-xml` is a
usage error rather than a silently ignored request: the record already
describes one configuration, chosen when it was written.

## Filtering by a linked image

Your source code contains functions that your actual build does not keep.
Some are excluded by conditional compilation, some the linker discards
because nothing calls them, and some live in a source file the final link
never included at all. A report that counts them describes a program that
doesn't exist, and overstates every figure drawn from it.

A **linked image** is the compiled program itself — the executable or
shared library your build produces. It records each function it contains
under its **linkage name**: the exact symbol name the compiler and linker
use internally. For C this is just the function's name; for C++ and Rust it
is usually an encoded ("mangled") form of the name, which `elc` decodes in
order to match it against your source (more on this in
[C++ and Rust names](#c-and-rust-names) below). Point `elc` at the image
with `--elf` — named after **ELF** (Executable and Linkable Format), the
standard binary format on Linux and most other Unix-like systems — and it
measures only what actually shipped:

```sh
elc --elf build/app.elf src/
```

This answers the same question the previous section does, just a different
way. A `-D` **re-decides** the conditions your build resolved, based on
definitions you type in yourself. A linked image, by contrast, was produced
by the real toolchain with the real build flags, so it **observes what your
build actually did** — which makes it the stronger evidence, where you have
one available. Neither option replaces the other: the image tells you which
*functions* survived, but nothing about which lines inside a surviving
function were compiled out. The two options can be combined, and you can
give both at once.

### What the report looks like

New sections appear, and only when you supply an image — a run without
`--elf` reports exactly what it reported before the option existed:

```
Linked-image filter
  Property                          Value
  --------------------------------  ------------------
  Image                             build/app.elf
  Unresolved linkage names          0
  ELOC outside any function         2
  Lines not compiled by this build  14
  Files with no debug coverage      1
  Regions decided by this build     3

Functions the image places that the parse did not reach (1; no figures are measured for them)
  Function     File            Line
  -----------  --------------  ----
  __vector_12  /src/sys.c        44

Functions the image does not define (2)
  Function      File                Line
  ------------  ------------------  ----
  unlinked_add  /src/dropped.c        13
  unlinked_max  /src/dropped.c        18
```

Everything else in the report — the totals, the per-function table, the call
tree, the coupling, the findings — describes the filtered set and nothing else.
A function the image does not define is never recorded, so no analysis has to
know a filter was applied.

### The two figures are different claims

**Unresolved linkage names** tells you how completely `elc` could read the
image — the same kind of claim the **Unresolved calls** figure makes about
the call graph, and reported for the same reason: a number whose accuracy is
unstated cannot be acted on. A large count means many of the image's symbols
used a mangling scheme this build cannot decode, so the filter is
correspondingly incomplete.

**Functions the image does not define** is the finding the option exists to
produce. It is dead code established by what your linker did, rather than
inferred from a call-graph traversal the way **Unreachable functions** is. The
two are reported separately and neither is offered as the other.

### The finer granularity: lines the build did not compile

Everything above works from the image's **symbol table**, which names the
*functions* the link kept. Where your image also carries **debug line
information** — where it was built with `-g` — `elc` reads that too, and
answers a second question the symbol table cannot: which *lines inside a kept
function* the compiler actually emitted an instruction for. Lines it did not
are excluded from every metric, exactly as a dropped function is.

There is no option for this and nothing to remember. The finer granularity is
governed by what your build wrote; an image made without `-g` behaves exactly
as it did before, reporting the function filter alone.

**This is where an image outreaches `-D`.** Take a region you never restated
on the command line:

```c
int configure(int flags)
{
	int n = flags;

#ifdef FEATURE_TELEMETRY
	n |= TELEMETRY_BIT;
	n |= REPORTING_BIT;
#endif
	return n;
}
```

`FEATURE_TELEMETRY` is a symbol `elc` cannot decide, so
[conditional compilation](#conditional-compilation) leaves the region whole and
counts it among the **Undecided regions** — the honest answer from the source
alone. The image settles it. Your build compiled nothing there, the line
mapping says so, and those two lines leave the count.

#### The region is settled whole, and the count says how often

`elc` does not take the lines out one at a time here. It asks whether the
*region* produced any instruction at all:

*   **No line of it did, and it has an `#else` whose lines did** → inactive.
    Exactly one of two branches was compiled and the mapping says which. This
    is the strongest form the evidence takes, because nothing outside the
    region is consulted.
*   **No line of it did, and a line before it and a line after it both did**
    → inactive. The bracketing is what makes the absence mean something: the
    mapping was being written across this stretch of the file, so the gap is a
    gap and not the edge of what the build described.
*   **Some line of it did** → active, and an `#else` it has goes instead.
*   **Anything else** → still undecidable, and still counted so.

Regions settled this way are reported as **Regions decided by this build**,
separately from the ones a `-D` settled and from the ones nothing settled. The
three are different claims, and the separation is the point:

> A `-D` is what **you** say the configuration is, and it is consulted first —
> supply the defining `-D` and no region is decided from the image at all. The
> image is evidence about the build in front of `elc`: strong enough to act on,
> and not a proof. Two things can mislead it. An optimiser removing code makes
> a region look uncompiled (the same limit the line count carries), and a
> region holding only declarations or data produces no line entries whether or
> not it was compiled. Both err in the direction of excluding, both are visible
> in this figure, and neither is silent.

A region excluded whole is not pruned again line by line, so a run reporting
regions here reports fewer lines under **Lines not compiled by this build**.
The two figures divide one mechanism's work; they do not count it twice.

**One more thing follows.** `elc` refuses to expand the macros of a file
holding a region it could not decide, because the preprocessor would silently
resolve what `elc` had just declared unresolvable. A file whose only such
regions the image settles is no longer in that position, so it is expanded —
which is why supplying an image can raise the **Files expanded** count.

#### Absence of a line proves nothing where coverage was never established

A translation unit compiled **without** `-g` contributes no line entries at
all. Treating that absence as proof would delete the entire file — a smaller
report, internally consistent, and completely wrong.

So `elc` establishes coverage **per file** before excluding a single line in
it. A file the image's line information does not cover loses nothing, and is
counted instead. The two figures state both halves:

| Figure | What it says |
| ------ | ------------ |
| **Lines not compiled by this build** | what the finer filter removed |
| **Files with no debug coverage** | where it could not look |

Read them the way you read **Unresolved calls** and **Undecided regions**. A
large second figure beside a small first one tells you the report describes
your *source* more nearly than your *image*, whatever image you named — which
you could not infer from the metrics themselves.

The first is a count of **source lines excluded**, not a difference in ELOC.
The `#ifdef` and `#endif` lines above are excluded along with the region they
guard, and neither was effective code to begin with, so a run reporting four
lines pruned there moved ELOC by two.

#### Two things the optimiser does to this

**Pruning happens only inside functions the image defines.** Code at file scope
has few line entries to its name, and it is the one figure
[kept separate](#code-outside-a-function-is-kept-and-counted-on-its-own) on
purpose — a rule that pruned uncovered lines everywhere would delete exactly
that.

**An optimiser folds lines, and the mapping does not record that it did.** One
source line's instructions can be merged into the entry recorded for its
neighbour, and a line so folded is indistinguishable — in the mapping alone —
from one that produced no instruction. Nothing in the image records the
difference and `elc` does not guess at it.

In practice: at `-O0` the mapping is dense and the result tracks what the
source says. At `-O2` and above, expect **more** to be pruned than any
`#ifdef` excluded. A call the compiler folds to a constant emits nothing for
the function's body, so that whole body is pruned and the function reports an
ELOC of 0. That is a true statement about what shipped — the code really did
contribute no instructions — and it is why the two counts exist rather than
being left to be inferred. Neither is a defect to correct.

### Functions only the image can place

`ISR(USART0_DRE_vect)` is a macro that expands to a whole function definition.
To the compiler it is a function; to the grammar `elc` parses with, it is an
expression. No repair helps, because a repair does not know that the macro
*defines* a function. The image's debug information does, and says which file
and which line:

```text
Functions the image places that the parse did not reach (11; no figures are measured for them)
  Function     File                        Line
  -----------  --------------------------  ----
  __vector_12  /home/u/avrOS/sys/sys.c       44
  __vector_19  /home/u/avrOS/drv/uart.c      54
  ...
```

**Three columns, and the absence of the other six is deliberate.** `elc` has a
name and a location, from the image, and no body at all — so it has no ELOC, no
complexity, no maintainability index and no fan-in or fan-out. A row carrying
zeroes for those would state an absence as a measurement, which is the one
thing `elc` will not do. For the same reason such a function:

*   is **not** in the Functions table and **not** in the project's function
    count — every figure there is a measurement, and none of these was
    measured;
*   is **not** in the call graph. It has no parsed body, so it has no outgoing
    edges, and a fan-out of 0 for it would be a measurement of something nobody
    measured;
*   is **not** banded and **not** named by a threshold, for the same reason.

A call *to* one is counted among the **Unresolved calls** — where every call
the graph cannot represent is counted, a call into libc among them.

**The symbol table decides which functions these are.** The debug information
only decides *where* they are. The two disagree ordinarily rather than
exceptionally: a link that discards unused sections removes the code while the
compiler's entry describing it stays behind. A function the link dropped is
reported under **Functions the image does not define** and never here — on a
real AVR build, trusting the debug information alone would have listed 82
functions where 11 were real, and the other 71 were already named, rightly, in
the other table.

**Where `elc` can run the preprocessor, there is nothing to recover.** The
definition is expanded into place at its own line and measured like any other
function. This section is what a cross-compiled tree gets, where the host
toolchain cannot reach the target's headers — see
[macro expansion](#macro-expansion) for when that happens.

### Code outside a function is kept, and counted on its own

An initialised object at file scope is not a function, and the image's function
set says nothing about it. `elc` therefore **retains** it and reports the total
as **ELOC outside any function**. A file whose every function was filtered away
still reports the lines its data occupies — which is how you tell a file of
retained functions from a file of retained data. Folding that figure into the
totals would hide the one part of them the filter did not narrow.

### When one name is defined twice

Two translation units can both define a `static helper`. That is ordinary C,
and it is the one case where the image's symbol table alone cannot answer the
question `--elf` asks: the link produced two separate symbols and may have
kept or discarded them independently, so matching on the name retains both or
discards both — and either way one of them is wrong.

**With debug information, `elc` reads which file each function was written in**
and matches on the name *and* the file. A definition the link dropped is
reported among the functions the image does not define, beside its namesake
that survived:

```text
Functions the image does not define (2)
  Function  File                  Line
  --------  --------------------  ----
  helper    /home/u/proj/src/b.c     1
  used_b    /home/u/proj/src/b.c     2
```

Names are compared in the form the report presents them, so the spelling each
side happens to record does not matter. Debug information writes a C++
template instantiation as `serialize_seq<int>` and an out-of-line member
definition as plain `f`, where your source writes `serialize_seq` and
`S::f` — the same functions, and `elc` treats them as such.

**Where the debug information cannot answer, `elc` refuses the run.** Which of
two reasons it gives depends on what it found, because they have different
fixes. If the image carries no debug information at all:

```sh
$ elc --elf build/app src/
elc: helper is defined in src/a.c and src/b.c, and build/app carries no
debug information at all; rebuild the image with -g so the filter can tell
them apart
$ echo $?
2
```

If the image carries debug information but nothing about this function —
the usual cause being one translation unit compiled without `-g` while the
rest of the build had it:

```sh
$ elc --elf build/app src/
elc: helper is defined in src/a.c and src/b.c, and the debug information in
build/app describes no definition of it; the unit defining it was compiled
without -g, or the definition was never emitted
$ echo $?
2
```

Rebuilding the whole image with `-g` fixes the first and does nothing for the
second, which is why the two are not one message.

No report is written. Retaining both definitions overstates what the build
contains, and retaining the first is a guess wearing the authority of a
measurement — and a filtered figure you cannot trust is worse than none,
because nothing on the page distinguishes it from a correct one.

The refusal is narrow. A name defined once is unaffected. So is a name the
image does not define, since both definitions are excluded whichever was
linked and the ambiguity changes nothing you would see. And **debug
information is still not required**: an image built without it filters exactly
as it always did, wherever each name is defined once.

### What counts as a function the image defines

A symbol only counts as one of your program's functions if it is marked as a
*function* symbol **and** is actually defined by the image, rather than
merely imported from somewhere else, such as a shared library. That second
condition is easy to overlook, and it matters: without it, every function
your program merely *calls into* — like `printf` from the C library — would
count as one your image contains, and a source file that happens to define
its own `printf` would be retained as though your build had compiled it.

`elc` reads the image's full symbol table (`.symtab`) where one is present,
and falls back to the smaller, dynamically exported table (`.dynsym`) where
it is not. A `static` function appears in the full table but not the dynamic
one, so an image reduced to just its dynamic table yields a smaller set of
defined functions and a longer list of ones it does not define. Nothing is
hidden by that — the list says so explicitly.

### C++ and Rust names

C is the only supported language whose linkage name is simply its source
name. A C++ member function instead reaches the image encoded as something
like `_ZNK8geometry4Rect4areaEv`, and matching that raw text against your
source would retain nothing at all. `elc` decodes names following the
**Itanium C++ ABI** — the naming convention most C++ compilers use, which
also covers Rust's older ("legacy") mangling scheme — and reduces the result
back down to the identifier your report presents. So `geometry::Rect::area()
const` correctly matches a function reported simply as `area`. The scheme is
worked out from the name itself, not from a language you declare, because
one image may hold symbols produced by several different compilers.

A name using a scheme this build cannot decode — Rust's newer "v0" mangling,
say, if the installed C++ runtime isn't recent enough to understand it —
resolves to nothing and is **counted as unresolved**, never guessed at. You
will see it in the unresolved count rather than in a filter that quietly
kept less than you actually asked for.

### `elc` still invokes no toolchain

No `nm`, `objdump`, or `readelf` (the usual command-line tools for inspecting
a binary's symbols), no compiler, no linker, and no build system. `elc` opens
the file you named, reads its symbol table, and closes it.
It does not look for an image you did not name, and it does not need debugging
information — the symbol table a linker writes by default is enough, so the
option is not restricted to debug builds.

### When the image cannot be used

An image that is absent, unreadable, not actually a compiled binary (an
"object file," in ELF terms), or **carrying no function symbols at all**
ends the run with a diagnostic and no report, before any source file is
measured:

```
$ elc --elf build/app.stripped src/
elc: build/app.stripped: no function symbols; a stripped image defines nothing
to filter by
```

The stripped image is the case that most needs to fail. An empty function set
would otherwise filter every function away and report a project containing
none — a result that is confidently wrong and that you could not tell from a
correct one.

### The filter travels with the report

The image and both counts appear in the report and in the saved record, so a
report always states which image it describes and one regenerated from a record
still does. Because the filter is applied when a file is measured rather than
when a report is rendered, combining `--elf` with `--from-xml` is a usage error
rather than a silently ignored request: the record already describes one
filtered run, chosen when it was written.

## Languages

`elc` works out each file's language from its extension and loads the parser
for it on first use. Nothing about any language is built into the binary:

```sh
elc src/main.c      # .c → the C module in runtime/
```

| Language | Extensions |
| -------- | ---------- |
| C | `.c` `.h` |
| C++ | `.cc` `.cpp` `.cxx` `.hh` `.hpp` `.hxx` |
| Rust | `.rs` |
| Python | `.py` `.pyi` |

`.h` maps to **C**, not C++. A header shared by both is far more often C, and
a project that knows otherwise edits `runtime/extensions.map` — one line, no
rebuild.

Files of different languages in one target are analysed in a single
invocation, and each language's share of the totals appears in its own row:

```
Languages
  Language  Files  Lines  ELOC
  --------  -----  -----  ----
  c             9    812   402
  rust          4    233   118
```

### The numbers are not translations of each other

Two functions doing the same work in two languages may report different ELOC,
and that is correct rather than a defect. Each language's query files decide
what counts, and the languages genuinely differ:

* **Rust's grammar has no separate `else` node** — the alternative branch of
  an `if` is simply a block — so `} else {` is a line C counts and Rust does
  not.
* **Python's `pass` is excluded**, being the
  language's way of writing an empty block.
* **`import`, `with`, and `use` clauses are excluded**, as `#include` is:
  they name what a file depends on.
* **Rust's `static` counts and its `const` does not.** A `static` is storage
  that exists at run time; a `const` is inlined at every use.

Each of these is written down beside the rule it governs, in that language's
`.scm` files under `runtime/queries/`.

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

Where the grammar cannot follow part of a file, that part is set aside and
everything around it is measured. The run exits 1 and says what was lost:

```
elc: /home/u/proj/src/broken.c:88: 3 lines could not be parsed; the rest of the file is measured
```

Only where a file cannot be read or decoded at all is it left out entirely.
[When the parser cannot follow your code](#when-the-parser-cannot-follow-your-code)
below covers what the report shows, why partial measurement is the right
answer, and the one case — an unbalanced delimiter — where the damaged region
runs to the end of the file.

### Adding a language

Drop a grammar and its query files into `runtime/` and add one line to
`runtime/extensions.map`. No rebuild, no patch, no upstream release to wait
for. Six query files are required and three are optional — a module that omits
`conditionals.scm` has no conditional compilation, one that omits
`deadcode.scm` is analysed for everything else while the report states that
dead-code analysis was not performed for that language, and one that omits
`visibility.scm` reports every function's visibility as unknown rather than
guessing that it is public. The contract a module
must satisfy — the file names, the capture names, and what each means — is
`runtime/queries/README.md` in the distribution.

The report has the same shape whatever the target was — a single file, a
directory, or a repository — so results from different targets are directly
comparable.

### Options

| Option | Argument | Default | Effect |
| ------ | -------- | ------- | ------ |
| `-f`, `--format` | `table\|csv\|xml\|md\|html` | `table` | Render the report as `FORMAT` |
| `--from-xml` | `FILE` | — | Rebuild a report from a saved record; takes no `TARGET` |
| `-c`, `--complexity-threshold` | `N` | `15` | List functions whose complexity is `N` or greater |
| `-o`, `--output` | `FILE` | standard output | Write the report to `FILE` |
| `--entry` | `SYMBOL` | none | Declare `SYMBOL` an entry point for call-depth and reachability analysis; repeatable |
| `--scope` | `NAME:GLOB[,GLOB…]` | none | Declare an execution scope named `NAME` holding the matching files; repeatable |
| `-b`, `--bottleneck-threshold` | `N` | `5` | Flag a component whose `Ca` and `Ce` are each `N` or greater |
| `--stratum` | `NAME:GLOB[,GLOB…]` | none | Declare an architectural layer named `NAME` holding the matching files; repeatable |
| `--stratum-order` | `NAME>NAME[>NAME…]` | none | State the permitted direction of dependency between the declared layers |
| `--sink-authority` | `PCT` | `90` | Authority rank at or above which a function is a utility sink in the recovery view |
| `--sink-hub` | `PCT` | `10` | Hub rank at or below which a utility sink's calls count as near zero |
| `--god-betweenness` | `PCT` | `90` | Betweenness rank at or above which a function may be a god object |
| `--god-hub` | `PCT` | `90` | Hub rank a god object must also reach |
| `--core-depth` | `N` | `2` | Core depth below which a function is peripheral and left out of the recovery view |
| `-D`, `--define` | `NAME[=VALUE]` | none | Define a conditional-compilation symbol, so the metrics describe that configuration; repeatable |
| `--elf` | `FILE` | none | Restrict every measurement to the functions the linked image `FILE` defines |
| `--rules` | `LANG:PATH` | none | Check the source against the custom rule query in `PATH`, compiled for `LANG`; repeatable |
| `--graphml` | — | off | Also write the dependence graph as GraphML, named from `--output` |
| `--dsm` | — | off | Also write the dependency structure matrix as CSV, named from `--output` |
| `--manifest` | `FILE` | none | Read the purification manifest `FILE`, whose statements overrule what `elc` concluded |
| `--write-manifest` | — | off | Also write the purification manifest as JSON, named from `--output` |
| `--purify-dot` | — | off | Also write the raw and purified graphs as two Graphviz files, named from `--output` |
| `--no-dot` | — | `.dot` written | Do not write the annotated Graphviz call tree, which is otherwise written beside the report |
| `--no-expand` | — | expansion on | Measure the source as written, without expanding its macros |
| `--cc` | `PROGRAM` | `gcc` / `g++` | Preprocessor to expand with |
| `--cc-flag` | `ARG` | none | Pass `ARG` to the preprocessor; repeatable, for the include paths and defines the build needs |
| `-h`, `--help` | — | — | Print the usage summary to standard output and exit 0 |

```sh
elc --output report.txt src/     # results in the file, stdout empty
elc -o report.txt src/           # the same, short form
```

Redirecting with `--output` and redirecting with the shell produce the same
bytes; the option exists so that a caller that has no shell around it can
still separate results from diagnostics. If the file cannot be opened, `elc`
says so on standard error and exits 2 without writing a partial report.

Every option in the table above selects behaviour this build has. An option is
never added before it does something, so a switch `elc` accepts is a switch
`elc` acts on.

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

When set, it takes precedence over everything below, and is used exactly as
given — a variable naming a directory that is not there is reported against
that path rather than quietly falling back to one you did not ask for.

When unset, `elc` looks in two places relative to the executable and uses the
first that exists:

| Path | Layout |
| ---- | ------ |
| `<dir of elc>/runtime` | an unpacked self-contained copy |
| `<dir of elc>/../share/elc/runtime` | what `make install` produces |

So an `elc` installed to `/usr/local/bin/elc` finds its runtime at
`/usr/local/share/elc/runtime`, and one unpacked into a directory beside its
own `runtime/` finds it there. If neither exists, the diagnostic names both
paths it tried.

## The runtime directory

Everything language-specific lives here as data, never in the binary. Once
installed, it sits at `<prefix>/share/elc/runtime`; in a source checkout it
is `runtime/` at the top level, reached through a symlink the build creates
beside `build/elc`. The contents are identical either way:

```text
runtime/
├── extensions.map          # "<ext> <lang>", one pair per line
├── binary.exts             # extensions excluded from analysis
├── parsers/<lang>.so       # Tree-sitter grammar, exports tree_sitter_<lang>
└── queries/<lang>/
    ├── comments.scm  functions.scm  complexity.scm   # required
    ├── eloc.scm      calls.scm      globals.scm      # required
    ├── conditionals.scm    # optional — the language's conditional compilation
    ├── deadcode.scm        # optional — dead code within a function
    └── rules/*.scm         # your own coding standard, optional
```

The six required files are what every module must supply. The two optional
ones are genuinely optional rather than merely unwritten: a language with no
conditional compilation has no `conditionals.scm` to write, and a language
whose false literal the grammar cannot tell from an identifier your program
declared should ship no `deadcode.scm` rather than guess. An optional file that
is *absent* is a choice the contract allows; one that is *present and will not
compile* is a defect, and disables the whole module exactly as a broken
required file does.

Adding a language means adding a directory here — no rebuild, no patch, no
upstream release to wait for. The same mechanism is open to you: a team's own
coding standard is expressed as `.scm` queries and checked by the same engine
that produces the built-in metrics.

Every shipped module was added as data alone — no line of the executable
changed to support any of them, which is the claim `runtime/queries/README.md`
exists to make good.

`runtime/queries/README.md` is the contract: read it before writing a module,
and treat its file names and capture names as fixed — renaming one breaks
every grammar anyone has shipped.

## Troubleshooting

**`elc: no target given`** — `elc` needs at least one file or directory. Exit
status 2.

**`elc: unrecognised option '--foo'`** — the option does not exist in this
build. Run `elc --help` for the list this build accepts. Exit status 2.

**`elc: <path>: No such file or directory`** — a target does not exist. All
targets are validated before any is walked, so nothing was analysed. Exit
status 2.

**`elc: <path>: not a regular file or directory`** — the target is something
else the filesystem can name but `elc` cannot analyse: a socket, a FIFO (a
named pipe), or a device node. Exit status 2.

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

**`elc: 'x' is not a complexity threshold`** — `-c` takes a plain decimal
number. A sign, a hex literal, or a trailing unit is rejected rather than
half-read. Exit status 2.

**The threshold list is empty and you expected entries** — the listing is at
or *above* the threshold, and the default is 15, which most functions are
comfortably under. Try `-c 1` to see everything.

**`elc: '''yaml''' is not a format`** — the formats are `table`, `csv`, `xml`,
and `md`. Exit status 2.

**`elc: --from-xml produces Markdown`** — regeneration has one output format.
Drop the `-f`, or pass `-f md` if you prefer to say it.

**`elc: --from-xml takes no target`** — the record *is* the input. Naming a
target as well would give the report two sources of truth.

**CSV rows have a different number of fields than you expect** — they should
not; every field that needs quoting gets it. If you are reading the file with
`cut` or `awk` rather than a CSV parser, a quoted field containing a comma
will confuse them. That is a property of those tools, not of the file.

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

**`elc: <file>:<line>: N lines could not be parsed; the rest of the file is
measured`, exit 1** — the grammar could not follow something at that line.
`elc` runs no preprocessor, so a macro that expands to something syntactically
significant is the usual cause. The file is still measured: only those N lines
are missing, they are listed under **Partially parsed files**, and the project
total appears as **Unparsed lines** in the summary.

The commonest case in embedded C is a run of macros standing in for string
literals:

```c
printf(BOLD FG_BLUE "%-16s", name);   /* two macros before the first string */
```

`tree-sitter-c` accepts one identifier before the first string literal of a
concatenation but not two, so this does not parse — though `printf(BOLD "%s"
FG_BLUE)` does. It is a limitation of the grammar rather than of your code,
which the compiler accepts happily. Nothing is lost but those lines.

**`elc: <path>: No such file or directory` naming a runtime directory, exit
2** — `ELC_RUNTIME_DIR` points somewhere that is not there. The path is
reported exactly as you gave it.

**`elc: no runtime directory at <path>, <path>; set ELC_RUNTIME_DIR to name
one`, exit 2** — the variable is unset and neither path relative to the binary
exists. The message names both. Either put the runtime at one of them, or set
the variable:

```sh
ELC_RUNTIME_DIR=/path/to/runtime elc src/
```

## When something goes wrong on a tree you cannot share

`--dbg` writes a debug log beside the report, named from it the way every
companion is — an `--output` of `report.md` yields `report.dbg`:

```sh
elc --dbg -o report.md src/
```

It exists for the bug reports nobody can reproduce: a proprietary tree, a
build that only stands up on one machine, a grammar failing on source that
cannot be attached to an issue. Three things go in it.

**What was run.** The invocation, at the head of the file, because that is the
first question anyone asks of a log from a machine they do not have.

**Everything that went to standard error.** Every diagnostic the run wrote,
with a timestamp, plus any detail the diagnosing code had that was too long for
a terminal.

**The source that would not parse.** Not just the line number — the lines
themselves:

```text
elc debug log
2026-08-24T14:02:11Z  invocation: elc --dbg -o report.md src/

2026-08-24T14:02:11Z  parse failure  src/odd.c:412-414
       412 | template<> struct X<int, [](){}> {
       413 |   auto f() -> decltype(auto) requires C<T>;
       414 | };
2026-08-24T14:02:11Z  elc: src/odd.c:412: 3 lines could not be parsed; the
rest of the file is measured
```

That is the part worth having. A grammar that fails on a construct is debugged
from the construct, and a line number without its line names a place nobody
can visit. The recorded text is bounded and says how many lines it left out,
so a file the grammar could follow nowhere is not copied into the log entire —
which serves no one and may disclose more of a private tree than you meant.

**It is written as the run proceeds**, not saved up and flushed at the end. If
`elc` faults or is killed part-way through, the log still holds everything up
to the fault — which is exactly the run you wanted it for.

**It changes nothing else.** Standard error, the report, and the exit status
are identical with the option and without it. Asking for it with the report on
standard output writes no file and is not an error: there is no name to derive
one from, the same rule `--graphml` and `--dsm` follow.

## Findings: where a measurement falls, and on whose authority

Every section above **measures**. One section judges — and it is the first
section of the report, immediately after the project summary, ahead of every
table that supplies its evidence:

```text
Findings
  Severity  Measurement                 Subject    Detail                                Source
  --------  --------------------------  ---------  ------------------------------------  ----------------------------
  critical  complexity                  helper     cyclomatic complexity 16              McCabe (NIST SP 500-235)
  critical  fan-out                     dispatch   calls 22 distinct subroutines         Henry-Kafura
  critical  component dependency cycle  a.c        a.c -> b.c -> a.c                     Martin, acyclic dependencies
  warning   fan-in                      chomp      called by 31 distinct functions       elc heuristic — not a published standard
  warning   single-function global      config      named by one function; belongs at
                                                    block scope                          MISRA C Rule 8.9
  warning   bottleneck                  util.c     Ca 7 and Ce 6, each at or above the
                                                    threshold of 5                       elc heuristic — not a published standard
```

Ranked most severe first, because the list exists to be worked from the top.

**Every row names its source.** That column is the point of the section: with
two exceptions, `elc` does not invent its thresholds — it draws them from
named, published sources, so you can look up and argue with any line it
draws:

- **MISRA C** is a widely used coding standard for safety-critical C,
  published by the Motor Industry Software Reliability Association. It is
  the source for the recursion and single-function-global/hidden-channel
  rules.
- **Henry–Kafura** refers to information-flow metrics published by S. Henry
  and D. Kafura, which relate how likely a function is to contain a defect
  to how much data flows through it. It is the source for the fan-out
  bands.
- **McCabe** is the author of the cyclomatic complexity measure. The limit
  of 10 is his own; NIST SP 500-235 records limits as high as 15 as having
  been used successfully, and only where the practices exist to justify
  going past 10. Those two numbers are the warning and critical bands.
- **Martin** refers to Robert C. Martin's software design principles,
  which include the Instability metric and the rule that dependencies
  between components should never form a cycle.

Where a threshold *is* `elc`'s own, the column says so in as many words:

> `elc heuristic — not a published standard`

There are exactly three such thresholds today: the **bottleneck**, the
**fan-in** band, and the **maintainability** bands. If you disagree with a published threshold, take it up with
the standard it comes from; if you disagree with one of these two, it is only
`elc`'s opinion, and it is labelled as such so you know that's all it is.

### The bands

| Measurement | Bands | Source |
| ----------- | ----- | ------ |
| Cyclomatic complexity | ≤10 silent; 11–15 **warning**; >15 **critical** | McCabe (NIST SP 500-235) |
| Adapted Maintainability Index | ≥65 silent; <65 **warning**; <55 **critical** — the one that runs *downwards* | `elc` heuristic |
| Function fan-out | 0–2 below healthy, 3–7 healthy, 8–10 acceptable — all silent; 11–15 **warning**; >15 **critical** | Henry–Kafura |
| Function fan-in | ≤25 silent; >25 **warning**, with no critical band | `elc` heuristic |
| Call depth | >8 **warning**; >12 **critical** | embedded practice |
| Recursion | any occurrence **critical** | MISRA C Rule 17.2 |
| Component dependency cycle | any occurrence **critical** | Martin |
| Single-function global | **warning** | MISRA C Rule 8.9 |
| Hidden channel | **warning** | MISRA C Rule 8.9 |
| Instability vs. declared layer | **warning** on mismatch | Martin |
| Bottleneck | **warning** | `elc` heuristic |

The fan-out bands are **exhaustive**: every value classifies exactly once, and
three of the five bands produce nothing at all. A fan-out of 9 is acceptable
and silent — that is a result, not an oversight.

**The complexity bands are not the same thing as `--complexity-threshold`.**
That option decides which functions are *listed*; these decide which produce a
finding, and moving the option moves neither. If it did, the number in the
Source column would be yours rather than McCabe's.

**Three rows are `elc`'s own**, and each says so where you read them: the
bottleneck, the fan-in band, and the maintainability bands. Nobody has
published a fan-in threshold, so 25 is this project's judgement — and there is
no critical band, because `elc` has no published basis for a first line and
none whatever for a second. The maintainability bands are `elc`'s for a
subtler reason, set out under
[The Adapted Maintainability Index](#the-adapted-maintainability-index): the
index is published, but this build adapts the formula, and the published
thresholds were calibrated for the term the adaptation replaced.

### The threshold listing

Every function a band names is collected into one table, alongside the
functions at or over the complexity threshold `--complexity-threshold` sets:

```text
At or over a threshold (complexity listed at 15; complexity, fan-in and fan-out banded)
  File                  Function  Complexity  Fan-in  Fan-out  Severity
  --------------------  --------  ----------  ------  -------  --------
  /home/u/proj/src/a.c  parse             12       3        7  warning
  /home/u/proj/src/b.c  dispatch           4       1       22  critical
  /home/u/proj/src/b.c  helper            16       2        1  critical
```

The **Severity** column is the highest band any of the three measurements put
the function in. It is blank for a function that is here only because its
complexity met the listing threshold: that threshold has never carried a
severity and does not start now.

Where the **Findings** table says what crossed a line, this says which
functions did, with the figures beside them — it is the short list the long
one is read through.

### Why the lines sit where they do

A threshold you cannot argue with is one you cannot act on, so here is the
reasoning behind each one:

**Fan-out.** The healthy band of 3–7 is delegation working as intended:
enough helper calls that the function composes rather than doing everything
itself, few enough that a reader can hold them all in mind at once. Below 3
is not necessarily a virtue — it is often a thin wrapper that earns nothing
— which is why it reads as *below healthy* rather than as *best*. Past 15
the function has stopped delegating and become a dispatcher: it violates the
**Single Responsibility Principle** (the idea that a function or module
should have one clear job) in a form you can literally count, and it is
nearly impossible to isolate for unit testing, because every one of those
callees has to be stood up first.

**Call depth.** This one is about the call stack — the region of memory that
tracks nested function calls — and the numbers come from embedded-systems
practice rather than a published standard, which is why the source column
says so. Each layer of nested calls costs one **stack frame** (a block of
memory holding that call's local variables and return address), and on a
target with only a few kilobytes of stack available, the depth multiplied by
the average frame size is the whole budget. Past 8 layers there is little
margin left for interrupt handling on top of it; past 12, the stack
colliding with the heap (running out of memory entirely) stops being a
theoretical risk. On a full-sized computer with megabytes of stack, the
same figure is more a readability observation than a safety concern, which
is why it is only a warning and not treated as a failure.

**Recursion.** MISRA C Rule 17.2 forbids it outright, and the reason is not
taste: with recursion the worst-case stack depth is a function of the input
rather than of the program, so it cannot be computed ahead of time. That is
also why `elc` reports the cycle *instead of* a depth figure — the number does
not exist.

**Component cycles.** This follows Martin's acyclic-dependencies principle:
dependencies between files should never form a loop. The acceptable count is
strictly zero because a cycle is not a matter of degree — the files caught
in it are effectively one unit, however many there are, and cannot be
built, tested, or understood separately from each other. Breaking a cycle is
an all-or-nothing act, so the finding is reported the same way.

**Single-function global and hidden channel.** Both are MISRA C Rule 8.9, from
opposite directions. A global only one function names should be a local — the
scope is simply wider than the use. A global shared across parts of the program
that never call each other is the dangerous case: nothing in the code says
which runs first, and the program works only as long as the order holds.

**Bottleneck.** `elc`'s own heuristic, and the only row here that is. The
rule requires `Ca` **and** `Ce` to both be at or above the threshold,
because either alone is ordinary: a file that's widely used but depends on
little itself is simply stable, and a file that depends on many others but
few things depend on is merely sitting high in the architecture. Both at
once is the trouble — hard to change because so much rests on it, hard to
isolate because it rests on so much. The default of 5 is a starting point
rather than a finding from research, and `-b` moves it.

### Three things findings are not

**They are not a gate.** A severity is a label. `elc` exits 0 on a project full
of critical findings, provided every file was read. The exit status is reserved
for failures — an unreadable file, a bad argument — because deciding what a
critical finding warrants is your call, not the tool's:

```sh
elc src/ && echo "ran cleanly"   # prints, even with criticals reported
```

If you want CI to fail on findings, grep the report or the record for the
severities you care about. `elc` will not decide that for you.

**They are not advice.** A finding says what was measured, where, and which
standard places it outside the range. It does not tell you to split the
function, and it will not rank one design above another beyond what the cited
source already says.

**They do not replace the measurements.** A value inside its band is still
reported in the table that measured it. The findings list is the subset that
crossed a line; the tables above it are the whole picture.

## Macro expansion

A macro standing where the grammar expects a keyword, a type, or a string is a
parse error, and no grammar can fix it — `MACRO MACRO` is genuinely ambiguous
with `Type var`. So before parsing a file, `elc` runs the language's own
preprocessor over it and parses the result:

```c
#define local      static
#define BOLD       "\033[1m"

local int branchy(int n) { ... }
printf(BOLD "value: %d\n", n);
```

Unexpanded, both lines defeat the parser. Expanded, they are ordinary C.

**Only your file survives the expansion.** Raw preprocessor output is not
something you would want measured: nineteen lines of C came back as 829, with
the functions of every header pulled in appearing as functions of the file, and
every line number moved. `elc` filters the output by the `# linenum "file"`
markers the preprocessor emits, keeping only the lines attributed to the file
being analysed — a system header, a project header, `<built-in>` — everything
else is discarded. One `#include <iostream>` costs you nothing: 68,468 lines of
expansion filtered back down to the file's own 21.

**Your line numbers do not move.** The filter pads with blank lines so every
retained line sits where it sits in your file. A function reported at 21–70 is
at 21–70 in the file you wrote, expansion or not.

### Telling `elc` how your project builds

`elc` does not know your build and will not guess. An include path it invented
would read a header you never named, and reaching the *wrong* header is worse
than reaching none — the expansion would succeed and be wrong. So you supply
what the build needs:

```sh
elc --cc-flag -Iinclude --cc-flag -DNDEBUG src/
```

Without them, a project whose headers are not beside its sources simply falls
back, which brings us to:

### When expansion does not happen

It often does not, and that is fine. A cross-compiled tree cannot be
preprocessed by a host compiler at all — an AVR project fails on `avr/io.h`
before it reaches a macro. A machine with no compiler installed cannot expand
anything. In every such case `elc` parses the file as written and completes the
run, giving you exactly the report it would have given before expansion
existed.

Because two files in one report may then have been measured two different ways,
and nothing in the figures says which, `elc` tells you:

```text
Project summary
  ...
  Files expanded        16
  Measured as written    6

Measured as written (macros not expanded)
  File            Why
  --------------  ----------------------------------
  src/arch.c      the preprocessor rejected the file
  src/graph.c     the preprocessor rejected the file
```

`--no-expand` turns expansion off entirely. Reach for it when two machines must
agree: an expansion depends on the headers installed where it runs, and
unexpanded source does not.

**A file that falls back is repaired instead**, which is the next section.

## Repairing what neither the grammar nor the preprocessor could follow

Falling back used to mean losing the lines a macro made unparsable. It no
longer does. Where a file was not expanded and the grammar still cannot follow
part of it, `elc` rewrites the rejected region in its own copy of the buffer
and parses again:

| The shape | What replaces it |
|---|---|
| An upper-case name beside a string literal — `printf(BOLD "%d" RESET, n)` | an empty string literal, which concatenates to the same string |
| An upper-case name in front of a declaration — `local int f(void)` | nothing; the declaration beneath it parses |
| An upper-case name where a declarator belongs — `ARR = { 1, 2, 3 };` | `int` before it, making it a definition |

This is a **guess about the shape of a failure**, where expansion is an exact
answer — which is why it runs only where expansion did not. A file whose macros
the preprocessor resolved is never guessed at.

It matters because expansion reaches less than you might expect. On a 49-file
AVR project, three files expand; the rest cannot, because a host compiler
cannot find `avr/io.h` and because `elc` will not expand a file whose `#if` it
could not decide. Repair covers those:

| | unparsed lines |
|---|---|
| neither | 54 |
| expansion alone | 50 |
| repair alone | 9 |
| both | **8** |

Three properties make it safe to trust:

- **Only rejected regions are rewritten.** Text the grammar accepted is never
  touched, so the worst a wrong guess can do is fail to fix something that was
  already broken.
- **Every repair spans the same number of lines it replaced.** Nothing beneath
  it moves, so every line number in the report still points where it did.
- **Your files are never modified.** The rewriting happens in memory.

And because a repair is a guess, the report says it made one:

```text
Repaired regions (rewritten in elc's buffer to be measured; the files are untouched)
  File        Rule                        Repairs
  ----------  --------------------------  -------
  drv/uart.c  macro adjacent to a string       29
  drv/uart.c  macro before a declaration        3
```

That count carries no severity and does not affect the exit status. Read
alongside the provenance table above, it tells you which files were answered
exactly and which were guessed at.

Repair runs in passes and stops when a pass rewrites nothing or fails to reduce
the damage; a pass that does not help is withdrawn whole. So it terminates on
source no rule fits, which is most of the source it will ever meet.

### C library use MISRA constrains

MISRA C:2012 §21 names the C library facilities a compliant program does not
use. `elc` reports each call to one as a **warning**, citing the rule:

```text
Findings
  Severity  Measurement    Subject  Detail                                                    Source
  --------  -------------  -------  --------------------------------------------------------  ------------
  warning   misra library  malloc   malloc is not available to a compliant program (Rule 21.3) MISRA C:2012
  warning   misra library  printf   printf is not available to a compliant program (Rule 21.6) MISRA C:2012
  warning   misra library  atoi     atoi is not available to a compliant program (Rule 21.7)   MISRA C:2012
```

The rule number is there so you can look it up and disagree with it. `elc` is
citing somebody else's published position, not offering one of its own — a
great many programs use these facilities correctly and have no obligation to
MISRA at all. No advice comes with the finding, and it does not affect the exit
status.

**Reported by function, never by header.** `<stdlib.h>` supplies `abs`, which
MISRA permits, beside `malloc`, which it does not — so including it is not the
finding, calling one is. You get the file and the line, because a reader fixing
one needs to find it and a function calling `malloc` twice has two things to
change.

Functions your own project defines are not reported, even where they share a
name with a constrained one: the rule is about the standard library's `system`,
not about yours.

### What your code depends on

The filter sees every header the preprocessor opened, so it can tell you what
it would take to build this somewhere else:

```text
Standard-library dependence
  File       Library  Headers  Which
  ---------  -------  -------  ---------------------------------
  app.cpp    C             11  wchar.h stddef.h stdarg.h locale.h ...
  app.cpp    C++           25  iostream ostream ios iosfwd cwchar ...
```

A fact, not a finding: it carries no severity, reaches no exit status, and
comes with no advice. Depending on the standard library is the ordinary case
for most programs. If you are heading for a freestanding or embedded target
that has no `<iostream>` and often no `malloc`, this is the list of what stands
in the way. If you are not, skip it.

A file that fell back reports nothing here — that is the absence of an answer,
not the answer "none". The provenance table above says which files could be
asked.

## Reading the Functions table

```text
Functions
  File                     Language  Function          Visibility  Lines  ELOC  Complexity  Fan-in  Fan-out   MI
  -----------------------  --------  ----------------  ----------  -----  ----  ----------  ------  -------  ---
  /home/u/proj/src/a.c:21  c         parse             public         50    31           9       3        7   62
  /home/u/proj/src/a.c:88  c         parse_one_header  private        14     9           3       1        2   81
```

**The File column is a location you can act on.** `path:line` is the form
editors and terminals already understand — ctrl-click it in VS Code's terminal
and you land on that line of that file. The path is absolute, deliberately: a
relative one would resolve against whatever directory your terminal happens to
be in, which is the one thing a navigable reference must not depend on.

**Language is the file's, not the function's**, and it repeats down every row
belonging to one file. It is here anyway, because this is the table you are
already looking at when you ask what language a function is written in, and
sending you to the Files table to answer it costs you more than the repetition
costs the page. It sits immediately after the location, which is where the
Files table has put it all along.

**Visibility says whether the language exposes the function** outside the file
or module that defines it. It is the first thing anyone asks of an unfamiliar
module — which of these are the interface, and which are its internals — and
the source already states it:

| language | `private` when | basis |
|---|---|---|
| C | `static` at file scope | linkage |
| C++ | `static`, or an anonymous namespace | linkage |
| Rust | no `pub` | the language's own rule |
| Python | a leading underscore (but not a dunder) | PEP 8 convention |

These are not the same kind of fact and `elc` does not pretend they are. C and
C++ report **linkage** — whether the linker can see the name — which is a
harder guarantee than Python's naming convention, and a different question
from C++ class access control. A `private:` method of an exported class is
reported **public** here, because it has external linkage and the linker does
see it; the access specifier answers a question about callers, not about the
program's interface.

A language whose module supplies no `visibility.scm` reports `—` for every
function. That is *not analysed*, which is a different claim from *public*.

**Lines is a count**, not a range: how many lines the function occupies. Its
first line is in the location beside it, so the range is the two read
together — and a count is the figure you compare between two functions, where
a range is something you have to subtract first.

**XML does not follow this**, and CSV no longer differs. The `xml` record
carries separate `start-line` and `end-line` fields, because it must be enough
to rebuild a report from and a rebuild needs both numbers as numbers. The `csv`
record is this table for a consumer that loads it rather than reads it, so it
carries these columns and not others — see
[The CSV record is the Functions table](#the-csv-record-is-the-functions-table-loaded-rather-than-read).

## When the parser cannot follow your code

`elc` parses source as written and runs no preprocessor (that is deliberate —
see below). A grammar occasionally meets something it cannot follow, and when
that happens **only the lines it could not read are lost**:

```text
Project summary
  Physical lines  5151
  ELOC            1006
  Unparsed lines    35

Partially parsed files (measured except for these lines)
  File                Unparsed lines
  ------------------  --------------
  drv/mem.c                       13
  drv/uart.c                       5
```

Every function around the damage is measured normally and appears in the usual
tables. The count is what tells you how far to trust the figures: 35 unparsed
lines in 5151 is noise, and 35 in 60 is not.

**Why not simply reject the file?** `elc` used to, and it was badly wrong. A
single unparsable macro damages one line; discarding the whole file over it
threw away every metric in it. On one embedded project, that turned
0.1%–1.4% of damaged code per file into the loss of half the codebase and
137 correctly parsed functions. `elc` parses a language without actually
compiling it, so gaps in its understanding of the grammar are permanent —
there is no way to close all of them. Tolerating a small amount of local
damage is therefore the general defence, not a one-off patch for a single
grammar's shortcoming.

There is one limit to know about. An **unbalanced delimiter** — a stray `(`
or `{` with no matching closer — leaves the parser with no way to find its
footing again ("resynchronise") and keep reading, so everything after it
counts as damaged. That is not silent: the line count covers what was lost,
so a large figure next to a small file is telling you the remaining metrics
are thin.

The run's exit status is 1 whenever any file was partly unparsed, because
something in it went unanalysed.

## Getting more detail

- `man elc` — the reference form of this material
- `runtime/queries/README.md` — the contract a language module or a custom rule
  is written against
- `doc/PVD.md` — why `elc` exists, and what it will and will not do

If you are working on `elc` itself rather than using it, the development
documents — the requirements, the design, the test plan, and the traceability
between them — are under `doc/` in the source distribution.

## License

MIT. See `LICENSE`.
