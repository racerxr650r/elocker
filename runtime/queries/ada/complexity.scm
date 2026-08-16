; complexity.scm — cyclomatic decision points for Ada.
;
; Contract: capture each decision point as @complexity.decision. Capture
; decision points only — the `1 +` base is added by the analyser.
; See ../README.md.

(if_statement) @complexity.decision
(elsif_statement_item) @complexity.decision
(loop_statement) @complexity.decision

; Each alternative of a `case` is a branch. Ada requires the alternatives to
; cover the whole subtype, so `when others` is the same "everything else" path
; C's `default:` is — but Ada writes it as an alternative like any other, and
; the grammar does not distinguish it structurally. It is counted, which
; slightly overstates an exhaustive case against the C convention; the
; alternative would be matching on the text `others`, which is the textual
; approximation HLR-013 forbids.
(case_statement_alternative) @complexity.decision

; Each handler is a path out of the guarded sequence (HLR-048).
(exception_handler) @complexity.decision

; `and then` and `or else` short-circuit and are therefore decision points.
; Plain `and` and `or` do not short-circuit in Ada — both operands are always
; evaluated — so they add no path, and are not captured.
;
; The grammar gives the short-circuit forms no node of their own: they are two
; anonymous tokens inside an ordinary `expression`. Requiring both tokens is
; what separates `and then` from a plain `and`, and it is a structural test
; rather than a textual one — the tokens are nodes in the tree, not characters
; matched in a line.
(expression "and" "then") @complexity.decision
(expression "or" "else") @complexity.decision

; A conditional expression is an `if` written where a value is expected.
(if_expression) @complexity.decision
(elsif_expression_item) @complexity.decision
(case_expression_alternative) @complexity.decision

; --- deliberately absent ---------------------------------------------------
;
; `else` adds no path the `if` did not already count. `goto` and `raise` move
; control without choosing.
