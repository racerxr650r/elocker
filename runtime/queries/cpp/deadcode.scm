; deadcode.scm — statements that cannot execute, for C++ (HLR-137).
;
; Close to the C module but not a copy of it, and the differences are the
; reason it is a separate file: tree-sitter-cpp wraps a condition in a
; `condition_clause`, and C++ has `throw` and the `true`/`false` keywords as
; nodes in their own right. See runtime/queries/README.md for the contract.

; --- terminators ------------------------------------------------------------

(return_statement)   @dead.terminator
(break_statement)    @dead.terminator
(continue_statement) @dead.terminator
(goto_statement)     @dead.terminator

; A throw leaves the block as unconditionally as a return does. What catches
; it is elsewhere; nothing after it in this block runs.
(throw_statement)    @dead.terminator

; --- re-entry ---------------------------------------------------------------
;
; A label following a `return` is live and is a *sibling* of it. Omitting this
; reports a `goto` target as dead and invites deleting code that runs
; (HLR-138).

(labeled_statement) @dead.reentry
(case_statement)    @dead.reentry

; --- branches a literal condition excludes ----------------------------------
;
; Two spellings of the same idea, because C++ has both. The keyword forms need
; no predicate: `true` and `false` are distinct node types in this grammar, so
; the source having written one is the whole test. The numeric forms are
; matched as narrowly as C's, and for the same reason.

; if (false) / if (0)
(if_statement
  condition: (condition_clause value: (false))
  consequence: (_) @dead.branch)

(if_statement
  condition: (condition_clause value: (number_literal) @_false)
  consequence: (_) @dead.branch
  (#match? @_false "^0[uUlL]*$"))

; if (true) ... else / if (1) ... else
(if_statement
  condition: (condition_clause value: (true))
  alternative: (_) @dead.branch)

(if_statement
  condition: (condition_clause value: (number_literal) @_true)
  alternative: (_) @dead.branch
  (#match? @_true "^[1-9][0-9]*[uUlL]*$"))

; while (false) / while (0)
(while_statement
  condition: (condition_clause value: (false))
  body: (_) @dead.branch)

(while_statement
  condition: (condition_clause value: (number_literal) @_false)
  body: (_) @dead.branch
  (#match? @_false "^0[uUlL]*$"))

; for (;false;) / for (;0;)
;
; `do { ... } while (false);` is deliberately absent: its body runs once.
(for_statement
  condition: (false)
  body: (_) @dead.branch)

(for_statement
  condition: (number_literal) @_false
  body: (_) @dead.branch
  (#match? @_false "^0[uUlL]*$"))
