# `dot/` — the annotated Graphviz call tree

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../dot.bats`](../dot.bats); they are never regenerated
from `elc`'s own output, which would make the fixture agree with the
implementation by construction and assert nothing (STP §2.4).

This is the first artefact `elc` produces for another *program* to read. So the
group asserts two separable things, and keeps them separable:

1.  **It is valid DOT.** Asserted by handing the file to `dot` and requiring it
    to render — not by pattern-matching text that looks like DOT.
2.  **The annotations say what the analyses found.** Asserted against the
    hand-worked table below.

And a third that only this artefact has: **stripping every annotation leaves
the same tree.** A renderer that ignores the attributes still draws a valid and
readable call tree (HLR-105), which is checked by deleting every attribute list
from the file and rendering what is left.

## Why two trees

Recursion makes the call depth unbounded, so a tree containing recursion has no
deepest call chain to measure and none to annotate (HLR-087). One tree cannot
demonstrate both, and that is a fact about the measurements rather than about
the drawing — so there are two.

```text
tree/           the main tree: a chain, a cycle, a wide function, dead code
├── deep.c      step4, step3, step2, step1, helper  — the deepest chain
├── left.c      left_hi, left_lo                    — half a component cycle
├── main.c      orphan, run                         — the entry, and dead code
├── right.c     right_hi, right_lo                  — the other half
├── state.c     producer, consumer                  — a hidden channel
└── wide.c      w01 … w11, reader                   — fan-out over the band

recursive/      mutual recursion, and nothing else
└── recursive.c ping, pong, kick
```

Both are run with `--entry` declared, because reachability is measured from
declared roots and is otherwise omitted with a stated reason (HLR-095,
HLR-115). `tree/` is run with `--bottleneck-threshold 1` so that a two-file
cycle is also a bottleneck; at the default of 5 it would take ten more files to
demonstrate the same annotation.

## `tree/` — the nodes, in identifier order

Node identifiers run in **sorted file order**, and within a file by start line
(LLR-SDG-09). Sorting is by path: `deep.c`, `left.c`, `main.c`, `right.c`,
`state.c`, `wide.c`.

| id | Function | File | What is annotated on it |
| -- | -------- | ---- | ----------------------- |
| n0 | `step4` | `deep.c` | deepest chain |
| n1 | `step3` | `deep.c` | deepest chain |
| n2 | `step2` | `deep.c` | deepest chain |
| n3 | `step1` | `deep.c` | deepest chain |
| n4 | `helper` | `deep.c` | deepest chain |
| n5 | `left_hi` | `left.c` | unreachable |
| n6 | `left_lo` | `left.c` | unreachable |
| n7 | `orphan` | `main.c` | unreachable |
| n8 | `run` | `main.c` | deepest chain — and the declared entry point |
| n9 | `right_hi` | `right.c` | unreachable |
| n10 | `right_lo` | `right.c` | unreachable |
| n11 | `producer` | `state.c` | hidden channel **and** unreachable |
| n12 | `consumer` | `state.c` | hidden channel **and** unreachable |
| n13–n23 | `w01` … `w11` | `wide.c` | nothing |
| n24 | `reader` | `wide.c` | fan-out over the warning band |

**Twenty-five nodes and six clusters**, one cluster per source file.

### The deepest chain: 6 functions, 5 edges

`run` → `helper` → `step1` → `step2` → `step3` → `step4`. Six layers, which is
inside the accepted band — the chain is *measured* without also being a
finding, which is the case worth pinning. HLR-105 asks for the chain to be
annotated because it is the chain, not because it crossed a line.

The five edges of the chain are annotated too, and they are the only annotated
edges in the file.

### Unreachable: 7 functions

`run` is the declared root. It reaches `helper` and `reader`, and through them
the four `step` functions and the eleven `w` leaves — thirteen functions
besides itself, eighteen reachable in all. The other seven are `orphan`,
`left_hi`, `left_lo`, `right_hi`, `right_lo`, `producer` and `consumer`.

`left_hi` calls `right_lo` and `right_hi` calls `left_lo`, so those four form
two live edges in a region no path from `run` enters. Reachable and *connected*
are different properties, and a drawing that confused them would show these
four as isolated.

### Fan-out: `reader` calls **11** distinct subroutines

Eleven is the first value that warns; 0–10 produce no finding at all, across
three silent bands (HLR-086). One leaf fewer and this tree would carry no
threshold annotation to check, which is why there are exactly eleven.

### The hidden channel: `shared_flag`

`producer` writes it, `consumer` reads it, and neither calls the other — two
regions of the call graph joined by an object rather than by a call (HLR-093).
Both functions are also unreachable, and the two annotations ride different
Graphviz attributes: the shape says hidden channel, the border and the dash say
unreachable, and **neither may overwrite the other**. That is the composition
case, and it is why `state.c` is in the tree at all.

## `tree/` — the components

| Cluster | Component | Ca | Ce | Annotated |
| ------- | --------- | -- | -- | --------- |
| 0 | `deep.c` | 1 | 0 | nothing |
| 1 | `left.c` | 1 | 1 | **dependency cycle** and bottleneck |
| 2 | `main.c` | 0 | 2 | nothing |
| 3 | `right.c` | 1 | 1 | **dependency cycle** and bottleneck |
| 4 | `state.c` | 0 | 0 | nothing |
| 5 | `wide.c` | 1 | 0 | nothing |

### The dependency cycle: `left.c` ↔ `right.c`

`left_hi` calls `right_lo`; `right_hi` calls `left_lo`. So `left.c` depends on
`right.c` and `right.c` depends on `left.c`, while **no function calls itself
even indirectly** — the call graph is acyclic and the component graph is not.

That distinction is the reason the SDG carries three views (HLR-083 against
HLR-089), and this pair is what lets one tree hold a cycle annotation and a
measured deepest chain at the same time. A tree whose cycle were a *call* cycle
would have unbounded depth and no chain.

**Both members are annotated, not just one.** The threshold catalogue locates a
finding at a single subject, because a finding has one and a set has no single
location; HLR-105 asks for the members, plural. The drawing therefore takes
cycle membership from the report's cycle rows rather than from the findings,
and the same holds for the recursive cycle in the other tree.

## `recursive/` — the nodes

| id | Function | Annotated |
| -- | -------- | --------- |
| n0 | `ping` | recursive cycle, critical |
| n1 | `pong` | recursive cycle, critical |
| n2 | `kick` | nothing |

`ping` and `pong` call each other; `kick` calls `ping` and is called by nothing
declared, but `kick` **is** the declared entry point, so nothing here is
unreachable. Recursion is critical on the authority of MISRA C Rule 17.2, and
`elc` says so rather than deciding it (HLR-099).

`static void pong(int n);` above `ping` is a forward *declaration*, not a
definition. Three nodes, not four — which also pins that the C module's
function query captures definitions.

## What is not here

No layering violation and no instability finding: both need declared strata,
which is a different fixture's business. No custom-rule match, which is Phase
14. No `.dot` from a saved record — that combination is a usage error and is
checked at the integration level, where the command line lives.
