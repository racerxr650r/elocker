; globals.scm — global-state access for C.
;
; Contract: capture each global declaration as @global.declaration, the type it
; was declared with as @global.type, each read as @global.read, and each write
; as @global.write. See ../README.md.
;
; @global.type is the declaration's `type:` field and nothing else — the base
; type as written. The pointer and array shape is carried by the declarator
; the name sits in, and `elc` reads it from the capture that matched rather
; than by inspecting C: which shapes exist is this file's business.
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
    type: (_) @global.type
    declarator: (identifier) @global.declaration))

(translation_unit
  (declaration
    type: (_) @global.type
    declarator: (init_declarator
      declarator: (identifier) @global.declaration)))

(translation_unit
  (declaration
    type: (_) @global.type
    declarator: (array_declarator
      declarator: (identifier) @global.declaration)))

(translation_unit
  (declaration
    type: (_) @global.type
    declarator: (pointer_declarator
      declarator: (identifier) @global.declaration)))

; An *initialised* pointer: `uint8_t *p = 0;`. Missing until Phase 34, which
; means every initialised pointer global was invisible to the whole of the
; global-state analysis — no writers, no readers, no hidden channel, no scope
; reduction. Found while testing the memory-mapped exemption, because that is
; the declaration shape a peripheral register takes: the exemption appeared to
; work when in fact nothing had ever reached it.
(translation_unit
  (declaration
    type: (_) @global.type
    declarator: (init_declarator
      declarator: (pointer_declarator
        declarator: (identifier) @global.declaration))))

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

; --- the qualifier, and the shape that must keep it (HLR-230, HLR-231) -------
;
; A declaration carrying `volatile` captures its identifier a second time as
; @global.volatile. `elc` reads the qualifier from these captures and holds no
; keyword of its own, which is what lets the same analysis serve a language
; whose qualifier is spelled differently (HLR-009).
;
; **@global.mmio is the exemption and not an afterthought.** `volatile` is how
; a memory-mapped peripheral register is declared as well as how shared state
; is, and a register is confined to one thread of control by construction — so
; the "confined, therefore unnecessary" finding of HLR-231 would fire on every
; one of them and advise removing a qualifier whose absence is a miscompile.
; The shape that says "this is an address, not a variable" is an initialiser
; that casts to a pointer, and it is recognised here rather than in C, because
; it is a fact about how the language spells a register.

(translation_unit
  (declaration
    (type_qualifier) @_q
    declarator: (identifier) @global.volatile)
  (#eq? @_q "volatile"))

(translation_unit
  (declaration
    (type_qualifier) @_q
    declarator: (init_declarator
      declarator: (identifier) @global.volatile))
  (#eq? @_q "volatile"))

(translation_unit
  (declaration
    (type_qualifier) @_q
    declarator: (array_declarator
      declarator: (identifier) @global.volatile))
  (#eq? @_q "volatile"))

(translation_unit
  (declaration
    (type_qualifier) @_q
    declarator: (pointer_declarator
      declarator: (identifier) @global.volatile))
  (#eq? @_q "volatile"))

(translation_unit
  (declaration
    declarator: (pointer_declarator
      (type_qualifier) @_q
      declarator: (identifier) @global.volatile))
  (#eq? @_q "volatile"))

; --- memory-mapped: an initialiser that casts to an address ------------------

(translation_unit
  (declaration
    declarator: (init_declarator
      declarator: (identifier) @global.mmio
      value: (cast_expression))))

(translation_unit
  (declaration
    declarator: (init_declarator
      declarator: (pointer_declarator
        declarator: (identifier) @global.mmio)
      value: (cast_expression))))

; `volatile uint8_t *const PORT = ...` — the qualifier on the pointee, with an
; initialiser. Written out because the first version of this file omitted it,
; and the omission was invisible: the object was simply never seen as qualified,
; so the exemption below appeared to be working when nothing had reached it.
(translation_unit
  (declaration
    (type_qualifier) @_q
    declarator: (init_declarator
      declarator: (pointer_declarator
        declarator: (identifier) @global.volatile)))
  (#eq? @_q "volatile"))
