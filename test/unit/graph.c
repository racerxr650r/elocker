/* test/unit/graph.c — unit tests for src/graph.c.
 *
 * The graph is built from facts and a report, both of which are ordinary
 * structures. So these tests build them by hand rather than by parsing: the
 * resolution rules are what is under test, and a fixture that had to be
 * parsed first would mix a query file's behaviour into every assertion about
 * them. The `graph/` fixture group covers the parse-to-graph path end to end.
 */

#include <criterion/criterion.h>
#include <igraph.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "elc.h"
#include "graph.h"
#include "report.h"

/* ------------------------------------------------------------ scaffolding */

/* A file's metrics with `count` functions named from `names`. Line numbers
 * ascend so the report's within-file ordering is well defined. */
static FileMetrics *file_with(const char *path, const char *const *names,
                              size_t count)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path     = strdup(path);
	m->language = strdup("c");
	cr_assert_not_null(m->path);
	cr_assert_not_null(m->language);

	m->functions = calloc(count ? count : 1, sizeof *m->functions);
	cr_assert_not_null(m->functions);
	for (size_t i = 0; i < count; i++) {
		m->functions[i].name       = strdup(names[i]);
		cr_assert_not_null(m->functions[i].name);
		m->functions[i].start_line = (uint32_t)(i * 10 + 1);
		m->functions[i].end_line   = (uint32_t)(i * 10 + 5);
		m->functions[i].eloc       = 1;
		m->functions[i].complexity = 1;
	}
	m->function_count = count;
	return m;
}

static FileFacts *facts_for(const char *path)
{
	FileFacts *f = calloc(1, sizeof *f);

	cr_assert_not_null(f);
	f->path = strdup(path);
	cr_assert_not_null(f->path);
	return f;
}

static void add_call(FileFacts *f, const char *callee, size_t caller,
                     uint32_t line)
{
	f->calls = realloc(f->calls, (f->call_count + 1) * sizeof *f->calls);
	cr_assert_not_null(f->calls);
	f->calls[f->call_count].callee = strdup(callee);
	cr_assert_not_null(f->calls[f->call_count].callee);
	f->calls[f->call_count].caller = caller;
	f->calls[f->call_count].line   = line;
	f->call_count++;
	f->call_capacity = f->call_count;
}

static void add_global(FileFacts *f, const char *name, size_t function,
                       GlobalAccessKind kind)
{
	f->globals = realloc(f->globals,
	                     (f->global_count + 1) * sizeof *f->globals);
	cr_assert_not_null(f->globals);
	f->globals[f->global_count].name     = strdup(name);
	cr_assert_not_null(f->globals[f->global_count].name);
	f->globals[f->global_count].function = function;
	f->globals[f->global_count].line     = 1;
	f->globals[f->global_count].kind     = kind;
	f->global_count++;
	f->global_capacity = f->global_count;
}

static void add_address_taken(FileFacts *f, const char *name)
{
	f->address_taken = realloc(f->address_taken,
	                           (f->address_taken_count + 1) *
	                                   sizeof *f->address_taken);
	cr_assert_not_null(f->address_taken);
	f->address_taken[f->address_taken_count] = strdup(name);
	cr_assert_not_null(f->address_taken[f->address_taken_count]);
	f->address_taken_count++;
	f->address_taken_capacity = f->address_taken_count;
}

/* Assemble a report over the given files, taking ownership of them. */
static Report report_of(FileMetrics **files, size_t count)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };

	for (size_t i = 0; i < count; i++)
		cr_assert_eq(metrics_add(&acc, files[i]), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);
	metrics_free(&acc);
	return report;
}

/* The node id of a named function, or SIZE_MAX. */
static size_t node_of(const Sdg *g, const char *name)
{
	for (size_t i = 0; i < g->node_count; i++)
		if (strcmp(g->nodes[i].name, name) == 0)
			return i;
	return SIZE_MAX;
}

static const SdgEdge *edge_between(const Sdg *g, const char *from,
                                   const char *to, SdgEdgeKind kind)
{
	size_t a = node_of(g, from);
	size_t b = node_of(g, to);

	for (size_t i = 0; i < g->edge_count; i++)
		if (g->edges[i].from == a && g->edges[i].to == b &&
		    g->edges[i].kind == kind)
			return &g->edges[i];
	return NULL;
}

/* ------------------------------------------------------------ node table */

Test(graph, every_discovered_function_becomes_a_node)
{
	const char  *a[]     = { "one", "two" };
	const char  *b[]     = { "three" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2),
	                         file_with("/p/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	Sdg          g       = { 0 };

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(g.node_count, 3,
	             "every function across every file is a node (LLR-SDG-01)");

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, node_identifiers_follow_sorted_file_order)
{
	const char  *a[]     = { "alpha" };
	const char  *b[]     = { "beta" };
	/* Handed over in the order that is *not* sorted, so a graph numbering
	 * by arrival would disagree with one numbering by path. */
	FileMetrics *files[] = { file_with("/p/z.c", b, 1),
	                         file_with("/p/a.c", a, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	Sdg          g       = { 0 };

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(node_of(&g, "alpha"), 0,
	             "node ids run in sorted file order, not discovery order "
	             "(LLR-SDG-09, HLR-033)");
	cr_assert_eq(node_of(&g, "beta"), 1);

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ------------------------------------------------------------ call edges */

Test(graph, a_call_resolves_across_files)
{
	const char  *a[]     = { "caller" };
	const char  *b[]     = { "callee" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1),
	                         file_with("/p/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	add_call(fa, "callee", 0, 3);
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_not_null(edge_between(&g, "caller", "callee", EDGE_CALL),
	                   "the call resolves though the definition is in "
	                   "another file (LLR-SDG-02, HLR-073)");
	cr_assert_eq(graph_unresolved_count(&g), 0);

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, repeated_calls_collapse_to_one_edge_with_a_count)
{
	const char  *a[]     = { "caller", "callee" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	add_call(fa, "callee", 0, 3);
	add_call(fa, "callee", 0, 9);
	add_call(fa, "callee", 0, 11);
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	const SdgEdge *e = edge_between(&g, "caller", "callee", EDGE_CALL);

	cr_assert_not_null(e);
	cr_assert_eq(g.edge_count, 1,
	             "the graph is simple: three call sites are one edge, or "
	             "fan-out counts call sites instead of callees "
	             "(LLR-SDG-04, HLR-085)");
	cr_assert_eq(e->call_sites, 3, "the collapsed count is retained");

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, an_unresolvable_call_is_counted_and_does_not_abort)
{
	const char  *a[]     = { "caller" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	add_call(fa, "printf", 0, 3);
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0,
	             "a call into a library is not a build failure (HLR-077)");
	cr_assert_eq(graph_unresolved_count(&g), 1);
	cr_assert_eq(g.edge_count, 0,
	             "no destination is invented for it: an edge that does not "
	             "exist would make a later dead-code proof unsound");

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, a_call_from_file_scope_has_no_caller_node)
{
	const char  *a[]     = { "callee" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	/* A static initialiser calling a function: real, and outside every
	 * reported function, so there is no node to draw the edge from. */
	add_call(fa, "callee", ELC_NO_FUNCTION, 1);
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(g.edge_count, 0);
	cr_assert_eq(graph_unresolved_count(&g), 1,
	             "counted, because the reader judging completeness cares "
	             "that the graph does not represent it (HLR-077)");

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ---------------------------------------------------------- global edges */

Test(graph, a_global_links_its_writer_to_its_reader)
{
	const char  *a[]     = { "writer" };
	const char  *b[]     = { "reader" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1),
	                         file_with("/p/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	FileFacts   *fb      = facts_for("/p/b.c");
	Sdg          g       = { 0 };

	add_global(fa, "shared", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "shared", 0, GLOBAL_WRITE);
	add_global(fb, "shared", 0, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, fb), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	const SdgEdge *e = edge_between(&g, "writer", "reader", EDGE_GLOBAL);

	cr_assert_not_null(e, "coupling through shared state is an edge, and "
	                      "one that spans files (LLR-SDG-03, HLR-074)");
	cr_assert_str_eq(e->global, "shared",
	                 "the edge names the object, and the name is the "
	                 "graph's own copy — the facts are released before "
	                 "anything reads it");

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, an_undeclared_name_is_not_global_state)
{
	const char  *a[]     = { "writer", "reader" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	/* Written and read, but declared nowhere: an ordinary local, which
	 * the query files capture because they cannot tell the difference.
	 * Deciding it here is the whole reason the parse records facts. */
	add_global(fa, "temp", 0, GLOBAL_WRITE);
	add_global(fa, "temp", 1, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(g.edge_count, 0,
	             "a local named in two functions is not shared state");

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, two_objects_between_one_pair_are_two_edges)
{
	const char  *a[]     = { "writer", "reader" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	add_global(fa, "one", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "two", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "one", 0, GLOBAL_WRITE);
	add_global(fa, "two", 0, GLOBAL_WRITE);
	add_global(fa, "one", 1, GLOBAL_READ);
	add_global(fa, "two", 1, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(g.edge_count, 2,
	             "call edges collapse, global edges do not: they are per "
	             "object, and merging them would lose which state couples "
	             "the two functions");

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* --------------------------------------------------------- address taken */

Test(graph, an_address_taken_function_is_marked)
{
	const char  *a[]     = { "handler", "other" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	add_address_taken(fa, "handler");
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert(g.nodes[node_of(&g, "handler")].address_taken,
	          "captured here though Phase 10 is what consumes it "
	          "(HLR-096)");
	cr_assert_not(g.nodes[node_of(&g, "other")].address_taken);

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, an_address_taken_name_that_is_not_a_function_is_discarded)
{
	const char  *a[]     = { "real" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	/* The query files capture identifiers in value position, most of which
	 * are variables. That over-capture is safe precisely because this
	 * resolution step drops what is not a function — which is why the
	 * queries do not have to decide, and could not. */
	add_address_taken(fa, "some_variable");
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_not(g.nodes[node_of(&g, "real")].address_taken);

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ------------------------------------------------------- the projection */

Test(graph, the_component_projection_is_file_level)
{
	const char  *a[]     = { "caller" };
	const char  *b[]     = { "callee" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1),
	                         file_with("/p/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	add_call(fa, "callee", 0, 3);
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(g.component_count, 2, "a component is a source file "
	                                   "(HLR-114)");
	cr_assert_eq(g.component_edge_count, 1);
	cr_assert_eq(g.component_edges[0].from, 0);
	cr_assert_eq(g.component_edges[0].to, 1);

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, a_call_within_one_file_is_no_component_dependency)
{
	const char  *a[]     = { "caller", "callee" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	add_call(fa, "callee", 0, 3);
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(g.component_edge_count, 0,
	             "a file does not depend on itself, and counting it as "
	             "one would give every file a coupling it does not have");

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* -------------------------------------------------------------- teardown */

Test(graph, an_empty_project_builds_an_empty_graph)
{
	Report   report = report_of(NULL, 0);
	FactList facts  = { 0 };
	Sdg      g      = { 0 };

	cr_assert_eq(graph_build(&facts, &report, &g), 0,
	             "a run that analysed nothing still builds (HLR-066)");
	cr_assert_eq(g.node_count, 0);
	cr_assert_eq(g.edge_count, 0);
	cr_assert_not_null(g.graph, "the library's structure exists even so");

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, the_library_returns_errors_instead_of_aborting)
{
	/* igraph's default error handler calls abort(). Every return-value
	 * check in graph.c is unreachable until that is changed, and from
	 * Phase 9 the consequence is worse than unreachable code: detecting
	 * recursion means asking for a topological sort of a cyclic graph,
	 * which is an error return — and would be a crash.
	 *
	 * Reaching the assertion is the result. If the handler is not
	 * installed, this test dies rather than fails, which Criterion
	 * reports as a crash against this name.
	 */
	const char  *a[]     = { "ping", "pong" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };

	add_call(fa, "pong", 0, 3);
	add_call(fa, "ping", 1, 9);
	cr_assert_eq(factlist_add(&facts, fa), 0);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	igraph_vector_int_t order;

	cr_assert_eq(igraph_vector_int_init(&order, 0), IGRAPH_SUCCESS);
	cr_assert_neq(igraph_topological_sorting((igraph_t *)g.graph, &order,
	                                         IGRAPH_OUT),
	              IGRAPH_SUCCESS,
	              "a cyclic graph has no topological order, and saying so "
	              "must be a return value rather than an abort");
	igraph_vector_int_destroy(&order);

	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(graph, graph_free_is_safe_on_null_and_twice)
{
	Sdg g = { 0 };

	graph_free(NULL);
	graph_free(&g);
	graph_free(&g);
	cr_assert_eq(graph_unresolved_count(NULL), 0);
	cr_assert(1, "teardown is unconditional on every exit path");
}
