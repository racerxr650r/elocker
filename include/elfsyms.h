/* elfsyms.h — the function set of a linked image.
 *
 * elfsyms.c reads the function symbols of an image the user named and answers
 * whether a given source function appears in it (doc/SDD.md §18). It is the
 * only module that opens a file that is not source, and it opens exactly the
 * one it was given: no toolchain is invoked and no image is searched for
 * (HLR-141).
 *
 * The filter itself is not applied here. `analyze.c` never records a function
 * this module does not report, which is what keeps the rest of the pipeline
 * unaware that a filter exists (HLR-144).
 */
#ifndef ELC_ELFSYMS_H
#define ELC_ELFSYMS_H

#include <stdbool.h>
#include <stddef.h>

/* The function set one image defines, resolved to source names.
 *
 * Sorted and de-duplicated on the resolved name, so membership is a binary
 * search and no property of symbol-table order can reach the output
 * (LLR-ELF-05, HLR-032).
 */
typedef struct {
	char  **names;      /* sorted, de-duplicated; owned              */
	size_t  count;
	size_t  capacity;
	/* Function symbols whose linkage name carried a mangling this build
	 * does not decode. Counted rather than guessed at, and reported with
	 * the run: a filter whose completeness is unstated cannot be acted on
	 * (HLR-143, LLR-ELF-08). */
	size_t  unresolved;
	char   *path;       /* the image, as the user named it; owned    */
} SymbolSet;

/* Read the named image and populate its function set.
 *
 * Returns 0. Returns non-zero after a diagnostic naming the path when the
 * image is absent, unreadable, not an object file, of a class this build does
 * not read, or carries no function symbols at all — every one of which the
 * caller turns into a fatal exit rather than a degraded run (HLR-146).
 *
 * The empty set is the case that most needs to be fatal: filtering every
 * function away would report a project containing none, which no reader could
 * tell from a correct result (LLR-ELF-07).
 */
int elfsyms_open(const char *path, SymbolSet *out);

/* Whether the image defines a function of this name.
 *
 * Both sides are reduced to the identifier the report presents before they are
 * compared, since reducing only one would make every qualified name a mismatch
 * (HLR-142, LLR-SYM-03).
 */
bool elfsyms_defines(const SymbolSet *set, const char *function);

/* How many of the image's function symbols carried an encoding this build does
 * not decode (HLR-143). */
size_t elfsyms_unresolved(const SymbolSet *set);

/* Release the set and every name it owns. Safe on NULL. */
void elfsyms_free(SymbolSet *set);

/* The source-level function name a linkage name encodes, newly allocated, or
 * NULL where the scheme is not one this build decodes.
 *
 * The reduction to a bare identifier lives here rather than at the call site,
 * so that one definition of "the name the report presents" serves the whole
 * comparison (HLR-142, LLR-SYM-01 – LLR-SYM-04).
 */
char *resolved_name(const char *linkage);

#endif /* ELC_ELFSYMS_H */
