# elc User Manual

**Version:** 0.7 (Phase 6)
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
one, and reports **effective lines of code** and **cyclomatic complexity** per
function — with project totals, a per-language breakdown, and a list of the
functions over a complexity threshold you set. It writes that report as a
table, Markdown, CSV, or XML, and can rebuild it later from the XML alone. It
ships with five languages: C, C++, Rust, Python, and Ada.

**What it does not do yet:** the System Dependence Graph and the findings
derived from it — coupling, cycles, call depth, dead code. Those arrive in
later phases.

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
directory is discovered by one of two routes, and `elc` tells you which one it
used — see [The discovery route](#the-discovery-route) below.

#### In a Git repository

If the target directory is tracked by a Git repository, `elc` analyses **the
files tracked at `HEAD`**. That is usually what you want, and it is the reason
the two routes exist: a source tree with a build directory in it has more
generated code than written code, and a filesystem walk counts all of it.

The consequences are worth stating plainly, because they are what makes this
route different rather than merely faster:

- **A file you have not committed is not analysed.** A new file you have
  written but not `git add`ed is invisible to `elc`, and so is an
  uncommitted change to a tracked file — the analysis is of the tree at
  `HEAD`, not of your working directory.
- **Nothing consults `.gitignore`.** Files are excluded because git does not
  track them, which is the same answer `git ls-files` gives and is not an
  interpretation of the ignore rules.
- **Binary files are excluded by content**, not by name, so a tracked blob
  that happens to end in `.c` is still left out.
- **Naming a subdirectory analyses that subdirectory**, not the repository
  around it.

Hidden entries and binary extensions are excluded here exactly as they are
below. Both routes answer the same question about a given directory; they
differ only in which files they can see.

#### Anywhere else

If the target is not in a repository, or is in one that does not track it — a
`.gitignore`d build directory, or anything under a version-controlled home
directory — the directory is walked recursively, and the walk leaves out:

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

Functions
  File                  Function    Lines  ELOC  Complexity
  --------------------  ----------  -----  ----  ----------
  /home/u/proj/src/a.c  parse        5-19     9           7
  /home/u/proj/src/a.c  emit        21-24     3           1
  /home/u/proj/src/b.c  main         3-11     6           2

At or over the complexity threshold (5)
  File                  Function  Complexity
  --------------------  --------  ----------
  /home/u/proj/src/a.c  parse              7

Skipped files (no language module)
  /home/u/proj/src/notes.md
```

Eleven sections: the project totals, the callouts, the discovery route applied
to each directory target, those totals broken down by language, one row per
file, one row per function, the functions at or over the complexity threshold,
each function's fan-out, any recursion, the deepest call chain, and whatever
was skipped. Paths are canonical and absolute, and each column is
padded to its longest value.

`Discovery` has a row per *directory* target only; a file named directly is
analysed with no traversal, so there is no route to report for it.

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

## Cyclomatic complexity

Complexity is **one plus** the number of decision points in a function: the
number of paths through it. A function that never branches is 1.

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

## The complexity threshold

`elc` lists the functions whose complexity is **at or above** a threshold:

```sh
elc src/                # the default, 15
elc -c 10 src/          # list anything 10 or greater
elc -c 1 src/           # list everything
```

```
At or over the complexity threshold (10)
  File                  Function  Complexity
  --------------------  --------  ----------
  /home/u/proj/src/a.c  parse             17
```

**The threshold changes what is listed and nothing else.** Not a total, not a
callout, and — this is the part worth being clear about — **not the exit
status**:

```sh
elc -c 1 src/ ; echo $?     # lists everything, exits 0
elc -c 999 src/ ; echo $?   # lists nothing, exits 0
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

```sh
elc src/                 # an aligned table, the default
elc -f md src/           # GitHub-Flavored Markdown
elc -f csv src/          # one record per function, RFC 4180
elc -f xml src/          # the complete record of the run
```

| Format | For | Notes |
| ------ | --- | ----- |
| `table` | reading | The default |
| `md` | a pull request, a wiki | Same tiers as the table, in the same order |
| `csv` | a spreadsheet, another tool | Complete dataset; the threshold does not filter it |
| `xml` | keeping | Complete record; what `--from-xml` reads back |

**`table` and `md` are the same report.** They are one traversal of one
model, so a tier cannot appear in one and be forgotten in the other. Only the
decoration differs.

**`csv` and `xml` are unfiltered.** The complexity threshold governs the
*listing* tier of a human-facing report. CSV has no such tier — it is the
complete dataset, and filtering is what the tool you pipe it into is for:

```sh
elc -f csv src/ | cut -d, -f3,7      # function names and complexities
```

CSV carries per-function metrics only. The architectural findings that later
phases add are absent by design: they are not expressible as one flat record
set, which is what XML is for.

Every field containing a comma, a quotation mark, or a line break is quoted,
so a name like `foo<int, long>` stays one field.

## Keeping a record

`-f xml` writes a record that is sufficient on its own to produce a report
later, without the source it describes:

```sh
elc -f xml -o build/metrics.xml src/     # today
elc --from-xml build/metrics.xml         # any time after
```

The regenerated Markdown is **byte-identical** to what a direct analysis
would have produced at the same threshold. Not similar — identical, so a diff
of the two is empty.

That is achievable because the record carries the *measurements* and nothing
derived from them. The totals, the callouts, the ordering, and the threshold
listing are all worked out when the report is assembled, by the same code on
both paths. There is no second implementation to drift.

The discovery route is in the record for the same reason a measurement is:
it is something the run observed and the report presents, and it cannot be
worked out again later — by the time you regenerate, the tree may not be a
repository, or may not exist. So a regenerated report still tells you how the
files it describes were found.

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

From Phase 8 `elc` resolves the calls between your functions into one
project-wide directed graph — the **System Dependence Graph**. Every analysis
from here on reads it: fan-out, call depth, dead code, coupling, cycles.

Nothing is re-read to build it. The graph comes from the same single parse
that produced the metrics, which is why analysing a project costs one pass
over each file however many questions are asked of the result.

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
worse than a missing one: the dead-code analysis of a later phase proves that
nothing calls a function, and one invented edge would make that proof wrong.

### Fan-out

For every function, the number of **distinct subroutines it invokes**:

```text
Fan-out (distinct callees)
  File                  Function  Fan-out
  --------------------  --------  -------
  /home/u/proj/src/a.c  parse           7
```

Distinct, not call sites. A function that calls one helper in a loop body and
again in its error path is coupled to *one* thing, and reports a fan-out of 1.

`elc` reports the number and does not judge it. What counts as too many
arrives in a later phase, along with the rest of the threshold catalogue.

### Recursion

```text
Recursion
  Kind    Functions
  ------  -----------
  direct  fact
  mutual  ping, pong
```

Direct recursion is a function that calls itself; mutual recursion is a group
that can reach each other. Both are found the same way, and both matter for
the same reason: MISRA C Rule 17.2 forbids recursion because it makes worst-
case stack depth unpredictable, which on a target with a few kilobytes of
stack is a crash waiting for the wrong input.

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

A **component is a source file**, and that is the unit for everything in this
section and the two below it. A dependency runs from file X to file Y when any
function in X calls any function in Y, or writes a global a function in Y
reads.

`Ca` counts the components that depend on this one, `Ce` the ones it depends
upon. Both count **components, not calls**: a file calling another in forty
places depends on it once.

**Instability** is `Ce / (Ce + Ca)`. Approaching 0 it means maximum stability —
widely depended upon, depending on little, and dangerous to change. Approaching
1 it means the opposite: freely changeable, because nothing rests on it.

**Where both couplings are zero it is `undefined`, not `0.00`.** A file nothing
depends on that depends on nothing is entirely ordinary — a lone file in a
single-file target is exactly that — and reporting zero there would claim
maximum stability for a component that has no relationships at all.

A component whose `Ca` **and** `Ce` are each at or above the bottleneck
threshold is flagged: it is simultaneously depended upon widely and dependent
widely, so it is both dangerous to change and hard to isolate for testing.
Both, not either — a widely-used leaf is stable, not a bottleneck.

```sh
elc -b 3 src/     # lower the bar from the default 5
```

**This threshold is `elc`'s own heuristic and says so on every row it flags.**
Everything else `elc` bands comes from a published source; this one does not,
and presenting it beside Henry–Kafura and MISRA without saying so would lend it
authority it has not got.

### Component dependency cycles

```text
Component dependency cycles
  Components                  Example loop
  --------------------------  ----------------------------------------
  /u/p/a.c, /u/p/b.c, /u/p/c.c  /u/p/a.c -> /u/p/b.c -> /u/p/c.c -> /u/p/a.c
```

Files that depend on each other, directly or through a chain. Two columns
because one alone misleads: the **group** is what has to be broken up, and the
**loop** is which edge to cut. Where a group holds several overlapping loops
one is shown — enumerating them all is exponential in the group's size, and one
witness is what you act on.

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

The order layers are **first declared is the permitted direction of
dependency**, topmost first. So above, `app` may depend on `hal`, and `hal` on
`drv`. State it explicitly instead if you prefer:

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

A file matching no `--stratum` lies outside the partition rather than in a
layer of its own, so a call touching it is neither finding. And a layer whose
pattern matches nothing is reported on standard error and kept — dropping it
would renumber the layers below and change what everything else is compared
against.

Only **calls** are checked here. A global two layers happen to share is a
different fact, with its own findings in Global state and `--scope`.

With no `--stratum` at all the section states that it was omitted. The coupling
table above it is still produced.

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

**A clique of unused functions is correctly reported.** If `clique_a` and
`clique_b` call each other and nothing else calls either, a rule looking for
"a function nothing calls" finds a caller for each and reports neither. A
traversal finds both, because no path leads into the pair from any root.

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

An object every one of whose accessing functions is unreachable is unreachable
itself. An object *no* analysed function touches is deliberately not claimed:
it may be written from file scope, or from a translation unit outside what you
pointed `elc` at.

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

**Scope reduction.** Only one function names the object, so it belongs at
block scope inside that function. Note that this case produces no edge in the
graph at all — a state edge joins a writer to a reader, and there is only one
function here — which is why the finding is computed from the access sets
rather than from the edges.

**Hidden channel.** The object is shared between functions lying in
*disconnected* regions of the call graph. The functions never call each other,
so nothing in either region says that one must run before the other, and yet
the program depends on it. That is temporal coupling, and it is the failure
mode that survives every code review because no single file shows it. The
finding names the disconnected groups, because the grouping is the finding.

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

**What `elc` deliberately will not tell you.** There is no data-flow analysis,
no constant propagation, and no evaluation of expressions. `x = 0; if (x)` is
not reported, and neither is `const int zero = 0; if (zero)`, however clearly
you can see the answer. Nor is `if (0x0)`, because the query matches a decimal
zero and nothing else.

That is a deliberate trade and it runs one way: a missed statement costs you a
cleanup opportunity, and a false claim invites you to delete code that runs —
a defect this tool would have introduced. Where both cannot be had, `elc`
reports nothing.

A `goto` label following a `return` is *not* reported, for the same reason. It
is reachable, and a tool that flagged it would be telling you to delete a live
branch target.

**Support is per language, and its absence is stated.** A language module may
supply a dead-code query or not; the four that do are C, C++, Python and Rust.
Ada does not, on purpose — it writes its false literal as an ordinary
identifier the grammar cannot distinguish from one your program declared, and
guessing would risk exactly the false claim above. When a language has no
query the heading says so:

```text
Dead code within functions (not analysed for: ada)
```

*Not analysed* and *none found* are different claims. A reader who cannot tell
them apart has been told nothing, so `elc` never renders the first as the
second.

### Execution scopes

Some targets run several components that share one memory map and one symbol
table — a host-driven sequential test harness, or a bootloader beside the
application it starts. Nothing in the source says where one ends and the next
begins, so you say it:

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

A file matching no declaration lies outside the partition rather than in a
scope of its own: you said nothing about it, and inventing a boundary would
report violations against a division nobody drew. With no `--scope` at all the
analysis is omitted with the reason stated.

### Exporting it

```sh
elc --graphml -o report.md src/     # writes report.md and report.graphml
```

GraphML is a standard graph format that igraph, NetworkX, Gephi and yEd all
read, so the graph can be queried and drawn by tools that already know how.
The export is off unless you ask for it, and its name comes from `--output`
by substituting the extension — it takes no path of its own. With the report
going to standard output there is no name to derive, so no file is written.

The node attributes are `name`, `file`, `line-start`, `line-end`,
`component`, `eloc`, `complexity`, `fan-out` and `address-taken`; edges carry
`kind` (`call` or `global`), the object's name on a global edge, and
`call-sites` on a call edge.

### Where the graph is imprecise, and in which direction

`elc` resolves calls by name across the files you gave it. Three consequences
are worth knowing, because they affect how much weight to put on a number:

- **A name defined twice resolves to the first definition** in sorted file
  order, and `elc` says so on standard error. Two `static` helpers of the
  same name in two files is ordinary C; the edge may go to the other one.
- **A method call resolves on the method name**, not on the receiver's type.
  Working out which class a call lands on needs type resolution, which a
  grammar does not do.
- **Ada writes an array index exactly like a function call.** `Table (2)`
  and `Scale (2)` are the same syntax, and the grammar manages the ambiguity
  rather than resolving it. In practice the index resolves against no
  subprogram and is counted as *unresolved*, which is visible. It becomes a
  wrong edge only if an array shares its name with a subprogram somewhere in
  the project.

`elc` does not correct for any of these, and the reason is the same each
time: the correction would have to live in the binary and would encode one
language's semantics there, which is exactly what makes adding a language a
data change rather than a code change.

**The first of the three can produce a false unreachable claim, and that is
worth knowing before you delete anything.** If two files each define a
`static` helper called `grow`, every call to either resolves to the first,
and the second has no incoming edge — so reachability reports it dead when it
is not. This is the one place `elc` errs toward *un*reachable rather than
toward reachable, and it is a limit of name-only resolution rather than a
finding.

It is visible, and the diagnostic is how:

```text
elc: grow is defined 5 times; calls to it resolve to /home/u/proj/src/analyze.c:127
```

**Read standard error before acting on the unreachable list.** A function
named there and reported unreachable is a duplicate-name artefact, not dead
code.

**The same artefact reaches the component analyses, and there it is worse.**
Every call to the duplicated name resolves into one file, so that file gains
afferent coupling it has not earned, the others lose it — and if the file
holding the winning definition already depends on one of the losers, the
invented edge **closes a dependency cycle that does not exist**. A false
circular dependency is a more expensive mistake than a false dead-code claim,
because it points at an architecture problem rather than at a line to delete.

`elc` analysing its own source demonstrates both: six files define a `static
grow` helper, one wins, and the report shows a cycle between the winner and one
of the losers. The diagnostic naming the duplicate is the tell in every case.
Give the helpers distinct names, or read the two outputs together.

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
| Ada | `.adb` `.ads` |

`.h` maps to **C**, not C++. A header shared by both is far more often C, and
a project that knows otherwise edits `runtime/extensions.map` — one line, no
rebuild.

Files of different languages in one target are analysed in a single
invocation, and each language's share of the totals appears in its own row:

```
Languages
  Language  Files  Lines  ELOC
  --------  -----  -----  ----
  ada           2    140    61
  c             9    812   402
  rust          4    233   118
```

### The numbers are not translations of each other

Two functions doing the same work in two languages may report different ELOC,
and that is correct rather than a defect. Each language's query files decide
what counts, and the languages genuinely differ:

* **Rust has no `else` node** — the alternative of an `if` is just a block —
  so `} else {` is a line C counts and Rust does not.
* **Python's `pass` and Ada's `null;` are excluded**, both being the
  language's way of writing an empty block.
* **`import`, `with`, and `use` clauses are excluded**, as `#include` is:
  they name what a file depends on.
* **Rust's `static` counts and its `const` does not.** A `static` is storage
  that exists at run time; a `const` is inlined at every use.
* **Ada's `and then` is a decision point and plain `and` is not**, because
  only the first may skip its right operand.

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
| `-f`, `--format` | `table\|csv\|xml\|md` | `table` | Render the report as `FORMAT` |
| `--from-xml` | `FILE` | — | Rebuild a report from a saved record; takes no `TARGET` |
| `-c`, `--complexity-threshold` | `N` | `15` | List functions whose complexity is `N` or greater |
| `-o`, `--output` | `FILE` | standard output | Write the report to `FILE` |
| `--entry` | `SYMBOL` | none | Declare `SYMBOL` an entry point for call-depth and reachability analysis; repeatable |
| `--scope` | `NAME:GLOB[,GLOB…]` | none | Declare an execution scope named `NAME` holding the matching files; repeatable |
| `-b`, `--bottleneck-threshold` | `N` | `5` | Flag a component whose `Ca` and `Ce` are each `N` or greater |
| `--stratum` | `NAME:GLOB[,GLOB…]` | none | Declare an architectural layer named `NAME` holding the matching files; repeatable |
| `--stratum-order` | `NAME>NAME[>NAME…]` | none | State the permitted direction of dependency between the declared layers |
| `--graphml` | — | off | Also write the dependence graph as GraphML, named from `--output` |
| `-h`, `--help` | — | — | Print the usage summary to standard output and exit 0 |

```sh
elc --output report.txt src/     # results in the file, stdout empty
elc -o report.txt src/           # the same, short form
```

Redirecting with `--output` and redirecting with the shell produce the same
bytes; the option exists so that a caller that has no shell around it can
still separate results from diagnostics. If the file cannot be opened, `elc`
says so on standard error and exits 2 without writing a partial report.

Later phases add options for architectural declarations and custom rules. Each arrives together with the
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

Everything language-specific lives here as data, never in the binary. Installed
it sits at `<prefix>/share/elc/runtime`; in a source checkout it is `runtime/`
at the top, reached through a symlink the build creates beside `build/elc`. The
contents are identical either way:

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

All five shipped modules were added as data alone — no line of the executable
changed to support any of them, which is the claim `runtime/queries/README.md`
exists to make good.

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

## Getting more detail

- `man elc` — the reference form of this material
- `doc/PVD.md` — why `elc` exists and what it will and will not do
- `doc/SDP.md` — the phase plan, and what each phase adds
- `doc/HLRs.md` — the requirements, if you need the precise contract

## License

MIT. See `LICENSE`.
