# `nesting/` — each statement to exactly one function

The fixture header for this group. Expected values are counted below and
asserted by [`../nesting.bats`](../nesting.bats).

`elc` reports a named function declared inside another as a function in its
own right (HLR-067). That makes attribution a question rather than a given: a
statement inside the inner function is also, textually, inside the outer one.
HLR-068 settles it — each statement contributes to the **innermost** reported
function enclosing it, and to no other.

Getting this wrong does not produce an error. It produces an outer function
whose ELOC quietly includes everything its nested functions do, which reads
like a large function and is not one.

## Expected result for `elc nested.c`

| Value | Expected |
| ----- | -------- |
| Physical lines | **20** |
| File ELOC | **5** |
| Functions | **3** |
| `outer` ELOC | **3** |
| `middle` ELOC | **1** |
| `inner` ELOC | **1** |

## The count

`nested.c` nests three deep: `inner` inside `middle` inside `outer`.

| Line | Statement | Innermost function |
| ---- | --------- | ------------------ |
| 6 | `int total = seed;` | `outer` |
| 12 | `return b * 2;` | `inner` |
| 15 | `return inner(a) + 1;` | `middle` |
| 18 | `total += middle(seed);` | `outer` |
| 19 | `return total;` | `outer` |

Five statements on five distinct lines, so the **file** is 5. Each belongs to
exactly one function: `outer` 3, `middle` 1, `inner` 1.

The per-function figures sum to 5 here, and that is a coincidence of this
file rather than a rule. File ELOC counts distinct *lines*; two functions
written on one line would each count it while the file counted it once.

## What a wrong implementation reports

| Mistake | `outer` | `middle` | `inner` |
| ------- | ------- | -------- | ------- |
| **Correct** | 3 | 1 | 1 |
| Attribute to the *outermost* enclosing function | 5 | 0 | 0 |
| Attribute to *every* enclosing function | 5 | 2 | 1 |
| Attribute to the first range found containing it | depends on declaration order — non-deterministic |

The last is the one worth naming: an implementation that returns the first
containing range rather than the narrowest produces an answer that depends on
the order the query happened to match, which would also break HLR-032. The
unit tests cover that case directly by putting the ranges in both orders;
this fixture pins the observable result.

## The nested functions are a GNU extension

C has no nested functions in the standard; GCC provides them, and
`tree-sitter-c` parses them. The fixture is source that is read, never
compiled, so the extension costs nothing — and it lets the attribution rule be
tested in the language that ships today rather than waiting for Ada, whose
nested subprograms are the requirement's real motivation (HLR-067).
