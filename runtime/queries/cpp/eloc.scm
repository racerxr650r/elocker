; eloc.scm — the statements that count toward ELOC in C++.
;
; Contract: capture each counting statement as @eloc.statement, at the node
; rather than at its lines. See ../README.md.
;
; C's set, plus the two things C++ adds: range-for and exception handling.

; --- data assignment, operations, and calls (HLR-044, HLR-046) ------------
(expression_statement) @eloc.statement

; A declaration counts only when it initialises. `int x = 5;` does work;
; `int x;` reserves a name (HLR-051).
(declaration (init_declarator)) @eloc.statement

; --- control flow (HLR-045) -----------------------------------------------
(if_statement) @eloc.statement
(else_clause) @eloc.statement
(while_statement) @eloc.statement
(do_statement) @eloc.statement
(for_statement) @eloc.statement
(for_range_loop) @eloc.statement
(switch_statement) @eloc.statement
(case_statement) @eloc.statement
(break_statement) @eloc.statement
(continue_statement) @eloc.statement
(goto_statement) @eloc.statement

; --- returning (HLR-047) ---------------------------------------------------
(return_statement) @eloc.statement

; --- exception handling (HLR-048) ------------------------------------------
;
; The category C could not exercise. Each of these does work a reader must
; follow, so each is a line.
(try_statement) @eloc.statement
(catch_clause) @eloc.statement
(throw_statement) @eloc.statement
