; visibility.scm — whether a Python function is part of its module's interface.
;
; Contract: each match supplies exactly one of @function.public or
; @function.private, on the same identifier functions.scm captures.
;
; **This is a convention and not a rule the language enforces**, which is the
; one place this query differs in kind from C's or Rust's. PEP 8 states that a
; leading underscore marks a name as non-public, every Python reader treats it
; that way, and `import *` honours it — but nothing stops a caller reaching in.
; The column reports what the code says about itself, which for Python is this
; and nothing stronger.
;
; A dunder — `__init__`, `__repr__` — is an interface Python itself calls, so
; the leading-underscore rule must not swallow it. That pattern comes first,
; which is what "the earliest pattern decides" (HLR-209) is for.

(function_definition
  name: (identifier) @function.public
  (#match? @function.public "^__.*__$"))

(function_definition
  name: (identifier) @function.private
  (#match? @function.private "^_"))

(function_definition
  name: (identifier) @function.public)
