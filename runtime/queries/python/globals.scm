; globals.scm — global-state access for Python.
;
; Contract: capture each global declaration as @global.declaration, each read
; as @global.read, and each write as @global.write. See ../README.md.
;
; A module-level assignment *is* the declaration — Python has no separate
; declaring form — so the declaration and the first write are the same node.
; Anchoring to (module) is what distinguishes a module-level binding from a
; local of the same shape, and is the only place scope is visible to a
; grammar.

; --- declarations ----------------------------------------------------------

(module
  (expression_statement
    (assignment
      left: (identifier) @global.declaration)))

; `global counter` inside a function names one explicitly, which is the
; clearest declaration of intent the language offers.
(global_statement (identifier) @global.declaration)

; --- writes ----------------------------------------------------------------

(assignment
  left: (identifier) @global.write)

(augmented_assignment
  left: (identifier) @global.write)

; --- reads -----------------------------------------------------------------

(binary_operator
  (identifier) @global.read)

(argument_list (identifier) @global.read)

(return_statement (identifier) @global.read)

(assignment
  right: (identifier) @global.read)

(comparison_operator
  (identifier) @global.read)
