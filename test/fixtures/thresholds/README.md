# `thresholds/` — banding, severity, and attribution

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../thresholds.bats`](../thresholds.bats); they are never
regenerated from `elc`'s own output, which would make the fixture agree with
the implementation by construction and assert nothing (STP §2.4).

This group tests the **only** module in `elc` that judges. Everything else
measures and refuses to; here the measurements meet the catalogue of PVD
Appendix A and come back with a severity and a citation.

Three properties matter more than any single band, and each is asserted
directly:

* **The bands are exhaustive.** Every fan-out value classifies exactly once,
  including the 8–10 *acceptable* range that produces no finding.
* **Every threshold names its source**, and the one that is `elc`'s own says
  so where a reader sees it.
* **A measurement inside its band is still reported** — in the table that
  measured it. The Findings section is the subset that crossed a line, never a
  replacement for the tables above it.

## The tree

```text
tree/
├── bands.c   one function per fan-out band boundary
└── clean.c   a project with nothing outside any band
```

Two files, because the second asserts an absence. A suite that only ever saw
findings would pass against an implementation that reported everything.

## `bands.c` — the eight boundary values

Sixteen do-nothing helpers `h01` … `h16`, and one caller per boundary from PVD
Appendix A.2. The values are the same eight `calltree/fanout.c` pins as
*measurements*, so a disagreement here is a **banding** error rather than a
counting one — that separation is why Phase 9 built that fixture and deferred
the banding to this phase.

| Function | Fan-out | Band | Finding |
| -------- | ------- | ---- | ------- |
| `band_below` | 2 | below healthy | **none** |
| `band_healthy_low` | 3 | healthy | **none** |
| `band_healthy_high` | 7 | healthy | **none** |
| `band_acceptable_low` | 8 | acceptable | **none** |
| `band_acceptable_high` | 10 | acceptable | **none** |
| `band_warn_low` | 11 | weak abstraction | **warning** |
| `band_warn_high` | 15 | weak abstraction | **warning** |
| `band_critical_one` | 16 | god function | **critical** |

**Five of the eight produce nothing, and that is the half of the table worth
asserting.** An implementation that banded 8 and 10 as warnings would still
pass a suite that only checked 11 and 16. The *acceptable* band in particular
was a gap in an earlier reading of the thresholds, which is why HLR-086 states
the bands are exhaustive rather than leaving it to be inferred.

Exactly three findings come out of this file. Asserting the count is what
catches a band claiming a value twice.

## `clean.c` — the empty result

Three leaves and an entry point that calls them: a fan-out of 3, which is
healthy; no recursion, no cycles, no globals, and a depth of 2.

The Findings section is present and empty. That is a **result** — `elc` looked
and found nothing outside any band — and it is why the section is emitted with
its heading rather than suppressed when there is nothing in it. An absent
section is indistinguishable from a renderer that forgot.

The Files and Functions tables still carry every measurement, because a value
inside its accepted band is still a value the reader asked for (HLR-031).

## What this group does not cover

The severity of a **dependency cycle**, **recursion**, a **bottleneck** and a
**single-function global** are exercised against the `arch/` and
`reachability/` trees, which already build those shapes by hand. Rebuilding
them here would duplicate a fixture rather than test a threshold.

Severity's effect on the **exit status** is asserted here, because it is a
property of the whole run rather than of any tree: a project full of critical
findings still exits 0 when every file was read (HLR-100).
