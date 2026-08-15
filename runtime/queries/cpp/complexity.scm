; complexity.scm — cyclomatic decision points for C++.
;
; Contract: capture each decision point as @complexity.decision. Capture
; decision points only — the `1 +` base is added by the analyser.
; See ../README.md.

(if_statement) @complexity.decision
(while_statement) @complexity.decision
(for_statement) @complexity.decision
(for_range_loop) @complexity.decision
(do_statement) @complexity.decision

; A `case` is a branch; `default:` is not — reaching it is what happens when
; no branch was taken. The `value:` field is present on one and absent on the
; other, which distinguishes them structurally.
(case_statement value: (_)) @complexity.decision

(conditional_expression) @complexity.decision
(binary_expression operator: "&&") @complexity.decision
(binary_expression operator: "||") @complexity.decision

; Each handler is a path out of the guarded block (HLR-048).
(catch_clause) @complexity.decision

; --- deliberately absent ---------------------------------------------------
;
; `else` adds no path the `if` did not already count. `goto` and `throw` move
; control without choosing — the choice, where there is one, is in the `if`
; that guards them.
