; globals.scm — global-state access for Python.
;
; Contract: capture each global declaration as @global.declaration, each read
; as @global.read, and each write as @global.write. See ../README.md.
;
; Phase 10 fills this in. Python's `global` and `nonlocal` statements make the
; intent explicit in a way C cannot, which should make this query simpler here
; than elsewhere.
