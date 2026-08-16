# `calltree/` — fan-out, recursion, and call depth

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../calltree.bats`](../calltree.bats); they are never
regenerated from `elc`'s own output, which would make the fixture agree with
the implementation by construction and assert nothing (STP §2.4).

This phase **measures**. Nothing here asserts that a fan-out of 16 is bad —
the bands that turn a number into a finding are Phase 12's, and this group
exists partly to give that phase something already-verified to band.

## The tree

```text
tree/
├── fanout.c      one function per fan-out boundary value
├── depth.c       a straight chain, with a shallower branch beside it
└── recursion.c   direct and mutual recursion, smallest form of each
```

The three are separate files because two of the measurements are *whole-graph*
answers: recursion anywhere makes depth unbounded everywhere, so a tree
holding a cycle can never assert a depth. Keeping them apart is what lets each
be measured.

## `fanout.c` — the boundary values

Sixteen do-nothing helpers `h01` … `h16`, and one caller per boundary value
Phase 12 will band:

| Function | Calls | Expected fan-out |
| -------- | ----- | ---------------- |
| `fan02` | `h01`, `h02`, and `h01` again | **2** |
| `fan03` | `h01` … `h03`, and `h01` again | **3** |
| `fan07` | `h01` … `h07`, and `h01` again | **7** |
| `fan08` | `h01` … `h08`, and `h01` again | **8** |
| `fan10` | `h01` … `h10`, and `h01` again | **10** |
| `fan11` | `h01` … `h11`, and `h01` again | **11** |
| `fan15` | `h01` … `h15`, and `h01` again | **15** |
| `fan16` | `h01` … `h16`, and `h01` again | **16** |
| `h01` … `h16` | nothing | **0** |

**Every caller invokes `h01` a second time, and that is the point.** Fan-out
counts distinct callees, not call sites, so the repeat must not move the
number. Without it the fixture would pass equally against an implementation
that counted call sites, and would assert nothing about the distinction the
requirement is built on (HLR-085).

The values are exactly the band boundaries of PVD Appendix A.2: 2 and 3 either
side of the healthy floor, 7 and 8 either side of its ceiling, 10 and 11 either
side of the warning threshold, 15 and 16 either side of critical. Phase 12
bands them; if a boundary is off by one, it will be off here first.

## `depth.c` — the deepest chain

```text
entry_main ──► shallow          (depth 2)
           └─► level2 ──► level3 ──► level4   (depth 4)
```

| Value | Expected | Counted how |
| ----- | -------- | ----------- |
| Depth from `entry_main` | **4** | `entry_main`, `level2`, `level3`, `level4` |
| Deepest chain | **that sequence, in that order** | HLR-088 |
| Unresolved calls | **0** | every callee is defined in the file |

Depth counts the functions in the chain, entry point included: a lone entry
point that calls nothing is depth 1, not 0.

**The `shallow` branch is load-bearing.** `entry_main` calls it *first*, so a
traversal that followed the first edge rather than the deepest would report
`entry_main → shallow` — the wrong chain, and the wrong length. With only one
branch the fixture could not tell a longest-path search from a first-path one.

## `recursion.c` — both kinds

| Kind | Functions | Why |
| ---- | --------- | --- |
| direct | `self_calling` | calls itself |
| mutual | `bounce`, `countdown` | each reaches the other |

Both are found by one decomposition of the call graph, which is why the
requirement names them together (HLR-089).

Analysed with `--entry recursive_entry`, this file reports **no depth at
all**. That is the whole of HLR-090: on a cyclic call graph the longest path
has no finite answer, so `elc` reports the cycle in place of a number rather
than a plausible finite value, and rather than not terminating. The suite
asserts that the run *completes*, which is half the requirement.

The `Functions` column is a **set**, not a path. A strongly connected
component says every member can reach every other; it does not yield a
particular cycle, and rendering one would assert a path that may not exist.

## The four depth outcomes

A reader who sees no depth figure must be able to tell which of these
happened, so each states its own reason:

| Situation | Heading |
| --------- | ------- |
| Measured | `(N layers; a lower bound, M calls unresolved)` |
| Recursion present | `(unbounded: the call graph is recursive)` |
| No `--entry` given | `(omitted: no entry points declared, see --entry)` |
| `--entry` given, matches nothing | `(omitted: no declared entry point matches an analysed function)` |

The last two are distinct because the actions differ: one means *declare your
entry points*, the other means *you declared them, but they are not in what
you analysed*. Collapsing them would send the reader looking in the wrong
place.

## Why depth is a lower bound

A chain continuing through a call `elc` could not resolve is not followed, so
the true worst case may be deeper. The unresolved count therefore travels in
the heading beside the depth: 4 layers with 0 unresolved is a measurement,
and 4 layers with 300 unresolved is a number not to rely on (HLR-087).

## What is not here

No fan-out bands, no severity, no findings — Phase 12. No reachability or dead
code — Phase 10, which consumes the address-taken facts Phase 8 already
records. This group asserts the measurements those phases will judge.
