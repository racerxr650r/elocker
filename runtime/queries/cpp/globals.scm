; globals.scm — global-state access for C++.
;
; Contract: capture each global declaration as @global.declaration, each read
; as @global.read, and each write as @global.write. See ../README.md.
;
; A class's static data member is global state wearing a scope, and belongs
; here beside a namespace-scope variable. It is captured by its declarator
; name; `elc` matches accesses by identifier, so a qualified use resolves
; through the field name rather than the qualification.

; --- declarations ----------------------------------------------------------

(translation_unit
  (declaration
    declarator: (identifier) @global.declaration))

(translation_unit
  (declaration
    declarator: (init_declarator
      declarator: (identifier) @global.declaration)))

; namespace-scope, which is where most C++ globals actually live
(namespace_definition
  body: (declaration_list
    (declaration
      declarator: (init_declarator
        declarator: (identifier) @global.declaration))))

(namespace_definition
  body: (declaration_list
    (declaration
      declarator: (identifier) @global.declaration)))

; a static data member's out-of-line definition
(translation_unit
  (declaration
    declarator: (init_declarator
      declarator: (qualified_identifier
        name: (identifier) @global.declaration))))

; --- writes ----------------------------------------------------------------

(assignment_expression
  left: (identifier) @global.write)

(assignment_expression
  left: (qualified_identifier
    name: (identifier) @global.write))

(update_expression
  argument: (identifier) @global.write)

; --- reads -----------------------------------------------------------------

(binary_expression
  (identifier) @global.read)

(call_expression
  arguments: (argument_list (identifier) @global.read))

(return_statement (identifier) @global.read)

(init_declarator
  value: (identifier) @global.read)

(assignment_expression
  right: (identifier) @global.read)

(qualified_identifier
  name: (identifier) @global.read)
