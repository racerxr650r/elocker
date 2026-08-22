# `graph/` — the topology of the System Dependence Graph

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../graph.bats`](../graph.bats); they are never
regenerated from `elc`'s own output, which would make the fixture agree with
the implementation by construction and assert nothing (STP §2.4).

GraphML is the assertion surface. The rendered report states conclusions —
totals, callouts, findings — and conclusions cannot distinguish a graph with
the right edges from one that reached the same number by the wrong ones. The
export is the only channel that carries *topology*, which is why it ships in
this phase rather than with the visualisation work of Phase 13.

## The tree

```text
tree/
├── core.c      bump, tick       — the collapsed call, the writer
└── reader.c    report, install  — the reader, the callback
```

## The nodes, in identifier order

Node identifiers run in **sorted file order**, and within a file by start
line (LLR-SDG-09). Sorting is by path, so `core.c` precedes `reader.c` — which
is *not* the order a directory walk yields them in, and is the point.

| id | Function | File | address-taken | fan-out | fan-in |
| -- | -------- | ---- | ------------- | ------- | ------ |
| n0 | `bump` | `core.c` | no | 0 | **1** |
| n1 | `tick` | `core.c` | no | 1 | 0 |
| n2 | `report` | `reader.c` | **yes** | 0 | **0** |
| n3 | `install` | `reader.c` | no | 0 | 0 |

**`report`'s fan-in is 0 and that is the column this group exists to pin.**
`tick` writes `shared_counter` and `report` reads it, so the SDG holds an edge
from n1 to n2 — and in-degree taken over the whole graph would make `report`'s
fan-in 1. Fan-in counts *callers*, over the call view alone, exactly as fan-out
counts callees (HLR-156, LLR-CTR-07). Nothing calls `report`; being read by
someone is not being called by them. The error would not stay a fan-in error
either: the Henry-Kafura value squares the product of the two degrees, so a
fan-in inflated by one is a Henry-Kafura value inflated by more (HLR-157).

`bump`'s fan-in is 1 for the converse reason `tick`'s fan-out is 1: two call
sites, one caller.

`report` is address-taken because `install` assigns it to `hook` without
calling it. That is the callback pattern Phase 10's reachability analysis
exists to not report as dead code (HLR-096), captured here because this is
the phase that can see it.

## The edges

| From | To | Kind | Why |
| ---- | -- | ---- | --- |
| n1 `tick` | n0 `bump` | `call` ×**2** | **two call sites, one edge** |
| n1 `tick` | n2 `report` | `global` `shared_counter` | writer to reader, across files |

**`tick` calls `bump` twice and the graph holds one edge**, carrying
`call-sites` 2. This is the simple-graph rule, and it is what makes `tick`'s
fan-out 1 rather than 2: fan-out counts distinct subroutines invoked, not
call sites (HLR-085, LLR-SDG-04). A multigraph would report a function that
calls one helper in a loop body and again in its error path as coupled to two
things.

**The global edge spans two files**, which is the whole claim of HLR-074:
`tick` in `core.c` writes `shared_counter`, `report` in `reader.c` reads it,
and they are coupled without either naming the other. No call edge exists
between them and none should.

## Unresolved calls: **1**

| Call site | Why it does not resolve |
| --------- | ----------------------- |
| `external_log()` in `core.c` | defined outside the analysis target — a library call |

It is counted and it is not fatal (HLR-077). **No destination is invented for
it**: an edge that does not exist would make Phase 10's dead-code proof
unsound, and a wrong edge is worse than a missing one there.

## Henry-Kafura: **0** for every node, and the project total is **0**

Every function here sits at one end of the call graph or the other — `bump`,
`report` and `install` call nothing, `tick` is called by nothing — so the
product term vanishes for all four and each scores zero whatever its length
(HLR-159). The non-zero values live in the [`calltree/`](../calltree/README.md)
group, beside the fan-in figures that produce them. What this group asserts is
the input: that a global edge contributed nothing to the degrees the formula
multiplies.

## What is not here

No recursion, no cycle, and no call chain deeper than one. Those are Phase 9
and Phase 11 material, and their fixtures belong with the analyses that read
them. This group asserts that the graph *is what the source says*, which is
the thing every later analysis assumes.
