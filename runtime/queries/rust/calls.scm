; calls.scm — call sites for Rust.
;
; Contract: capture the callee identifier of each call site as @call.name,
; and each function whose reference is taken without being called as
; @call.address_taken. See ../README.md.

; A free function.
(call_expression
  function: (identifier) @call.name)

; A path call: `module::function(..)` resolves on the final segment, which is
; the function's own name. The path prefix is where it was found, not what it
; is called, and `elc` resolves by name across the project.
(call_expression
  function: (scoped_identifier
    name: (identifier) @call.name))

; A method call. As in C++, which type it lands on needs resolution the
; grammar does not perform, so this claims the method name alone.
(call_expression
  function: (field_expression
    field: (field_identifier) @call.name))

(call_expression
  function: (generic_function
    function: (identifier) @call.name))

; --- reference taken -------------------------------------------------------
;
; Rust has no `&fn` idiom to key on: a function used as a value is written as
; a bare path, exactly like a variable. So these capture identifiers in value
; position and let resolution decide, as the C queries do.
(reference_expression
  value: (identifier) @call.address_taken)

(call_expression
  arguments: (arguments (identifier) @call.address_taken))

(let_declaration
  value: (identifier) @call.address_taken)

(assignment_expression
  right: (identifier) @call.address_taken)
