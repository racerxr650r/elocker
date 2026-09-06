; sync.scm — the calls that acquire and release a critical section (HLR-229).
;
; Contract: capture the call expression that acquires as @sync.acquire and the
; one that releases as @sync.release. See ../README.md.
;
; **Which names these are is a project's fact and not C's**, which is why this
; file exists at all rather than a list in the binary. The names below are the
; ones the C library and the common bare-metal idioms use; a project whose
; primitives are named differently supplies its own runtime and this file with
; it, exactly as it would for any other language-specific decision (HLR-009,
; HLR-107).
;
; A scoped guard — one construct that acquires on entry and releases on every
; exit — is not a call, and listing its name among the calls below matched
; nothing at all. C has no such construct of its own, but the idiom is written
; as a *macro* taking a block, and unexpanded that has a shape of its own:
; @sync.scoped below matches it, and the C carries it as balanced rather than as
; an acquisition, which is what it is (HLR-234).

; --- acquiring ---------------------------------------------------------------

(call_expression
  function: (identifier) @_f
  (#any-of? @_f
     "cli"                    ; AVR: disable interrupts
     "taskENTER_CRITICAL"
     "portENTER_CRITICAL"
     "vPortEnterCritical"
     "pthread_mutex_lock"
     "pthread_mutex_trylock"
     "pthread_spin_lock"
     "mtx_lock"
     "osMutexAcquire"
     "xSemaphoreTake"
     "irq_disable"
     "enter_critical_section")) @sync.acquire

; --- releasing ---------------------------------------------------------------

(call_expression
  function: (identifier) @_f
  (#any-of? @_f
     "sei"                    ; AVR: enable interrupts
     "taskEXIT_CRITICAL"
     "portEXIT_CRITICAL"
     "vPortExitCritical"
     "pthread_mutex_unlock"
     "pthread_spin_unlock"
     "mtx_unlock"
     "osMutexRelease"
     "xSemaphoreGive"
     "irq_enable"
     "exit_critical_section")) @sync.release

; --- the scoped guard --------------------------------------------------------
;
; ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { ... }
;
; Unexpanded, a macro taking a block parses as a GNU nested function: the macro
; name as the return type, its argument as a parenthesized declarator, and the
; guarded region as the body. Anchored to a `compound_statement` because that
; parent is what says the shape is *inside* a function — the same shape at file
; scope is a macro-written definition, which functions.scm matches and this must
; not.
;
; **Balanced by construction, and never an acquisition.** The construct releases
; on every exit from the region, `return` included, which is the whole reason to
; use one. Recording it as an acquire/release pair would put the release after
; the return on that path and report a leak on the one idiom that cannot leak.
;
; `NONATOMIC_BLOCK` is deliberately absent from the list below: it is the
; inverse construct, opening a window *inside* a guarded region, and naming it
; here would report the one place interrupts are enabled as the place they are
; disabled.
(compound_statement
  (function_definition
    type: (type_identifier) @_guard
    declarator: (parenthesized_declarator)
    body: (compound_statement)) @sync.scoped
  (#any-of? @_guard
     "ATOMIC_BLOCK"           ; AVR: <util/atomic.h>
     "ATOMIC_SECTION"
     "CRITICAL_SECTION"))
