; comments.scm — comment spans for Ada.
;
; Contract: capture every comment as @comment. See ../README.md.
;
; Ada has one comment form, `--` to end of line, and no block comment at all.
; There is therefore nothing here that can nest and nothing that can be
; opened inside a string — which makes this the one shipped language where a
; textual approach would nearly work, and no reason to use one.

(comment) @comment
