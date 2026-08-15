; calls.scm — call sites for C.
;
; Contract: capture the callee identifier of each call site as @call.name,
; and each function whose address is taken without being called as
; @call.address_taken. See ../README.md.
;
; Phase 8 fills this in, where the System Dependence Graph is built.
