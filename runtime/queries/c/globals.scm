; globals.scm — global-state access for C.
;
; Contract: capture each global declaration as @global.declaration, each read
; as @global.read, and each write as @global.write. See ../README.md.
;
; Phase 8 uses these for the graph's global-state edges (HLR-074); Phase 10
; uses the same captures for hidden-channel analysis. One set of facts, two
; consumers — which is the point of extracting them during the single parse.

; --- declarations ----------------------------------------------------------
;
; A declaration at file scope is a global. Anchoring to (translation_unit)
; is what distinguishes it from a local of the same shape: this is the one
; place where scope is expressible in the grammar, and it is where the
; distinction belongs.
(translation_unit
  (declaration
    declarator: (identifier) @global.declaration))

(translation_unit
  (declaration
    declarator: (init_declarator
      declarator: (identifier) @global.declaration)))

(translation_unit
  (declaration
    declarator: (array_declarator
      declarator: (identifier) @global.declaration)))

(translation_unit
  (declaration
    declarator: (pointer_declarator
      declarator: (identifier) @global.declaration)))

; --- writes ----------------------------------------------------------------
;
; Captured wherever they appear. Which identifiers are actually globals is
; settled by `elc` against the declarations above, so these patterns need not
; — and could not — decide it themselves.
(assignment_expression
  left: (identifier) @global.write)

(assignment_expression
  left: (subscript_expression
    argument: (identifier) @global.write))

(update_expression
  argument: (identifier) @global.write)

; --- reads -----------------------------------------------------------------
;
; An identifier in value position. The write patterns above capture the
; left-hand side, so a compound assignment such as `count += 1` is recorded
; as a write; `elc` records both when a name is captured both ways, which is
; what a read-modify-write is.
(binary_expression
  (identifier) @global.read)

(subscript_expression
  index: (identifier) @global.read)

(call_expression
  arguments: (argument_list (identifier) @global.read))

(return_statement (identifier) @global.read)

(init_declarator
  value: (identifier) @global.read)

(assignment_expression
  right: (identifier) @global.read)

(if_statement
  condition: (parenthesized_expression (identifier) @global.read))
