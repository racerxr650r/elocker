; calls.scm — call sites for Ada.
;
; Contract: capture the callee identifier of each call site as @call.name,
; and each function whose access is taken without being called as
; @call.address_taken. See ../README.md.
;
; ---------------------------------------------------------------------------
; READ THIS BEFORE TRUSTING AN ADA FAN-OUT NUMBER
;
; Ada writes a function call and an array index identically. `Foo (X)` is a
; call if Foo is a subprogram and an index if Foo is an array, and nothing in
; the syntax distinguishes them — telling them apart requires resolving Foo
; against its declaration, which is semantic analysis a grammar does not
; perform. This grammar *manages* the ambiguity with precedence rules and a
; `_name` / `_name_not_function_call` split rather than resolving it.
;
; So the patterns below capture array indexing as calls. `elc` does not
; correct for it, and deliberately: the correction would have to live in C
; and would encode Ada's semantics in the binary, which the extensibility
; pillar forbids outright (HLR-010). One language's ambiguity is not a reason
; to give the tool language knowledge.
;
; What the imprecision costs, by analysis:
;
;   Reachability   safe. A spurious edge only shrinks the unreachable set,
;                  the same direction the address-taken rule already errs in.
;                  A live subprogram is never called dead because of it.
;   Fan-out        inflated. An Ada subprogram that indexes three arrays
;                  looks like one that calls three subprograms.
;   Call depth     inflated, on top of already being a lower bound.
;   Cycles         the dangerous one. A spurious edge can close a cycle that
;                  is not in the program, and a false critical finding costs
;                  more than a noisy metric (SDD §8).
;
; This is pinned by the Ada case in the `graph/` fixture group, so it is a
; recorded decision rather than something a reader discovers.
; ---------------------------------------------------------------------------

; A procedure call statement is unambiguous: a statement position cannot hold
; an array index.
(procedure_call_statement
  name: (identifier) @call.name)

(procedure_call_statement
  name: (selected_component
    selector_name: (identifier) @call.name))

; A function call in an expression. This is the ambiguous one.
(function_call
  name: (identifier) @call.name)

(function_call
  name: (selected_component
    selector_name: (identifier) @call.name))

; --- access taken ----------------------------------------------------------
;
; `Foo'Access` is Ada's explicit form and is unambiguous, which makes it the
; one address-taken idiom among the five languages that says exactly what it
; means. The grammar flattens it: a `term` holding an identifier, a tick and
; an attribute designator, all under the same field name. Matching the shape
; rather than a dedicated node is what the tree actually offers.
(term
  name: (identifier) @call.address_taken
  name: (attribute_designator))
