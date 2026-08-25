/* symname.h — reducing a name to the identifier the report presents.
 *
 * A name reaches `elc` in more than one spelling. The Itanium ABI demangles
 * to `ns::C::f(int) const`, DWARF records a template as `f<int>`, and the
 * source declares plain `f`. The report presents the identifier alone
 * (HLR-014), so every comparison between two of those spellings has to reduce
 * both to it first — reducing one side only makes every qualified or
 * templated name a mismatch.
 *
 * **This lives in its own module because two others need it and neither may
 * depend on the other.** `elfsyms.c` matches source names against image
 * symbols; `dwarfline.c` matches them against the subprogram names in the
 * debug information. `elfsyms.h` already includes `dwarfline.h`, so the
 * reduction cannot live in `elfsyms.c` without a dependency cycle — and a
 * second copy of it in `dwarfline.c` would be the defect Phase 26 exists to
 * remove, reintroduced one file along (HLR-200).
 *
 * Nothing here allocates beyond the returned string, reads a file, or
 * consults anything but its argument.
 */
#ifndef ELC_SYMNAME_H
#define ELC_SYMNAME_H

/* Reduce a name — demangled, read from debug information, or written in
 * source — to the identifier the report presents.
 *
 * Strips a parameter list, a qualification (`ns::C::`), a trailing template
 * argument list, a return type a template's demangling carries in front, and
 * Rust's legacy disambiguating hash. An `operator` token is stepped over
 * whole, so `operator<`, `operator>>` and `operator()` survive intact.
 *
 * Returns a newly allocated string the caller frees, or NULL on allocation
 * failure or where nothing would be left to compare.
 */
char *symname_reduce(const char *name);

#endif /* ELC_SYMNAME_H */
