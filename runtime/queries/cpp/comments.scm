; comments.scm — comment spans for C++.
;
; Contract: capture every comment as @comment. See ../README.md.
;
; As in C, block comments do not nest: the first `*/` closes one. Everything
; that makes comment detection hard textually is already decided by the parser
; by the time a query runs (HLR-013).

(comment) @comment
