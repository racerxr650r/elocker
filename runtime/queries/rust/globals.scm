; globals.scm — global-state access for Rust.
;
; Contract: capture each global declaration as @global.declaration, each read
; as @global.read, and each write as @global.write. See ../README.md.
;
; Rust's mutable global state is both rarer and more visibly marked than C's:
; a `static mut` requires `unsafe` to touch at all. Both `static` and `const`
; items are captured as declarations — a `const` is global state that happens
; to be immutable, and the reads of it are still coupling through a shared
; name, which is what the graph's global edges represent.

; --- declarations ----------------------------------------------------------

(static_item
  name: (identifier) @global.declaration)

(const_item
  name: (identifier) @global.declaration)

; --- writes ----------------------------------------------------------------

(assignment_expression
  left: (identifier) @global.write)

(compound_assignment_expr
  left: (identifier) @global.write)

(unary_expression
  (identifier) @global.write)

; --- reads -----------------------------------------------------------------

(binary_expression
  (identifier) @global.read)

(call_expression
  arguments: (arguments (identifier) @global.read))

(return_expression (identifier) @global.read)

(let_declaration
  value: (identifier) @global.read)

(assignment_expression
  right: (identifier) @global.read)
