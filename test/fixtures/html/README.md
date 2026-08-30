# `html/` — the interactive containment hierarchy

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../html.bats`](../html.bats); they are never regenerated
from `elc`'s own output, which would make the fixture agree with the
implementation by construction and assert nothing (STP §2.4).

Like `dot/`, this artefact is produced for another *program* to read, so the
group keeps two claims separable:

1.  **The payload is well-formed and parses.** Asserted by extracting the
    embedded document and handing it to a JSON parser — not by pattern-matching
    text that looks like JSON. The embedding escape of LLR-HTM-03 is only
    observable this way: text that is valid JSON can still be a syntax error
    once it is inside a script element.
2.  **The hierarchy says what the analyses found.** Asserted against the
    hand-worked table below.

## The tree

Three directories, deliberately unequal: two are named by a `--stratum` and
the third is named by none, so that both branches of LLR-CYT-02 are present in
one run — and one directory holds a header as well as its source, so that a
component defining no function is present too.

```text
tree/
├── app/main.c     run, boot        — declared layer `app`, ordinal 0
├── hal/port.c     hal_open, hal_close — declared layer `hal`, ordinal 1
├── hal/port.h     (declarations)   — measured, and *not drawn*
└── vendor/blob.c  vendor_init      — matched by no stratum
```

**Four files are measured and three are drawn.** `hal/port.h` defines no
function, which is the ordinary shape of a C header and the shape `--elf`
leaves behind when an image defines none of a file's functions. Such a
component can hold no node and join no edge, so a box for it would state
nothing; the `.dot` companion has never drawn one, and the header is here so
that the interactive drawing is held to the same answer (LLR-CYT-02).

It is counted everywhere a file is counted — the project summary says four —
so the omission is of a box and never of a measurement.

Run as:

```sh
elc -o report.md --html --stratum app:'*/app/*' --stratum hal:'*/hal/*' tree
```

## The expected nodes

Counted by hand from the sources. The component index is the report's sorted
file order, which is ascending by canonical path — `app/main.c`, `hal/port.c`,
`vendor/blob.c` — and the function index is the SDG's, which follows the same
file order and then the order the functions are defined in.

| Tier | `id` | `label` | `parent` |
| ---- | ---- | ------- | -------- |
| layer | `layer_0` | `app` | — |
| layer | `layer_1` | `hal` | — |
| file | `file_0` | `app/main.c` | `layer_0` |
| file | `file_1` | `hal/port.c` | `layer_1` |
| file | `file_2` | `vendor/blob.c` | **absent** |
| function | `func_0` | `run` | `file_0` |
| function | `func_1` | `boot` | `file_0` |
| function | `func_2` | `hal_open` | `file_1` |
| function | `func_3` | `hal_close` | `file_1` |
| function | `func_4` | `vendor_init` | `file_2` |

**Two layers, not three.** `vendor/` is matched by no stratum, so no layer node
exists for it and its file node carries no `parent` key. That is
`stratum_of_components`' judgement — a file the user said nothing about lies
outside the declared architecture — followed rather than reversed. A layer
named `other` would be a structure nobody declared.

Totals: **2** layer nodes, **3** file nodes, **5** function nodes, **10**
elements before the edges.

## The expected edges

Read off the sources by hand. Every one joins two *function* nodes.

| From | To | Why |
| ---- | -- | --- |
| `func_1` (`boot`) | `func_0` (`run`) | inside one file, and one layer |
| `func_0` (`run`) | `func_2` (`hal_open`) | app → hal, crossing a layer |
| `func_0` (`run`) | `func_3` (`hal_close`) | app → hal, crossing a layer |
| `func_2` (`hal_open`) | `func_4` (`vendor_init`) | hal → the undeclared file |

Total: **4** edges.

**No meta-edges** (HLR-214). Three of these four cross a container boundary —
`boot`→`run` does not — and none of them produces a `file_`-to-`file_` or
`layer_`-to-`layer_` edge in the payload. The connection between two collapsed
boxes is synthesised by the viewer from the edges crossing between them, so an
emitted one would state the same coupling a second time, by a rule with no
threshold behind it and irreconcilable with the Ca/Ce figures the report prints
beside it (HLR-081).

`boot`→`run` is in the fixture specifically because it is the edge a wrong
implementation is most likely to get right by accident: it needs no meta-edge
under any collapsing, so a renderer that emitted one for the other three would
still draw this one correctly.
