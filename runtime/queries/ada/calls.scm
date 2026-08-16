; calls.scm — call sites for Ada.
;
; Contract: capture the callee identifier of each call site as @call.name,
; and each function whose reference is taken without being called as
; @call.address_taken. See ../README.md.
;
; Phase 8 fills this in, where the System Dependence Graph is built.
;
; A caution recorded in doc/notes.md §2.2 applies here: Ada's `Foo (X)` is
; genuinely ambiguous between a function call and an array index, and the
; grammar manages that ambiguity with precedence rules rather than resolving
; it — resolution needs semantic analysis a grammar does not do. Call edges
; extracted here may therefore include array indexing. That is the safe
; direction, since extra edges only shrink the unreachable set, but the
; graph/ fixture group needs an Ada case pinning the behaviour.
