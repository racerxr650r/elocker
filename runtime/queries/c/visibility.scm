; visibility.scm — whether a C function is visible outside its translation unit.
;
; Contract: each match supplies exactly one of @function.public or
; @function.private, captured on the same identifier `functions.scm` captures
; as @function.name. See ../README.md.
;
; **The earliest pattern that matches a function decides** (HLR-209), which is
; what lets the specific case be stated before the general one. `static` comes
; first; everything else is external by C's own default, and the catch-all at
; the end says so.
;
; What is reported is *linkage*, which is what C has. A `static` definition is
; private to its translation unit; anything else is a name the linker can see
; from every other one.
;
; The declarator nests once per level of return-type indirection and a query
; cannot match at any depth, so each depth is a pattern — the same shape, and
; for the same reason, as functions.scm.

; --- static: internal linkage ------------------------------------------------

(function_definition
  (storage_class_specifier) @_storage
  declarator: (function_declarator
                declarator: (identifier) @function.private)
  (#eq? @_storage "static"))

(function_definition
  (storage_class_specifier) @_storage
  declarator: (pointer_declarator
                declarator: (function_declarator
                              declarator: (identifier) @function.private))
  (#eq? @_storage "static"))

(function_definition
  (storage_class_specifier) @_storage
  declarator: (pointer_declarator
                declarator: (pointer_declarator
                              declarator: (function_declarator
                                            declarator: (identifier) @function.private)))
  (#eq? @_storage "static"))

; --- everything else: external linkage ---------------------------------------

(function_definition
  declarator: (function_declarator
                declarator: (identifier) @function.public))

(function_definition
  declarator: (pointer_declarator
                declarator: (function_declarator
                              declarator: (identifier) @function.public)))

(function_definition
  declarator: (pointer_declarator
                declarator: (pointer_declarator
                              declarator: (function_declarator
                                            declarator: (identifier) @function.public))))
