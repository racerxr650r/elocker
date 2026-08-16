; complexity.scm — cyclomatic decision points for Python.
;
; Contract: capture each decision point as @complexity.decision. Capture
; decision points only — the `1 +` base is added by the analyser.
; See ../README.md.

(if_statement) @complexity.decision
(elif_clause) @complexity.decision
(for_statement) @complexity.decision
(while_statement) @complexity.decision
(case_clause) @complexity.decision
(conditional_expression) @complexity.decision

; `and` and `or` short-circuit, so each is a place execution can take a
; different path.
(boolean_operator) @complexity.decision

; A comprehension's `for` iterates and its `if` filters; both are branches
; written as an expression.
(for_in_clause) @complexity.decision
(if_clause) @complexity.decision

; Each handler is a path the body can take (HLR-048).
(except_clause) @complexity.decision

; --- deliberately absent ---------------------------------------------------
;
; `else` — on an `if`, a `for`, or a `try` — adds no path the construct did
; not already count. `finally` runs on every path and so adds none either.
; `raise` transfers control without choosing, as `goto` does in C.
