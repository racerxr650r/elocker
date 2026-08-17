; conditionals.scm — conditionally compiled regions, for C (HLR-131 – HLR-134).
;
; Optional. A language without one has no conditional compilation, which is
; the truth for a language that has none. See runtime/queries/README.md for
; the contract, and read its "what it deliberately cannot do" section before
; changing anything here.
;
; **elc runs no preprocessor.** These patterns describe the *shape* of a
; region — which node introduces it, which part is the alternative, and what
; its condition tests. They never decide whether a symbol is defined; only the
; `-D` set can answer that, and only elc holds it.
;
; **Patterns are tried in the order written**, and the first that matches a
; region decides it. So every specific case is written before the catch-all at
; the bottom, and the catch-all is what makes an unrecognised condition
; *undecided* rather than invisible.

; --- constant conditions: decidable without any definition -----------------
;
; The regular expressions are narrow, and deliberately so — the same argument
; deadcode.scm makes. A condition is false only when written as a decimal zero
; with optional integer suffixes, and true only when written as a non-zero
; decimal, so `0x0` and an octal `00` fall through to the catch-all as
; undecided rather than being judged. Missing one costs a pruning opportunity;
; misjudging one deletes live code.
;
; The verdict is a capture on the condition, not a span: this file says whether
; the condition holds, and elc works out which bytes that excludes. A query
; that pointed at a span would have to know that a `#if` with an `#else` keeps
; half of itself, which is elc's arithmetic and not a fact about C.

; #if 0 … [#else …] #endif
((preproc_if
   condition: (number_literal) @conditional.false
   alternative: (_)? @conditional.alternative) @conditional.region
 (#match? @conditional.false "^0[uUlL]*$"))

; #if 1 … [#else …] #endif
((preproc_if
   condition: (number_literal) @conditional.true
   alternative: (_)? @conditional.alternative) @conditional.region
 (#match? @conditional.true "^[1-9][0-9]*[uUlL]*$"))

; --- definedness: elc decides, this file only says what is being tested ----

; #ifdef NAME … [#else …] #endif
(preproc_ifdef
  "#ifdef"
  name: (identifier) @conditional.symbol
  alternative: (_)? @conditional.alternative) @conditional.region

; #ifndef NAME … [#else …] #endif
;
; The directive token is captured, and its presence is the whole of what
; @conditional.negated means: the region is active while the symbol is *un*
; defined. Requiring the token in each pattern is what keeps the two from both
; matching one construct.
(preproc_ifdef
  "#ifndef" @conditional.negated
  name: (identifier) @conditional.symbol
  alternative: (_)? @conditional.alternative) @conditional.region

; #if defined(NAME) … [#else …] #endif
(preproc_if
  condition: (preproc_defined (identifier) @conditional.symbol)
  alternative: (_)? @conditional.alternative) @conditional.region

; #if !defined(NAME) … [#else …] #endif
(preproc_if
  condition: (unary_expression
               operator: "!" @conditional.negated
               argument: (preproc_defined (identifier) @conditional.symbol))
  alternative: (_)? @conditional.alternative) @conditional.region

; #elif — decided on its own terms, as the tail of a chain the pattern above
; has already excluded or kept.
(preproc_elif
  condition: (preproc_defined (identifier) @conditional.symbol)
  alternative: (_)? @conditional.alternative) @conditional.region

; --- everything else: undecided, and counted as such ------------------------
;
; `#if VERSION > 2` needs macro values elc does not have. Both branches stay
; and the region is reported undecided (HLR-133). Without these patterns such
; a region would be silently absent from that count, and the count is the only
; thing telling a reader how complete the pruning was.

(preproc_if) @conditional.region
(preproc_ifdef) @conditional.region
(preproc_elif) @conditional.region
