# `conditional/` — measuring one configuration rather than all of them

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../conditional.bats`](../conditional.bats); they are
never regenerated from `elc`'s output, which would make the fixture agree with
the implementation by construction and assert nothing (STP §2.4).

This group exists because conditionally compiled source describes several
programs, and measuring it without saying which one produces the union of them
all — a figure describing no build that exists.

## The tree

```text
tree/
├── config.c   every form elc can decide, and one it cannot
├── nested.c   a region inside a region that is not compiled
└── cfg.rs     the same mechanism through Rust's attribute syntax

chain/
└── chain.c    an #elif chain, kept apart so tree/'s totals stay one table
```

## `config.c` — the functions, and what decides each

| Function | Guarded by | Decided by |
| -------- | ---------- | ---------- |
| `never_built` | `#if 0` | the constant — **always pruned, with no `-D` at all** |
| `with_feature` | `#ifdef FEATURE` | `-DFEATURE` |
| `without_feature` | its `#else` | the same |
| `fat` | `#ifndef LEAN` | `-DLEAN`, which makes the condition *false* |
| `undecidable` | `#if VERSION > 2` | nothing — macro values `elc` does not have |
| `always` | nothing | — |

## `nested.c` — and the count it protects

`#ifdef INNER` sits inside `#if 0`. On its own terms it is undecidable, but it
lies in a region nothing compiles, so it is **not counted as undecided
either**. A region nobody builds has no condition worth reporting, and counting
it would inflate the one figure a reader uses to judge how complete the pruning
was.

This is the case a naive implementation gets wrong, which is why it has a file
to itself.

## `cfg.rs` — the same mechanism, different syntax

An attribute has no `#else`, so `#[cfg(X)]` can only ever be *removed*, never
swapped for something else. And because a symbol no `-D` mentions is
undecidable rather than undefined, **`#[cfg(feature_a)]` is pruned by nothing**
— including by `-Dfeature_a`, which decides the condition *true* and so prunes
an alternative that does not exist.

It is `#[cfg(not(feature_b))]` with `-Dfeature_b` that prunes: the symbol is
known, the negation makes the condition false, and the item goes.

That asymmetry is not a defect and it is why this file is here. It demonstrates
that the mechanism is shared — no line of `src/` changed for Rust — while the
*shape* of what a language can express is the language's own.

## The expected figures

| Run | Functions | ELOC | Undecided |
| --- | --------- | ---- | --------- |
| `elc` | 9 | 9 | 5 |
| `elc -DFEATURE` | 8 | 8 | 4 |
| `elc -DLEAN` | 8 | 8 | 4 |
| `elc -DFEATURE -DLEAN` | 7 | 7 | 3 |
| `elc -Dfeature_b` | 8 | 8 | 4 |

Every function holds exactly one statement, so ELOC equals the function count
throughout — chosen so a reader can check the second column against the first
without trusting either.

### Where the 5 undecided regions come from

| File | Region | Why |
| ---- | ------ | --- |
| `config.c` | `#ifdef FEATURE` | `FEATURE` named by no `-D` |
| `config.c` | `#ifndef LEAN` | `LEAN` named by no `-D` |
| `config.c` | `#if VERSION > 2` | no pattern decides it; the catch-all counts it |
| `cfg.rs` | `#[cfg(feature_a)]` | `feature_a` named by no `-D` |
| `cfg.rs` | `#[cfg(not(feature_b))]` | `feature_b` named by no `-D` |

**`nested.c` contributes nothing**, and `config.c`'s `#if 0` contributes
nothing — a decided region is not an undecided one.

## What this group must also prove

**That `elc` with no `-D` reports what it reported before the option existed.**
That is the cheapest regression test available and it is already written: every
other fixture in the suite. This group asserts the narrower version directly —
that adding `-D` for a symbol the tree never mentions changes nothing.

**That `#if 0` prunes anyway.** This is the one place the "with no definitions
nothing changes" rule does not read literally, and it is deliberate: a constant
condition is the same in every configuration, so it needs no configuration to
decide. Phase 15 reversed the Phase 3 judgement recorded in `doc/notes.md` §3
for exactly this case.

## `chain/` — what "the alternative" means in a chain

For the leading `#if`, the alternative is not the `#else` — it is **the whole
rest of the chain**. That is what makes the following table work:

| Run | Functions kept | Undecided |
| --- | -------------- | --------- |
| `elc` | `alpha`, `beta`, `neither` | 2 |
| `elc -DALPHA` | `alpha` | 0 |
| `elc -DBETA` | `alpha`, `beta` | 1 |

`-DALPHA` decides the head true, so the entire `#elif`/`#else` tail goes — and
the `#elif` inside it stops being undecided, because a region nobody builds has
no condition worth reporting. That is why the count falls to **0** and not 1.

`-DBETA` leaves the head undecidable, so nothing there is pruned and it is
counted; the `#elif` is then decided on its own terms and takes its `#else`
with it. `alpha` survives because `ALPHA` is unknown, not because it is
defined — which is the open-world rule doing exactly what it should.

## What is not here

No `-U`. There is no such option, which is precisely why a symbol no `-D`
mentions is undecidable rather than undefined.
