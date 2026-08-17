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

| id | Function | File | address-taken | fan-out |
| -- | -------- | ---- | ------------- | ------- |
| n0 | `bump` | `core.c` | no | 0 |
| n1 | `tick` | `core.c` | no | 1 |
| n2 | `report` | `reader.c` | **yes** | 0 |
| n3 | `install` | `reader.c` | no | 0 |

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

## What is not here

No recursion, no cycle, and no call chain deeper than one. Those are Phase 9
and Phase 11 material, and their fixtures belong with the analyses that read
them. This group asserts that the graph *is what the source says*, which is
the thing every later analysis assumes.
