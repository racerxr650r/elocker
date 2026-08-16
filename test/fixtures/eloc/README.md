# `eloc/` — one instance of each category, in every shipped language

The fixture header for this group. Expected values are counted below and
asserted by [`../eloc.bats`](../eloc.bats). They are **not** generated from
`elc`'s output: a fixture that agrees with the implementation by construction
asserts nothing (STP §2.4).

## Expected result for `elc categories.c`

| Value | Expected |
| ----- | -------- |
| Physical lines | **40** |
| File ELOC | **19** |
| Functions | **1** |
| `categories` ELOC | **18** |
| `categories` complexity | **6** |

## The count, line by line

`categories.c` is 40 lines. Nineteen of them carry a statement.

### Counted (HLR-044 – HLR-047)

| Line | Text | Category |
| ---- | ---- | -------- |
| 8 | `int initialised_global = 1;` | assignment — a declaration that initialises (HLR-044) |
| 14 | `int total = 0;` | assignment (HLR-044) |
| 16 | `for (i = 0; i < LIMIT; i++) {` | control flow (HLR-045) |
| 17 | `if (i == n)` | control flow |
| 18 | `total += i;` | operation (HLR-044) |
| 19 | `else if (i > n)` | control flow — **one** line, though it is both an `else` and an `if` |
| 20 | `break;` | control flow |
| 21 | `else` | control flow |
| 22 | `continue;` | control flow |
| 25 | `while (total > LIMIT) {` | control flow |
| 26 | `total--;` | operation |
| 29 | `switch (n) {` | control flow |
| 30 | `case 0:` | control flow |
| 31 | `total = 0;` | assignment |
| 32 | `break;` | control flow |
| 33 | `default:` | control flow |
| 34 | `goto done;` | control flow |
| 38 | `prototype_only();` | a call, result unused (HLR-046) |
| 39 | `return total;` | a return (HLR-047) |

Nineteen lines. `categories` contains all but line 8, so its ELOC is
**eighteen**; line 8 is file-scope and contributes to the file alone
(HLR-019).

### Not counted, and why

| Line | Text | Excluded by |
| ---- | ---- | ----------- |
| 4 | `#include <stddef.h>` | preprocessor directive (HLR-052) |
| 5 | `#define LIMIT 3` | preprocessor directive |
| 7 | `int bare_declaration;` | declares without initialising (HLR-051) |
| 9 | `int prototype_only(void);` | a bare prototype (HLR-051) |
| 13 | `int i;` | declares without initialising |
| 37 | `done:` | a label directs nothing on its own; the statement it labels is line 38 |
| 12, 23, 27, 35, 40 | `{` or `}` alone | standalone structural token (HLR-050) |
| 6, 10, 15, 24, 28, 36 | *(empty)* | blank (HLR-049) |
| 1–3 | the header comment | documentary |

### The two judgements in here

**`else if` on one line is one line.** Line 19 is captured twice — once as the
`else`, once as the `if` it introduces — and both captures start on line 19.
ELOC counts distinct *lines*, so it contributes one. Written across two lines
it would contribute two, which is correct: there are then two lines to read.

**A label is not a statement.** `done:` on line 37 directs nothing by itself;
what it labels is the call on line 38, which is counted in its own right.
Counting both would make labelling a function's exit worth an extra line.

## The complexity count

Complexity is one plus the decision points, and the set of decision points is
not the set of ELOC lines. Five of the nineteen counting lines branch:

| Line | Construct | Why it is a decision |
| ---- | --------- | -------------------- |
| 16 | `for` | the loop may or may not be entered |
| 17 | `if` | two paths |
| 19 | `else if` | the `if` branches; the `else` does not |
| 25 | `while` | two paths |
| 30 | `case 0:` | a labelled branch |

One plus five is **six**.

Three things in the file look like decisions and are not:

* **`switch` on line 29** is not itself a branch — each of its `case` labels
  is, which is where the count comes from. Counting the `switch` as well
  would charge a two-case switch three.
* **`default:` on line 33** is where control goes when no branch was taken.
  It adds no path that was not already counted.
* **`goto` on line 34** moves control without choosing. The choice, where
  there is one, is in the `if` that guards the `goto`.

**Exception handling (HLR-048) is absent** because C has none. The category is
exercised when C++ arrives in Phase 6, and its absence here is a fact about
the language rather than a gap in the fixture.


---

# The other four languages

Each of `categories.cpp`, `categories.py`, `categories.rs`, and
`categories.adb` does for its language what `categories.c` does for C: one
instance of every category the language has, and one of every exclusion.

| Fixture | Physical | File ELOC | Function ELOC | Complexity |
| ------- | -------- | --------- | ------------- | ---------- |
| `categories.c` | 40 | **19** | **18** | **6** |
| `categories.cpp` | 47 | **25** | **24** | **8** |
| `categories.py` | 44 | **27** | **25** | **8** |
| `categories.rs` | 45 | **19** | **18** | **10** |
| `categories.adb` | 42 | **20** | **20** | **10** |

**The numbers differ because the languages differ.** That is the point of
having five fixtures rather than one translated four times, and each
difference below is a decision recorded in the language's query files.

## C++ — what C has, plus two things it does not

Adds a range-for and, more importantly, **exception handling** — the one ELOC
category (HLR-048) C cannot express at all. `try`, `catch`, and `throw` each
count as a line, and a `catch` is a decision point because it is a path out of
the guarded block. `throw` is not: it transfers control without choosing, as
`goto` does.

That accounts for the whole difference from C: 25 lines against 19, and
complexity 8 against 6.

## Python — no braces to exclude, and a docstring that counts

* **`pass` is excluded.** It is Python's way of writing an empty block — the
  language's substitute for the brace C would put there — and HLR-050 excludes
  a line holding nothing but structure.
* **`import` is excluded**, with `global` and `nonlocal`. They bind names;
  what a reader sees is a declaration of what this module depends on, the same
  category as the `#include` HLR-052 excludes. Counting them would make a
  module's ELOC rise with its import list.
* **The module docstring counts.** It is an expression statement, not a
  comment, and Python keeps it at run time as `__doc__`. Line 1 is one line of
  ELOC. Treating it as documentary would be defensible; it is not what the
  grammar says, and this fixture is where that decision is visible.
* `else`, `elif`, `except`, and `finally` are all clause nodes, so each
  contributes its own line — as in C, and unlike Rust.

## Rust — an expression language, counted as one

* **The tail expression counts.** `fn double(x: i32) -> i32 { x * 2 }` has no
  `return` and no semicolon, and reporting it as zero effective lines would be
  plainly wrong about a function that does arithmetic.
* **`static` counts and `const` does not.** A `static` is storage that exists
  at run time, which is what C's initialised global is. A `const` is inlined at
  every use and never exists as a variable, which puts it with `#define`.
* **`else` contributes nothing.** Rust's grammar has no `else` node: the
  alternative of an `if` is just a block. So `} else {` is a line C would count
  and Rust does not — the same code shape, a different number, for a reason in
  the grammar rather than in `elc`.
* Each `match` arm counts, as each `case` does in C; the `match` itself is the
  fork rather than one of the paths, so it adds a line but not a decision.
* `?` is a decision point: it returns early when its operand is an error, so a
  function threading a dozen of them has a dozen paths out.

## Ada — the most explicit, and the one real compromise

* **`null;` is excluded**, as Python's `pass` is: an explicit do-nothing.
* **`with` and `use` are excluded**, as `#include` is.
* **`and then` and `or else` are decision points; plain `and` and `or` are
  not.** Ada actually distinguishes them — the short-circuit forms may skip
  their right operand and the plain forms always evaluate both — so this is
  the language's own semantics rather than a convention imported from C.
* **`when others` is counted as a decision, and C's `default:` is not.** This
  is the compromise. Ada writes the catch-all as an alternative like any
  other and the grammar does not mark it, so distinguishing it would mean
  matching the text `others` — the textual approximation HLR-013 forbids. An
  exhaustive Ada case therefore scores one higher than the equivalent C
  switch. Stated here rather than left for someone to find.
