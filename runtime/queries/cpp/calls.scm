; calls.scm — call sites for C++.
;
; Contract: capture the callee identifier of each call site as @call.name,
; and each function whose reference is taken without being called as
; @call.address_taken. See ../README.md.

; A free function or a function template.
(call_expression
  function: (identifier) @call.name)

(call_expression
  function: (template_function
    name: (identifier) @call.name))

; A member call resolves on the *member* name. Which object it is called on
; needs type resolution, which a grammar does not perform — so this claims
; the name and nothing about the receiver. Where two classes define a method
; of the same name, the graph carries an edge to whichever is defined first,
; and `elc` reports the duplicate so the reader knows it happened.
(call_expression
  function: (field_expression
    field: (field_identifier) @call.name))

; --- reference taken -------------------------------------------------------
;
; Broader than "function reference", as in C, and safe for the same reason:
; a name that is not a defined function resolves to nothing (see c/calls.scm
; for the argument in full).
(pointer_expression
  argument: (identifier) @call.address_taken)

(call_expression
  arguments: (argument_list (identifier) @call.address_taken))

(init_declarator
  value: (identifier) @call.address_taken)

(assignment_expression
  right: (identifier) @call.address_taken)

(return_statement (identifier) @call.address_taken)
