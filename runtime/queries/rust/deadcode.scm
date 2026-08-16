; deadcode.scm — statements that cannot execute, for Rust (HLR-137).
;
; Rust is expression-oriented, and that changes what has to be captured. A
; `return` is a `return_expression` wrapped in an `expression_statement`, and
; the *statement* is what sits in the block beside its neighbours — so the
; statement is what elc must walk the siblings of. Capturing the inner
; expression would find no siblings at all and report nothing.
;
; See runtime/queries/README.md for the contract.

; --- terminators ------------------------------------------------------------
;
; The wrapping `expression_statement` is captured, not the expression: it is
; the node the following statements are siblings of.

(expression_statement (return_expression))   @dead.terminator
(expression_statement (break_expression))    @dead.terminator
(expression_statement (continue_expression)) @dead.terminator

; --- re-entry ---------------------------------------------------------------
;
; **Deliberately none, and verified rather than assumed.** Rust has no `goto`
; and no label a statement can be entered at: a loop label belongs to the loop
; expression rather than sitting between statements, and a `match` arm is a
; child of the match expression rather than a sibling of what precedes it.
;
; That is a property of tree-sitter-rust, not of the language's syntax in the
; abstract. A grammar that flattened match arms would need a pattern here.

; --- branches a literal condition excludes ----------------------------------
;
; `boolean_literal` covers both spellings, so a predicate distinguishes them.
; Only the literal counts: `let x = false; if x { }` is not captured, because
; deciding it needs data flow and elc performs none (HLR-138, LLR-DED-03).

(if_expression
  condition: (boolean_literal) @_false
  consequence: (_) @dead.branch
  (#eq? @_false "false"))

(if_expression
  condition: (boolean_literal) @_true
  alternative: (_) @dead.branch
  (#eq? @_true "true"))

; `while false { ... }` — the body cannot run.
;
; `loop { ... }` has no condition to be false, and is deliberately absent.
(while_expression
  condition: (boolean_literal) @_false
  body: (_) @dead.branch
  (#eq? @_false "false"))
