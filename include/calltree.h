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
 * may travel along one. Fan-in obeys the rule as strictly as fan-out does,
 * and has more riding on it: the Henry-Kafura value squares the product of
 * the two, so an in-degree taken over the whole SDG would inflate that figure
 * by the square of the error (HLR-156, HLR-157).
 */
#ifndef ELC_CALLTREE_H
#define ELC_CALLTREE_H

#include <stddef.h>
#include <stdint.h>

#include "elc.h"
#include "graph.h"
#include "report.h"

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
	/* Length weighed by the traffic through the function:
	 *
	 *     HK = ELOC * (Fan-In * Fan-Out)^2
	 *
	 * Sixty-four bits because the squared term grows far faster than any
	 * other figure elc reports, and a total that silently wrapped would
	 * be a wrong number wearing the authority of a right one (HLR-157,
	 * HLR-158). Zero where either degree is zero, which is a value and
	 * not an absence: an entry point and a leaf both score nothing
	 * whatever their length (HLR-159).
	 */
	uint64_t       *henry_kafura; /* per node id; owned (HLR-157)      */
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
