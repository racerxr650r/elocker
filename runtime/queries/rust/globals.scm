; globals.scm — global-state access for Rust.
;
; Contract: capture each global declaration as @global.declaration, each read
; as @global.read, and each write as @global.write. See ../README.md.
;
; Phase 10 fills this in. Rust's `static mut` requires `unsafe` to touch, so
; the mutable global state this analysis looks for is both rarer and more
; visibly marked here than in C.
