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
├── ambiguous.adb   Ada: Scale, Drive       — and the array-index ambiguity
├── core.c          C:  bump, tick          — the collapsed call, the writer
└── reader.c        C:  report, install     — the reader, the callback
```

## The nodes, in identifier order

Node identifiers run in **sorted file order**, and within a file by start
line (LLR-SDG-09). Sorting is by path, so `ambiguous.adb` precedes `core.c`
precedes `reader.c` — which is *not* the order a directory walk yields them
in, and is the point.

| id | Function | File | address-taken | fan-out |
| -- | -------- | ---- | ------------- | ------- |
| n0 | `Scale` | `ambiguous.adb` | no | 0 |
| n1 | `Drive` | `ambiguous.adb` | no | 1 |
| n2 | `bump` | `core.c` | no | 0 |
| n3 | `tick` | `core.c` | no | 1 |
| n4 | `report` | `reader.c` | **yes** | 0 |
| n5 | `install` | `reader.c` | no | 0 |

`report` is address-taken because `install` assigns it to `hook` without
calling it. That is the callback pattern Phase 10's reachability analysis
exists to not report as dead code (HLR-096), captured here because this is
the phase that can see it.

## The edges

| From | To | Kind | Why |
| ---- | -- | ---- | --- |
| n1 `Drive` | n0 `Scale` | `call` ×1 | an ordinary Ada function call |
| n3 `tick` | n2 `bump` | `call` ×**2** | **two call sites, one edge** |
| n3 `tick` | n4 `report` | `global` `shared_counter` | writer to reader, across files |

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

## Unresolved calls: **2**

| Call site | Why it does not resolve |
| --------- | ----------------------- |
| `external_log()` in `core.c` | defined outside the analysis target — a library call |
| `Table (2)` in `ambiguous.adb` | **an array index the Ada grammar cannot distinguish from a call** |

Both are counted and neither is fatal (HLR-077). No destination is invented
for either: an edge that does not exist would make Phase 10's dead-code proof
unsound, and a wrong edge is worse than a missing one there.

### What the Ada case pins

`Table (2)` is an array index. Ada writes it identically to a function call,
and the grammar manages the ambiguity with precedence rules rather than
resolving it — resolution needs semantic analysis a grammar does not perform
(`doc/notes.md` §2.2). So `ambiguous.adb`'s `calls.scm` captures it as a call
site.

**What happens next is better than the note predicted, and this fixture is
what pins it.** The captured name `Table` is resolved against the project
symbol table, finds no subprogram of that name, and is therefore recorded as
*unresolved* rather than becoming a spurious edge. The ambiguity inflates the
unresolved count, which is reported and visible, instead of inventing an edge,
which would not be.

A spurious *edge* still occurs in the one case this fixture deliberately does
not contain: an array whose name is also a subprogram's somewhere in the
project. There the index resolves, and the graph carries an edge the program
does not. That is the residual risk, it is a property of what the grammar can
express rather than of `elc`, and the direction of the error is stated in
SDD §8 — safe for reachability, inflating for fan-out and depth, and
dangerous only for cycle detection.

Do not "fix" this in C. Disambiguating would put Ada's semantics in the
binary, which the extensibility pillar forbids outright (HLR-010).

## What is not here

No recursion, no cycle, and no call chain deeper than one. Those are Phase 9
and Phase 11 material, and their fixtures belong with the analyses that read
them. This group asserts that the graph *is what the source says*, which is
the thing every later analysis assumes.
