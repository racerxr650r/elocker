# `elf/` — filtering by a linked image

The source a filtered run measures, and the hand-worked counts
[`../elf.bats`](../elf.bats) asserts against. Every figure here was counted by
reading the files; none was taken from `elc`'s output, and a disagreement
between the two is a defect in one of them rather than a number to update.

**The images are built by the suite, never committed.** A binary in a
repository is a fixture nobody can review, and one built elsewhere pins the
reviewer to a toolchain they may not have. Each is built in
`$BATS_TEST_TMPDIR` from the sources named below, and where the compiler that
case needs is unavailable the test skips explicitly, naming the requirement
that thereby went unverified ([STP](../../doc/STP.md) §5).

## `tree/` — the C case

The image is a **shared object built from `kept.c` alone**, which is what makes
the fixture express three things one executable could not:

*   `dropped.c` is never built, so its functions are in the source and not in
    the image — the finding the option exists to produce.
*   `kept.c` calls `unlinked_add`, which `dropped.c` defines. A shared object
    links with that symbol left undefined, so the image *imports* a function it
    does not define — and a filter that failed to test `SHN_UNDEF` would retain
    `dropped.c`'s definition of it (LLR-ELF-02).
*   The same call is one whose target the filter removed, so it is counted
    unresolved rather than resolved to a function the image does not contain
    (HLR-144).

`scale` is `static`. It reaches `.symtab` and not `.dynsym`, so a run that
retains it is also the evidence that the symbol table read is the one a linker
writes by default (LLR-ELF-01, LLR-ELF-04).

### `tree/kept.c` — 38 physical lines

| Function | Lines | ELOC | Complexity | Counted from |
| -------- | ----- | ---- | ---------- | ------------ |
| `scale`   | 21–26 | 3 | 2 | 23, 24, 25; the `if` is the one decision |
| `measure` | 28–32 | 2 | 1 | 30, 31 |
| `main`    | 34–38 | 2 | 1 | 36, 37 |

Line 19, `int kept_counter = 0;`, is an initialised declaration outside every
function: 1 line of file-scope ELOC. Line 17 is a bare declaration and line 14
a preprocessor directive, and neither counts (HLR-051, HLR-052).

File ELOC 8 = 1 + 3 + 2 + 2.

### `tree/dropped.c` — 23 physical lines

| Function | Lines | ELOC | Complexity | Counted from |
| -------- | ----- | ---- | ---------- | ------------ |
| `unlinked_add` | 13–16 | 1 | 1 | 15 |
| `unlinked_max` | 18–23 | 3 | 2 | 20, 21, 22 |

Line 11, `int dropped_counter = 1;`, is 1 line of file-scope ELOC.

File ELOC 5 = 1 + 1 + 3.

### The two runs over `tree/`

| Figure | No image | Filtered by the image |
| ------ | -------- | --------------------- |
| Files | 2 | 2 |
| Physical lines | 61 | 61 |
| Functions | 5 | 3 |
| ELOC | 13 | 9 |
| Unresolved calls | 1 | 2 |
| ELOC outside any function | *not reported* | 2 |
| Functions the image does not define | *not reported* | 2 |

The filtered ELOC is 9 and not 8: `dropped_counter` is retained even though
every function in its file was removed, because the image's *function* set says
nothing about code that is not a function (HLR-145). That single line is the
difference between a figure a reader can interpret and one that silently folds
away the part the filter did not narrow.

The unresolved-call count rises by exactly one, and the one is `unlinked_add`.
`printf` is the other, in both runs, and is unresolved for the ordinary reason
that a project calling into libc has unresolved calls by definition (HLR-077).

## `imported/shim.c` — the `SHN_UNDEF` rule on its own

A definition of `printf`, which the `tree/` image imports from libc and does
not define. Filtered by that image the file reports **no functions and one
absent**: without the definedness test every function the image calls into a
shared library would count as one the image contains, and this definition would
be retained (LLR-ELF-02).

The file is parsed and never compiled, so the redefinition it would be to a
compiler is of no consequence.

## `cpp/shapes.cpp` — Itanium demangling — 48 physical lines

Built with `c++` into an executable; the case skips where no C++ compiler is
available.

| Function | Lines | ELOC | In the image | Linkage name |
| -------- | ----- | ---- | ------------ | ------------ |
| `Rect`      | 19–19 | 0 | yes | `_ZN8geometry4RectC2Eii` |
| `area`      | 21–24 | 1 | yes | `_ZNK8geometry4Rect4areaEv` |
| `perimeter` | 26–29 | 1 | **no** | — |
| `scale`     | 36–39 | 1 | yes | `_ZN8geometry5scaleERKNS_4RectEi` |
| `main`      | 43–48 | 2 | yes | `main` |

File ELOC 5; filtered, 4 functions and ELOC 4.

`perimeter` is defined and never called, so an unoptimised build emits no
out-of-line copy and the image does not define it. Every other name is
qualified, so the case fails in two distinct ways if either half of the
resolution is missing: matching raw linkage names retains nothing at all, and
matching `geometry::Rect::area() const` against `area` matches nothing either.

## `mangled/opaque.s` — a scheme this build does not decode

An assembly file defining one function symbol named `_RNvCs1234_7mycrate3foo`,
which is Rust's v0 mangling. The C++ runtime's Itanium demangler rejects it, so
it resolves to no source name and is **counted, not dropped**: an image linked
with it reports one unresolved linkage name (HLR-143, LLR-SYM-04).

Written in assembly rather than produced by a Rust compiler for the reason the
images are built rather than committed. Requiring a second toolchain to run the
suite would leave the case unverified wherever that toolchain is absent, and
the symbol needs only a name, a type, and a definition — no instruction, since
nothing calls it.
