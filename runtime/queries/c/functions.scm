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

; ISR(TCB0_INT_vect) { ... } — a definition written through a function-shaped
; macro, which `elc` does not expand (HLR-135).
;
; Unexpanded, the macro name parses as the return type and the argument it is
; given as a parenthesized declarator, so none of the patterns above matches and
; the definition is not a definition at all: its body's statements fall outside
; every function, and the calls it makes have no caller. On a bare-metal target
; that silently deletes the *whole interrupt half of the program* — which is the
; one half HLR-227 exists to find (HLR-212).
;
; The name reported is the one the source writes, which is the vector's name and
; not the linkage name the macro renames it to; the image supplies that, and
; `elc` joins the two by where the definition begins (HLR-193).
;
; **Anchored to the translation unit, and that anchor is the whole of what makes
; this safe.** The identical shape occurs inside a function body, where it is a
; scoped-guard macro — `ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { ... }` — parsed as a
; GNU nested function. Unanchored, this pattern would mint a function named for
; the guard's argument at every critical section in the project. That shape is
; matched deliberately, and as a critical section rather than as a definition,
; by sync.scm.
;
; @function.wrapper is the macro's own name, and it is what says the definition
; arrived this way rather than being written out (HLR-233).
(translation_unit
  (function_definition
    type: (type_identifier) @function.wrapper
    declarator: (parenthesized_declarator (identifier) @function.name)
    body: (compound_statement) @function.body))
