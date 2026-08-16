/* state.c — global-state coupling, execution-scope isolation, reachability.
 *
 * The module behind the product's headline claim: *this function is dead*
 * (doc/SDD.md §11).
 *
 * That claim is either sound or merely plausible, and one decision settles
 * which. **The root set is the declared entry points together with every
 * function whose address is taken.** A handler installed in an interrupt
 * vector, or a callback stored in a table, has no resolved caller anywhere in
 * the graph; without it in the root set it is reported as provably dead, and a
 * user who acts on that deletes code that runs. The asymmetry is deliberate:
 * an extra root can only shrink the unreachable set, while a missing one
 * produces a false claim of death (HLR-096).
 *
 * The other half of the claim is what makes it worth making. Unreachability is
 * established **solely by traversal** — no name heuristics, no "looks
 * unused" — which is why a clique of unused functions calling one another is
 * still reported dead. That is the case a textual linter gets wrong, and it is
 * the reason this analysis exists rather than a grep (HLR-097).
 */

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>

#include "elc.h"
#include "graph.h"
#include "state.h"

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

/* --------------------------------------------------------- the root set --
 *
 * The union of what the user declared and what the graph knows may be entered
 * indirectly. Neither alone is the answer: entry points alone report every
 * callback as dead, and address-taken functions alone report a program with no
 * function pointers as entirely dead.
 */
int collect_roots(const Sdg *g, const ElcOptions *opts, uint32_t **out,
                  size_t *out_count, size_t *resolved)
{
	size_t    capacity = opts->entry_point_count + g->node_count + 1;
	uint32_t *roots    = calloc(capacity, sizeof *roots);
	size_t    count    = 0;

	*out       = NULL;
	*out_count = 0;
	*resolved  = 0;

	if (!roots)
		return -1;

	/* A declared symbol naming no analysed function is diagnosed and
	 * skipped rather than failing the run: analysing one directory of a
	 * project whose `main` lives in another is ordinary, and the count
	 * below lets the caller distinguish that from no declaration at all
	 * (LLR-STA-01). */
	for (size_t i = 0; i < opts->entry_point_count; i++) {
		bool matched = false;

		for (size_t n = 0; n < g->node_count; n++)
			if (strcmp(g->nodes[n].name, opts->entry_points[i]) == 0) {
				roots[count++] = (uint32_t)n;
				matched        = true;
				break;
			}

		if (matched)
			(*resolved)++;
	}

	/* **The half that makes the claim sound.** Phase 8 resolved each
	 * `@call.address_taken` capture against the whole-project symbol table
	 * and marked the node; reading the field is all this needs, and
	 * nothing new is asked of any query file (LLR-RTS-02). */
	for (size_t n = 0; n < g->node_count; n++)
		if (g->nodes[n].address_taken)
			roots[count++] = (uint32_t)n;

	if (count > 1) {
		qsort(roots, count, sizeof *roots, by_node_id);

		size_t kept = 1;

		for (size_t i = 1; i < count; i++)
			if (roots[i] != roots[kept - 1])
				roots[kept++] = roots[i];
		count = kept;
	}

	*out       = roots;
	*out_count = count;
	return 0;
}

/* ------------------------------------------------------------ traversal --
 *
 * Breadth-first over the *call* view. The complement of what is visited is the
 * answer, and it is arrived at by traversal alone: no name is inspected, no
 * heuristic is applied, and nothing about how a function looks contributes
 * (LLR-RCH-02).
 */
int reachability(const Sdg *g, const uint32_t *roots, size_t root_count,
                 uint32_t **out, size_t *out_count)
{
	igraph_vector_int_t neighbours;
	bool               *seen   = NULL;
	uint32_t           *queue  = NULL;
	uint32_t           *dead   = NULL;
	size_t              head   = 0;
	size_t              tail   = 0;
	size_t              count  = 0;
	int                 status = -1;

	*out       = NULL;
	*out_count = 0;

	if (g->node_count == 0)
		return 0;

	if (igraph_vector_int_init(&neighbours, 0) != IGRAPH_SUCCESS)
		return -1;

	seen  = calloc(g->node_count, sizeof *seen);
	queue = calloc(g->node_count, sizeof *queue);
	dead  = calloc(g->node_count, sizeof *dead);
	if (!seen || !queue || !dead)
		goto cleanup;

	for (size_t i = 0; i < root_count; i++)
		if (roots[i] < g->node_count && !seen[roots[i]]) {
			seen[roots[i]] = true;
			queue[tail++]  = roots[i];
		}

	while (head < tail) {
		uint32_t node = queue[head++];

		/* Self-loops and repeated edges are kept rather than filtered:
		 * a function already marked seen costs one comparison, and
		 * asking the library to deduplicate would be paying for an
		 * answer this loop already has. */
		if (igraph_neighbors((const igraph_t *)g->call_graph,
		                     &neighbours, (igraph_int_t)node,
		                     IGRAPH_OUT, IGRAPH_LOOPS,
		                     IGRAPH_MULTIPLE) != IGRAPH_SUCCESS)
			goto cleanup;

		for (igraph_integer_t i = 0;
		     i < igraph_vector_int_size(&neighbours); i++) {
			igraph_integer_t next = VECTOR(neighbours)[i];

			if (next < 0 || (size_t)next >= g->node_count)
				continue;
			if (seen[next])
				continue;
			seen[next]    = true;
			queue[tail++] = (uint32_t)next;
		}
	}

	/* Ascending by construction, which is sorted-file order — so the list
	 * is a property of the source tree rather than of the traversal
	 * (HLR-032). */
	for (size_t i = 0; i < g->node_count; i++)
		if (!seen[i])
			dead[count++] = (uint32_t)i;

	*out       = dead;
	*out_count = count;
	dead       = NULL;
	status     = 0;

cleanup:
	free(dead);
	free(queue);
	free(seen);
	igraph_vector_int_destroy(&neighbours);
	return status;
}

/* Globals accessed *solely* by functions that are themselves unreachable
 * (HLR-096, LLR-UGL-01).
 *
 * "Solely" is read literally, and an object no analysed function touches at
 * all is therefore not claimed. It may be touched from file scope, from a
 * language whose `globals.scm` captures nothing, or from a translation unit
 * outside the target — and the same asymmetry that governs the functions
 * governs the data: an object wrongly called dead invites deleting storage
 * something writes.
 */
static int unreachable_globals(const Sdg *g, const uint32_t *dead,
                               size_t dead_count, StateResults *out)
{
	const char **objects = calloc(g->global_name_count ?
	                              g->global_name_count : 1, sizeof *objects);
	size_t       count   = 0;

	if (!objects)
		return -1;

	for (size_t i = 0; i < g->touch_count; ) {
		const char *object = g->touches[i].object;
		bool        live   = false;
		size_t      j      = i;

		while (j < g->touch_count &&
		       strcmp(g->touches[j].object, object) == 0) {
			bool is_dead = false;

			for (size_t d = 0; d < dead_count && !is_dead; d++)
				is_dead = dead[d] == g->touches[j].node;
			if (!is_dead)
				live = true;
			j++;
		}

		if (!live)
			objects[count++] = object;
		i = j;
	}

	out->dead_globals      = objects;
	out->dead_global_count = count;
	return 0;
}

/* --------------------------------------------------------- global state --
 *
 * The writer and reader sets of every object, and the two verdicts MISRA C
 * Rule 8.9 is concerned with.
 *
 * The hidden-channel test asks whether the functions touching an object fall
 * into more than one weakly connected region of the *call* graph. One region
 * is ordinary shared state between functions that already know about each
 * other; two is temporal coupling, in which execution order silently governs
 * whether the system works, and nothing in either region says so (HLR-093).
 */
int classify_globals(const Sdg *g, StateResults *out)
{
	igraph_vector_int_t membership;
	int                 status = -1;
	size_t              rows   = 0;

	if (g->touch_count == 0)
		return 0;

	if (igraph_vector_int_init(&membership, 0) != IGRAPH_SUCCESS)
		return -1;

	/* Weak, not strong. Two functions calling in one direction only are
	 * still part of one design; requiring mutual reachability would report
	 * every ordinary caller/callee pair as disconnected. */
	if (igraph_connected_components((const igraph_t *)g->call_graph,
	                                &membership, NULL, NULL,
	                                IGRAPH_WEAK) != IGRAPH_SUCCESS)
		goto cleanup;

	for (size_t i = 0; i < g->touch_count; i++)
		if (i == 0 || strcmp(g->touches[i].object,
		                     g->touches[i - 1].object) != 0)
			rows++;

	out->globals = calloc(rows, sizeof *out->globals);
	if (!out->globals)
		goto cleanup;

	for (size_t i = 0; i < g->touch_count; ) {
		const char *object = g->touches[i].object;
		size_t      j      = i;

		while (j < g->touch_count &&
		       strcmp(g->touches[j].object, object) == 0)
			j++;

		GlobalRow *row = &out->globals[out->global_count];

		row->object   = object;
		row->touchers = calloc(j - i, sizeof *row->touchers);
		if (!row->touchers)
			goto cleanup;

		/* The access records arrive sorted by object then node, so one
		 * function reading and writing the same object is two adjacent
		 * records and folds into one toucher here. */
		for (size_t k = i; k < j; k++) {
			GlobalToucher *touch = NULL;

			if (row->toucher_count > 0 &&
			    row->touchers[row->toucher_count - 1].node ==
			            g->touches[k].node)
				touch = &row->touchers[row->toucher_count - 1];
			else {
				touch = &row->touchers[row->toucher_count++];
				touch->node   = g->touches[k].node;
				touch->region = (size_t)VECTOR(membership)[
					g->touches[k].node];
				touch->writes = false;
				touch->reads  = false;
			}

			if (g->touches[k].write)
				touch->writes = true;
			else
				touch->reads = true;
		}

		for (size_t a = 0; a < row->toucher_count; a++) {
			bool first = true;

			for (size_t b = 0; b < a; b++)
				if (row->touchers[b].region ==
				    row->touchers[a].region)
					first = false;
			if (first)
				row->region_count++;
		}

		/* A single function touching an object should have declared it
		 * at block scope; more than one region touching it is the
		 * hidden channel. The order of the two tests matters only in
		 * that one function is always one region (HLR-092, HLR-093). */
		if (row->toucher_count == 1)
			row->verdict = GLOBAL_SCOPE_REDUCTION;
		else if (row->region_count > 1)
			row->verdict = GLOBAL_HIDDEN_CHANNEL;
		else
			row->verdict = GLOBAL_ORDINARY;

		out->global_count++;
		i = j;
	}

	status = 0;

cleanup:
	igraph_vector_int_destroy(&membership);
	return status;
}

/* ------------------------------------------------------ scope isolation -- */

/* The scope a component belongs to, or SIZE_MAX for one no declaration names.
 *
 * A file matching no scope is *outside* the declaration rather than in a scope
 * of its own: the user said nothing about it, and inventing a boundary would
 * report violations against a partition nobody drew.
 */
static size_t *scope_of_components(const Sdg *g, const ElcOptions *opts)
{
	size_t *map = calloc(g->component_count ? g->component_count : 1,
	                     sizeof *map);

	if (!map)
		return NULL;

	for (size_t c = 0; c < g->component_count; c++) {
		map[c] = SIZE_MAX;

		for (size_t s = 0; s < opts->scopes.count &&
		     map[c] == SIZE_MAX; s++)
			for (size_t p = 0;
			     p < opts->scopes.items[s].pattern_count; p++)
				if (fnmatch(opts->scopes.items[s].patterns[p],
				            g->component_paths[c], 0) == 0) {
					map[c] = s;
					break;
				}
	}

	return map;
}

/* Every call edge and every global-state edge by which one declared scope
 * reaches a function or object belonging to another (LLR-ISO-01).
 *
 * Both kinds, not only calls. A scope that never calls into another but writes
 * a variable the other reads has not been isolated — that is precisely the
 * shared memory map HLR-094 exists for.
 */
static int check_scopes(const Sdg *g, const ElcOptions *opts,
                        StateResults *out)
{
	size_t *scope  = scope_of_components(g, opts);
	int     status = -1;

	if (!scope)
		return -1;

	for (size_t e = 0; e < g->edge_count; e++) {
		const SdgEdge *edge = &g->edges[e];

		if (edge->from >= g->node_count || edge->to >= g->node_count)
			continue;

		size_t from = scope[g->nodes[edge->from].component];
		size_t to   = scope[g->nodes[edge->to].component];

		if (from == SIZE_MAX || to == SIZE_MAX || from == to)
			continue;

		if (out->violation_count == out->violation_capacity &&
		    grow((void **)&out->violations, &out->violation_capacity,
		         sizeof *out->violations) != 0)
			goto cleanup;

		ScopeViolation *v = &out->violations[out->violation_count++];

		v->from       = edge->from;
		v->to         = edge->to;
		v->from_scope = from;
		v->to_scope   = to;
		v->kind       = edge->kind;
		v->object     = edge->kind == EDGE_GLOBAL ? edge->global : NULL;
	}

	status = 0;

cleanup:
	free(scope);
	return status;
}

/* ---------------------------------------------------------- the analysis -- */

int state_analyse(const Sdg *g, const ElcOptions *opts, StateResults *out)
{
	uint32_t *roots    = NULL;
	size_t    count    = 0;
	size_t    resolved = 0;
	int       status   = -1;

	memset(out, 0, sizeof *out);

	/* Independent of every declaration, so it runs first and runs always.
	 * Omitting one analysis must not omit its neighbours: a run with no
	 * `--entry` still gets its global-access map (LLR-CTR-09). */
	if (classify_globals(g, out) != 0)
		goto cleanup;

	/* Each of the two absences below is a different thing to tell the
	 * reader, and neither is an error. Nothing is reported unreachable in
	 * either case — `elc` never calls a function dead for want of a
	 * declaration (HLR-115, LLR-STA-01). */
	if (opts->entry_point_count == 0) {
		out->reach_state = REACH_OMITTED_NO_ENTRY_POINTS;
	} else {
		if (collect_roots(g, opts, &roots, &count, &resolved) != 0)
			goto cleanup;

		if (resolved == 0) {
			out->reach_state = REACH_OMITTED_ENTRY_UNRESOLVED;
		} else if (reachability(g, roots, count, &out->unreachable,
		                        &out->unreachable_count) != 0) {
			goto cleanup;
		} else if (unreachable_globals(g, out->unreachable,
		                               out->unreachable_count,
		                               out) != 0) {
			goto cleanup;
		} else {
			out->reach_state = REACH_MEASURED;
		}
	}

	if (opts->scopes.count == 0) {
		out->scope_state = SCOPES_OMITTED_NONE_DECLARED;
	} else {
		if (check_scopes(g, opts, out) != 0)
			goto cleanup;
		out->scope_state = SCOPES_MEASURED;
	}

	status = 0;

cleanup:
	free(roots);
	if (status != 0)
		state_results_free(out);
	return status;
}

void state_results_free(StateResults *r)
{
	if (!r)
		return;

	for (size_t i = 0; i < r->global_count; i++)
		free(r->globals[i].touchers);
	free(r->globals);
	free(r->unreachable);
	free((void *)r->dead_globals);
	free(r->violations);
	memset(r, 0, sizeof *r);
}
