; deadcode.scm — statements that cannot execute, for C (HLR-137).
;
; Three captures, and the second is the one that keeps the analysis honest.
; See runtime/queries/README.md for the contract.

; --- terminators: control does not continue past these in the same block ----
;
; elc walks the named siblings following each of these and reports them, up to
; the first sibling captured as a re-entry point.

(return_statement)   @dead.terminator
(break_statement)    @dead.terminator
(continue_statement) @dead.terminator
(goto_statement)     @dead.terminator

; --- re-entry: reachable other than by falling into it ----------------------
;
; **A label following a `return` is live**, and it is a *sibling* of that
; return in this grammar. Without this pattern the walk above would report it
; as dead and invite deleting a `goto` target that runs — the false claim
; HLR-138 forbids outright.
;
; `case_statement` is here for a shape that is rarer but no different in kind:
; a statement written directly in a switch body before the first `case` is a
; sibling of the case that follows it. Verified against this grammar: a
; `case_statement` otherwise *contains* the statements of its arm, so a return
; in one arm cannot leak into the next — but that is a property of
; tree-sitter-c, not a rule, and capturing the label costs nothing.

(labeled_statement) @dead.reentry
(case_statement)    @dead.reentry

; --- branches a literal condition excludes ----------------------------------
;
; Only what the source *writes*. `x = 0; if (x)` is not captured and must not
; be: deciding it needs data flow, elc performs none, and a query that tried
; would be claiming knowledge it does not have (HLR-138, LLR-DED-03).
;
; The two regular expressions are deliberately narrow, in opposite directions.
; A condition is false only when it is written as a decimal zero with optional
; integer suffixes, and true only when it is written as a non-zero decimal —
; so `0x0`, `0.0`, and an octal `00` all fall through as undecided rather than
; being judged. Missing one costs a cleanup; misjudging one deletes live code.

; if (0) { ... }            — the consequence cannot run
(if_statement
  condition: (parenthesized_expression (number_literal) @_false)
  consequence: (_) @dead.branch
  (#match? @_false "^0[uUlL]*$"))

; if (1) { ... } else { ... }  — the alternative cannot run
(if_statement
  condition: (parenthesized_expression (number_literal) @_true)
  alternative: (_) @dead.branch
  (#match? @_true "^[1-9][0-9]*[uUlL]*$"))

; while (0) { ... }         — the body cannot run
;
; `do { ... } while (0);` is deliberately absent: its body runs exactly once,
; and it is one of the most common idioms in C.
(while_statement
  condition: (parenthesized_expression (number_literal) @_false)
  body: (_) @dead.branch
  (#match? @_false "^0[uUlL]*$"))

; for (;; 0) { ... }        — the body cannot run
(for_statement
  condition: (number_literal) @_false
  body: (_) @dead.branch
  (#match? @_false "^0[uUlL]*$"))
