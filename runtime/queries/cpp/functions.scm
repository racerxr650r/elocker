; functions.scm — the reported function set for C++.
;
; Contract: each match supplies @function.name and @function.body.
; See ../README.md.
;
; C++ inherits C's problem — the declarator nests once per level of return-type
; indirection, and a query cannot match "at any depth" — and adds four more
; spellings of a name: a member function's `field_identifier`, an out-of-line
; definition's `qualified_identifier`, a destructor, and an operator. Each
; needs its own pattern, and `(_)` covers them where the shape above is what
; distinguishes the pattern.
;
; A lambda is deliberately absent: `lambda_expression` has no name to report,
; so its decision points belong to the nearest enclosing named function
; (HLR-018).

; int foo(void) { ... }        — and a constructor, which is an identifier
(function_definition
  declarator: (function_declarator declarator: (identifier) @function.name)
  body: (compound_statement) @function.body)

; int Widget::size() const { ... }
(function_definition
  declarator: (function_declarator declarator: (qualified_identifier) @function.name)
  body: (compound_statement) @function.body)

; a member function defined inside its class
(function_definition
  declarator: (function_declarator declarator: (field_identifier) @function.name)
  body: (compound_statement) @function.body)

; ~Widget() { ... }
(function_definition
  declarator: (function_declarator declarator: (destructor_name) @function.name)
  body: (compound_statement) @function.body)

; operator==(...) { ... }
(function_definition
  declarator: (function_declarator declarator: (operator_name) @function.name)
  body: (compound_statement) @function.body)

; template <> void combine<int, long>(...) { ... }
;
; An explicit specialisation names itself with its template arguments, so the
; reported name carries a comma and two angle brackets. It is the identifier
; HLR-064 and HLR-065 were written for, and the reason those requirements
; could only be tested against a contrived path until C++ arrived.
(function_definition
  declarator: (function_declarator declarator: (template_function) @function.name)
  body: (compound_statement) @function.body)

; template <> void Widget::apply<int, long>(...) { ... }
(function_definition
  declarator: (function_declarator
                declarator: (qualified_identifier
                              name: (template_function) @function.name))
  body: (compound_statement) @function.body)

; char *foo(void) { ... }   and   std::vector<int> &pick(...) { ... }
(function_definition
  declarator: (pointer_declarator
                declarator: (function_declarator declarator: (_) @function.name))
  body: (compound_statement) @function.body)

(function_definition
  declarator: (reference_declarator
                (function_declarator declarator: (_) @function.name))
  body: (compound_statement) @function.body)

; char **foo(void) { ... }
(function_definition
  declarator: (pointer_declarator
                declarator: (pointer_declarator
                              declarator: (function_declarator
                                            declarator: (_) @function.name)))
  body: (compound_statement) @function.body)
