# The language-module contract

This is the interface a language module codes against (HLR-121). It is a
**published contract**, not an internal arrangement: renaming a file or a
capture below is a breaking change for every grammar anyone has shipped, and
is treated as one.

Adding a language requires no change to `elc` and no rebuild of it (HLR-010).
If a language addition seems to need a C change, the extensibility pillar is
broken and that is the thing to fix.

## What a language module is

```text
runtime/
├── extensions.map              # "<ext> <lang>" per line          (HLR-060)
├── parsers/<lang>.so           # exports tree_sitter_<lang>       (HLR-009)
└── queries/<lang>/
    ├── functions.scm  comments.scm  complexity.scm
    ├── eloc.scm       calls.scm     globals.scm
    └── rules/*.scm             # optional custom rules            (HLR-107)
```

All six query files are **required**. A module that omits one is reported as
unusable, excluded from the run, and does not stop it (HLR-070) — it is not
undefined behaviour, and it is not fatal.

A query file that captures nothing is valid, and is how an unimplemented
query is expressed. `elc` reads captures; it never asks whether a file is
"filled in".

## The captures

Everything `elc` extracts arrives through a capture name. No C code contains
a language name, a file extension, or a grammar node type — if you are typing
`"if_statement"` into a `.c` file, it belongs in a `.scm` file instead.

| File | Capture | Meaning |
| ---- | ------- | ------- |
| `functions.scm` | `@function.name` | The identifier reported as the function's name |
| | `@function.body` | The function's body: the node against which `complexity.scm` is run, and whose last line ends the reported span |
| `comments.scm` | `@comment` | One comment span. Spans may overlap and nest; `elc` coalesces them |
| `eloc.scm` | `@eloc.statement` | One statement counting toward ELOC. Capture the statement node, not its lines — a multi-line statement counts once, at its start line (HLR-053) |
| `complexity.scm` | `@complexity.decision` | One decision point. Do **not** capture the function: the `1 +` base is added by `elc`, and capturing it double-counts |
| `calls.scm` | `@call.name` | The callee identifier at a call site |
| | `@call.address_taken` | A function whose address is taken without being called |
| `globals.scm` | `@global.declaration` | A global's declaration |
| | `@global.read` | An identifier reading a global |
| | `@global.write` | An identifier writing a global |

### Rules that are easy to get wrong

**The reported line span runs from the name to the end of the body**, not
from the body's opening brace. A reader asked where `foo` starts points at
its signature, so a span beginning at the brace would be an artefact of the
query rather than a property of the code — and a fixture would have to
hand-count around it. If a language's query captures the name *after* the
body, the span is the body's alone rather than an inverted one.

**`@function.name` and `@function.body` must appear in the same match.** They
are paired per match, not per query. A pattern capturing only one of them
contributes no function, silently — which is why each pattern in a
`functions.scm` carries both.

**Nested named functions are functions.** A named callable declared inside
another one is reported in its own right, with its own name, line range,
ELOC, and complexity (HLR-067). Do not anchor patterns to the top of the
tree; a pattern that matches wherever the construct appears gets this for
free.

**`complexity.scm` runs against `@function.body`, not the root.** Captures
inside a nested lambda or closure therefore belong to the enclosing function
unless the query excludes them explicitly. If that is wrong for a language,
it is the query file that says so.

**A comment sharing a line with code is the query file's judgement.**
Whatever a language decides, it decides once, in one place.

## Adding a language

1. Build the grammar as `runtime/parsers/<name>.so`, exporting
   `tree_sitter_<name>`. The generated `parser.c` ships in the grammar's
   upstream release, so no code generation is needed at build time (HLR-040).
   Its ABI must lie within the range the linked `libtree-sitter` supports.
2. Create `runtime/queries/<name>/` with all six files above.
3. Add the extension mapping to `runtime/extensions.map` — one `<ext> <lang>`
   pair per line. This is data; it needs no rebuild (HLR-060).
4. Add a fixture under `test/fixtures/` with hand-counted values, and a Bats
   case asserting on them.

## The shipped modules

| Language | Grammar | Version | State |
| -------- | ------- | ------- | ----- |
| C | [`tree-sitter/tree-sitter-c`](https://github.com/tree-sitter/tree-sitter-c) | pinned in the Makefile as `GRAMMAR_C_VER` | `functions.scm` complete; the other five are contract stubs |

C++, Rust, Python, and Ada arrive in Phase 6 (HLR-011) — as data, with no C
change.
