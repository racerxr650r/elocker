# `debugline/` — the lines a build did not compile

Part of the [`elf/`](../README.md) group. Its expected values are worked out
by hand here and asserted by [`../../elf.bats`](../../elf.bats); they are never
regenerated from `elc`'s output (STP §2.4).

An image answers "what did this build keep?" at two granularities. The symbol
table answers it for **functions**, which is Phase 16 and the rest of this
group. The debug line information answers it for **lines inside a function the
link kept**, which is this directory (HLR-153).

## The tree

```text
debugline/
├── covered.c   compiled WITH -g: a guarded region the build excludes
└── opaque.c    compiled WITHOUT -g: the file that must not be touched
```

Both are linked into one image. Neither is committed — the image is built in
`$BATS_TEST_TMPDIR`, and a case whose compiler cannot produce it skips
explicitly, naming the requirement that thereby went unverified (STP §6).

## `covered.c` — the region only the image can rule out

`guarded` holds three statements inside `#ifdef ELC_FIXTURE_FEATURE`. The
fixture's build never defines that symbol and **`elc` is never told about it
either**, so with no `-D` on the command line the condition is one `elc` cannot
decide: Phase 15 leaves the region active and counts it undecided (HLR-133).
Every line of it survives into the measurement, and only the image's line
information can say the build compiled none of them.

**A fixture using `#if 0` would prove nothing.** That condition is decidable
from the source alone, so the region is gone before the image is consulted and
the test would pass against an implementation that read no debug information at
all. The whole assertion rests on the region being undecidable.

| Function | ELOC, no image | ELOC, with image | Why |
| -------- | -------------- | ---------------- | --- |
| `always` | 3 | **3** | compiled by every build; kept whole |
| `guarded` | 7 | **4** | the three guarded statements produced no instruction |
| `main` | 1 | **1** | compiled |

`always` and `main` are load-bearing in the other direction: a run that pruned
the file wholesale would show them shrink, so the table fails rather than
passing quietly.

## `opaque.c` — the file that must not lose a line

Compiled **without `-g`** into the same image. It contributes no line entries
at all.

| Function | ELOC, no image | ELOC, with image |
| -------- | -------------- | ---------------- |
| `opaque` | 4 | **4** |

A rule keyed on the absence of a line would find every line here uncompiled and
delete the file. Absence from a mapping that never described this file is
evidence of nothing at all (HLR-154), and this is the file that says so. It is
the dangerous case in the phase, and it is dangerous because it is *silent*:
the wrong answer is a smaller report that is internally consistent and gives no
sign it is wrong.

## The project figures

| Figure | No image | With image | With image and `-D` | Counted how |
| ------ | -------- | ---------- | ------------------- | ----------- |
| ELOC | **15** | **12** | **12** | 3 + 7 + 1 + 4, less the three guarded statements |
| Functions | **4** | **4** | **4** | the link kept every one |
| Undecided regions | **1** | **0** | **0** | see below |
| Regions decided by this build | — | **1** | **0** | HLR-211 |
| Lines not compiled by this build | — | **0** | **3** | HLR-155 |
| Files with no debug coverage | — | **1** | **1** | `opaque.c`; HLR-155 |

The `-D` column is `-D ELC_FIXTURE_FEATURE`, and it is what keeps this fixture
about the *line* granularity. Read the three columns across one row at a time:
the ELOC is the same in all three, and what differs is **which mechanism
removed the guarded statements**.

*   **No image.** Nothing removes them. The condition is undecidable from the
    source, both branches stay, and the region is counted undecided (HLR-133).
*   **With the image.** The region goes *whole*, at the coarser grain: the build
    compiled no instruction for any line of it while the lines either side were
    compiled, so it is inactive for this build and every line of it is excluded
    at once (HLR-211). Nothing is left for the line granularity to remove, which
    is why that count is 0 — a line already excluded is not pruned again, and
    counting it twice would inflate a figure a reader acts on (LLR-ANL-58).
*   **With the image and the `-D`.** The definition settles the region first: a
    `-D` is what the user says this configuration is, and evidence about one
    build does not overrule it. So the region is *active*, its three statements
    are counted — and the image, built without that definition, compiled none of
    them. The line granularity removes exactly those three.

**Three lines pruned in that last column, not five.** The `#ifdef` and `#endif`
lines produce no instruction either, and on an unexpanded parse they would be
pruned with the rest; but the file *expands* under the same `-D` (HLR-202) and
the preprocessor has already taken the directives out. The count states what the
mechanism removed from the measured text — the reach of the filter — and is read
the way the unresolved-call count and the undecided-region count are read, not
as an ELOC difference.

**And the file expands where it did not before.** Without the image, HLR-208
refuses to expand `covered.c`: it holds a region `elc` could not decide, and the
preprocessor would silently resolve it. With the image the region *is* decided,
so the gate opens and one more file is expanded. That interaction is asserted
rather than left to be noticed.

## Two optimisation levels, and what the second one shows

The same sources are built at `-O0` and at `-O2`, and both are asserted.

At **`-O0`** the mapping is dense and the result is the table above: exactly
the guarded region goes.

At **`-O2`** more goes, and the fixture exists partly to show it. GCC folds
`guarded(2)` to a constant and emits no instruction for the body at all, so the
line table holds nothing for lines the source plainly contains and `guarded`
reports an ELOC of **0**. Those lines are indistinguishable, in the mapping
alone, from the ones the `#ifdef` excluded — which is the limit HLR-154 states,
observed rather than argued.

It is not a defect to be corrected. Nothing in the image records the
difference, and the lines really did contribute nothing to what shipped; a
report saying so is describing the image, which is what the option is for. What
the reader needs is to know it happened, which is what the pruned-line count of
HLR-155 is for — and why a large one beside an optimised build is information
rather than alarm.

Both `-O2` cases are run under the `-D`, for the reason the table above gives:
without one the region is settled whole from the image and there is nothing left
for the optimiser to be observed folding.

**The suite therefore asserts at `-O2` only what holds whatever the optimiser
did:**

*   `opaque.c` keeps every line. Coverage governs pruning, so a file the
    mapping never described loses nothing at any optimisation level. This is
    the invariant the phase is unsafe without.
*   Every function is still reported — pruning removes lines from *within* a
    function, never the function itself.
*   `opaque.c` is still counted uncovered.
*   The `-O2` count is at least the `-O0` count.

Pinning an exact figure there would pin the fixture to one compiler's optimiser
rather than to a requirement.

## What is not here

No unresolved linkage names and no absent functions — those are the rest of the
[`elf/`](../README.md) group, at the granularity the symbol table answers. And
no region settled from its own `#else`, and no function the grammar could not
see: those are [`evidence/`](../evidence/README.md), which asks the image the
two questions the source cannot answer at all.
