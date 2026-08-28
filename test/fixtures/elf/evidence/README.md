# `evidence/` — what the image answers that the source cannot

Part of the [`elf/`](../README.md) group. Its expected values are worked out by
hand here and asserted by [`../../elf.bats`](../../elf.bats); they are never
regenerated from `elc`'s output (STP §2.4).

[`debugline/`](../debugline/README.md) asks the image which **lines** inside a
kept function this build compiled. This directory asks it two further
questions, and both are questions the source cannot answer at all:

*   **Which branch of an undecidable `#ifdef` did this build take?** (HLR-211)
    A condition whose symbol no `-D` mentions is undecidable from the source and
    stays so however long it is stared at. The image knows: one branch produced
    instructions and the other did not.
*   **Where are the functions the grammar cannot see?** (HLR-212) A macro that
    expands to a whole function definition is a function to the compiler and an
    expression to tree-sitter. Repair cannot reach it either, because repair
    does not know the macro *defines* one. Only the debug information places it.

## The tree

```text
evidence/
├── branched.c   compiled WITH -g: two undecidable regions, opposite answers
├── dark.c       compiled WITHOUT -g: the region that must stay undecidable
└── macro.c      compiled WITH -g: a function only the image can place
```

All three are linked into one image, which is built in `$BATS_TEST_TMPDIR` and
never committed; a case whose compiler cannot produce it skips explicitly,
naming the requirement that thereby went unverified (STP §6).

`ELC_FIXTURE_FEATURE` is never defined — not by the build, and not to `elc`. So
every region here is undecidable from the source, which is the whole point: were
any of them decidable, Phase 15 would settle it before the image was consulted
and the tests would pass against an implementation that read no debug
information at all.

## `branched.c` — 33 physical lines

`branched` holds two regions of the same shape reaching opposite answers,
because the two dispositions prune opposite halves and a rule that only ever
answered one of them would pass a test for the other by doing nothing.

| Region | Lines | Body | Alternative | The image says | So `elc` removes |
| ------ | ----- | ---- | ----------- | -------------- | ---------------- |
| `#ifdef ELC_FIXTURE_FEATURE` | 21–26 | 22, 23 | 25 | body absent, alternative present | the body: 22, 23 |
| `#ifndef ELC_FIXTURE_FEATURE` | 27–31 | 28 | 30 | body present, alternative absent | the alternative: 30 |

Both are settled from the alternative rather than from the lines around the
region, which is the strongest form the evidence takes: exactly one of the two
branches produced instructions, and nothing outside the region is consulted.

| Function | ELOC, no image | ELOC, with image | Counted from |
| -------- | -------------- | ---------------- | ------------ |
| `branched` | **8** | **5** | 18, 20, 22, 23, 25, 28, 30, 32 — less 22, 23, 30 |

## `dark.c` — 23 physical lines

Compiled **without `-g`** into the same image, and holding a region of the same
shape as `branched.c`'s first. It contributes no line entries at all, so a rule
keyed on the absence of a line would find its region uncompiled and delete it.

Coverage is established per file and governs the question (HLR-154): with no
coverage there is no evidence, the region stays whole, and it is counted
undecided exactly as it is on a run with no image.

| Function | ELOC, no image | ELOC, with image |
| -------- | -------------- | ---------------- |
| `dark` | **4** | **4** | 16, 19, 21, 22 |

It is the dangerous case in the phase, and it is dangerous because it is
*silent*: the wrong answer is a smaller report that is internally consistent and
gives no sign it is wrong.

## `macro.c` — 32 physical lines

`ELC_FIXTURE_HANDLER(from_macro)` on line 26 expands to a whole function
definition, which is what `ISR(USART0_DRE_vect)` does on an AVR target.

| Function | Line | Reported as | Why |
| -------- | ---- | ----------- | --- |
| `reached` | 21 | a measured function, ELOC 1 | ordinary; the parse found it |
| `main` | 28 | a measured function, ELOC 2 | ordinary |
| `from_macro` | **26** | **name and line, and nothing else** | `elc` has no body for it |

**The row has three columns and that is the requirement.** `elc` has a name and
a location, from the image, and no body at all — so no ELOC, no complexity, no
maintainability index and no degrees. A row carrying zeroes for those would
report an absence as a measurement, which is what HLR-133 refuses for an
undecidable condition and HLR-138 for a language with no dead-code query. The
same reasoning keeps `from_macro` out of the call graph and out of the project's
function count.

Line 26 is 1 line of **file-scope ELOC**: the grammar reads it as an expression,
which is exactly the misreading the requirement works around, and it is measured
as what it appears to be rather than silently dropped.

**Two calls are unresolved.** `main` calls four functions and three of them
became edges; the call to `from_macro` cannot, because there is no node to carry
it. The macro invocation on line 26 is the second — a call to
`ELC_FIXTURE_HANDLER`, as far as the grammar is concerned. Both are counted
where every call the graph cannot represent is counted (HLR-077).

### `--no-expand`, and why the suite names it

Where the preprocessor can be run and succeeds, the definition is expanded into
place at its own line and parsed like any other function — there is nothing left
to recover, and the suite asserts that too. `--no-expand` is the state a
cross-compiled tree is in for real: the host toolchain cannot find the target's
headers, expansion falls back, and the macro stands unexpanded (HLR-205).

## The project figures

| Figure | No image | With image, `--no-expand` | With image, expanded |
| ------ | -------- | ------------------------- | -------------------- |
| ELOC | **16** | **13** | 13 |
| Functions | **4** | **4** | **5** |
| Undecided regions | **3** | **1** | 1 |
| Regions decided by this build | — | **2** | 2 |
| Files with no debug coverage | — | **1** | 1 |
| Functions the image places that the parse did not reach | — | **1** | **0** |
| Unresolved calls | — | **2** | — |

ELOC 16 = 8 + 4 + 4, and 13 = 5 + 4 + 4.

Three undecided regions with no image: two in `branched.c` and one in `dark.c`.
One with the image: `dark.c`'s, which no evidence covers.

**No exact pruned-line count is asserted here.** Every region this fixture
settles is settled whole, before any line inside it is judged, so the figure
HLR-155 reports is the residue of whatever the compiler did with the lines that
remain — a property of one optimiser rather than of a requirement.
[`debugline/`](../debugline/README.md) is where that count is pinned, through a
`-D` that leaves its region active for the line granularity to work on.

## What is not here

No unresolved linkage names and no absent functions — those are the rest of the
[`elf/`](../README.md) group, at the granularity the symbol table answers. And
no optimised build: what `-O2` shows is a property of the *line* granularity,
and [`debugline/`](../debugline/README.md) shows it.
