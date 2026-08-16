; calls.scm — call sites for Python.
;
; Contract: capture the callee identifier of each call site as @call.name,
; and each function whose reference is taken without being called as
; @call.address_taken. See ../README.md.

; A plain call.
(call
  function: (identifier) @call.name)

; An attribute call resolves on the attribute name: `obj.method()` claims
; `method`. What `obj` is has no static answer in Python, so the receiver is
; not part of the claim.
(call
  function: (attribute
    attribute: (identifier) @call.name))

; --- reference taken -------------------------------------------------------
;
; Python passes functions around constantly and marks it in no way at all —
; a callback is a bare name. These capture identifiers in value position and
; leave resolution to decide, which is the same bargain the other languages
; strike and matters more here than anywhere.
(argument_list (identifier) @call.address_taken)

(assignment
  right: (identifier) @call.address_taken)

(return_statement (identifier) @call.address_taken)

(decorator (identifier) @call.address_taken)

(list (identifier) @call.address_taken)

(tuple (identifier) @call.address_taken)

(dictionary
  (pair value: (identifier) @call.address_taken))
