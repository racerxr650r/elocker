; eloc.scm — the statements that count toward ELOC in Python.
;
; Contract: capture each counting statement as @eloc.statement, at the node
; rather than at its lines. See ../README.md.

; --- data assignment, operations, and calls (HLR-044, HLR-046) ------------
;
; Python has no separate assignment statement: an assignment, an augmented
; assignment, and a bare call are all expression statements.
(expression_statement) @eloc.statement

; --- control flow (HLR-045) -----------------------------------------------
(if_statement) @eloc.statement
(elif_clause) @eloc.statement
(else_clause) @eloc.statement
(for_statement) @eloc.statement
(while_statement) @eloc.statement
(with_statement) @eloc.statement
(match_statement) @eloc.statement
(case_clause) @eloc.statement
(break_statement) @eloc.statement
(continue_statement) @eloc.statement
(delete_statement) @eloc.statement
(assert_statement) @eloc.statement

; --- returning (HLR-047) ---------------------------------------------------
(return_statement) @eloc.statement

; --- exception handling (HLR-048) ------------------------------------------
(try_statement) @eloc.statement
(except_clause) @eloc.statement
(finally_clause) @eloc.statement
(raise_statement) @eloc.statement

; --- deliberately absent ---------------------------------------------------
;
; `pass` is Python's way of writing an empty block. It is the language's
; substitute for the brace C would put there, and HLR-050 excludes a line
; holding nothing but structure.
;
; `import`, `global`, and `nonlocal` bind names. They do work at run time, but
; what a reader sees is a declaration of what this module depends on or which
; scope a name lives in — the same category as the `#include` HLR-052
; excludes. Counting them would make a module's ELOC rise with its import
; list.
;
; A `class` statement is a definition, not an operation; the methods inside it
; are counted in their own right.
