/* graph.c — the System Dependence Graph.
 *
 * Turns the per-file facts of the single parse into one project-wide graph
 * (doc/SDD.md §8). Everything here is arithmetic over facts already gathered:
 * no file is opened, and nothing is re-parsed, which is how HLR-076 is met
 * structurally rather than by promise.
 *
 * The two questions this module answers that no earlier stage could:
 *
 *   * **Is this identifier a function this project defines?** A call site
 *     records a name; only the whole-project symbol table can say whether it
 *     resolves. That is why the parse records facts rather than edges.
 *   * **Is this identifier a global?** Same shape of answer. A file's
 *     `globals.scm` captures reads and writes wherever they appear, and the
 *     declarations that make a name global may be in another file entirely.
 *
 * Both questions are asked here, once, against tables built from every file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>

#include "analyze.h"
#include "elc.h"
#include "graph.h"
#include "report.h"

/* ------------------------------------------------------------- utilities -- */

static int grow(void **items, size_t *capacity, size_t item_size)
{
	size_t next   = *capacity ? *capacity * 2 : 32;
	void  *bigger = realloc(*items, next * item_size);

	if (!bigger)
		return -1;

	*items    = bigger;
	*capacity = next;
	return 0;
}

/* --------------------------------------------------------- symbol table --
 *
 * Name to node id. Built as an array and sorted once, then searched by
 * bisection: the table is written in one pass and read in another, so a hash
 * table would buy nothing a sort does not, and the sorted form makes the
 * duplicate-definition rule fall out of adjacency rather than needing a
 * second structure to detect it.
 */

typedef struct {
	const char *name;
	uint32_t    node;
} Symbol;

static int by_symbol_name(const void *a, const void *b)
{
	const Symbol *x = a;
	const Symbol *y = b;
	int           c = strcmp(x->name, y->name);

	if (c != 0)
		return c;
	/* A tie orders by node id, which is sorted-file order. That is what
	 * makes "the first definition wins" a deterministic rule rather than
	 * a property of qsort's behaviour on equal keys. */
	return x->node < y->node ? -1 : x->node > y->node;
}

/* The lowest-numbered node defining `name`, or NULL.
 *
 * Lowest-numbered because node ids run in sorted file order, so the first
 * definition encountered across the project wins and a rebuild of the same
 * tree resolves the same way (SDD §8.5).
 */
static const Symbol *symbol_find(const Symbol *table, size_t count,
                                 const char *name)
{
	size_t low  = 0;
	size_t high = count;

	while (low < high) {
		size_t mid = low + (high - low) / 2;
		int    c   = strcmp(table[mid].name, name);

		if (c < 0)
			low = mid + 1;
		else
			high = mid;
	}

	return (low < count && strcmp(table[low].name, name) == 0)
	               ? &table[low] : NULL;
}

/* ---------------------------------------------------------- global names --
 *
 * The set of identifiers some file declares at file scope. Sorted and
 * searched the same way, and for the same reason: a read or a write is only
 * a global access if the name is declared as one *somewhere in the project*,
 * which is a question no single file's facts can answer.
 */

static int by_string(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* The graph's own copy of a declared global's name, or NULL when the name is
 * not a global anywhere in the project.
 *
 * Returning the *interned* pointer rather than a yes/no is what keeps an edge
 * valid: the fact list is released the moment graph_build returns, so an edge
 * holding the fact's string would point into freed memory — and would render
 * as a plausible-looking object name rather than crash, which is the worst
 * way for it to be wrong.
 */
static const char *global_intern(const char *const *names, size_t count,
                                 const char *name)
{
	size_t low  = 0;
	size_t high = count;

	while (low < high) {
		size_t mid = low + (high - low) / 2;
		int    c   = strcmp(names[mid], name);

		if (c < 0)
			low = mid + 1;
		else
			high = mid;
	}

	return (low < count && strcmp(names[low], name) == 0) ? names[low]
	                                                      : NULL;
}

/* ---------------------------------------------------------------- edges -- */

/* Add an edge, or increment the call-site count of the one already there.
 *
 * The linear scan is over one node's existing edges rather than the whole
 * table, because the builder adds every edge of one caller before moving on.
 * That keeps the collapse local: a function with four callees compares
 * against at most four edges, whatever the size of the project.
 */
static int edge_add(Sdg *g, size_t first_of_caller, uint32_t from, uint32_t to,
                    SdgEdgeKind kind, const char *global)
{
	for (size_t i = first_of_caller; i < g->edge_count; i++) {
		SdgEdge *e = &g->edges[i];

		if (e->from != from || e->to != to || e->kind != kind)
			continue;
		/* A global edge is per object, so two different objects
		 * between the same pair of functions are two edges. Calls
		 * collapse; that is the simple-graph rule (LLR-SDG-04). */
		if (kind == EDGE_GLOBAL && strcmp(e->global, global) != 0)
			continue;
		if (kind == EDGE_CALL)
			e->call_sites++;
		return 0;
	}

	if (g->edge_count == g->edge_capacity &&
	    grow((void **)&g->edges, &g->edge_capacity, sizeof *g->edges) != 0)
		return -1;

	SdgEdge *e = &g->edges[g->edge_count++];

	e->from       = from;
	e->to         = to;
	e->kind       = kind;
	e->call_sites = kind == EDGE_CALL ? 1 : 0;
	e->global     = global;
	return 0;
}

static int component_edge_add(Sdg *g, size_t from, size_t to)
{
	if (from == to)
		return 0;   /* a file does not depend on itself */

	for (size_t i = 0; i < g->component_edge_count; i++)
		if (g->component_edges[i].from == from &&
		    g->component_edges[i].to == to)
			return 0;

	if (g->component_edge_count == g->component_edge_capacity &&
	    grow((void **)&g->component_edges, &g->component_edge_capacity,
	         sizeof *g->component_edges) != 0)
		return -1;

	g->component_edges[g->component_edge_count].from = from;
	g->component_edges[g->component_edge_count].to   = to;
	g->component_edge_count++;
	return 0;
}

static int unresolved_add(Sdg *g, const char *callee, const char *file,
                          uint32_t line)
{
	char *owned = strdup(callee);

	if (!owned)
		return -1;

	/* The name is copied because the fact list is released as soon as
	 * graph_build returns (SDD §18), while the graph outlives the run's
	 * analysis phase and is what the report asks for these counts. */
	char **names = realloc(g->unresolved_names,
	                       (g->unresolved_name_count + 1) * sizeof *names);

	if (!names) {
		free(owned);
		return -1;
	}
	g->unresolved_names = names;
	g->unresolved_names[g->unresolved_name_count++] = owned;

	if (g->unresolved == g->unresolved_capacity &&
	    grow((void **)&g->unresolved_sites, &g->unresolved_capacity,
	         sizeof *g->unresolved_sites) != 0)
		return -1;

	UnresolvedCall *u = &g->unresolved_sites[g->unresolved++];

	u->callee = owned;
	u->file   = file;
	u->line   = line;
	return 0;
}

/* ------------------------------------------------------------- the build -- */

/* The facts recorded for one file, matched by path.
 *
 * Linear rather than sorted-and-bisected: the fact list is in discovery
 * order and the report in sorted order, and for the file counts elc deals in
 * the difference does not pay for the index. Made explicit here so that if it
 * ever does, this is the one place to change.
 */
static const FileFacts *facts_for(const FactList *facts, const char *path)
{
	for (size_t i = 0; i < facts->count; i++)
		if (strcmp(facts->items[i]->path, path) == 0)
			return facts->items[i];
	return NULL;
}

/* Assign a node to every function, in report order.
 *
 * Report order is sorted-file order, and within a file the functions are
 * already ordered by start line. Node ids therefore depend on the source
 * tree and on nothing else — not on the order files were discovered in, and
 * not on any container's internal enumeration (LLR-SDG-09, HLR-033).
 */
static int build_nodes(const Report *report, Sdg *g)
{
	size_t total = 0;

	for (size_t f = 0; f < report->file_count; f++)
		total += report->files[f]->function_count;

	g->nodes = calloc(total ? total : 1, sizeof *g->nodes);
	if (!g->nodes)
		return -1;

	g->component_paths = calloc(report->file_count ? report->file_count : 1,
	                            sizeof *g->component_paths);
	if (!g->component_paths)
		return -1;
	g->component_count = report->file_count;

	for (size_t f = 0; f < report->file_count; f++) {
		const FileMetrics *fm = report->files[f];

		g->component_paths[f] = fm->path;

		for (size_t i = 0; i < fm->function_count; i++) {
			SdgNode *n = &g->nodes[g->node_count++];

			n->name       = fm->functions[i].name;
			n->file       = fm->path;
			n->component  = f;
			n->line_start = fm->functions[i].start_line;
			n->line_end   = fm->functions[i].end_line;
			n->eloc       = fm->functions[i].eloc;
			n->complexity = fm->functions[i].complexity;
		}
	}

	return 0;
}

int graph_build(const FactList *facts, const Report *report, Sdg *out)
{
	Symbol      *symbols     = NULL;
	size_t       symbol_count = 0;
	const char **declared    = NULL;
	size_t       declared_count = 0;
	igraph_t    *ig          = NULL;
	igraph_t    *call_ig     = NULL;
	int          status      = -1;

	memset(out, 0, sizeof *out);

	/* **igraph aborts the process on error unless told not to.** Its
	 * default handler calls abort(), so every `!= IGRAPH_SUCCESS` check
	 * below is unreachable without this line — an allocation failure
	 * inside the library would kill the run rather than produce the
	 * diagnostic and exit status main promises (HLR-125).
	 *
	 * It matters more from Phase 9 on. `igraph_topological_sorting`
	 * returns an error on a cyclic graph, which is exactly how the call
	 * depth analysis will detect recursion — an ordinary, expected answer
	 * that the default handler turns into a crash.
	 *
	 * Set here rather than in main() because this module is the only one
	 * that touches igraph, and a global whose setting lives in a different
	 * file from its use is a global waiting to be unset. */
	igraph_set_error_handler(igraph_error_handler_ignore);

	if (build_nodes(report, out) != 0)
		goto cleanup;

	/* --- the symbol table ------------------------------------------- */

	symbols = calloc(out->node_count ? out->node_count : 1, sizeof *symbols);
	if (!symbols)
		goto cleanup;

	for (size_t i = 0; i < out->node_count; i++) {
		symbols[symbol_count].name = out->nodes[i].name;
		symbols[symbol_count].node = (uint32_t)i;
		symbol_count++;
	}
	qsort(symbols, symbol_count, sizeof *symbols, by_symbol_name);

	/* A name defined more than once is reported once and resolves to its
	 * first definition. Two static functions of the same name in two
	 * files is ordinary C, so this is a note about the graph's precision
	 * rather than a complaint about the code — but a reader comparing
	 * fan-out against the source deserves to know an edge may have gone
	 * to the other one (SDD §8.5). */
	for (size_t i = 0; i < symbol_count; ) {
		size_t run = i + 1;

		while (run < symbol_count &&
		       strcmp(symbols[run].name, symbols[i].name) == 0)
			run++;

		if (run - i > 1) {
			const SdgNode *winner = &out->nodes[symbols[i].node];

			fprintf(stderr,
			        "elc: %s is defined %zu times; calls to it "
			        "resolve to %s:%u\n",
			        symbols[i].name, run - i,
			        winner->file, winner->line_start);
		}
		i = run;
	}

	/* --- the set of names some file declares globally ---------------- */

	for (size_t i = 0; i < facts->count; i++)
		for (size_t j = 0; j < facts->items[i]->global_count; j++)
			if (facts->items[i]->globals[j].kind == GLOBAL_DECLARATION)
				declared_count++;

	declared = calloc(declared_count ? declared_count : 1, sizeof *declared);
	if (!declared)
		goto cleanup;

	declared_count = 0;
	for (size_t i = 0; i < facts->count; i++)
		for (size_t j = 0; j < facts->items[i]->global_count; j++)
			if (facts->items[i]->globals[j].kind == GLOBAL_DECLARATION)
				declared[declared_count++] =
					facts->items[i]->globals[j].name;
	qsort(declared, declared_count, sizeof *declared, by_string);

	/* Copied into the graph, de-duplicated on the way: one object declared
	 * in a header included by six files is one name, and every edge naming
	 * it points at the same string. The copy is what lets main() release
	 * the facts the instant the build returns. */
	out->global_names = calloc(declared_count ? declared_count : 1,
	                           sizeof *out->global_names);
	if (!out->global_names)
		goto cleanup;

	for (size_t i = 0; i < declared_count; i++) {
		if (i > 0 && strcmp(declared[i], declared[i - 1]) == 0)
			continue;
		out->global_names[out->global_name_count] = strdup(declared[i]);
		if (!out->global_names[out->global_name_count])
			goto cleanup;
		out->global_name_count++;
	}

	/* --- call edges --------------------------------------------------- */

	size_t node_base = 0;

	for (size_t f = 0; f < report->file_count; f++) {
		const FileMetrics *fm = report->files[f];
		const FileFacts   *ff = facts_for(facts, fm->path);

		if (!ff) {
			node_base += fm->function_count;
			continue;
		}

		size_t first_edge = out->edge_count;

		for (size_t c = 0; c < ff->call_count; c++) {
			const CallSite *site = &ff->calls[c];
			const Symbol   *hit  = symbol_find(symbols, symbol_count,
			                                   site->callee);

			/* A call from file scope has no caller node, so there
			 * is nothing to draw an edge from. It is still a call
			 * the graph does not represent, and is counted as
			 * unresolved for the same reason a library call is:
			 * the reader is judging completeness. */
			if (!hit || site->caller == ELC_NO_FUNCTION) {
				if (unresolved_add(out, site->callee, fm->path,
				                   site->line) != 0)
					goto cleanup;
				continue;
			}

			uint32_t from = (uint32_t)(node_base + site->caller);

			/* Every index is checked against the table's extent
			 * before it is used. An out-of-range caller index
			 * would mean the facts and the metrics disagreed
			 * about how many functions a file has, which is not
			 * something to discover by writing past the table
			 * (LLR-SDG-11). */
			if (from >= out->node_count || hit->node >= out->node_count)
				continue;

			if (edge_add(out, first_edge, from, hit->node,
			             EDGE_CALL, NULL) != 0)
				goto cleanup;
			if (component_edge_add(out, out->nodes[from].component,
			                       out->nodes[hit->node].component) != 0)
				goto cleanup;
		}

		/* --- address-taken ---------------------------------------- */

		for (size_t a = 0; a < ff->address_taken_count; a++) {
			const Symbol *hit = symbol_find(symbols, symbol_count,
			                                ff->address_taken[a]);

			/* Most of these resolve to nothing: the queries
			 * capture identifiers in value position, of which
			 * variables are the majority. A name that is not a
			 * defined function is simply not a root, and no
			 * diagnostic is warranted — this is the query doing
			 * what its file says it does. */
			if (hit && hit->node < out->node_count)
				out->nodes[hit->node].address_taken = true;
		}

		node_base += fm->function_count;
	}

	/* --- global-state edges ------------------------------------------- */

	node_base = 0;
	for (size_t f = 0; f < report->file_count; f++) {
		const FileMetrics *fm = report->files[f];
		const FileFacts   *ff = facts_for(facts, fm->path);

		if (!ff) {
			node_base += fm->function_count;
			continue;
		}

		for (size_t w = 0; w < ff->global_count; w++) {
			const GlobalAccess *write = &ff->globals[w];

			const char *object = NULL;

			if (write->kind == GLOBAL_WRITE &&
			    write->function != ELC_NO_FUNCTION)
				object = global_intern(
					(const char *const *)out->global_names,
					out->global_name_count, write->name);
			if (!object)
				continue;

			uint32_t from = (uint32_t)(node_base + write->function);

			if (from >= out->node_count)
				continue;

			/* An edge from every writer to every reader of the
			 * same object, across the whole project — which is
			 * why this runs after every file's facts are in hand
			 * rather than per file (HLR-074, LLR-SDG-03). */
			size_t reader_base = 0;

			for (size_t g2 = 0; g2 < report->file_count; g2++) {
				const FileMetrics *rfm = report->files[g2];
				const FileFacts   *rff = facts_for(facts,
				                                   rfm->path);

				if (!rff) {
					reader_base += rfm->function_count;
					continue;
				}

				for (size_t r = 0; r < rff->global_count; r++) {
					const GlobalAccess *read =
						&rff->globals[r];

					if (read->kind != GLOBAL_READ ||
					    read->function == ELC_NO_FUNCTION ||
					    strcmp(read->name, write->name) != 0)
						continue;

					uint32_t to = (uint32_t)(reader_base +
					                         read->function);

					if (to >= out->node_count || to == from)
						continue;

					if (edge_add(out, 0, from, to,
					             EDGE_GLOBAL, object) != 0)
						goto cleanup;
					if (component_edge_add(out,
					        out->nodes[from].component,
					        out->nodes[to].component) != 0)
						goto cleanup;
				}

				reader_base += rfm->function_count;
			}
		}

		node_base += fm->function_count;
	}

	/* --- the library's structure -------------------------------------- */
	//
	// Built last, from the edge table this module already holds. igraph
	// owns the topology so that the traversals of Phases 9 to 11 are its
	// algorithms rather than ours (HLR-113); the attributes stay here,
	// indexed by edge position, because igraph's C attribute interface
	// would mean carrying a second representation of data we already have.

	ig = calloc(1, sizeof *ig);
	if (!ig)
		goto cleanup;

	igraph_vector_int_t edges;

	if (igraph_vector_int_init(&edges, (igraph_integer_t)(out->edge_count * 2)) !=
	    IGRAPH_SUCCESS) {
		free(ig);
		ig = NULL;
		goto cleanup;
	}
	for (size_t i = 0; i < out->edge_count; i++) {
		VECTOR(edges)[2 * i]     = out->edges[i].from;
		VECTOR(edges)[2 * i + 1] = out->edges[i].to;
	}

	if (igraph_create(ig, &edges, (igraph_integer_t)out->node_count,
	                  IGRAPH_DIRECTED) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&edges);
		free(ig);
		ig = NULL;
		goto cleanup;
	}
	igraph_vector_int_destroy(&edges);

	/* The call-only view. Built here rather than by each consumer so that
	 * every analysis asking "what calls what" asks the same question of
	 * the same structure (see graph.h). */
	call_ig = calloc(1, sizeof *call_ig);
	if (!call_ig)
		goto cleanup;

	size_t call_edges = 0;

	for (size_t i = 0; i < out->edge_count; i++)
		if (out->edges[i].kind == EDGE_CALL)
			call_edges++;

	igraph_vector_int_t only_calls;

	if (igraph_vector_int_init(&only_calls,
	                           (igraph_integer_t)(call_edges * 2)) !=
	    IGRAPH_SUCCESS)
		goto cleanup;

	size_t at = 0;

	for (size_t i = 0; i < out->edge_count; i++) {
		if (out->edges[i].kind != EDGE_CALL)
			continue;
		VECTOR(only_calls)[at++] = out->edges[i].from;
		VECTOR(only_calls)[at++] = out->edges[i].to;
	}

	if (igraph_create(call_ig, &only_calls,
	                  (igraph_integer_t)out->node_count,
	                  IGRAPH_DIRECTED) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&only_calls);
		goto cleanup;
	}
	igraph_vector_int_destroy(&only_calls);

	out->graph      = ig;
	out->call_graph = call_ig;
	ig              = NULL;
	call_ig         = NULL;
	status          = 0;

cleanup:
	free(symbols);
	free(declared);
	free(ig);
	free(call_ig);
	if (status != 0)
		graph_free(out);
	return status;
}

size_t graph_unresolved_count(const Sdg *g)
{
	return g ? g->unresolved : 0;
}

void graph_free(Sdg *g)
{
	if (!g)
		return;

	if (g->graph) {
		igraph_destroy((igraph_t *)g->graph);
		free(g->graph);
	}
	if (g->call_graph) {
		igraph_destroy((igraph_t *)g->call_graph);
		free(g->call_graph);
	}
	for (size_t i = 0; i < g->global_name_count; i++)
		free(g->global_names[i]);
	free(g->global_names);
	for (size_t i = 0; i < g->unresolved_name_count; i++)
		free(g->unresolved_names[i]);
	free(g->unresolved_names);
	free(g->unresolved_sites);
	free(g->component_edges);
	free(g->component_paths);
	free(g->edges);
	free(g->nodes);
	memset(g, 0, sizeof *g);
}
