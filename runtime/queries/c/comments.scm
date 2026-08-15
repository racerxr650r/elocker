; comments.scm — comment spans for C.
;
; Contract: capture every comment as @comment. Spans may overlap and nest;
; coalescing them is the analyser's job, not this file's. See ../README.md.
;
; One pattern is the whole file, and that is the point. Everything that makes
; comment detection hard textually — `//` inside a block comment, `/*` inside
; a string, a quote inside a comment — is already decided by the parser by the
; time a query runs. A tool that matched text here would need every one of
; those cases as a special rule, and would still be wrong on the next one
; (HLR-013).

(comment) @comment
