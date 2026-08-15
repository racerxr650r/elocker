# `comments/` — the cases no textual approach survives

The fixture header for this group. Expected values are counted below and
asserted by [`../comments.bats`](../comments.bats).

This group is where **HLR-013** is verified. That requirement — every metric
derived from a parsed syntax tree, never from regular expressions, brace
counting, or any other textual approximation — has no observable of its own:
you cannot look at a number and see how it was obtained. What you *can* do is
construct source on which every textual approach gives a different answer from
the parser's, and check which answer arrives. That is what this file is.

## Expected result for `elc adversarial.c`

| Value | Expected |
| ----- | -------- |
| Physical lines | **23** |
| File ELOC | **4** |
| Functions | **3** |
| `block_opener_in_string` ELOC | **1** |
| `line_opener_in_string` ELOC | **1** |
| `quote_in_comment` ELOC | **2** |

## The count

Four lines carry a statement: 6, 11, 20, and 22 — three `return`s and one
initialising declaration. Everything else is a comment, a blank, a brace, or a
signature.

## What each case defeats

| Line | Source | A textual matcher would |
| ---- | ------ | ----------------------- |
| 6 | `return "/* this opens nothing */";` | see `/*`, open a comment, and swallow the rest of the file |
| 11 | `return "// nor does this";` | see `//` and discard the rest of the line — losing a statement |
| 16 | `/* a comment containing " an unbalanced quote` | open a string on the quote, then mis-parse everything after it |
| 17 | `   and // inline syntax` | count a second comment inside the first, and exclude line 17 twice |
| 18 | `   and /* what looks like a nested opener` | open a second block comment, and then need two `*/` to close |
| 19 | `*/` | close only the inner comment, leaving the file in a comment for ever |
| 20 | `int n = 1;      /* trailing */` | exclude the whole line as a comment, losing a line of code |
| 21 | `// a whole-line comment` | correctly exclude — the one case that is easy |

None of these needs a rule. The parser has already decided what is a string
and what is a comment before a query runs, so `comments.scm` is one pattern
and `eloc.scm` never sees any of it.

## Line 20 is the one that bit

`int n = 1;      /* trailing */` is a line of code *and* a line touched by a
comment span. The first implementation of the comment exclusion asked whether
a statement's **line** fell inside a comment span, and this line does — so it
silently deleted a statement, and the file counted 3 instead of 4.

Comment spans are byte ranges. The exclusion is one too: what matters is
whether the *statement* lies inside a comment, not whether its line touches
one. The case is in the fixture rather than only in a unit test because it is
the kind of thing a future change to the exclusion would break without
noticing.

## Nesting is a language question, not a fixture one

C block comments do not nest: the `/*` on line 18 is ordinary text inside the
comment opened on line 16, and the `*/` on line 19 closes it. A language whose
comments *do* nest — Rust, Ada — produces genuinely nested spans, which is why
`merge_comment_spans` coalesces rather than assuming disjointness. Those cases
are unit-tested directly against the merge, where they can be constructed
exactly, rather than approximated here in a language that cannot express them.
