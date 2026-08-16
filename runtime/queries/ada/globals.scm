; globals.scm — global-state access for Ada.
;
; Contract: capture each global declaration as @global.declaration, each read
; as @global.read, and each write as @global.write. See ../README.md.
;
; A package body's declarative part is where Ada keeps what other languages
; call a global: an object declared there is visible to every subprogram in
; the package and to nothing outside it. That is state shared between
; subprograms, which is what the graph's global edges represent, so it is
; captured here even though Ada would call it package-level rather than
; global.

; --- declarations ----------------------------------------------------------

; The declarative part of a package body. `non_empty_declarative_part` is the
; grammar's name for it — there is no `declarative_part` node, which is the
; kind of detail that belongs in a query file and nowhere else.
(package_body
  (non_empty_declarative_part
    (object_declaration
      name: (identifier) @global.declaration)))

; --- writes ----------------------------------------------------------------

(assignment_statement
  variable_name: (identifier) @global.write)

(assignment_statement
  variable_name: (selected_component
    selector_name: (identifier) @global.write))

; --- reads -----------------------------------------------------------------

; An identifier in value position. The grammar wraps every expression operand
; in a `term` whose `name` field holds the identifier — so one pattern covers
; the right-hand side of an assignment, a return, and an actual parameter
; alike, which is fewer patterns than the other four languages need and not a
; coincidence: Ada's expression grammar is more uniform than theirs.
(term
  name: (identifier) @global.read)
