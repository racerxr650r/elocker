; functions.scm — the reported function set for Rust.
;
; Contract: each match supplies @function.name and @function.body.
; See ../README.md.
;
; `function_item` carries `name` and `body` as fields and appears wherever a
; `fn` is legal — at module scope, in an `impl` block, in a trait's default
; method, and inside another function — so one pattern covers nesting
; (HLR-067). A `function_signature_item` is a trait method *declaration* with
; no body; it supplies no @function.body and so contributes no function,
; which is what a declaration should do (HLR-051's reasoning, one level up).
;
; A closure is deliberately absent: `closure_expression` has no name to
; report, so its decision points belong to the nearest enclosing named
; function (HLR-018). That is the case Rust exercises and C cannot.

(function_item
  name: (identifier) @function.name
  body: (block) @function.body)
