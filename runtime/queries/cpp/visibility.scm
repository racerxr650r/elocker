; visibility.scm — whether a C++ function is visible outside its
; translation unit.
;
; Contract: each match supplies exactly one of @function.public or
; @function.private, on the same node functions.scm captures as
; @function.name. See ../README.md.
;
; **The earliest pattern decides** (HLR-209): `static` is stated before the
; catch-all, because C++ shares C's default of external linkage.
;
; **This reports linkage and not access control.** `private:` in a class is a
; different axis — a private method of an exported class still has external
; linkage, and the linker still sees it. Reporting the access specifier here
; would answer neither question: not "can other code call this" (access says
; yes for a friend, no otherwise, and the linker disagrees with both) and not
; "is this name in the image's interface". Linkage is the one the rest of this
; tool is built on, and the one `--elf` filtering and DWARF placement already
; use.
;
; An anonymous namespace gives internal linkage to everything inside it and is
; the modern spelling of file-static, so it is matched the same way. The
; nesting is explicit because a query cannot match at any depth.

; --- static: internal linkage ------------------------------------------------

(function_definition
  (storage_class_specifier) @_storage
  declarator: (function_declarator declarator: (_) @function.private)
  (#eq? @_storage "static"))

(function_definition
  (storage_class_specifier) @_storage
  declarator: (pointer_declarator
                declarator: (function_declarator declarator: (_) @function.private))
  (#eq? @_storage "static"))

; --- a namespace: named exposes, anonymous does not --------------------------
;
; A query cannot match the *absence* of a name, so the named case is stated
; first and claims its functions as public; the pattern below it matches every
; namespace, and reaches only the ones the first did not — which are exactly
; the anonymous ones. This is the earliest-pattern rule doing the work a
; negation would (HLR-209).

(namespace_definition
  name: (namespace_identifier)
  body: (declaration_list
          (function_definition
            declarator: (function_declarator
                          declarator: (_) @function.public))))

(namespace_definition
  body: (declaration_list
          (function_definition
            declarator: (function_declarator
                          declarator: (_) @function.private))))

; --- everything else: external linkage ---------------------------------------

(function_definition
  declarator: (function_declarator declarator: (_) @function.public))

(function_definition
  declarator: (pointer_declarator
                declarator: (function_declarator declarator: (_) @function.public)))

(function_definition
  declarator: (reference_declarator
                (function_declarator declarator: (_) @function.public)))
