; complexity.scm — cyclomatic decision points for Rust.
;
; Contract: capture each decision point as @complexity.decision. Capture
; decision points only — the `1 +` base is added by the analyser.
; See ../README.md.

(if_expression) @complexity.decision
(for_expression) @complexity.decision
(while_expression) @complexity.decision
(loop_expression) @complexity.decision

; Each arm of a `match` is a branch, exactly as each `case` is in C. The
; `match` itself is not: it is the fork, not one of the paths.
(match_arm) @complexity.decision

; `&&` and `||` short-circuit, so each is a second place a condition can be
; decided.
(binary_expression operator: "&&") @complexity.decision
(binary_expression operator: "||") @complexity.decision

; `?` returns early when its operand is an error. That is a conditional exit
; written as one character, and a function threading a dozen of them has a
; dozen paths out of it — which is precisely what this metric is for.
(try_expression) @complexity.decision

; --- deliberately absent ---------------------------------------------------
;
; `else` adds no path the `if` did not already count. A `match` with a single
; arm is complexity 1 plus that arm, which is right: one way in, one way out.
