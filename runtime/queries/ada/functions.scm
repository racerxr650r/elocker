; functions.scm — the reported function set for Ada.
;
; Contract: each match supplies @function.name and @function.body.
; See ../README.md.
;
; Ada is the language HLR-067 was written for. A subprogram's declarative part
; — everything between `is` and `begin` — may itself contain subprograms, and
; those are ordinary subprograms with their own names, bodies, and metrics.
; The patterns below are not anchored to the top of the tree, so a nested
; subprogram matches exactly as an outer one does.
;
; @function.body is the whole `subprogram_body` rather than its statement
; sequence. That is deliberate: the reported span then ends at `end Name;`
; where a reader expects it, and the nested subprograms in the declarative
; part fall inside their parent's range, which is what makes the innermost
; attribution of HLR-068 come out right.
;
; A separate specification — `procedure Foo (N : Integer);` in a package spec
; — is a declaration with no body, supplies no @function.body, and so
; contributes no function.

(subprogram_body
  (procedure_specification name: (_) @function.name)) @function.body

(subprogram_body
  (function_specification name: (_) @function.name)) @function.body

; An expression function is a body written as one expression:
;   function Double (X : Integer) return Integer is (X * 2);
(expression_function_declaration
  (function_specification name: (_) @function.name)) @function.body
