/* calltree.h — the function-level call-tree analyses.
 *
 * Fan-out, recursion, maximum call depth, and the deepest call stack
 * (doc/SDD.md §10). Everything here is a *measurement*: the bands that turn a
 * fan-out into a finding are `thresholds.c`'s work and arrive with Phase 12,
 * so nothing in this module knows what a large number means.
 *
 * Every analysis reads the call-edge view of the graph, not the whole SDG. A
 * global-state edge joins a function that writes an object to one that reads
 * it; that is coupling, not a call, and neither recursion nor a call chain
 * may travel along one. Fan-in obeys the rule as strictly as fan-out does:
 * an in-degree taken over the whole SDG would count a reader of a global as
 * a caller, and the report presents the two degrees side by side, where a
 * discrepancy between them is exactly what a reader is looking at (HLR-156).
 */
#ifndef ELC_CALLTREE_H
#define ELC_CALLTREE_H

#include <stddef.h>
#include <stdint.h>

#include "elc.h"
#include "graph.h"
#include "report.h"

/* The Adapted Maintainability Index of one function, normalised to 0-100
 * (HLR-191).
 *
 *     IF  = (Fan-In x Fan-Out)^2
 *     MI  = 171 - 5.2 ln(IF + 1) - 0.23 v(G) - 16.2 ln(ELOC)
 *     MI' = max(0, MI / 171 x 100)
 *
 * Coleman and Oman's index with the information flow through a function
 * substituted for its Halstead Volume: the two other terms are theirs
 * unchanged, and what the substitution buys is a figure that falls when a
 * function is entangled with its neighbours and not only when it is long or
 * branchy.
 *
 * **A pure function of four measurements, and the only definition of the
 * formula.** The report needs the value for its function table and
 * `thresholds.c` needs it to band; computing it twice would be two places it
 * could be computed differently, which is the failure the module comments
 * throughout this project keep naming. It is declared here because this is
 * the module that owns the two degrees the adaptation turns on.
 *
 * Three edge cases, all of them reachable:
 *
 *   * **A function with no effective lines** would put `ln(0)` in the third
 *     term. Its length is taken as 1, so the term vanishes: a function with
 *     nothing in it has nothing to maintain, and 100 is the honest answer
 *     rather than an infinity.
 *   * **A function at either end of the call graph** has an information flow
 *     of zero, and `ln(1)` is zero — so the first term vanishes and the
 *     figure rests on length and branching alone. That is the intended
 *     reading, not a gap: an entry point is not coupled by being an entry
 *     point.
 *   * **The clamp at zero** is what stops a monolith reporting a negative
 *     score. It is reached only by a function of some millions of effective
 *     lines, and exists so that the scale is a scale rather than an
 *     unbounded deficit.
 *
 * Returned rounded to the integer the report presents, so that the value a
 * reader sees is the value that was banded. A band read off a figure other
 * than the printed one is a band nobody can check against the table.
 */
uint32_t calltree_maintainability(uint32_t eloc, uint32_t complexity,
                                  uint32_t fan_in, uint32_t fan_out);

/* An ordered call chain, entry point first (HLR-088). */
typedef struct {
	uint32_t *nodes;   /* node identifiers into Sdg.nodes; owned */
	size_t    count;
} Chain;

/* One recursive cycle: a self-loop, or the members of a non-trivial strongly
 * connected component of the call graph (HLR-089). */
typedef struct {
	uint32_t *members; /* node identifiers, ascending; owned */
	size_t    count;
} RecursiveCycle;

typedef struct {
	uint32_t       *fan_out;      /* per node id; owned (HLR-085)      */
	/* The converse measurement, counted the same way and over the same
	 * view: the number of *distinct* functions that invoke this one. A
	 * global-state edge joins a function that writes an object to one
	 * that reads it, and writing a variable another function reads is not
	 * calling it, so none of them is counted here (HLR-156, LLR-CTR-07).
	 */
	uint32_t       *fan_in;       /* per node id; owned (HLR-156)      */
	size_t          node_count;

	RecursiveCycle *cycles;       /* owned (HLR-089)                   */
	size_t          cycle_count;
	size_t          cycle_capacity;

	DepthState      depth_state;
	uint32_t        depth;        /* functions in the deepest chain    */
	Chain           deepest;      /* the chain itself (HLR-088)        */

	/* Carried so that the depth is never presented without it: a chain
	 * that continues through a call the graph could not resolve is not
	 * followed, which makes the depth a lower bound (HLR-087). */
	size_t          unresolved_calls;
} TreeResults;

/* Produce every call-tree measurement the report requires.
 *
 * Returns 0 on success; non-zero only on allocation failure. Recursion is not
 * a failure, an unresolvable entry point is not a failure, and an absent
 * declaration is not a failure — each is a stated outcome (HLR-090, HLR-115).
 */
int calltree_analyse(const Sdg *g, const ElcOptions *opts, TreeResults *out);

/* The longest call chain from any node in `entries`, over the acyclic call
 * graph, as an ordered sequence rather than a number (LLR-LPD-01 – LPD-03).
 *
 * `entries` is an array of `count` node identifiers. The caller has already
 * established acyclicity: on a cyclic graph the longest path has no finite
 * answer, and this function would not terminate.
 */
int longest_path_dag(const Sdg *g, const uint32_t *entries, size_t count,
                     Chain *out);

/* Copy the call-tree measurements onto an assembled report.
 *
 * Set after assembly for the same reason the unresolved count is: the graph,
 * and therefore the analyses reading it, are built *from* the assembled model
 * (HLR-085, HLR-087 – HLR-090).
 */
int report_set_calltree(Report *report, const TreeResults *tree, const Sdg *g);

/* Release the per-node measurement tables, the recursive-cycle list, and the
 * chain. */
void tree_results_free(TreeResults *r);

#endif /* ELC_CALLTREE_H */
