; eloc.scm — the statements that count toward ELOC in Ada.
;
; Contract: capture each counting statement as @eloc.statement, at the node
; rather than at its lines. See ../README.md.

; --- data assignment and operations (HLR-044) ------------------------------
(assignment_statement) @eloc.statement

; An object declaration counts only when it initialises, as C's does. Ada
; spells the initialiser as an `expression` child of the declaration, so the
; pattern asks for one directly.
;
; `(_)` would not do: every object declaration has named children — its name
; and its subtype mark — so a pattern matching any child matches them all, and
; `B : Integer;` would count. Naming the node is what makes the distinction.
; A constrained declaration such as `C : String (1 .. 3);` holds its
; expressions inside the constraint rather than as direct children, and is
; correctly excluded.
(object_declaration (expression)) @eloc.statement

; --- calls (HLR-046) -------------------------------------------------------
(procedure_call_statement) @eloc.statement

; --- control flow (HLR-045) -----------------------------------------------
(if_statement) @eloc.statement
(elsif_statement_item) @eloc.statement
(case_statement) @eloc.statement
(case_statement_alternative) @eloc.statement
(loop_statement) @eloc.statement
(exit_statement) @eloc.statement
(goto_statement) @eloc.statement
(block_statement) @eloc.statement

; --- returning (HLR-047) ---------------------------------------------------
(simple_return_statement) @eloc.statement
(extended_return_statement) @eloc.statement

; --- exception handling (HLR-048) ------------------------------------------
(exception_handler) @eloc.statement
(raise_statement) @eloc.statement

; --- deliberately absent ---------------------------------------------------
;
; `null;` is Ada's explicit do-nothing statement — the counterpart of Python's
; `pass` and of C's empty block — and is excluded for the same reason
; (HLR-050).
;
; `with` and `use` clauses name dependencies, as C's `#include` does
; (HLR-052). A type declaration, a package specification, and a subprogram
; specification declare rather than do (HLR-051).
