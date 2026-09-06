# `concurrency/` — the second thread of control, hand-counted

`handlers.c` is the shape a bare-metal target writes, reduced to the six
functions it takes to distinguish every case HLR-227 – HLR-234 turn on. It is
measured **with `--no-expand`**, which is the mode the requirement is about: on a
real embedded target the preprocessor cannot expand the file at all, because the
headers belong to a cross toolchain (`elc` reported all 44 files of `avrOS` as
"measured as written"). Expanded, `ISR(TIMER_vect)` becomes an ordinary
definition and the fixture verifies nothing.

An image *is* built from it, by the host compiler, because the linkage join of
HLR-233 can only be verified against a real symbol table.

## What each function is, and why

| Function | Reached from | Expected |
| --- | --- | --- |
| `TIMER_vect` | nothing | asynchronous root, origin **macro definition**, marked `I` |
| `bump` | `main` and `TIMER_vect` | **re-entrant**, marked `R`, attributed to `TIMER_vect` |
| `note_overflow` | `TIMER_vect` alone | *not* re-entrant — nothing interrupts it inside itself |
| `on_idle` | nothing; address passed to `dispatch` | asynchronous root, origin **address taken**, unmarked |
| `dispatch` | `main` | ordinary |
| `main` | the declared entry point | ordinary |

Six functions, so `Functions` is **6** and the parse must find `TIMER_vect`
among them: unexpanded, `ISR(TIMER_vect) { ... }` parses without error into a
definition whose *type* is `ISR` and whose *declarator* is parenthesised, which
no ordinary function pattern matches. Before HLR-233 the count was five, the
three statements of the handler's body counted as file-scope ELOC, and its two
calls were unresolved.

## The name the linker keeps

`ISR(vec)` here defines `vec##_isr`, so the image contains `TIMER_vect_isr` and
the source says `TIMER_vect`. The two share no characters in common beyond the
stem, which is the point: matching on the name discards the handler as a
function the image does not define, and `--elf` then reports the program's whole
interrupt half as absent. The join is by the line the image's debug information
places the definition on (line 44), so `TIMER_vect` is kept and records
`linkage="TIMER_vect_isr"`.

## The two roots are not one claim

`TIMER_vect` and `on_idle` are both roots of in-degree zero, and only one of
them is a handler. `on_idle` is dispatched by `main` through a function pointer,
on the application's own thread — which is why the address-taken origin cannot
carry a claim about a second thread of control, and why `pending`, which only
`on_idle` touches, must draw **no** confinement warning. `elc` reported exactly
this class of object as "reached only from an asynchronous root" on `avrOS`,
against a `volatile` the program depends on.

## The critical section

`bump` guards its update with `ATOMIC_BLOCK`, which unexpanded is a macro taking
a block: a nested function definition to the grammar, not a call. It is
therefore matched as `@sync.scoped` and recorded as balanced, never as an
acquisition — a scoped guard releases on every exit including `return`, so an
acquire/release pair would report a leak on the one construct that cannot leak.
`bump` is the only function with `critical-section="1"`.

## Counts

| Metric | Value | Reasoning |
| --- | ---: | --- |
| Functions | 6 | the five written out, plus the macro-written handler |
| Asynchronous roots | 2 | `TIMER_vect` (macro), `on_idle` (address) |
| Re-entrant | 1 | `bump` alone; `note_overflow` is reached from one thread |
| Unreachable | 0 | the handler is a reachability root, and it reaches `note_overflow` |
