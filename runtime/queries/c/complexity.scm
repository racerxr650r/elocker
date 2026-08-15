; complexity.scm — cyclomatic decision points for C.
;
; Contract: capture each decision point as @complexity.decision. Capture
; decision points only: the `1 +` base is added by the analyser, so capturing
; the function itself would double-count it. This query is executed against
; the @function.body node rather than the root. See ../README.md.
;
; Phase 4 fills this in.
