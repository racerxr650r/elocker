/* include/concurrency.h — the second thread of control (HLR-227 – HLR-231).
 *
 * Every other analysis in `elc` assumes one. On a bare-metal target there are
 * at least two — the application, and whatever the hardware calls without
 * asking — and a function both can be inside is re-entered.
 *
 * The hard part is the root set, because nothing in C says "this is an
 * interrupt": the attribute that installs a handler is the compiler's and the
 * section that holds it is the target's, and `elc` may use neither. So the
 * roots are inferred, narrowly, and where they cannot be inferred the analysis
 * says so rather than guessing (HLR-227).
 */
#ifndef ELC_CONCURRENCY_H
#define ELC_CONCURRENCY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elc.h"
#include "elfsyms.h"
#include "graph.h"
#include "thresholds.h"

/* How a root came to be one. Reported, because a name matching a convention is
 * a weaker claim than an address taken, and a reader deciding what to do about
 * a finding built on it needs to know which they have (HLR-227). */
typedef enum {
	ROOT_BY_ADDRESS = 0, /* its address is taken without being called */
	ROOT_BY_PATTERN      /* its name matched --isr-regex              */
} RootOrigin;

/* Why no root set could be identified, where none was. */
typedef enum {
	ROOTS_IDENTIFIED = 0,
	ROOTS_NO_EVIDENCE,   /* neither --elf nor --isr-regex was given    */
	ROOTS_NONE_FOUND     /* evidence was given and matched nothing     */
} RootState;

typedef struct {
	uint32_t   node;
	RootOrigin origin;
} AsyncRoot;

typedef struct {
	AsyncRoot *items;
	size_t     count;
	size_t     capacity;
	RootState  state;
} RootSet;

/* The asynchronous roots of HLR-227.
 *
 * Admits a node only where all four conditions hold: it is not a declared
 * entry point, its in-degree over call edges is zero, its name is defined in
 * `image`, and either its address is taken or its name matches
 * `opts->isr_regex`.
 *
 * `image` may be NULL, which is a run with no `--elf`. With no image and no
 * pattern the set is empty and its state is ROOTS_NO_EVIDENCE — an omission
 * the caller reports, never a fallback to every function of in-degree zero
 * (LLR-ASY-02).
 *
 * Returns 0, or -1 on allocation failure.
 */
int concurrency_roots(const Sdg *g, const ElcOptions *opts,
                      const SymbolSet *image, RootSet *out);

/* The functions both threads of control can be inside (HLR-228).
 *
 * The intersection of what the declared entry points reach and what the roots
 * reach, over call edges alone. `out` is the caller's array of one bool per
 * node. Returns 0, or -1 on allocation failure.
 */
int concurrency_reentrant(const Sdg *g, const uint32_t *entries,
                          size_t entry_count, const RootSet *async,
                          bool *out);

/* Each global object classified by the trees that touch it (HLR-230, HLR-231).
 *
 * Shared and unqualified is critical; qualified and confined to one tree is a
 * warning, except where the declaration has the shape of a memory-mapped
 * address. Returns 0, or -1 on allocation failure.
 */
int concurrency_qualifiers(const Sdg *g, const bool *main_tree,
                           const bool *async_tree, FindingList *out);

void rootset_free(RootSet *r);

#endif /* ELC_CONCURRENCY_H */
