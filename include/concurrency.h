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
	ROOT_BY_PATTERN,     /* its name matched --isr-regex              */
	/* A function-shaped macro wrote the definition, and nothing calls it
	 * (HLR-233). The strongest of the three where it applies, and the only
	 * one that names a *handler* rather than merely something entered from
	 * outside: a macro that writes a definition exists to attach that body
	 * to a table the source never names, which is what installing a handler
	 * is. An address taken is equally the shape of an ordinary callback the
	 * application itself dispatches, on its own thread. */
	ROOT_BY_WRAPPER
} RootOrigin;

/* Whether a root's evidence names it a handler rather than only an entry.
 *
 * The distinction the qualifier analysis is stratified on (HLR-231) and the
 * mark the function table carries (HLR-233): a root admitted by the macro shape
 * or by an interrupt-naming pattern is an asynchronous handler, and one
 * admitted by an address alone is a callback whose thread of control `elc`
 * cannot place — it may be dispatched from the application's own loop.
 */
static inline bool root_is_handler(RootOrigin origin)
{
	return origin != ROOT_BY_ADDRESS;
}

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
 * node.
 *
 * `via` is optional and, where given, receives for each marked function the
 * root that reaches it — preferring a handler's over a callback's, so a reader
 * meets the strongest evidence there is for the mark rather than whichever root
 * the walk happened to reach it from first (HLR-233). Entries for unmarked
 * functions are left at UINT32_MAX.
 *
 * Returns 0, or -1 on allocation failure.
 */
int concurrency_reentrant(const Sdg *g, const uint32_t *entries,
                          size_t entry_count, const RootSet *async,
                          bool *out, uint32_t *via);

/* Each global object classified by the trees that touch it (HLR-230, HLR-231).
 *
 * Three trees, not two, and the third is what keeps the warnings honest. A
 * *handler* tree is a second thread of control; a *callback* tree — what the
 * address-taken roots reach — may be dispatched from the application's own
 * loop, in which case it is the first thread under another name. An object the
 * callback tree touches is therefore neither provably shared nor provably
 * confined, and is reported as neither (HLR-231).
 *
 * Shared between the application and a handler, unqualified, is critical;
 * qualified and confined to one tree is a warning, except where the declaration
 * has the shape of a memory-mapped address. Returns 0, or -1 on allocation
 * failure.
 */
int concurrency_qualifiers(Sdg *g, const bool *main_tree,
                           const bool *handler_tree, const bool *callback_tree,
                           FindingList *out);

/* The critical sections of every re-entrant function (HLR-229).
 *
 * The path question was answered during the parse and rides on the node; this
 * decides which answers are reportable. A function whose control flow could
 * not be built is reported as *not analysed* rather than as safe.
 */
int concurrency_sections(const Sdg *g, const bool *reentrant,
                         FindingList *out);

void rootset_free(RootSet *r);

#endif /* ELC_CONCURRENCY_H */
