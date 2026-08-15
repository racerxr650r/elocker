; calls.scm — call sites for Python.
;
; Contract: capture the callee identifier of each call site as @call.name,
; and each function whose reference is taken without being called as
; @call.address_taken. See ../README.md.
;
; Phase 8 fills this in, where the System Dependence Graph is built.
