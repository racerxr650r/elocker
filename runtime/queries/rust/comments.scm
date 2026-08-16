; comments.scm — comment spans for Rust.
;
; Contract: capture every comment as @comment. Spans may overlap and nest.
; See ../README.md.
;
; Rust block comments **do** nest — `/* /* */ */` is one comment, where in C
; the first `*/` would close it. The grammar resolves that, so what reaches
; the analyser is one span rather than two overlapping ones. The coalescing in
; `merge_comment_spans` is what makes either shape safe.
;
; Doc comments are comments. `///` and `//!` are `line_comment` nodes carrying
; a doc marker, and `/**` likewise — documentation is documentary whichever
; syntax it uses.

(line_comment) @comment
(block_comment) @comment
