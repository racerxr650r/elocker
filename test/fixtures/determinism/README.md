# `determinism/` — the same tree, the same bytes, every time

The fixture header for this group. Its expected values are counted by hand
here and asserted by [`../determinism.bats`](../determinism.bats).

There is only one number to count, because the point of this group is not
what the numbers are but that they, and the bytes around them, never move.

## The tree

```text
tree/
├── a.c      1 line
├── z.c      1 line
└── m/n.c    1 line
```

## Expected result for `elc tree/`

| Value | Expected | Counted how |
| ----- | -------- | ----------- |
| Files | **3** | `a.c`, `m/n.c`, `z.c` |
| Physical lines | **3** | 1 + 1 + 1 |

The file names are chosen so that byte order and creation order disagree:
`z.c` was created first and `a.c` second, and neither `readdir` order nor the
order the targets are named may show through into the report (HLR-033).

## What each case establishes

* **Two runs, unmodified** — byte-identical output (HLR-032). A determinism
  failure here is never a flaky test; it is a product defect.
* **Targets in a different order** — the report does not record the order the
  user typed (HLR-033).
* **A file target reordered against a directory target** — the same, across
  the two classification routes.
* **A decoy `.elcrc` in the working directory, in the target, and in an
  ancestor of the target** — output byte-identical to its absence (HLR-039).
  The decoys are planted in a copy of the tree under `$BATS_TEST_TMPDIR`
  rather than committed here, because the *ancestor* case needs a directory
  above the tree that the suite owns, and because a committed decoy could
  only ever be planted, never removed for the comparison.
