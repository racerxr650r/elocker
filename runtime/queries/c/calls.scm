; calls.scm — call sites for C.
;
; Contract: capture the callee identifier of each call site as @call.name,
; and each function whose address is taken without being called as
; @call.address_taken. See ../README.md.

; A direct call. The callee is an identifier; a call through a pointer
; expression or a member is deliberately not captured here, because there is
; no name to resolve and inventing one would claim an edge the source does
; not support (HLR-077).
(call_expression
  function: (identifier) @call.name)

; --- address taken ---------------------------------------------------------
;
; These patterns capture *identifiers in value position*, which is broader
; than "function whose address is taken" — most of what they match is an
; ordinary variable. That is deliberate and safe: `elc` resolves each captured
; name against the project symbol table, and a name that is not a defined
; function resolves to nothing and is discarded. The query does not need to
; know which identifiers are functions, and it could not: that is semantic
; analysis, which a grammar does not perform.
;
; The direction of the error matters. A *missed* address-taken fact reports a
; live callback as dead code, which is a wrong answer; an extra one only
; shrinks the unreachable set, which costs a pruning opportunity and nothing
; else (SDD §8).

; &handler
(pointer_expression
  argument: (identifier) @call.address_taken)

; register(handler) — the identifier is passed, not called
(call_expression
  arguments: (argument_list (identifier) @call.address_taken))

; static void (*fp)(void) = handler;
(init_declarator
  value: (identifier) @call.address_taken)

; .handler = on_tick
(initializer_pair
  value: (identifier) @call.address_taken)

; { on_tick, on_tock }
(initializer_list
  (identifier) @call.address_taken)

; fp = handler;
(assignment_expression
  right: (identifier) @call.address_taken)

; return handler;
(return_statement (identifier) @call.address_taken)
