; eloc.scm — the statements that count toward Effective Lines of Code in C.
;
; Contract: capture each counting statement as @eloc.statement. A statement
; spanning several physical lines is counted once at its start line, by the
; analyser — capture the statement node, not its lines. See ../README.md.
;
; Phase 3 fills this in; the nine categories are HLR-044 through HLR-052.
