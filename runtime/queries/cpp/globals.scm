; globals.scm — global-state access for C++.
;
; Contract: capture each global declaration as @global.declaration, each read
; as @global.read, and each write as @global.write. See ../README.md.
;
; Phase 10 fills this in. A class's static data member is global state wearing
; a scope, and belongs here alongside a namespace-scope variable.
