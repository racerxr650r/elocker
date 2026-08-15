; functions.scm — the reported function set for C.
;
; Contract: each match supplies @function.name and @function.body.
; See ../README.md. Complete as of Phase 2.
;
; A C function definition's declarator nests once per level of return-type
; indirection, and a tree-sitter query cannot match "at any depth", so each
; depth is a pattern. Three covers everything a real codebase writes; a
; fourth level of indirection in a return type is not a thing that happens.
;
; A prototype is a `declaration`, not a `function_definition`, so it is
; excluded by construction rather than by a predicate. A GNU nested function
; is a `function_definition` inside a `compound_statement` and therefore
; matches these same patterns — which is what HLR-067 requires, and why no
; pattern here anchors to the translation unit.

; int foo(void) { ... }
(function_definition
  declarator: (function_declarator
                declarator: (identifier) @function.name)
  body: (compound_statement) @function.body)

; char *foo(void) { ... }
(function_definition
  declarator: (pointer_declarator
                declarator: (function_declarator
                              declarator: (identifier) @function.name))
  body: (compound_statement) @function.body)

; char **foo(void) { ... }
(function_definition
  declarator: (pointer_declarator
                declarator: (pointer_declarator
                              declarator: (function_declarator
                                            declarator: (identifier) @function.name)))
  body: (compound_statement) @function.body)
