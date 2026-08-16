; globals.scm — global-state access for Ada.
;
; Contract: capture each global declaration as @global.declaration, each read
; as @global.read, and each write as @global.write. See ../README.md.
;
; Phase 10 fills this in. A package body's variables are the Ada shape of
; global state, and the package specification says which of them are visible.
