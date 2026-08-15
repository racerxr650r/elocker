### `regeneration/` — a saved record is worth exactly what it can rebuild

The fixture header for this group. Asserted by
[`../regeneration.bats`](../regeneration.bats).

A saved XML record (HLR-054) exists so that a report can be produced later
without the source it describes — a build artefact kept after the tree it came
from has moved on. That is only true if the record carries **everything** a
report can present. Anything left out is not a gap that announces itself: it
is a regenerated report that looks complete and quietly is not.

HLR-056 makes the property checkable by making it absolute. The Markdown
regenerated from a record must be **byte-identical** to the Markdown a direct
analysis would have produced at the same threshold — not similar, not
equivalent. A single missing element shows up as a diff.

## The subject

`tree/` holds something in every tier the report has today:

| | |
| --- | --- |
| `a.c` | two functions, one branchy and one not; an initialised global outside both, so the file's ELOC exceeds the sum of its functions' |
| `b.h` | a header that defines a function, so more than one file appears |
| `notes.md` | no language module, so the skipped list is non-empty |

Between them the record must carry per-file totals, per-function detail, a
per-language breakdown, the callouts, and the skipped list. A tier that is
empty in the fixture is a tier the round trip cannot check.

## Expected result for `elc tree/`

| Value | Expected |
| ----- | -------- |
| Files | **2** |
| Physical lines | **20** |
| ELOC | **8** |
| Functions | **3** |
| Skipped | **1** |

## The cases

| Case | Expected |
| ---- | -------- |
| `-f xml` then `--from-xml` | Markdown byte-identical to `-f md` on the tree |
| the same, at `-c 2` | byte-identical to `-f md -c 2`, and *different* from the run above |
| `--from-xml` with a threshold the record never saw | the new threshold's listing |
| a record that is not XML | exit 2, no output |
| a well-formed document of another shape | exit 2, no output |
| a record with a bumped `format-version` | exit 2, no output, naming the version |
| `--from-xml` with a target | usage error |
| `--from-xml -f csv` | usage error |

## Why the threshold is not in the record

The record stores what was **measured**; the threshold is what somebody
**decided** about it. Keeping the two separate is what lets one record answer
"which functions are over 10?" and "which are over 20?" without re-analysing
anything — and it is why `--from-xml` takes `-c` at all (HLR-057).

The second case above is the one that proves it: the same record, two
thresholds, two different listings, each matching what a direct run would have
produced.

## Why byte-identical is achievable rather than aspirational

The reader reconstructs the per-file and per-function *facts* and then calls
the same `report_assemble` a live run calls. Every derived value — the totals,
the per-language breakdown, the callouts and their tie-break, the threshold
listing, the ordering — is computed once, by one function, on both paths.

Had the reader rebuilt the derived values from the record instead, this group
would be testing that two pieces of code agree, which they would eventually
stop doing.
