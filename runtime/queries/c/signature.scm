; signature.scm — what a C function costs to replace with a mock (HLR-221).
;
; Contract: each match supplies @function.name, captured on the same identifier
; `functions.scm` captures as @function.name, together with exactly one of
;
;   @return.void  @return.primitive  @return.aggregate
;   @param.primitive  @param.aggregate
;
; and the score is the sum of what those captures weigh. See ../README.md.
;
; **The kind of a type is decided here and never in C.** `elc` maps a capture
; name to a weight and knows nothing else about the type — no list of
; primitive spellings is compiled into the binary. That is Principle 2 applied
; to a new measurement, and it is not a formality: a list of built-in names
; would be wrong for the first project that types its own integers, which on
; the bare-metal targets this measurement exists for is every project.
; `avr_reg_t` is a (type_identifier) and is charged as a primitive because the
; grammar says what it is, not because `elc` has heard of it.
;
; **Returns are matched through `function_definition` and parameters through
; `function_declarator`.** A definition's declarator nests once per level of
; return-type indirection and a query cannot match at any depth, so the return
; patterns are written once per depth exactly as functions.scm is. Parameters
; need no such repetition: `function_declarator` is the same node whatever the
; return type wraps it in, so one pattern per parameter kind covers every
; depth. It also matches a prototype's parameter list, which is harmless — a
; prototype's identifier starts at an offset no defined function's name
; starts at, so nothing is ever attributed to it.

; --- return: void ------------------------------------------------------------
; Charged nothing: a mock returning void has no return state to simulate.

(function_definition
  type: (primitive_type) @return.void
  declarator: (function_declarator
                declarator: (identifier) @function.name)
  (#eq? @return.void "void"))

; --- return: primitive -------------------------------------------------------
; Anything the grammar calls a primitive and is not `void`, anything it calls
; a sized specifier (`unsigned long`), and every typedef name — the last being
; how a project's own scalar types arrive.

(function_definition
  type: (primitive_type) @return.primitive
  declarator: (function_declarator
                declarator: (identifier) @function.name)
  (#not-eq? @return.primitive "void"))

(function_definition
  type: (sized_type_specifier) @return.primitive
  declarator: (function_declarator
                declarator: (identifier) @function.name))

(function_definition
  type: (type_identifier) @return.primitive
  declarator: (function_declarator
                declarator: (identifier) @function.name))

; --- return: aggregate -------------------------------------------------------
; A struct, union or enum returned by value needs storage decided in the mock;
; so does any pointer return, at every depth of indirection.

(function_definition
  type: (struct_specifier) @return.aggregate
  declarator: (function_declarator
                declarator: (identifier) @function.name))

(function_definition
  type: (union_specifier) @return.aggregate
  declarator: (function_declarator
                declarator: (identifier) @function.name))

(function_definition
  type: (enum_specifier) @return.aggregate
  declarator: (function_declarator
                declarator: (identifier) @function.name))

(function_definition
  declarator: (pointer_declarator
                declarator: (function_declarator
                              declarator: (identifier) @function.name))) @return.aggregate

(function_definition
  declarator: (pointer_declarator
                declarator: (pointer_declarator
                              declarator: (function_declarator
                                            declarator: (identifier) @function.name)))) @return.aggregate

; --- parameters: primitive ---------------------------------------------------
; A parameter carrying a value, whether or not it is named. `(void)` is a
; parameter list with no parameters in it and is excluded by the predicate
; rather than by a shape, since the grammar spells it as an ordinary one.

(function_declarator
  declarator: (identifier) @function.name
  parameters: (parameter_list
                (parameter_declaration
                  type: (primitive_type) @param.primitive
                  declarator: (identifier))))

(function_declarator
  declarator: (identifier) @function.name
  parameters: (parameter_list
                (parameter_declaration
                  type: (primitive_type) @param.primitive
                  !declarator)
                (#not-eq? @param.primitive "void")))

(function_declarator
  declarator: (identifier) @function.name
  parameters: (parameter_list
                (parameter_declaration
                  type: (sized_type_specifier) @param.primitive)))

(function_declarator
  declarator: (identifier) @function.name
  parameters: (parameter_list
                (parameter_declaration
                  type: (type_identifier) @param.primitive
                  declarator: (identifier))))

(function_declarator
  declarator: (identifier) @function.name
  parameters: (parameter_list
                (parameter_declaration
                  type: (type_identifier) @param.primitive
                  !declarator)))

; --- parameters: aggregate ---------------------------------------------------
; A pointer or an array parameter, charged once whatever its indirection: the
; score taxes the kind of a type and does not count its tokens. A struct
; passed by value is charged here too — the mock must construct one.

(function_declarator
  declarator: (identifier) @function.name
  parameters: (parameter_list
                (parameter_declaration
                  declarator: (pointer_declarator)) @param.aggregate))

(function_declarator
  declarator: (identifier) @function.name
  parameters: (parameter_list
                (parameter_declaration
                  declarator: (array_declarator)) @param.aggregate))

(function_declarator
  declarator: (identifier) @function.name
  parameters: (parameter_list
                (parameter_declaration
                  type: (struct_specifier)
                  declarator: (identifier)) @param.aggregate))

(function_declarator
  declarator: (identifier) @function.name
  parameters: (parameter_list
                (parameter_declaration
                  type: (union_specifier)
                  declarator: (identifier)) @param.aggregate))
