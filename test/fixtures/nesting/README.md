# `nesting/` — each statement and each decision to exactly one function

The fixture header for this group. Expected values are counted below and
asserted by [`../nesting.bats`](../nesting.bats).

`elc` reports a named function declared inside another as a function in its
own right (HLR-067). That makes attribution a question rather than a given: a
statement inside the inner function is also, textually, inside the outer one.
HLR-068 settles it — each statement contributes to the **innermost** reported
function enclosing it, and to no other. HLR-017's decision points follow the
same rule, for the same reason.

Getting this wrong does not produce an error. It produces an outer function
whose ELOC and complexity quietly include everything its nested functions do,
which reads like a large, branchy function and is not one.

## Expected result for `elc nested.c`

| Value | Expected |
| ----- | -------- |
| Physical lines | **23** |
| File ELOC | **8** |
| Functions | **3** |

| Function | ELOC | Complexity |
| -------- | ---- | ---------- |
| `outer` | **4** | **3** |
| `middle` | **3** | **2** |
| `inner` | **1** | **2** |

## The ELOC count

`nested.c` nests three deep: `inner` inside `middle` inside `outer`.

| Line | Statement | Innermost function |
| ---- | --------- | ------------------ |
| 6 | `int total = seed;` | `outer` |
| 12 | `return b > 0 ? b * 2 : 0;` | `inner` |
| 15 | `if (a > 0)` | `middle` |
| 16 | `return inner(a) + 1;` | `middle` |
| 17 | `return 0;` | `middle` |
| 20 | `if (seed > 0 && seed < 100)` | `outer` |
| 21 | `total += middle(seed);` | `outer` |
| 22 | `return total;` | `outer` |

Eight statements on eight distinct lines, so the **file** is 8. Each belongs
to exactly one function: `outer` 4, `middle` 3, `inner` 1.

That the per-function figures sum to the file total is a coincidence of this
file rather than a rule. File ELOC counts distinct *lines*; two functions
written on one line would each count it while the file counted it once.

## The complexity count

Complexity is **one plus** the decision points in a function.

| Function | Decision points | Complexity |
| -------- | --------------- | ---------- |
| `inner` | the `? :` on line 12 | 1 + 1 = **2** |
| `middle` | the `if` on line 15 | 1 + 1 = **2** |
| `outer` | the `if` on line 20, and the `&&` in its condition | 1 + 2 = **3** |

Line 20 is the one to look at. `if (seed > 0 && seed < 100)` is **two**
decision points, not one: the `&&` short-circuits, so there are two places
control can take a different path. A function built from one long compound
condition would otherwise score the same as a function with no condition at
all.

## What a wrong implementation reports

| Mistake | `outer` | `middle` | `inner` |
| ------- | ------- | -------- | ------- |
| **Correct** | 3 | 2 | 2 |
| Run the query against each body, without attribution | 5 | 3 | 2 |
| Attribute to the first range found containing it | depends on declaration order — non-deterministic |
| Capture the function itself as a decision point | 4 | 3 | 3 |

The second is the one worth naming: an implementation that returns the first
containing range rather than the narrowest produces an answer that depends on
the order the query happened to match, which would also break HLR-032. The
unit tests cover that directly by putting the ranges in both orders; this
fixture pins the observable result.

## The nested functions are a GNU extension

C has no nested functions in the standard; GCC provides them, and
`tree-sitter-c` parses them. The fixture is source that is read, never
compiled, so the extension costs nothing — and it lets the attribution rule be
tested in the language that ships today rather than waiting for Ada, whose
nested subprograms are the requirement's real motivation (HLR-067).

**An *anonymous* callable is the other half of the rule** (HLR-018): a
lambda is not reported, so its decision points belong to the nearest enclosing
*named* function. C has no lambdas, so that half cannot be exercised here. It
is unit-tested against `innermost_enclosing` directly — an offset inside an
unreported scope resolving to the named function containing it — and gains a
fixture when C++ arrives in Phase 6.
