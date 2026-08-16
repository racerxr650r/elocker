; comments.scm — comment spans for Python.
;
; Contract: capture every comment as @comment. See ../README.md.
;
; A docstring is *not* a comment: it is a string expression, and Python keeps
; it at run time as `__doc__`. Treating it as documentary would be defensible
; and is not what the grammar says, so it is left as the statement it is —
; and a module whose body is one docstring reports one line of ELOC, which is
; honest about what the interpreter does with it.

(comment) @comment
