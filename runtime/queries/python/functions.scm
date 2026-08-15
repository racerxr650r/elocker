; functions.scm — the reported function set for Python.
;
; Contract: each match supplies @function.name and @function.body.
; See ../README.md.
;
; One pattern is the whole language. `function_definition` carries `name` and
; `body` as fields, and it appears wherever a `def` is legal — at module
; scope, inside a class, and inside another function — so nesting needs no
; second pattern (HLR-067). A decorated definition wraps this node rather than
; replacing it, so decorators need none either.
;
; A `lambda` is deliberately absent: it has no name to report, and its
; decision points therefore belong to the nearest enclosing named function
; (HLR-018).

(function_definition
  name: (identifier) @function.name
  body: (block) @function.body)
