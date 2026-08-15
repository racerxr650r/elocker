; complexity.scm — cyclomatic decision points for C.
;
; Contract: capture each decision point as @complexity.decision. Capture
; decision points *only* — the `1 +` base is added by the analyser, so
; capturing the function itself would make a straight-line function 2.
; See ../README.md.
;
; Complexity is one plus the number of places control can branch. What
; follows is McCabe's set for C, and two of the exclusions matter as much as
; the inclusions.

; --- statements that branch --------------------------------------------
(if_statement) @complexity.decision
(while_statement) @complexity.decision
(for_statement) @complexity.decision
(do_statement) @complexity.decision

; A `case` is a branch; `default:` is not. Reaching the default is what
; happens when no branch was taken, so counting it would charge a function
; for the path it already has. The `value:` field is present on a `case` and
; absent on a `default`, which distinguishes them structurally — no text is
; inspected.
(case_statement value: (_)) @complexity.decision

; --- expressions that branch -------------------------------------------
;
; The conditional operator is an `if` written as an expression.
(conditional_expression) @complexity.decision

; `&&` and `||` short-circuit, so each one is a place execution can take a
; different path. `a && b` has two outcomes reached two ways; without these
; a function of one long compound condition scores the same as a function
; with no condition at all.
(binary_expression operator: "&&") @complexity.decision
(binary_expression operator: "||") @complexity.decision

; --- deliberately absent ------------------------------------------------
;
; `else` is not a decision. The branch was already counted at the `if` that
; owns it; counting the alternative as well would charge every if-else twice.
; An `else if` still counts once, for the `if` inside it.
;
; `goto` is not a decision. It moves control without choosing — the choice,
; where there is one, is in the `if` that guards it.
;
; `catch` belongs here for a language that has it. C does not; C++ adds it in
; its own query file.
