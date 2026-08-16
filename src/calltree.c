/* calltree.c — the function-level call-tree analyses.
 *
 * Fan-out, recursion, maximum call depth, and the deepest call stack
 * (doc/SDD.md §10).
 *
 * The order of the steps is the design, and it is not arbitrary. Acyclicity
 * is established *before* depth is measured, because on a cyclic graph the
 * longest path has no finite answer and a naive traversal does not terminate.
 * Reporting a cycle in place of a number is not a fallback for a computation
 * that failed; it is the correct answer to the question asked, and it is why
 * MISRA C Rule 17.2 forbids recursion in the code this tool is aimed at.
 *
 * This module measures. What a fan-out of 16 *means* is `thresholds.c`'s
 * judgement in Phase 12, and nothing here carries a band.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>

#include "calltree.h"
#include "elc.h"
#include "graph.h"

/* ------------------------------------------------------------- utilities -- */

static int grow(void **items, size_t *capacity, size_t item_size)
{
	size_t next   = *capacity ? *capacity * 2 : 16;
	void  *bigger = realloc(*items, next * item_size);

	if (!bigger)
		return -1;

	*items    = bigger;
	*capacity = next;
	return 0;
}

static int by_node_id(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a;
	uint32_t y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* ------------------------------------------------------------- fan-out --
 *
 * The number of *distinct* subroutines a function invokes. The graph is
 * simple and its call edges already collapsed, so this is the out-degree over
 * call edges — the counting was done when the graph was built, and doing it
 * again here from call sites would be a second answer to one question
 * (HLR-085, LLR-CTR-01).
 */
static int compute_fan_out(const Sdg *g, TreeResults *out)
{
	out->fan_out = calloc(g->node_count ? g->node_count : 1,
	                      sizeof *out->fan_out);
	if (!out->fan_out)
		return -1;
	out->node_count = g->node_count;

	for (size_t i = 0; i < g->edge_count; i++)
		if (g->edges[i].kind == EDGE_CALL &&
		    g->edges[i].from < g->node_count)
			out->fan_out[g->edges[i].from]++;

	return 0;
}

/* ----------------------------------------------------------- recursion --
 *
 * Direct and mutual recursion are one question, not two: a function that
 * calls itself is a strongly connected component of one node with a self
 * loop, and a mutually recursive pair is a component of two. Decomposing the
 * call graph answers both at once, which is why the requirement names them
 * together (HLR-089, LLR-CTR-02).
 */
static int cycle_add(TreeResults *out, uint32_t *members, size_t count)
{
	if (out->cycle_count == out->cycle_capacity &&
	    grow((void **)&out->cycles, &out->cycle_capacity,
	         sizeof *out->cycles) != 0)
		return -1;

	qsort(members, count, sizeof *members, by_node_id);
	out->cycles[out->cycle_count].members = members;
	out->cycles[out->cycle_count].count   = count;
	out->cycle_count++;
	return 0;
}

/* Cycles are ordered by their lowest-numbered member, so the report lists
 * them in a defined order rather than the order the decomposition happened to
 * produce (HLR-033). */
static int by_lowest_member(const void *a, const void *b)
{
	const RecursiveCycle *x = a;
	const RecursiveCycle *y = b;

	if (x->count == 0 || y->count == 0)
		return x->count == y->count ? 0 : (x->count == 0 ? -1 : 1);
	return x->members[0] < y->members[0] ? -1
	                                     : x->members[0] > y->members[0];
}

static int detect_recursion(const Sdg *g, TreeResults *out)
{
	igraph_vector_int_t membership;
	igraph_integer_t    components = 0;
	int                 status     = -1;
	size_t             *sizes      = NULL;

	if (g->node_count == 0)
		return 0;

	if (igraph_vector_int_init(&membership, 0) != IGRAPH_SUCCESS)
		return -1;

	if (igraph_connected_components((const igraph_t *)g->call_graph,
	                                &membership, NULL, &components,
	                                IGRAPH_STRONG) != IGRAPH_SUCCESS)
		goto cleanup;

	sizes = calloc(components ? (size_t)components : 1, sizeof *sizes);
	if (!sizes)
		goto cleanup;

	for (size_t i = 0; i < g->node_count; i++)
		sizes[VECTOR(membership)[i]]++;

	/* A component of more than one node is mutual recursion. A component
	 * of exactly one is recursive only if the function calls itself —
	 * every other function is its own trivial component, and reporting
	 * those would report every program as recursive. */
	for (igraph_integer_t c = 0; c < components; c++) {
		size_t    n       = sizes[c];
		bool      self    = false;
		uint32_t *members = NULL;

		if (n == 1) {
			uint32_t only = 0;

			for (size_t i = 0; i < g->node_count; i++)
				if (VECTOR(membership)[i] == c) {
					only = (uint32_t)i;
					break;
				}
			for (size_t e = 0; e < g->edge_count; e++)
				if (g->edges[e].kind == EDGE_CALL &&
				    g->edges[e].from == only &&
				    g->edges[e].to == only) {
					self = true;
					break;
				}
			if (!self)
				continue;
		}

		members = calloc(n, sizeof *members);
		if (!members)
			goto cleanup;

		size_t at = 0;

		for (size_t i = 0; i < g->node_count && at < n; i++)
			if (VECTOR(membership)[i] == c)
				members[at++] = (uint32_t)i;

		if (cycle_add(out, members, n) != 0) {
			free(members);
			goto cleanup;
		}
	}

	if (out->cycle_count > 1)
		qsort(out->cycles, out->cycle_count, sizeof *out->cycles,
		      by_lowest_member);

	status = 0;

cleanup:
	free(sizes);
	igraph_vector_int_destroy(&membership);
	return status;
}

/* --------------------------------------------------------- longest path --
 *
 * Memoised traversal in reverse topological order. Each node's depth is one
 * plus the deepest of its callees, computed once; the predecessor achieving
 * that maximum is retained so the chain can be walked back out (LLR-LPD-01,
 * LLR-LPD-02).
 *
 * The caller has established acyclicity. That is not a politeness — this
 * function would not terminate otherwise, and the guarantee is what makes the
 * memoisation valid.
 */
int longest_path_dag(const Sdg *g, const uint32_t *entries, size_t count,
                     Chain *out)
{
	igraph_vector_int_t order;
	uint32_t           *depth     = NULL;
	uint32_t           *successor = NULL;
	int                 status    = -1;

	memset(out, 0, sizeof *out);

	if (g->node_count == 0 || count == 0)
		return 0;

	if (igraph_vector_int_init(&order, 0) != IGRAPH_SUCCESS)
		return -1;

	/* IGRAPH_OUT yields callers before callees, so walking it backwards
	 * visits every callee before the function that calls it — which is
	 * what lets each node's answer be final the first time it is set. */
	if (igraph_topological_sorting((const igraph_t *)g->call_graph, &order,
	                               IGRAPH_OUT) != IGRAPH_SUCCESS)
		goto cleanup;

	depth     = calloc(g->node_count, sizeof *depth);
	successor = calloc(g->node_count, sizeof *successor);
	if (!depth || !successor)
		goto cleanup;

	for (size_t i = 0; i < g->node_count; i++) {
		depth[i]     = 1;              /* the function itself */
		successor[i] = UINT32_MAX;     /* no deeper callee    */
	}

	for (igraph_integer_t i = igraph_vector_int_size(&order) - 1; i >= 0; i--) {
		uint32_t node = (uint32_t)VECTOR(order)[i];

		for (size_t e = 0; e < g->edge_count; e++) {
			const SdgEdge *edge = &g->edges[e];

			if (edge->kind != EDGE_CALL || edge->from != node)
				continue;
			if (edge->to >= g->node_count)
				continue;
			if (depth[edge->to] + 1 > depth[node]) {
				depth[node]     = depth[edge->to] + 1;
				successor[node] = edge->to;
			}
		}
	}

	/* The deepest of the declared entry points. A tie resolves to the
	 * lowest node identifier, which is sorted-file order — so two chains
	 * of equal length always yield the same report (HLR-032). */
	uint32_t best      = 0;
	uint32_t best_node = UINT32_MAX;

	for (size_t i = 0; i < count; i++) {
		if (entries[i] >= g->node_count)
			continue;
		if (depth[entries[i]] > best) {
			best      = depth[entries[i]];
			best_node = entries[i];
		}
	}

	if (best_node == UINT32_MAX) {
		status = 0;   /* nothing reachable to measure */
		goto cleanup;
	}

	out->nodes = calloc(best, sizeof *out->nodes);
	if (!out->nodes)
		goto cleanup;

	/* Walk the retained successors out from the entry point. The chain is
	 * reported in full because the reader's next action is to shorten it,
	 * and a number says nothing about where (HLR-088, LLR-LPD-03). */
	for (uint32_t node = best_node; node != UINT32_MAX;
	     node = successor[node]) {
		out->nodes[out->count++] = node;
		if (out->count >= best)
			break;
	}

	status = 0;

cleanup:
	free(depth);
	free(successor);
	igraph_vector_int_destroy(&order);
	if (status != 0) {
		free(out->nodes);
		memset(out, 0, sizeof *out);
	}
	return status;
}

/* ------------------------------------------------------ entry resolution -- */

/* Resolve declared entry-point symbols to node identifiers.
 *
 * A symbol naming no analysed function is diagnosed and skipped rather than
 * treated as a usage error: analysing one directory of a project whose `main`
 * lives in another is an ordinary thing to do, and failing the run would make
 * `--entry main` unusable there. If *none* resolves the caller says so with
 * its own omission reason, which is a different message from "you declared
 * nothing".
 */
static int resolve_entries(const Sdg *g, const ElcOptions *opts,
                           uint32_t **out, size_t *out_count)
{
	uint32_t *nodes = calloc(opts->entry_point_count ?
	                         opts->entry_point_count : 1, sizeof *nodes);
	size_t    found = 0;

	if (!nodes)
		return -1;

	for (size_t i = 0; i < opts->entry_point_count; i++) {
		bool matched = false;

		for (size_t n = 0; n < g->node_count; n++)
			if (strcmp(g->nodes[n].name, opts->entry_points[i]) == 0) {
				nodes[found++] = (uint32_t)n;
				matched        = true;
				break;
			}

		if (!matched)
			fprintf(stderr,
			        "elc: entry point %s matches no analysed "
			        "function\n", opts->entry_points[i]);
	}

	*out       = nodes;
	*out_count = found;
	return 0;
}

/* ---------------------------------------------------------- the analysis -- */

int calltree_analyse(const Sdg *g, const ElcOptions *opts, TreeResults *out)
{
	uint32_t *entries = NULL;
	size_t    count   = 0;
	int       status  = -1;

	memset(out, 0, sizeof *out);
	out->unresolved_calls = graph_unresolved_count(g);

	if (compute_fan_out(g, out) != 0)
		goto cleanup;

	if (detect_recursion(g, out) != 0)
		goto cleanup;

	/* Order matters from here. Each of the three outcomes below is final,
	 * and each is a different thing to tell the reader. */

	if (opts->entry_point_count == 0) {
		out->depth_state = DEPTH_OMITTED_NO_ENTRY_POINTS;
		status           = 0;
		goto cleanup;
	}

	if (resolve_entries(g, opts, &entries, &count) != 0)
		goto cleanup;

	if (count == 0) {
		out->depth_state = DEPTH_OMITTED_ENTRY_UNRESOLVED;
		status           = 0;
		goto cleanup;
	}

	/* Recursion is checked before the traversal, not after it fails.
	 * Depth is unbounded where a cycle exists, and there is no deepest
	 * chain to report — a finite number here would be a wrong answer
	 * rather than an approximate one (HLR-090, LLR-CTR-04). */
	if (out->cycle_count > 0) {
		out->depth_state = DEPTH_UNBOUNDED_RECURSION;
		status           = 0;
		goto cleanup;
	}

	if (longest_path_dag(g, entries, count, &out->deepest) != 0)
		goto cleanup;

	out->depth_state = DEPTH_MEASURED;
	out->depth       = (uint32_t)out->deepest.count;
	status           = 0;

cleanup:
	free(entries);
	if (status != 0)
		tree_results_free(out);
	return status;
}

void tree_results_free(TreeResults *r)
{
	if (!r)
		return;

	for (size_t i = 0; i < r->cycle_count; i++)
		free(r->cycles[i].members);
	free(r->cycles);
	free(r->deepest.nodes);
	free(r->fan_out);
	memset(r, 0, sizeof *r);
}
