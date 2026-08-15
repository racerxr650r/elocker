; globals.scm — global-state access for C.
;
; Contract: capture the identifier of each global read as @global.read and
; each global write as @global.write, and each global declaration as
; @global.declaration. See ../README.md.
;
; Phase 10 fills this in, where global coupling and hidden channels are
; analysed.
