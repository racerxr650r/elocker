# `recover/` — the layering read off the purified view, and the manifest

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../recover.bats`](../recover.bats); they are never
regenerated from `elc`'s own output, which would make the fixture agree with
the implementation by construction and assert nothing (STP §2.4).

One property governs everything below, and it is the one the phase exists to
keep: **what recovery produces is a proposal, and a proposal is never the
baseline it is measured against** (HLR-173). Two of this group's cases assert
that directly — a run over `tree/` recovers a three-layer architecture with
complete confidence, *and* its architecture-conformance section is still
omitted for want of a declaration. A tool measuring conformance against its own
proposal would find every code base conformant, because the standard would have
been read off the thing it judged.

## The two trees

```text
tree/                          cyclic/
├── app/app.c  app_start       ├── app/app.c  app_start
│              app_stop        │              app_stop
├── svc/svc.c  svc_open        ├── svc/svc.c  svc_open
│              svc_close       │              svc_close
│              svc_leaf        └── hal/hal.c  hal_init
└── hal/hal.c  hal_init                       hal_stop
               hal_stop
```

They differ in one property and nothing else: whether the recovery view still
holds a cycle.

### `tree/` — ten call edges, counted by hand

| From | To |
| ---- | -- |
| `app_start` | `svc_open`, `svc_close` |
| `app_stop` | `svc_open`, `svc_close` |
| `svc_open` | `hal_init`, `hal_stop` |
| `svc_close` | `hal_init`, `hal_stop` |
| `hal_init` | `svc_leaf` |
| `hal_stop` | `svc_leaf` |

`svc_leaf` is the reason this tree is shaped this way rather than as three
plain layers. It is called *from the layer below its own* — a completion
callback, an ordinary shape in a layered embedded program — so it sits at the
very bottom of the topological order while the rest of `svc/` sits near the
top. The graph stays a directed acyclic graph, because `svc_leaf` calls
nothing, so a layering still exists to get wrong.

### `cyclic/` — the same shape with two calls back up

`hal_init` calls `svc_open` and `hal_stop` calls `svc_close`, each of which
already called it. Those two mutual pairs put all four functions in one
strongly connected component, so no topological ordering of the recovery view
exists.

## Nothing in either tree is classified, and the reason is symmetry

Worked from the structure, at the default thresholds (sink authority ≥ 90 %
with hub ≤ 10 %, god object at betweenness ≥ 90 % and hub ≥ 90 %, peripheral
below core depth 2). A rank is the percentage of the **other** functions
scoring strictly below, compared in integers as `below × 100 ≥ percent ×
(n − 1)`.

**A rank of 90 in a seven-function tree means outranking every other
function.** With six others, `below × 100 ≥ 90 × 6` requires `below ≥ 5.4`, so
`below` must be 6 — every one of them strictly below. Nothing short of a sole
maximum meets it.

**And this tree has no sole maximum in any distribution**, because it is
symmetric. `app_start` and `app_stop` have identical out-neighbourhoods and no
callers; `svc_open` and `svc_close` have identical neighbourhoods in both
directions; `hal_init` and `hal_stop` likewise. Each pair therefore holds
*equal* hub, authority and betweenness scores, whatever those scores are — so
whichever function tops a distribution, its twin ties with it and neither
outranks all six others. No utility sink and no god object can exist here, and
that conclusion needs no eigenvector computed to reach it.

The same argument holds in `cyclic/`, where a rank of 90 over five others needs
`below ≥ 4.5`, so all five — and where the three symmetric pairs survive the
two calls back up.

**Coreness.** Every function in `tree/` has an undirected degree of at least
two: `app_start` and `app_stop` each call two service functions, `svc_open` and
`svc_close` are each called twice and call twice, `hal_init` and `hal_stop` are
each called twice and call once, and `svc_leaf` is called twice. No node has
degree below two, so nothing peels and all seven lie in the second core. In
`cyclic/` every one of the six has degree two or more for the same reason.

So the recovery view **is** the call graph in both trees: seven functions and
ten edges in `tree/`, six functions and ten edges in `cyclic/`, nothing masked
and nothing excluded. That is deliberate. This group is about the *fold*, and a
tree whose classifications had to be reasoned about first would test two things
at once — the `purify/` group beside it is where the classifications are pinned.

## The fold, worked by hand

A topological order of `tree/` places the two application functions first
(nothing calls them), then the two service functions, then the two hardware
functions, then `svc_leaf`:

| Position | Function |
| -------- | -------- |
| 0, 1 | `app_start`, `app_stop` |
| 2, 3 | `svc_open`, `svc_close` |
| 4, 5 | `hal_init`, `hal_stop` |
| 6 | `svc_leaf` |

A directory's position is the mean of its functions' positions **weighted by
how many retained edges each carries**, which is what "where the bulk of a
directory's edges point" means:

| Directory | Degrees | Weighted sum | Weight | Position |
| --------- | ------- | ------------ | ------ | -------- |
| `app/` | 2, 2 | 0×2 + 1×2 = 2 | 4 | **0.5** |
| `svc/` | 4, 4, 2 | 2×4 + 3×4 + 6×2 = 32 | 10 | **3.2** |
| `hal/` | 3, 3 | 4×3 + 5×3 = 27 | 6 | **4.5** |

So `app/` is layer 0, `svc/` layer 1 and `hal/` layer 2 — the architecture the
tree was written to have.

**The `svc_leaf` row is the whole point of the table.** A fold that placed a
directory at its *latest* member would put `svc/` at position 6, below `hal/`
at 5, and propose an architecture that is upside down in its middle. One that
asks where the bulk of the directory's edges point does not: `svc_leaf` carries
two of `svc/`'s ten edge ends, and the other eight are where the layer really
is.

## The proposal

Rendered as the arguments that would declare it, so that adopting it is a copy
rather than a transcription (HLR-173):

```text
--stratum app:'<tree>/app/*' --stratum hal:'<tree>/hal/*' --stratum svc:'<tree>/svc/*' --stratum-order 'app>svc>hal'
```

Three properties of that line are asserted, and each would be a defect if it
were absent.

*   **It is quoted.** The patterns hold a `*` and the order holds `>`. An
    unquoted order would not merely fail to be adopted — the shell would read
    it as a redirection, create files named after the layers, and hand `elc` a
    partial order it rejects.
*   **The declarations are sorted by directory depth, and the order is stated
    separately.** `stratum_of_components` takes the first declared layer whose
    pattern matches a file, and a directory wildcard matches everything beneath
    that directory. Declaring an ancestor before its child would hand the
    child's files to the wrong layer. The ordinals come from `--stratum-order`,
    so the declaration order is free to be chosen for correctness.
*   **`elc` does not apply it.** Running the same command again without the
    arguments recovers the same proposal and leaves the conformance section
    omitted, which is HLR-173 checked from outside.

## The manifest

Written from `tree/` on request and named from the report's own path, like
every other companion (HLR-119): an `--output` of `report.md` yields
`report.manifest.json`.

A manifest is read **only when named** (HLR-176). The case that pins it plants
a manifest in the working directory under every name a tool might look for and
asserts that the run is unchanged — the same guarantee the `determinism/` group
holds for rule files.

Because nothing in `tree/` is classified, the manifest that group writes is an
empty list — which is the honest answer and a poor subject. The manifest cases
therefore run over the `purify/` tree next door, where three functions are
classified, and assert:

*   a statement setting the planted dispatcher's `mask` to `false` **changes
    the recovered layering**, because its edges are then in the view;
*   the report attributes that classification to the manifest rather than to
    `elc`, so a reader can tell the tool's assumptions from their own team's
    (HLR-177);
*   a manifest naming a function no analysed file defines is **reported and
    ignored**, the run continuing — analysing one directory of a project whose
    manifest covers all of it is ordinary use;
*   a manifest that is not JSON, and one that is JSON and not a manifest, are
    **both fatal**, because the user named the file and the failure is theirs
    to correct.

## The two drawings

`--purify-dot` writes the graph as built and the recovery view read off it,
named from the report's path: `report.raw.dot` and `report.purified.dot`. The
assertion that matters is an *absence of an absence* — the masked and excluded
nodes are present in the purified drawing, greyed and holding no edge. A
drawing that deleted them could not show what purification did, which is the
entire reason there are two of them.
