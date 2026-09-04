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
; A call that both acquires and releases — a scoped guard — has no expression
; in C to be written as, so none is matched here. That is not an omission this
; file can fix: a language with such a construct would describe it in its own
; version of this query.

; --- acquiring ---------------------------------------------------------------

(call_expression
  function: (identifier) @_f
  (#any-of? @_f
     "cli"                    ; AVR: disable interrupts
     "ATOMIC_BLOCK"
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
