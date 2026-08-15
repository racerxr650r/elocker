; eloc.scm — the statements that count toward Effective Lines of Code in C.
;
; Contract: capture each counting statement as @eloc.statement. Capture the
; statement *node*, not its lines: the analyser counts each capture once, at
; its start line, so a statement spread over four lines counts the same as
; one written on a single line (HLR-053). See ../README.md.
;
; What is absent from this file matters as much as what is in it. Blank lines,
; lone braces, bare declarations, and preprocessor directives are excluded by
; not being captured (HLR-049 – HLR-052) rather than by a rule somewhere in C
; that strips them afterwards. If a construct should not count, the fix is to
; not capture it here.

; --- data assignment and operations (HLR-044) ------------------------------
;
; An expression used as a statement: an assignment, an operation, a call.
(expression_statement) @eloc.statement

; A declaration counts only when it initialises. `int x = 5;` does work;
; `int x;` reserves a name and does nothing, and is excluded (HLR-051). The
; distinction is structural — an initialised declarator is a different node —
; so it needs no inspection of the text.
(declaration (init_declarator)) @eloc.statement

; --- control flow (HLR-045) ------------------------------------------------
;
; Each of these is captured at the construct, not at its body: the body's own
; statements are captured in their own right, and the enclosing statement
; contributes only the line its keyword sits on.
(if_statement) @eloc.statement
(else_clause) @eloc.statement
(while_statement) @eloc.statement
(do_statement) @eloc.statement
(for_statement) @eloc.statement
(switch_statement) @eloc.statement
(case_statement) @eloc.statement
(break_statement) @eloc.statement
(continue_statement) @eloc.statement
(goto_statement) @eloc.statement

; --- returning (HLR-047) ---------------------------------------------------
(return_statement) @eloc.statement

; --- exception handling (HLR-048) ------------------------------------------
;
; C has none. C++ adds try, catch, and throw in its own query file; the
; category is listed here so that the absence is a statement rather than an
; oversight.
