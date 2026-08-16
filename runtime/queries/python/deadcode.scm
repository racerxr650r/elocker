; deadcode.scm — statements that cannot execute, for Python (HLR-137).
;
; The simplest of the five, and the reason is the grammar. Python writes its
; false literal as a keyword the parser gives a node type of its own, so no
; predicate is needed; and it has no `goto`, so nothing is a re-entry point.
; See runtime/queries/README.md for the contract.

; --- terminators ------------------------------------------------------------

(return_statement)   @dead.terminator
(break_statement)    @dead.terminator
(continue_statement) @dead.terminator
(raise_statement)    @dead.terminator

; --- re-entry ---------------------------------------------------------------
;
; **Deliberately none, and verified rather than assumed.** Python has no label
; and no fallthrough. Every construct that can be entered without falling into
; it — `except`, `else`, `finally`, a `case` of a `match` — is a *child* of the
; statement introducing it in this grammar, never a sibling of the statements
; preceding it. A `return` inside a `try` body therefore cannot reach its own
; `finally` through the sibling walk.
;
; That is a property of tree-sitter-python and not a rule. A grammar that
; flattened any of those would need a pattern here, and the absence of one
; would be a false claim of dead code rather than a missing feature.

; --- branches a literal condition excludes ----------------------------------
;
; `if False:` is dead because the source wrote `False`. `x = False` followed by
; `if x:` is not, and must not be: deciding it needs data flow, which elc does
; not perform (HLR-138, LLR-DED-03).
;
; The alternative of `if True:` covers both `else:` and every `elif`, since the
; grammar makes each an alternative of the same statement — and every one of
; them is unreachable once the first condition is literally true.

(if_statement
  condition: (false)
  consequence: (_) @dead.branch)

(if_statement
  condition: (true)
  alternative: (_) @dead.branch)

(while_statement
  condition: (false)
  body: (_) @dead.branch)
