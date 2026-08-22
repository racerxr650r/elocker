# `arch/` — coupling, instability, cycles, and layering

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../arch.bats`](../arch.bats); they are never regenerated
from `elc`'s own output, which would make the fixture agree with the
implementation by construction and assert nothing (STP §2.4).

Everything in this group is about **components**, and a component is a source
file (HLR-114). That is the whole distinction between this group and
`calltree/`: the same two mutually recursive functions are a recursion finding
in one and a dependency cycle in the other *only if they live in different
files*, and this group pins both halves of that.

This phase **measures**. Nothing here asserts that a cycle is critical — the
severity is Phase 12's (HLR-084).

## The tree

```text
tree/                 three declared layers, one skip and one inversion
├── app/app.c
├── hal/hal.c
└── drv/drv.c
cycles/               a two-component cycle: both a recursion and a cycle
├── a.c
└── b.c
lone/only.c           Ca = Ce = 0, and a recursion that is not a cycle
```

Three directories rather than one, because coupling is a whole-graph answer:
analysing them together would join their graphs and change every figure below.

## `tree/` — the coupling table

Declared with:

```sh
elc --stratum 'app:*/app/*' --stratum 'hal:*/hal/*' --stratum 'drv:*/drv/*' tree
```

The dependencies, counted by hand:

| From | To | Because |
| ---- | -- | ------- |
| `app.c` | `hal.c` | `app_run` calls `hal_read` and `hal_write` |
| `app.c` | `drv.c` | `app_shortcut` calls `drv_poke` |
| `hal.c` | `drv.c` | `hal_read` and `hal_write` call `drv_poke` |
| `hal.c` | `app.c` | `hal_callback` calls `app_notify` |

| Component | Ca | Ce | Instability |
| --------- | -- | -- | ----------- |
| `app/app.c` | **1** | **2** | **0.67** |
| `hal/hal.c` | **1** | **2** | **0.67** |
| `drv/drv.c` | **2** | **0** | **0.00** |

**`app_run` calls `hal_read` twice, and that is deliberate.** Coupling counts
*components depended upon*, not calls made, so the repeat must not move `Ce`.
Without it the fixture would pass equally against an implementation that
counted call sites — the same trap `calltree/`'s fan-out table sets, and it is
set again here because the two analyses count different things over the same
edges.

`drv.c` has `Ce = 0` and `I = 0.00`: maximally stable, depended on by everyone
and depending on nothing. It is **not** undefined — `Ca` is 2, so the division
is well formed. The undefined case lives in `lone/`, deliberately apart.

No component is a bottleneck at the default threshold of 5. Asserting the
absence matters: a bottleneck test that only ever saw flagged components would
pass against an implementation that flagged everything. `arch.bats` lowers the
threshold with `-b 1` to see the flag appear, which pins the comparison rather
than the default.

## `tree/` — the two layering findings

The layer ordinals come from the declaration order: `app` is 0, `hal` is 1,
`drv` is 2. Dependency is permitted downward, so a call from a lower ordinal
to a higher one is ordinary.

| Kind | From | To | Distance | Why |
| ---- | ---- | -- | -------- | --- |
| **skip-level** | `app_shortcut` (app, 0) | `drv_poke` (drv, 2) | 2 | descends past `hal` without inverting |
| **inverted** | `hal_callback` (hal, 1) | `app_notify` (app, 0) | 1 | ascends without bypassing anything |

**These are the two cases that make the findings orthogonal**, which is the
whole of HLR-118 and LLR-LAY-03. One call skips without inverting; the other
inverts without skipping. An implementation folding them into a single
"layering violation" passes neither, and an implementation that reported
skip-level only when the call also inverted would pass the second and fail the
first.

A call ascending *two* layers would be both, and would be reported twice —
both statements are true of it and each has its own remedy. The tree does not
contain one, because a fixture cannot hold every combination and these two are
the ones the requirement names.

`hal_read` calling `drv_poke` descends exactly one layer and is reported as
nothing at all. That absence is asserted: without it the suite would pass
against an implementation that flagged every inter-layer call.

**With no `--stratum`, the section states that it was omitted and why.** The
coupling table is still produced — omitting one analysis for want of a
declaration must not omit its neighbours.

## `tree/` — the two conformance indices

Both indices are proportions of **the same denominator**: the run's call edges
joining two components in *different* declared layers. Counted by hand from
the dependency table above, taking each distinct caller-to-callee edge once:

| # | Call | From | To | Inter-layer? |
| - | ---- | ---- | -- | ------------ |
| 1 | `app_run` → `hal_read` | app (0) | hal (1) | yes |
| 2 | `app_run` → `hal_write` | app (0) | hal (1) | yes |
| 3 | `app_shortcut` → `drv_poke` | app (0) | drv (2) | yes — **skip** |
| 4 | `hal_read` → `drv_poke` | hal (1) | drv (2) | yes |
| 5 | `hal_write` → `drv_poke` | hal (1) | drv (2) | yes |
| 6 | `hal_callback` → `app_notify` | hal (1) | app (0) | yes — **back-call** |

**The denominator is 6.** `app_run` calls `hal_read` twice and that is one
edge, not two — the same collapse the fan-out table relies on, so the
percentages are over the figure the tables beside them show.

| Index | Violating | Conforming |
| ----- | --------- | ---------- |
| Back-call (HLR-162) | 1/6 = **16.67%** | **83.33%** |
| Skip-call (HLR-163) | 1/6 = **16.67%** | **83.33%** |

**The two are never added.** Here they happen to describe different calls, but
one call ascending two layers would be counted in each, and a combined score
would count twice exactly the call most worth acting on. Each index names its
own remedy; their sum names none.

The two conforming figures are 83.33% rather than one of them being 83.34%:
each is its own division rounded to two places, not one subtracted from the
other, so a reader who checks 1/6 and 5/6 against the printed pair finds both.

## `cycles/` — the undefined case for both indices

Declared with a single layer holding both files:

```sh
elc --stratum 'all:*/cycles/*' cycles
```

`a_side` calls `b_side` and `b_side` calls `a_side`, so there are two call
edges — and **both indices are `undefined`, not 0%**. Every edge is *within*
one layer, and an edge inside a layer has no direction to invert, so the
denominator is zero.

This is the case worth being careful about: the project has calls, has a
declared architecture, and has committed no violation — and reporting 0% (or
100% conforming) would claim it had demonstrated conformance. It has
demonstrated nothing either way. It is the same rule that makes Instability
undefined in `lone/` rather than 0.00, and deliberately *not* the rule
Henry–Kafura follows, which is genuinely zero when its inputs vanish.

## `tree/` — the hand-drawn matrix

**Rows are callers and columns are callees**, both in ascending layer order.
Every cell is the count of call edges from the row's subject to the column's,
taken from the six-edge table above:

|   | app | hal | drv |
| - | --- | --- | --- |
| **app** | 0 | 2 | 1 |
| **hal** | 1 | 0 | 2 |
| **drv** | 0 | 0 | 0 |

The cell that matters is `hal → app` = **1**, and it sits *below* the
diagonal. Below-diagonal cells sum to 1, which is exactly the count of
inverted findings in the layering table — the two views of one fact, which is
what makes the grid checkable against the list printed beside it. A matrix
built the other way round would still be a square of plausible numbers with
every reading taken from it exactly backwards, which is why the convention is
printed with every rendering.

The diagonal is all zeroes here because each layer is a single file and a
component does not call itself across a component boundary. A diagonal cell is
not a violation in any case: no declared order constrains a dependency inside
one subject.

With **no** `--stratum` the same tree yields a matrix over directories,
ordered by path — `app`, `drv`, `hal`, which is *not* the layer order:

|   | app | drv | hal |
| - | --- | --- | --- |
| **app** | 0 | 1 | 2 |
| **drv** | 0 | 0 | 0 |
| **hal** | 1 | 2 | 0 |

The two grids hold the same six edges arranged two ways, and that is the
point of the fallback: a reader who has declared nothing still gets the
dependencies, and is told in the heading that the order is alphabetical rather
than architectural, so no cell below this diagonal is read as a violation.

## `cycles/` — both facts at once

`a_side` calls `b_side` and `b_side` calls `a_side`, across two files.

* **Recursion** reports the pair, because two functions call each other.
* **Component dependency cycles** reports `a.c → b.c → a.c`, because two files
  depend on each other.

Both, because they are different statements: one says the stack depth has no
finite bound, the other says the two files cannot be built, tested, or
understood apart. The report shows the group and a concrete loop through it —
the group is what has to be broken up, the loop is which edge to cut.

## `lone/only.c` — the two negative cases

```text
Component coupling
  only.c   Ca 0   Ce 0   Instability undefined
```

**Instability is undefined, not zero and not an error.** `Ce / (Ce + Ca)` with
both zero is a division by zero, and a component nothing depends on that
depends on nothing is entirely ordinary — a lone file in a single-file target
is exactly that. Reporting `0.00` there would claim maximum stability for a
file that has no relationships at all.

**`ping` and `pong` are a recursion finding and not a component cycle.** They
call each other, so the call view holds a cycle; they live in one file, so the
component projection holds none, because a file does not depend on itself.
This is the case HLR-083 calls out by name, and reporting it as a circular
dependency would tell an architect to split a file over what is a MISRA C Rule
17.2 finding about two functions.
