# `deadcode/` — statements that cannot execute

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../deadcode.bats`](../deadcode.bats); they are never
regenerated from `elc`'s own output, which would make the fixture agree with
the implementation by construction and assert nothing (STP §2.4).

**Half the cases in this group are things `elc` must *not* report.** That is
the shape of HLR-138: a missed statement costs a cleanup opportunity, and a
false claim invites deleting code that runs. A suite that only checked the
findings would pass against an implementation that reported every `if`.

Line numbers are load-bearing. Both files carry their own line numbers in
trailing comments, and the comments are themselves part of what is under test:
a comment is a *named* sibling in tree-sitter-c, so a walk that did not exclude
them would report the trailing note on a `return`'s own line as dead code.

## The tree

```text
tree/
├── terminator.c   the sibling walk, and the label that must survive it
└── literal.c      branches a written literal excludes, and the ones it does not
```

Two files rather than one because the two classes are found by different
means — a structural walk and a query predicate — and a failure should name
which.

## `terminator.c` — the sibling walk

| Function | Dead lines | Why |
| -------- | ---------- | --- |
| `after_return` | **9, 10** | statements following `return 0;` in the same block |
| `after_return` | *not 11* | **`done:` is a sibling of the return and is reachable** |
| `after_return` | *not 12* | reached only through that label, and reached nonetheless |
| `after_break` | **19** | follows `break` inside the loop body |
| `after_break` | *not 21* | a different block; the walk does not leave its parent |
| `after_continue` | **28** | follows `continue` inside the loop body |
| `switch_arms` | *nothing* | see below |
| `two_terminators` | **47, 48, 49** | everything after the first `return` |

**The label is the case that decides whether this analysis can be trusted.**
In `tree-sitter-c` a `labeled_statement` is a *sibling* of the preceding
`return`, so a naive walk reports it — and reports the live code inside it. The
`@dead.reentry` capture in `c/deadcode.scm` is what stops the walk there, and
omitting that one pattern is the easiest way to make `elc` invite the deletion
of a working `goto` target.

**`switch_arms` reports nothing, and that is a property of the grammar rather
than a rule.** A `case_statement` in this grammar *contains* the statements of
its arm, so the `return 1;` in `case 1:` has no sibling to leak into and
`case 2:` is not a sibling of it. Another grammar could flatten switch arms and
would then need the re-entry pattern; `c/deadcode.scm` carries one for
`case_statement` anyway, because verifying the shape is cheaper than relying on
it staying that way.

**`two_terminators` pins the de-duplication.** Line 48 follows both `return 1;`
and `return 2;`, so the walk reaches it twice; it is reported once.

## `literal.c` — branches a literal excludes

| Function | Dead lines | Why |
| -------- | ---------- | --- |
| `excluded` | **8–10** | the consequence of `if (0)` |
| `excluded` | **13–15** | the alternative of `if (1)`, `else` keyword included |
| `loops` | **20–22** | the body of `while (0)` |
| `loops` | *not 23–25* | **a `do`-`while (0)` body runs exactly once** |
| `needs_data_flow` | *nothing* | see below |

A dead branch is reported as its whole span, not as its first line: the
reader's next action is to delete it, and the span is what they delete. The
`else` keyword is inside the reported range because the `else` goes with the
block.

**`needs_data_flow` is the whole of HLR-138 in one function**, and every line
in it is a case `elc` must stay silent about:

* `int x = 0; if (x)` — a reader can see the value; `elc` performs no data flow
  and no constant propagation, and a query that tried would be claiming
  knowledge it does not have.
* `const int zero = 0; if (zero)` — the same, made more tempting by the `const`.
  The temptation is the point of including it.
* `if (0x0)` — a zero the source did not write as a decimal zero. The regular
  expression in `c/deadcode.scm` matches `^0[uUlL]*$` and nothing else, so this
  falls through as *undecided* rather than being judged. Missing it costs a
  cleanup; a looser pattern that also matched `0x1` would delete live code.

## What this group does not cover

`elc` reports dead code only *within a reported function* (HLR-137). A
statement at file scope in a language that permits one has no enclosing
function to attribute it to, and is not recorded.
