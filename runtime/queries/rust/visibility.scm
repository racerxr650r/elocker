; visibility.scm — whether a Rust function is visible outside its module.
;
; Contract: each match supplies exactly one of @function.public or
; @function.private, on the same identifier functions.scm captures.
;
; **The earliest pattern decides** (HLR-209), and here the polarity is the
; reverse of C's: Rust's default is private and `pub` is the exception, so the
; exception is stated first and the catch-all names everything else private.
; That the two languages need opposite orderings and no other difference is
; why the rule is "earliest pattern" rather than "private wins".
;
; This is the language's own visibility rule rather than a convention, and
; `pub(crate)` and `pub(super)` carry the same modifier node — reported public
; because the function is exposed beyond the module that defines it, which is
; the question the column asks.

(function_item
  (visibility_modifier)
  name: (identifier) @function.public)

(function_item
  name: (identifier) @function.private)
