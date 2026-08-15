# `traversal/` — what the filesystem walk includes, and what it does not

The fixture header for this group. Its expected values are counted by hand
here and asserted by [`../traversal.bats`](../traversal.bats); they are never
regenerated from `elc`'s own output, which would make the fixture agree with
the implementation by construction and assert nothing (STP §2.4).

## The tree

```text
tree/
├── a.c              4 lines   included
├── b.h              3 lines   included
├── sub/c.c          1 line    included
├── image.png                  excluded — extension in runtime/binary.exts
├── archive.zip                excluded — extension in runtime/binary.exts
├── .elcrc                     excluded — hidden entry
├── .hidden/secret.c           excluded — inside a hidden directory
├── link.c -> a.c              excluded — a link is not followed during the walk
└── loop -> ..                 excluded — a cyclic directory link, never descended
```

## Expected result for `elc tree/`

| Value | Expected | Counted how |
| ----- | -------- | ----------- |
| Files | **3** | `a.c`, `b.h`, `sub/c.c` |
| Physical lines | **8** | 4 + 3 + 1 |

`a.c` is four lines: the signature, the opening brace, the `return`, and the
closing brace. `b.h` is three: the guard's `#ifndef`, `#define`, and `#endif`.
`sub/c.c` is one. Every file ends with a newline, so no unterminated final
line is in play here.

## Why each exclusion is in the tree

* **`image.png`, `archive.zip`** — the binary-extension exclusion (HLR-005).
  Their contents are text: the requirement is about the extension, and a real
  binary would prove nothing extra while making the fixture unreadable.
* **`.elcrc`, `.hidden/`** — hidden entries are excluded (HLR-005). `.elcrc`
  is the specific case HLR-039 turns on: a configuration-like file planted in
  the analysis target must not change the output, and it cannot if the walk
  never yields it.
* **`link.c -> a.c`** — a symbolic link to a file already in the tree. Were it
  followed, `a.c` would be counted twice (HLR-069).
* **`loop -> ..`** — the self-referential directory link. `FTS_PHYSICAL` does
  not descend into it; `FTS_LOGICAL` would walk it for ever. This is the entry
  whose presence turns "the walk terminates" into a test rather than a hope.

A symbolic link named *directly* as a target is the other half of HLR-069 and
is resolved rather than skipped. `link.c` serves both cases: skipped when
reached through the walk, analysed when named.
