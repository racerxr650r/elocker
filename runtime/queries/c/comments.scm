; comments.scm — comment spans for C.
;
; Contract: capture every comment as @comment. Spans may overlap and nest;
; coalescing them is the analyser's job, not this file's. See ../README.md.
;
; Phase 3 fills this in. It compiles and captures nothing until then, which
; is the defined state of an unimplemented query rather than a missing file:
; a language module is required to supply all six (HLR-121), and one that
; omits a file is treated as unusable (HLR-070).
