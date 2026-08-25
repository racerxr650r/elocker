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

#include "dwarfline.h"

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
	/* The finer granularity, read from the same open as the symbols and
	 * empty where the build wrote no debug information (HLR-153).
	 *
	 * Here rather than in a structure of its own because it comes from the
	 * same file and must come from the same *open*: the image is read once
	 * and nothing beside it, which is a property HLR-141 states and an
	 * instrumented test observes. A second module opening the image again
	 * would break it while every unit test still passed.
	 */
	LineCoverage lines;
	/* Which source file the image's debug information places each function
	 * in, from the same open and for the reason the line coverage is: the
	 * image is read once (HLR-141). Empty where the build wrote no debug
	 * information, which is what makes an ambiguous name fatal rather than
	 * silently resolved (HLR-193). */
	OriginMap    origins;
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

/* Whether the image defines a function of this name **written in this file**.
 *
 * The question the `--elf` filter actually has to answer. Where the image's
 * debug information places the name in a source file, the placement governs
 * and a definition in any other file is not the one the image kept. Where it
 * does not — no debug information, or a name it attributes to two files — this
 * falls back to the name alone, which is what `elfsyms_defines` answers and is
 * the best that can be said without the debug information (HLR-193).
 *
 * A NULL `file` asks the name-only question deliberately, for a caller that
 * has no source file in hand.
 */
bool elfsyms_defines_in(const SymbolSet *set, const char *function,
                        const char *file);

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
