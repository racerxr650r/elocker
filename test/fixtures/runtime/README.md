# `runtime/` — what a broken language module does, and does not, do to a run

The fixture header for this group. Its cases are asserted by
[`../runtime.bats`](../runtime.bats).

This group has no hand-counted numbers, because what it pins is a **shape of
failure** rather than a value: which breakages are fatal, which are survived,
and what the exit status says about each. That distinction is the whole of
HLR-036 versus HLR-070, and it is the one an implementation gets wrong by
being uniformly strict or uniformly lenient.

## The subject

```c
int only(void)      /* subject.c — one function, four lines */
{
	return 0;
}
```

Each case builds a runtime directory in `$BATS_TEST_TMPDIR`, breaks exactly
one thing in it, and runs `elc` over that subject. The directory is a copy of
the shipped `runtime/`, so every case differs from a working run in one
respect only — a fixture that built a runtime from scratch would be asserting
against its own construction.

## The cases

| Broken | Expected | Because |
| ------ | -------- | ------- |
| Runtime directory absent | diagnostic, **exit 2**, no report | No analysis is possible at all (HLR-036) |
| Runtime location is a file | diagnostic, **exit 2**, no report | Same state, reached differently |
| `extensions.map` absent | diagnostic, **exit 2**, no report | Nothing can be routed to a language |
| `extensions.map` names no language | diagnostic, **exit 2**, no report | An empty map is a runtime that yields no module |
| Grammar `.so` absent | diagnostic naming the language, **exit 0**, file listed as skipped | One unusable module is not the end of a run (HLR-070) |
| Grammar exports no entry point | as above | The `.so` loads; `tree_sitter_<lang>` does not resolve |
| A query file absent | as above | All six are required, and a module missing one is *handled* (HLR-121) |
| A query file unparseable | as above | Reported with the reason, not a bare error number |
| Nothing broken | **exit 0**, one function reported | The control the rest is read against |

## The line the group exists to draw

A **fatal** failure is one where `elc` can do nothing: no runtime location, no
extension map, no language whatsoever. It exits 2 and emits no report, because
a report covering nothing is worse than an error.

An **unusable module** is one language failing. `elc` says which, excludes it,
and finishes over the rest — exit 0, with every affected file named in the
skipped list. It is deliberately *not* a failure: a skip is a file `elc` chose
not to analyse, and HLR-037 reserves a non-zero status for files it tried to
analyse and could not.

Every survivable case here therefore asserts three things together — the
diagnostic, the exit status, and the skipped-file entry. Asserting only the
first would pass for an implementation that exited 1, and asserting only the
status would pass for one that said nothing.
