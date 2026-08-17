; conditionals.scm — conditionally compiled regions, for Rust (HLR-134).
;
; Optional. See runtime/queries/README.md for the contract.
;
; Rust writes conditional compilation as an *attribute on an item* rather than
; as a region between two directives, and this file is the demonstration that
; the mechanism does not care. The captures mean the same things they mean for
; C's preprocessor; only the patterns differ, and no line of `elc` changed to
; support the difference (HLR-010, HLR-134).
;
; One consequence of the shape is worth knowing before writing a `-D` for
; Rust. An attribute has no `#else`, so a `#[cfg(X)]` item can only ever be
; *removed*, never swapped for another — and since a symbol no `-D` mentions is
; undecidable rather than undefined, `#[cfg(X)]` is pruned by nothing. It is
; `#[cfg(not(X))]` with `-DX` that prunes: the symbol is known, the negation
; makes the condition false, and the item goes.

; #[cfg(SYMBOL)] item
;
; `(#not-eq? … "not")` is load-bearing. Without it this pattern also matches
; `#[cfg(not(X))]`, capturing the word `not` as the symbol — and a `-Dnot`
; would then decide a condition it has nothing to do with.
((attribute_item
   (attribute
     (identifier) @_cfg
     (token_tree (identifier) @conditional.symbol)))
 . (_) @conditional.region
 (#eq? @_cfg "cfg")
 (#not-eq? @conditional.symbol "not"))

; #[cfg(not(SYMBOL))] item
(
  (attribute_item
    (attribute
      (identifier) @_cfg
      (token_tree
        (identifier) @conditional.negated
        (token_tree (identifier) @conditional.symbol))))
  . (_) @conditional.region
  (#eq? @_cfg "cfg")
  (#eq? @conditional.negated "not"))

; --- everything else: undecided, and counted as such ------------------------
;
; `#[cfg(feature = "x")]` and `#[cfg(all(a, b))]` are not decided here. Both
; branches — which for an attribute means the item itself — stay, and the
; region is reported undecided (HLR-133). Without this the region would be
; absent from that count, and the count is the only thing telling a reader how
; complete the pruning was.
(
  (attribute_item (attribute (identifier) @_cfg))
  . (_) @conditional.region
  (#eq? @_cfg "cfg"))
