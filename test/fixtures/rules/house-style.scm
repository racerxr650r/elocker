; Two rules in one file, which is the point of this fixture.
;
; The capture name is half the rule's identity (HLR-109), so these are
; reported as house-style.allocation and house-style.jump.

; Calls to malloc. The predicate matters: without it this captures every call
; in the file, and elc evaluates it because tree-sitter's C library does not.
((call_expression function: (identifier) @allocation)
 (#eq? @allocation "malloc"))

; Every goto.
(goto_statement) @jump
