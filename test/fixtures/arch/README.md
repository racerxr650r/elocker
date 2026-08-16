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
