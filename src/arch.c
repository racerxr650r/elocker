/* arch.c — the component-level analyses.
 *
 * Coupling, Instability, bottlenecks, dependency cycles, and layering
 * validation (doc/SDD.md §9).
 *
 * **Everything here reads the component projection, and that is the design.**
 * `calltree.c` asks what calls what; this module asks what *depends on* what,
 * and the unit of the answer is a source file (HLR-114). The distinction is
 * not presentational: two mutually recursive functions in one file close a
 * loop in the call view and none at all in this one, because a component does
 * not depend on itself. Reporting that pair as a dependency cycle would tell
 * an architect to split a file over something that is a MISRA C Rule 17.2
 * finding about two functions. Across two files the same pair is legitimately
 * both, because the two facts are different (HLR-083, HLR-089).
 *
 * This module measures. What a Ca of 12 *means* is `thresholds.c`'s judgement
 * in Phase 12, and nothing here carries a severity — including the cycles,
 * which HLR-084 reports at critical severity there rather than here.
 */

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>

#include "arch.h"
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

/* -------------------------------------------------------------- coupling --
 *
 * Afferent coupling is the number of components depending on this one;
 * efferent is the number it depends upon (HLR-080). Both are counts of
 * *components*, not of edges, which is why they are taken from the component
 * projection: that table is already de-duplicated and drops self-edges, so one
 * file calling another in forty places is one dependency (LLR-CPL-03).
 */
int compute_coupling(const Sdg *g, ArchResults *out)
{
	out->coupling = calloc(g->component_count ? g->component_count : 1,
	                       sizeof *out->coupling);
	if (!out->coupling)
		return -1;
	out->component_count = g->component_count;

	for (size_t i = 0; i < g->component_edge_count; i++) {
		const ComponentEdge *e = &g->component_edges[i];

		if (e->from >= g->component_count || e->to >= g->component_count)
			continue;
		out->coupling[e->from].ce++;
		out->coupling[e->to].ca++;
	}

	for (size_t i = 0; i < out->component_count; i++) {
		ComponentCoupling *c = &out->coupling[i];

		c->instability = instability(c->ca, c->ce,
		                             &c->instability_defined);
	}

	return 0;
}

double instability(uint32_t ca, uint32_t ce, bool *defined)
{
	/* **No division when both are zero**, and the guard comes first for
	 * that reason rather than as a tidy early return. A component nothing
	 * depends on and which depends on nothing is ordinary — a lone file in
	 * a single-file target is exactly that — and the metric is genuinely
	 * undefined there rather than being 0, 1, or an error (HLR-082,
	 * LLR-INS-02). */
	if (ca == 0 && ce == 0) {
		*defined = false;
		return 0.0;
	}

	*defined = true;
	return (double)ce / (double)(ce + ca);
}

/* ---------------------------------------------------------------- cycles --
 *
 * The non-trivial strongly connected components of the component projection.
 * Every member of such a group can reach every other, which is precisely the
 * circular dependency Martin's acyclic-dependencies principle forbids.
 */

/* Find a loop through `start` using only members of its group.
 *
 * A depth-first search that stops the moment it can return to where it began.
 * Successors are visited in ascending order and the search starts from the
 * group's lowest-numbered member, so the loop reported for a given group is a
 * property of the source tree rather than of the decomposition's internal
 * ordering (HLR-032).
 *
 * One loop, not all of them: a group of n components can hold a number of
 * distinct loops exponential in n, and the reader needs an edge to cut rather
 * than a catalogue. The full membership travels beside it for the cases where
 * the loop does not touch every member.
 */
static bool find_loop(const Sdg *g, const size_t *group, size_t group_count,
                      size_t start, size_t current, bool *visited,
                      size_t *path, size_t *path_count)
{
	path[(*path_count)++] = current;
	visited[current]      = true;

	/* Ascending by construction: the component edges are appended in the
	 * order the report's sorted files produce them, and scanning the table
	 * in order visits each successor once. */
	for (size_t e = 0; e < g->component_edge_count; e++) {
		const ComponentEdge *edge = &g->component_edges[e];
		bool                 in_group = false;

		if (edge->from != current)
			continue;

		for (size_t i = 0; i < group_count && !in_group; i++)
			in_group = group[i] == edge->to;
		if (!in_group)
			continue;

		if (edge->to == start)
			return true;   /* the loop closes here */

		if (visited[edge->to])
			continue;

		if (find_loop(g, group, group_count, start, edge->to, visited,
		              path, path_count))
			return true;
	}

	(*path_count)--;   /* this component is not on the loop after all */
	return false;
}

static int cycle_add(const Sdg *g, ArchResults *out, size_t *members,
                     size_t count)
{
	bool   *visited = calloc(g->component_count ? g->component_count : 1,
	                         sizeof *visited);
	size_t *path    = calloc(count, sizeof *path);
	size_t  found   = 0;

	if (!visited || !path) {
		free(visited);
		free(path);
		return -1;
	}

	/* Members arrive ascending, so the search begins at the lowest —
	 * sorted-file order, which makes the reported loop the same on every
	 * run over the same tree. */
	find_loop(g, members, count, members[0], members[0], visited, path,
	          &found);
	free(visited);

	if (out->cycle_count == out->cycle_capacity &&
	    grow((void **)&out->cycles, &out->cycle_capacity,
	         sizeof *out->cycles) != 0) {
		free(path);
		return -1;
	}

	ComponentCycle *cycle = &out->cycles[out->cycle_count++];

	cycle->members      = members;
	cycle->member_count = count;
	cycle->path         = path;
	cycle->path_count   = found;
	return 0;
}

/* Groups are ordered by their lowest-numbered member, so the report lists them
 * in a defined order rather than the decomposition's (HLR-033). */
static int by_lowest_component(const void *a, const void *b)
{
	const ComponentCycle *x = a;
	const ComponentCycle *y = b;

	if (x->member_count == 0 || y->member_count == 0)
		return x->member_count == y->member_count
		               ? 0 : (x->member_count == 0 ? -1 : 1);
	return x->members[0] < y->members[0] ? -1
	                                     : x->members[0] > y->members[0];
}

int find_cycles(const Sdg *g, ArchResults *out)
{
	igraph_vector_int_t membership;
	igraph_integer_t    groups = 0;
	size_t             *sizes  = NULL;
	int                 status = -1;

	if (g->component_count == 0 || !g->component_graph)
		return 0;

	if (igraph_vector_int_init(&membership, 0) != IGRAPH_SUCCESS)
		return -1;

	if (igraph_connected_components((const igraph_t *)g->component_graph,
	                                &membership, NULL, &groups,
	                                IGRAPH_STRONG) != IGRAPH_SUCCESS)
		goto cleanup;

	sizes = calloc(groups ? (size_t)groups : 1, sizeof *sizes);
	if (!sizes)
		goto cleanup;

	for (size_t i = 0; i < g->component_count; i++)
		sizes[VECTOR(membership)[i]]++;

	for (igraph_integer_t c = 0; c < groups; c++) {
		size_t  n       = sizes[c];
		size_t *members = NULL;
		size_t  at      = 0;

		/* **A group of one is not a cycle**, and there is no self-loop
		 * exception here as there is for recursion. `component_edge_add`
		 * drops an edge from a component to itself, because a file
		 * does not depend on itself — which is exactly what keeps two
		 * mutually recursive functions inside one file from being
		 * reported as a circular dependency between components
		 * (HLR-083, LLR-CYC-03). */
		if (n < 2)
			continue;

		members = calloc(n, sizeof *members);
		if (!members)
			goto cleanup;

		for (size_t i = 0; i < g->component_count && at < n; i++)
			if (VECTOR(membership)[i] == c)
				members[at++] = i;

		if (cycle_add(g, out, members, n) != 0) {
			free(members);
			goto cleanup;
		}
	}

	if (out->cycle_count > 1)
		qsort(out->cycles, out->cycle_count, sizeof *out->cycles,
		      by_lowest_component);

	status = 0;

cleanup:
	free(sizes);
	igraph_vector_int_destroy(&membership);
	return status;
}

/* -------------------------------------------------------------- layering -- */

/* The stratum a component belongs to, or SIZE_MAX for one no declaration
 * names.
 *
 * A file matching no stratum is *outside* the declared architecture rather
 * than in a layer of its own: the user said nothing about it, and placing it
 * would report violations against a design nobody drew. The same call
 * `check_scopes` makes for an undeclared component.
 */
static size_t *stratum_of_components(const Sdg *g, const ElcOptions *opts)
{
	size_t *map = calloc(g->component_count ? g->component_count : 1,
	                     sizeof *map);

	if (!map)
		return NULL;

	for (size_t c = 0; c < g->component_count; c++) {
		map[c] = SIZE_MAX;

		for (size_t sidx = 0; sidx < opts->strata.count &&
		     map[c] == SIZE_MAX; sidx++)
			for (size_t p = 0;
			     p < opts->strata.items[sidx].pattern_count; p++)
				if (fnmatch(opts->strata.items[sidx].patterns[p],
				            g->component_paths[c], 0) == 0) {
					map[c] = sidx;
					break;
				}
	}

	return map;
}

/* A declared layer matching no component is a diagnostic, and the layer stays
 * in effect holding nothing (LLR-ARC-04).
 *
 * Not an omission and not an error: a project analysed one directory at a time
 * will routinely have layers that are empty this run, and failing or silently
 * dropping the layer would change the ordinals the remaining ones are compared
 * against — turning a typo into a wrong answer rather than a warning.
 */
static void warn_empty_strata(const Sdg *g, const ElcOptions *opts,
                              const size_t *stratum)
{
	for (size_t sidx = 0; sidx < opts->strata.count; sidx++) {
		bool used = false;

		for (size_t c = 0; c < g->component_count && !used; c++)
			used = stratum[c] == sidx;

		if (!used)
			fprintf(stderr,
			        "elc: stratum %s matches no analysed "
			        "component\n", opts->strata.items[sidx].name);
	}
}

static int violation_add(ArchResults *out, uint32_t from, uint32_t to,
                         size_t from_stratum, size_t to_stratum,
                         LayerViolationKind kind, size_t crossed)
{
	if (out->violation_count == out->violation_capacity &&
	    grow((void **)&out->violations, &out->violation_capacity,
	         sizeof *out->violations) != 0)
		return -1;

	LayerViolation *v = &out->violations[out->violation_count++];

	v->from           = from;
	v->to             = to;
	v->from_stratum   = from_stratum;
	v->to_stratum     = to_stratum;
	v->kind           = kind;
	v->layers_crossed = crossed;
	return 0;
}

int check_strata(const Sdg *g, const ElcOptions *opts, ArchResults *out)
{
	size_t *stratum = stratum_of_components(g, opts);
	int     status  = -1;

	if (!stratum)
		return -1;

	warn_empty_strata(g, opts, stratum);

	for (size_t e = 0; e < g->edge_count; e++) {
		const SdgEdge *edge = &g->edges[e];

		/* **Calls only.** HLR-079 and HLR-118 are about a call
		 * reaching into a layer it should not, and a shared global is a
		 * different fact with its own analysis (HLR-093, HLR-094).
		 * Folding state edges in here would report a layering
		 * violation for a variable two layers both read. */
		if (edge->kind != EDGE_CALL)
			continue;
		if (edge->from >= g->node_count || edge->to >= g->node_count)
			continue;

		size_t from = stratum[g->nodes[edge->from].component];
		size_t to   = stratum[g->nodes[edge->to].component];

		if (from == SIZE_MAX || to == SIZE_MAX)
			continue;

		size_t from_ord = opts->strata.items[from].ordinal;
		size_t to_ord   = opts->strata.items[to].ordinal;

		if (from_ord == to_ord)
			continue;   /* within a layer; nothing is declared about it */

		size_t distance = from_ord > to_ord ? from_ord - to_ord
		                                    : to_ord - from_ord;

		/* **Two independent tests on one comparison**, which is what
		 * makes these distinct findings rather than one "layering
		 * violation" (HLR-118, LLR-LAY-03). A call descending two
		 * layers bypasses an intervening one without inverting
		 * anything; a call ascending one inverts without bypassing
		 * anything; a call ascending two does both, and is reported
		 * twice because both statements are true and each has its own
		 * remedy. */
		if (to_ord < from_ord &&
		    violation_add(out, edge->from, edge->to, from, to,
		                  LAYER_INVERTED, distance) != 0)
			goto cleanup;

		if (distance > 1 &&
		    violation_add(out, edge->from, edge->to, from, to,
		                  LAYER_SKIP_LEVEL, distance) != 0)
			goto cleanup;
	}

	status = 0;

cleanup:
	free(stratum);
	return status;
}

/* ---------------------------------------------------------- the analysis -- */

int arch_analyse(const Sdg *g, const ElcOptions *opts, ArchResults *out)
{
	int status = -1;

	memset(out, 0, sizeof *out);

	/* Coupling and cycles need no declaration, so they run on every run.
	 * Omitting one analysis for want of a declaration must not omit its
	 * neighbours (LLR-CTR-09). */
	if (compute_coupling(g, out) != 0)
		goto cleanup;

	for (size_t i = 0; i < out->component_count; i++)
		out->coupling[i].bottleneck =
			out->coupling[i].ca >= opts->bottleneck_threshold &&
			out->coupling[i].ce >= opts->bottleneck_threshold;

	if (find_cycles(g, out) != 0)
		goto cleanup;

	/* Layering is the only analysis here that needs a declaration, and
	 * with none it is omitted with a stated reason rather than reported as
	 * an empty result (HLR-115, LLR-ARC-03). */
	if (opts->strata.count == 0) {
		out->strata_state = STRATA_OMITTED_NONE_DECLARED;
	} else {
		if (check_strata(g, opts, out) != 0)
			goto cleanup;
		out->strata_state = STRATA_MEASURED;
	}

	status = 0;

cleanup:
	if (status != 0)
		arch_results_free(out);
	return status;
}

void arch_results_free(ArchResults *r)
{
	if (!r)
		return;

	free(r->coupling);
	for (size_t i = 0; i < r->cycle_count; i++) {
		free(r->cycles[i].members);
		free(r->cycles[i].path);
	}
	free(r->cycles);
	free(r->violations);
	memset(r, 0, sizeof *r);
}
