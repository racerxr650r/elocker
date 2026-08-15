# `eloc/` — one instance of each category, counted by hand

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

**Exception handling (HLR-048) is absent** because C has none. The category is
exercised when C++ arrives in Phase 6, and its absence here is a fact about
the language rather than a gap in the fixture.
