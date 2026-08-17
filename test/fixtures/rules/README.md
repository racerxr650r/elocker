# `rules/` — user-supplied custom rules

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../rules.bats`](../rules.bats); they are never
regenerated from `elc`'s output, which would make the fixture agree with the
implementation by construction and assert nothing (STP §2.4).

This group is where the extensibility pillar stops being an internal property
and becomes a promise to a user. Everything here is a **data** change: no rule
in this directory required a line of C, and the group asserts that.

## The subject

```text
tree/
└── subject.c   grab, release, classify
```

Deliberately ordinary. What is under test is the rule mechanism, not `elc`'s
opinion of this code — `elc` has none about it, which is precisely the property
HLR-111 states.

| Function | Line | Contains |
| -------- | ---- | -------- |
| `grab` | 10 | one `malloc` call (line 12), one `goto` (line 15) |
| `release` | 22 | one `free` call — **not** a match; the rule names `malloc` |
| `classify` | 27 | one `goto` (line 30) |

## The rules

`house-style.scm` holds **two** rules, which is the point of it:

| Capture | Identity | Matches |
| ------- | -------- | ------- |
| `@allocation` | `house-style.allocation` | line 12 |
| `@jump` | `house-style.jump` | lines 15 and 30 |

**Three matches from one file**, under two identities. A rule's identity is the
file's basename plus the capture name, so one file expresses several
independently named rules (HLR-109) — and a fixture with one capture per file
would not show that.

### `free` is not a match, and that is the assertion

`@allocation` carries `(#eq? @allocation "malloc")`. Without predicate
evaluation the capture matches *every* call in the file, so `release`'s `free`
would appear as an allocation and the count would be 4 rather than 3.

**Tree-sitter's C library does not evaluate predicates** — it returns them as
step data and leaves the deciding to the caller. `elc` evaluates them for every
query, and this fixture is what proves a *user's* rule gets that same treatment
rather than silently having its filter dropped. A rule author who writes
`#eq?` and finds it ignored has been given a tool that lies.

`broken.scm` names a node type no C grammar has. It is used from **both**
provenances, deliberately: the same bytes must produce two different outcomes,
and a fixture using two different broken files could not tell a provenance rule
from a file-contents rule.

## What is asserted, and why each

| Claim | Why it is not obvious |
| ----- | --------------------- |
| Three matches under two identities | Identity is basename + capture, not per file |
| `free` does not match | The predicate is evaluated, not discarded |
| A rule in `queries/c/rules/` is used unnamed | Binding by directory (HLR-107) |
| The same rule named on the command line behaves identically | Binding by argument; what a rule *does* cannot depend on how it arrived |
| `broken.scm` from the runtime location: exit 0, diagnosed | A malformed component is survived (HLR-116, HLR-070) |
| `broken.scm` from `--rules`: exit 2, no report | A user error stops the run (HLR-116, HLR-063) |
| A rule naming an unavailable language: exit 0, skipped | What is missing is the module, not the rule |
| No `.scm` in the target or the working directory is read | HLR-110, and it is asserted by planting decoys |
| Matches survive a record round trip byte-identically | HLR-056 — a new report section is a new thing to get wrong |
| No match carries a severity | HLR-111 — the absence *is* the requirement |

## The runtime location is built by the test

A rule under `queries/c/rules/` has to live in a runtime directory, and the
in-tree one is shared by every other suite — planting a rule in it would change
what those suites measure. Each case that needs one therefore **builds a
runtime directory in `$BATS_TEST_TMPDIR`**, by symlinking the real one's
contents and adding a `rules/` directory of its own.

That is also the only honest way to assert HLR-110: a test that plants a decoy
`.scm` in the working directory must be sure `elc` ignores *that* file rather
than merely happening not to look in the directory it was run from.

## What is not here

No rule for a language other than C. The binding mechanism is per-language by
construction — a query compiles against one `TSLanguage` and there is no code
path that could bind it to another — and a second language would exercise the
same one line twice. The `--rules cobol:…` case covers the interesting half of
that: what happens when the named language has no module at all.
