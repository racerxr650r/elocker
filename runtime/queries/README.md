# The language-module contract

This is the interface a language module codes against (HLR-121). It is a
**published contract**, not an internal arrangement: renaming a file or a
capture below is a breaking change for every grammar anyone has shipped, and
is treated as one.

Adding a language requires no change to `elc` and no rebuild of it (HLR-010).
If a language addition seems to need a C change, the extensibility pillar is
broken and that is the thing to fix.

This has now been demonstrated twice. Phase 6 added four languages without a
line of change under `src/`. Phase 8 filled in `calls.scm` and `globals.scm`
for all five, and the C that consumes them names two capture prefixes and no
node type. Where a grammar cannot separate two constructs, the query file says
so and captures neither, rather than a special case in C deciding for it.

## What a language module is

```text
runtime/
├── extensions.map              # "<ext> <lang>" per line          (HLR-060)
├── parsers/<lang>.so           # exports tree_sitter_<lang>       (HLR-009)
└── queries/<lang>/
    ├── functions.scm  comments.scm  complexity.scm
    ├── eloc.scm       calls.scm     globals.scm
    ├── conditionals.scm        # optional                         (HLR-134)
    ├── deadcode.scm            # optional                         (HLR-139)
    └── rules/*.scm             # optional custom rules            (HLR-107)
```

All six query files are **required**. A module that omits one is reported as
unusable, excluded from the run, and does not stop it (HLR-070) — it is not
undefined behaviour, and it is not fatal.

Two further files are **optional**, and deliberately so: making either
required would invalidate every language module already shipped, which is the
thing this contract exists to prevent.

`conditionals.scm` gains the language conditional-region pruning. Omitting one
means the language has no conditional compilation, which is simply true of
most of them.

`deadcode.scm` gains the language dead-code detection. A module without it is
analysed for everything else, and `elc` reports that dead-code analysis was
*not performed* for that language — never that none was found (HLR-139). The
distinction matters: they are different claims, and a reader who cannot tell
them apart has been told nothing.

Every module shipped today supplies one, and the file stays optional all the
same, because a language can be unable to supply one honestly. Consider a
language that writes its false literal as an ordinary identifier —
indistinguishable from a name the program declared itself, and perhaps
case-insensitively spelled besides. Capturing it would mean asserting that the
identifier resolves to the language's own `False` without having resolved
anything, and the cost of being wrong is a claim that live code is dead.

The terminator half alone would be sound, but shipping half a query would
report the language as *analysed* while quietly finding no literal branches,
which is the confident-and-wrong outcome the whole design avoids. **Omit the
file.** Being reported unanalysed is the true statement, and the project has
shipped a module that took exactly that option.

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
| | `@call.address_taken` | An identifier used as a value, which may be a function |
| `globals.scm` | `@global.declaration` | A global's declaration |
| | `@global.read` | An identifier reading a global |
| | `@global.write` | An identifier writing a global |
| `conditionals.scm` | `@conditional.region` | The whole conditional construct. Required in every pattern; a pattern without one describes nothing `elc` can act on |
| | `@conditional.alternative` | The part of the region active when the condition does *not* hold — C's `#else` or `#elif`. Absent where the language has no such form |
| | `@conditional.true` | The query recognised the condition and it holds |
| | `@conditional.false` | The query recognised it and it does not |
| | `@conditional.symbol` | A symbol whose definedness decides the region, for `elc` to look up |
| | `@conditional.negated` | Present when the region is active while that symbol is *un*defined |
| `deadcode.scm` | `@dead.terminator` | A statement after which control does not continue in this block |
| | `@dead.reentry` | A construct that can be entered *without* falling into it |
| | `@dead.branch` | A branch a literal condition excludes |

## Predicates

A pattern may carry predicates, and `elc` evaluates them. That is worth stating
because it is not free: **tree-sitter's C library treats a predicate as data**
— it parses `(#eq? @c "0")` and hands the steps back, leaving the decision to
the caller. A tool that never asks accepts every match as though the predicate
were not written, which for a `deadcode.scm` would turn "`if (0)` is dead" into
"every `if` is dead".

Five filters are supported, and they are evaluated against the text a capture
spans:

| Predicate | Holds when |
| --------- | ---------- |
| `(#eq? @a "text")` | the capture's text equals the string |
| `(#eq? @a @b)` | two captures span identical text |
| `(#not-eq? @a "text")` | it does not |
| `(#any-of? @a "x" "y")` | it equals any of the strings |
| `(#match? @a "regex")` | it matches a POSIX **extended** regular expression |
| `(#not-match? @a "regex")` | it does not |

Two rules about what `elc` does with anything else, and they run in opposite
directions on purpose. A **directive** — any name ending in `!`, such as
`#set!` — attaches information rather than filtering, and is ignored. An
**unrecognised filter** — any other name ending in `?` — *rejects* the match.
The query author wrote a condition this build cannot apply, and accepting the
match would apply that condition's inverse; under-reporting is the safe
direction, and it is the direction every capture in this contract errs in.

`#match?` is POSIX ERE rather than the Rust regex dialect tree-sitter's own
tooling uses. The two agree on character classes, anchors, and repetition,
which is the whole of what these query files need; a pattern relying on
anything beyond that will not compile, and a pattern that does not compile
matches nothing.

### Conditional compilation, and what it deliberately cannot do

`elc` runs **no preprocessor** (HLR-135). There is no macro expansion, no
include resolution, and no arithmetic over macro values, because each needs a
toolchain whose presence and configuration `elc` cannot reproduce — and the
answer would then depend on the machine rather than on the source.

So a region is decided only when its condition is a **literal**, or tests the
**definedness** of symbols the user named with `-D`. A C `#if VERSION > 2` is
**undecidable, not false**: both branches stay active and the region is
counted as undecided (HLR-133).

That asymmetry is the safety argument, and it is worth understanding before
writing one of these files. Treating an unrecognised condition as false would
silently delete code and produce a report that is confidently wrong and looks
exactly like a correct one. Treating it as true over-counts, which is visible
in the undecided count printed beside the figures.

### Who decides what

The division is exact, and it is what keeps a C `#if` and a Rust `#[cfg]` one
mechanism.

**The query decides truth.** A condition it recognises is settled with
`@conditional.true` or `@conditional.false` — captured on the condition, not on
a span. That is where a language's own rules about what counts as a false
constant belong, and it is why `elc` never learns that `0` is false in C.

**`elc` decides bytes.** Given a verdict it works out what that excludes: the
alternative when the condition holds, everything up to the alternative when it
does not, and the whole region where there is none. A query that pointed at a
span would have to know that a `#if` with an `#else` keeps half of itself,
which is arithmetic rather than a fact about the language.

**`elc` decides definedness, and only `elc` can.** Where the answer depends on
a `-D`, the query captures `@conditional.symbol` and stops. It has no way to
know what the user supplied and must not guess.

### A symbol no `-D` mentions is undecidable, not undefined

`-D` can only assert that a symbol *is* defined; there is no `-U`. A symbol it
never mentions may still be defined by a header or by a command line `elc` will
never see, so the honest answer is that the region cannot be decided — both
branches stay and the undecided count rises.

That one rule is also why supplying no definitions at all changes nothing: with
an empty set every definedness test is undecidable. A constant condition is
different in kind and prunes regardless, because `#if 0` means the same thing
in every configuration.

An `#ifndef` guard is the case this is most visible on. It is undecidable in
every run, which is correct and is also why a header-heavy project reports a
large undecided count.

### Patterns are tried in the order written

Where several patterns match one region, **the earliest in the file wins**. So
a `conditionals.scm` puts its specific cases first and a catch-all last, and
the catch-all is what makes an unrecognised condition *undecided* rather than
invisible. A region no pattern matches is not counted at all, and the count is
the only thing telling a reader how complete the pruning was.

### Writing a `deadcode.scm`

Three captures, and the second is the one that keeps the analysis honest.

**`@dead.terminator`** marks a statement after which control does not continue
— a return, a break, a continue, an unconditional transfer. `elc` walks the
following siblings of what you capture and reports each as unreachable. You
capture *what ends control flow*; the walk is generic and knows nothing about
your language.

**`@dead.reentry`** marks a construct reachable other than by falling into it,
and the walk stops there. **Omitting it produces false claims of dead code.**
In C:

```c
return 0;
n++;          /* dead */
done:         /* NOT dead — a goto can land here */
    return 3;
```

A `labeled_statement` is a sibling of the `return`, so without a re-entry
pattern `elc` would report a live label as dead and invite its deletion. Check
your grammar rather than assuming: in `tree-sitter-c` a `case` label is a
*child* of the switch construct rather than a sibling of the statements before
it, so switch arms need no pattern there — but that is a property of that
grammar, not a rule, and another may flatten them.

**`@dead.branch`** marks the branch a literal condition excludes: the body of
`if (0)`, the alternative of `if (1)`, a loop body whose condition is
literally false. Use a predicate to test the literal's text, because what
reads as false is yours to decide — `0` in C, `false` in Rust, `False` in
Python:

```scheme
(if_statement
  condition: (parenthesized_expression (number_literal) @_c)
  consequence: (_) @dead.branch
  (#eq? @_c "0"))
```

**Capture only what the source states.** `x = 0; if (x)` must not be captured.
Deciding that needs data flow, `elc` performs none, and a query that tried
would be claiming knowledge it does not have. The asymmetry is the whole
design: missing dead code costs a cleanup, inventing it invites deleting code
that runs.

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

**Capture broadly for `@call.address_taken`, `@global.read` and
`@global.write`.** These three are the exception to "capture what you mean",
and the reason is that you *cannot* mean it from one file. Whether an
identifier names a function, and whether it names a global, are questions
about the whole project — a global declared in a header and written in three
translation units cannot be recognised from any one of them. So capture
identifiers in value position and let `elc` resolve them: a name that is not
a defined function is not a reachability root, and a name no file declares at
file scope is not global state. Both are discarded silently, because a
variable appearing where the pattern looks is the expected case and not a
problem.

The direction of the remaining error matters and is not symmetric. A
**missed** address-taken fact reports a live callback as dead code, which is
a wrong answer a user acts on. An **extra** one only shrinks the unreachable
set, which costs a pruning opportunity. Err toward capturing.

**`@call.name` is the one place over-capture is not free.** An extra call
edge inflates fan-out and call depth, and can close a dependency cycle that
does not exist in the program — and a false critical finding costs more than
a noisy metric. Where a language's grammar cannot separate a call from
something else, say so in the query file. A language that writes an array index
exactly like a call is the usual shape of this, and no query can tell the two
apart.

## Custom rules

`queries/<lang>/rules/*.scm` holds rules you write. They are ordinary
Tree-sitter queries, compiled against that language's grammar exactly as the
files above are, and matched during the same parse — so everything this
document says about captures and predicates applies to them unchanged.

Two things differ, and both are about what `elc` does with the result.

**A rule's identity is the file's basename plus the capture name.** A file
`house-style.scm` containing

```scheme
((call_expression function: (identifier) @allocation)
 (#eq? @allocation "malloc"))

(goto_statement) @jump
```

expresses two rules, reported as `house-style.allocation` and
`house-style.jump`. So a capture name is user-visible here in a way it is not
elsewhere: name it for the reader of the report, not for the query.

**`elc` reports what a rule matched and forms no opinion about it.** A match
carries no severity and cites no source, because there is nothing honest to put
in either — `elc` did not decide the rule was worth writing, you did. Matches
appear in their own section beside the findings and never among them
(HLR-109, HLR-111).

### Where a rule may come from

Two places, and no others. A rule in `queries/<lang>/rules/` is bound to that
language by the directory holding it. A rule named as `--rules <lang>:<path>`
is bound by the argument. **No rule file is ever discovered from the working
directory, the analysis target, or a dotfile** (HLR-110) — two people running
the same command on the same tree must get the same answer, and a rule picked
up from a checkout would make that false.

### What happens when a rule is broken

Provenance decides, not the failure:

| Where it came from | Unreadable or will not compile |
| ------------------ | ------------------------------ |
| `queries/<lang>/rules/` | diagnosed, excluded, the run continues |
| `--rules lang:path` | diagnosed, and the run stops before any file is analysed |

A rule you named is a mistake you can fix now, so `elc` stops and says so. A
rule sitting in the runtime location is a malformed component, handled like any
other: reported, skipped, survived (HLR-116).

A rule naming a language with no module is reported and skipped from either
place — what is missing is the module, not the rule.

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

| Language | Grammar | Pinned as | State |
| -------- | ------- | --------- | ----- |
| C | [`tree-sitter/tree-sitter-c`](https://github.com/tree-sitter/tree-sitter-c) | `GRAMMAR_C_VER` | four of six complete |
| C++ | [`tree-sitter/tree-sitter-cpp`](https://github.com/tree-sitter/tree-sitter-cpp) | `GRAMMAR_CPP_VER` | four of six complete |
| Rust | [`tree-sitter/tree-sitter-rust`](https://github.com/tree-sitter/tree-sitter-rust) | `GRAMMAR_RUST_VER` | four of six complete |
| Python | [`tree-sitter/tree-sitter-python`](https://github.com/tree-sitter/tree-sitter-python) | `GRAMMAR_PYTHON_VER` | four of six complete |

"Four of six complete" means `functions.scm`, `comments.scm`, `eloc.scm`, and
`complexity.scm` carry real patterns; `calls.scm` and `globals.scm` are
contract stubs until the phases that consume them.

## What adding four languages taught the contract

All four were added **without a single change under `src/`** — the
demonstration HLR-010 asks for. The contract held. Four things it is worth
knowing before writing a fifth:

**A capture name is a contract; a node type is not.** Every disagreement
between the languages lives in a `.scm` file. Rust has no `else` node, one
spells short-circuit operators as two anonymous tokens inside an ordinary
expression, Python's `elif` is a clause where Rust's is a nested `if` — and
none of that reached C.

**Anonymous callables need no pattern, only restraint.** A lambda, a closure,
and a Rust `|x| ...` are all absent from their `functions.scm` on purpose. Not
capturing them is what makes HLR-018 work: an unreported scope is transparent
to attribution, so what is inside it lands on the nearest reported function
around it. There is nothing to write.

**A pattern that needs a *specific* node is not the same as one that needs
any.** `(object_declaration (expression))` counts an initialised declaration;
`(object_declaration (_))` counts every declaration, because every one has a
name and a subtype as named children. The first version of that pattern was
the second one, in a language since withdrawn — the mistake outlives it.

**Anchors do work that fields cannot.** Rust's tail expression — the usual way
a Rust function returns — has no field name. `(block (_) @eloc.statement .)`
identifies it as the last named child of a block, and without it a function
whose body is one expression reports zero effective lines.
