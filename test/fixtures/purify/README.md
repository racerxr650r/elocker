# `purify/` — the recovery view, and the classifications behind it

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../purify.bats`](../purify.bats); they are never
regenerated from `elc`'s own output, which would make the fixture agree with
the implementation by construction and assert nothing (STP §2.4).

Two properties govern everything below, and each has a test of its own because
each is a way the phase can be wrong while looking right.

**Purification changes no reported number** (HLR-167). The recovery view is a
*copy*; every measurement in the report — fan-out, fan-in, coupling, the
conformance indices, every cell of the matrix — is taken over the graph as
built. The masked utility sink still has a fan-in of six on the line that
reports it, and the masked dispatcher still has a fan-out of four. An
implementation that masked the graph in place would pass every classification
test in this file and fail those two.

**A classification is not a finding** (HLR-171, HLR-101). Nothing here carries
a severity, and the Findings section does not mention the god object. `elc`
observes that a function sits at the centre of a graph; it does not say the
design is wrong or what to do about it.

## The tree

```text
tree/
├── app/main.c        main, boot
├── app/dispatch.c    dispatch          — the planted god object
├── feat/feat.c       feat_a, feat_b, feat_c, helper_c
├── store/store.c     store_put, store_get
└── util/log.c        util_log          — the planted utility sink
```

Ten functions and fifteen call edges, counted by hand:

| From | To |
| ---- | -- |
| `main` | `boot`, `dispatch` |
| `boot` | `dispatch` |
| `dispatch` | `feat_a`, `feat_b`, `feat_c`, `util_log` |
| `feat_a` | `store_put`, `util_log` |
| `feat_b` | `store_get`, `util_log` |
| `feat_c` | `helper_c`, `util_log` |
| `store_put` | `util_log` |
| `store_get` | `util_log` |

Two callers of `dispatch` rather than one, deliberately. With a single entry
point the dispatcher would be nothing but the first step of every path, and the
fixture could not tell a dispatcher from an ordinary caller.

## The three centralities

### Hub and authority

HITS converges on the principal eigenvectors of the co-citation matrices, with
the maximum of each vector scaled to 1. Three of the values can be read off the
graph without iterating at all, and they are the three the classification turns
on:

*   **`util_log` has hub score exactly 0.** A hub score is the sum of the
    authorities of the nodes a function calls, and `util_log` calls nothing.
*   **`util_log` has the highest authority, 1.0000.** An authority score is the
    sum of the hub scores of a function's callers; six of the ten functions call
    it, and no other function is called by more than two.
*   **`dispatch` has the highest hub score, 1.0000.** Its four callees include
    `util_log`, which holds the largest authority in the graph, and three
    feature functions besides. No other function calls both the sink and
    anything else of weight.

`main` and `boot` come out at hub 0 as well, which is worth stating because it
is not obvious: their only authority-bearing successors are `dispatch` and each
other, a region whose eigenvalue is the smaller one, so the principal
eigenvector gives it no weight. It costs the fixture nothing — neither has the
authority a utility sink needs.

### Betweenness

The count of ordered pairs (*s*, *t*) whose shortest path passes through a
node, counted directly:

| Node | Betweenness | The pairs |
| ---- | ----------- | --------- |
| `dispatch` | **14** | from each of `main` and `boot` to each of `feat_a`, `feat_b`, `feat_c`, `util_log`, `store_put`, `store_get`, `helper_c` — 7 targets × 2 sources |
| `feat_a` | 3 | (`dispatch`, `store_put`), (`main`, `store_put`), (`boot`, `store_put`) |
| `feat_b` | 3 | the same three, to `store_get` |
| `feat_c` | 3 | the same three, to `helper_c` |
| everything else | 0 | |

`main → boot → dispatch` is length two where `main → dispatch` is length one,
so `boot` lies on no shortest path and scores zero despite being an
intermediary. That is the case the metric is *supposed* to answer that way, and
it is in the tree so that a high betweenness cannot be mistaken for a synonym
for "has both callers and callees".

### Coreness

Over the undirected neighbourhood, since a *k*-core is the mutually connected
centre of a program and a leaf hanging off it is peripheral whichever way its
one edge points.

`helper_c` has undirected degree 1 — `feat_c` calls it and it calls nothing —
so it lies in the first core. Removing it leaves every remaining function with
at least two neighbours, so all nine of the rest lie in the second core.

## The classifications

The thresholds are compared against a node's **position in the ordered
distribution**, as the percentage of the *other* nine functions it outranks —
never against the raw score. At the defaults (sink authority ≥ 90 % with hub
≤ 10 %, god object at betweenness ≥ 90 % and hub ≥ 90 %, peripheral below core
depth 2):

| Function | Hub rank | Authority rank | Betweenness rank | Coreness | Class |
| -------- | -------- | -------------- | ---------------- | -------- | ----- |
| `dispatch` | **100 %** | 0 % | **100 %** | 2 | **god object** |
| `util_log` | **0 %** | **100 %** | 0 % | 2 | **utility sink** |
| `helper_c` | 0 % | 33 % | 0 % | **1** | **peripheral** |
| `feat_a`, `feat_b`, `feat_c` | 66 % | 66 % | 66 % | 2 | — |
| `store_put`, `store_get` | 44 % | 33 % | 0 % | 2 | — |
| `main`, `boot` | 0 % | 0 % | 0 % | 2 | — |

**Exactly three of ten are classified**, and the seven that are not are as much
of the fixture as the three that are: a test seeing only classified functions
would pass against an implementation that classified everything. `main` and
`boot` are the sharpest of the seven — each has hub rank 0 and so passes the
*hub* half of the utility-sink test, and neither is a sink, because the
authority half fails. An implementation testing either half alone reports them.

**Rank, not raw score, is what makes one default work here at all.** The whole
tree's betweenness fits in a range of 0 to 14. Any fixed cut-off serviceable on
a project whose dispatcher lies on ten thousand shortest paths would classify
nothing in this tree, and any cut-off serviceable here would classify half of
that project.

## What the masking does

| Class | Action | Edges removed |
| ----- | ------ | ------------- |
| `dispatch`, god object | all edges masked | `main → dispatch`, `boot → dispatch`, `dispatch → feat_a`, `dispatch → feat_b`, `dispatch → feat_c`, `dispatch → util_log` |
| `util_log`, utility sink | **incoming** edges masked | `feat_a →`, `feat_b →`, `feat_c →`, `store_put →`, `store_get →` (and `dispatch →`, already counted above) |
| `helper_c`, peripheral | excluded from the view | `feat_c → helper_c` |

Twelve of the fifteen call edges, so **three survive**: `main → boot`,
`feat_a → store_put`, and `feat_b → store_get`. Nine of the ten functions are
retained; `helper_c` alone is excluded, and it is given no layer rather than the
bottom one (HLR-170).

**The asymmetry between the first two rows is the point** (HLR-168, HLR-169). A
utility sink loses its incoming edges only, because the fusion it causes is
between its *callers* — its outgoing edges join nothing that was not already
joined. A god object loses both directions, because it short-circuits in both.
`util_log` calls nothing, so this tree cannot show a sink's outgoing edge
surviving; that half is pinned by a unit test over a hand-built classification
instead (`test/unit/purify.c`).

## The thresholds are configurable, and they are `elc`'s own

None of the five is a published standard, and the report says so wherever a
classification is presented (HLR-171). Two settings are asserted because they
move the answer in opposite directions:

*   `--core-depth 1` puts `helper_c` in the view: nothing lies below the first
    core, so nothing is excluded and the two centrality classifications are
    unchanged.
*   `--core-depth 3` puts eight of the ten out of it: every function not
    already classified lies in the second core, and the third is now the floor.
    Two are retained — `dispatch` and `util_log` keep their classifications,
    because the centrality tests are asked first and a function they named is
    part of the centre by construction — and all fifteen call edges are masked.
