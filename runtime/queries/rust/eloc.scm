; eloc.scm — the statements that count toward ELOC in Rust.
;
; Contract: capture each counting statement as @eloc.statement, at the node
; rather than at its lines. See ../README.md.
;
; Rust is an expression language, so the line between "statement" and
; "expression" is not where C puts it. What counts here is what does work:
; a binding, an expression evaluated for effect, and each control-flow
; construct.

; --- data assignment and operations (HLR-044) ------------------------------
;
; A `let` binds, and binds to something — there is no uninitialised `let` to
; exclude, so unlike C's declarations this needs no qualification.
(let_declaration) @eloc.statement

; An expression evaluated as a statement: an assignment, an operation, a call.
(expression_statement) @eloc.statement

; A `static` is storage that exists at run time, which is what C's initialised
; global is. A `const` is not: it is inlined at every use and never exists as
; a variable, which puts it with the `#define` HLR-052 excludes rather than
; with the global HLR-044 counts. The two look alike and are not.
(static_item) @eloc.statement

; --- control flow (HLR-045) -----------------------------------------------
(if_expression) @eloc.statement
(match_expression) @eloc.statement
(match_arm) @eloc.statement
(for_expression) @eloc.statement
(while_expression) @eloc.statement
(loop_expression) @eloc.statement
(break_expression) @eloc.statement
(continue_expression) @eloc.statement

; --- returning (HLR-047) ---------------------------------------------------
;
; An explicit `return`.
(return_expression) @eloc.statement

; And the tail expression, which is how Rust usually returns. It carries no
; semicolon, so it is not an `expression_statement`, and it has no field name
; to ask for — the anchor is what identifies it: the last *named* child of a
; block.
;
; Without this, `fn double(x: i32) -> i32 { x * 2 }` reports zero effective
; lines, which is plainly wrong about a function that does arithmetic. The
; pattern also matches when a block's last child is already counted — a `let`,
; an `if` — and that costs nothing, because ELOC counts distinct lines and it
; is the same line.
(block (_) @eloc.statement .)

; --- deliberately absent ---------------------------------------------------
;
; A `block` is structure, as a brace is in C (HLR-050).
;
; `use`, `mod`, `struct`, `enum`, `impl`, and `trait` declare rather than do
; (HLR-051). The functions inside an `impl` are counted in their own right.
;
; `?` is not a statement; it is counted as a decision point instead, where it
; belongs.
