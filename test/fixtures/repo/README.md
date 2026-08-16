# `repo/` — what the repository route enumerates, and what it does not

The fixture header for this group. Its expected values are counted by hand
here and asserted by [`../repo.bats`](../repo.bats); they are never
regenerated from `elc`'s own output, which would make the fixture agree with
the implementation by construction and assert nothing (STP §2.4).

Unlike every other group, the tree is not committed — it is built by
[`build.sh`](build.sh) into the running test's temporary directory. A
directory holding `.git/` is a repository, and git will not track one inside
another. The script's header explains the second reason, which matters more:
no test here touches elocker's own checkout.

## The tree

```text
repo/
├── .git/                        the repository these tests are about
├── .gitignore       tracked     excluded — a hidden entry
├── .hidden.c        tracked     excluded — a hidden entry
├── logo.png         tracked     excluded — extension in runtime/binary.exts
├── docs/d.c         tracked     included, 1 line
├── src/a.c          tracked     included, 4 lines
├── src/b.c          tracked     included, 3 lines
├── src/blob.c       tracked     excluded — binary content, despite the .c
├── src/untracked.c  untracked   excluded — not in the tree at HEAD
└── build/gen.c      ignored     excluded — not in the tree at HEAD
```

## Expected result for `elc repo/`

| Value | Expected | Counted how |
| ----- | -------- | ----------- |
| Files | **3** | `docs/d.c`, `src/a.c`, `src/b.c` |
| Physical lines | **8** | 4 + 3 + 1 |
| Functions | **3** | one apiece |
| Route | **repository** | the target is a work tree with tracked files beneath it (HLR-002) |

`src/a.c` is four lines: the signature, the opening brace, the `return`, and
the closing brace. `src/b.c` is three, with the brace on the signature line.
`docs/d.c` is one. Every file ends with a newline.

## Expected result for `elc repo/src`

| Value | Expected | Counted how |
| ----- | -------- | ----------- |
| Files | **2** | `src/a.c`, `src/b.c` — `docs/d.c` is outside the target |
| Physical lines | **7** | 4 + 3 |
| Route | **repository** | the scope is narrower, the route is the same |

This second table is the whole point of the group's hardest test. The tree at
`HEAD` is the entire repository: enumerating it and stopping there would make
`elc src/` analyse `docs/` too, and it would do so *silently* — a larger
number is not obviously a wrong one. The scoped totals differ from the whole
repository's, so a missing scope test shows up as a failure rather than as a
figure nobody questions (HLR-126).

## Why each exclusion is in the tree

* **`src/untracked.c`** — the file the repository route exists to exclude
  (HLR-003). It is a perfectly ordinary source file that the filesystem walk
  would find; only its absence from `HEAD` keeps it out.
* **`build/gen.c`** — the generated-output case, and the reason the feature
  is worth having: build directories are where a filesystem walk goes wrong
  by tens of thousands of lines. It is excluded because it is not tracked,
  not because `.gitignore` was consulted — `elc` never parses that file.
* **`src/blob.c`** — tracked, named `.c`, and binary. The extension list
  cannot exclude it, so if it stays out, git's own content check did the
  work (HLR-003). Its first line is valid C, so a tool that gave up on
  detection and read the first line would include it.
* **`logo.png`** — tracked and excluded by extension, showing that the
  exclusions applied to the filesystem walk are applied to this route too
  rather than being bypassed by it. Its content is text: the requirement is
  about the extension.
* **`.gitignore`, `.hidden.c`** — tracked, and excluded as hidden entries
  (HLR-005). Both routes must agree about hidden files, and it would be easy
  for the repository route not to check, since git is happy to report them.

## What is not here

There is no fixture for a repository with no commits, a bare repository, or a
target inside `.git/` itself. Those are error and boundary cases rather than
enumeration ones, and they are constructed inline in `../repo.bats` where the
setup is three lines and belongs next to the assertion.
