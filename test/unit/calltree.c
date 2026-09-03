/* test/unit/calltree.c — unit tests for src/calltree.c.
 *
 * Built from hand-made facts and reports, as the graph tests are: the
 * measurement rules are what is under test, and a fixture that had to be
 * parsed first would put a query file's behaviour into every assertion about
 * them. The `calltree/` fixture group covers the source-to-report path.
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "calltree.h"
#include "elc.h"
#include "graph.h"
#include "report.h"

/* ------------------------------------------------------------ scaffolding */

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

static void add_call(FileFacts *f, const char *callee, size_t caller)
{
	f->calls = realloc(f->calls, (f->call_count + 1) * sizeof *f->calls);
	cr_assert_not_null(f->calls);
	f->calls[f->call_count].callee = strdup(callee);
	cr_assert_not_null(f->calls[f->call_count].callee);
	f->calls[f->call_count].caller = caller;
	f->calls[f->call_count].line   = 1;
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

static size_t node_of(const Sdg *g, const char *name)
{
	for (size_t i = 0; i < g->node_count; i++)
		if (strcmp(g->nodes[i].name, name) == 0)
			return i;
	return SIZE_MAX;
}

/* Options declaring the given entry-point symbols. */
static ElcOptions entries_of(const char **names, size_t count)
{
	ElcOptions opts = { 0 };

	opts.entry_points      = names;
	opts.entry_point_count = count;
	return opts;
}

/* Set every function's ELOC in a hand-made file, so that a Henry-Kafura value
 * can be checked against a length the test chose. */
static void set_eloc(FileMetrics *m, uint32_t eloc)
{
	for (size_t i = 0; i < m->function_count; i++)
		m->functions[i].eloc = eloc;
}

/* ---------------------------------------------------------------- fan-out */

Test(calltree, fan_out_counts_distinct_callees)
{
	const char  *a[]     = { "caller", "one", "two" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 3) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	ElcOptions   opts    = { 0 };

	/* Four call sites, two callees. Fan-out is the count of things
	 * invoked, not of times they are invoked (HLR-085). */
	add_call(fa, "one", 0);
	add_call(fa, "one", 0);
	add_call(fa, "one", 0);
	add_call(fa, "two", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.fan_out[node_of(&g, "caller")], 2);
	cr_assert_eq(tree.fan_out[node_of(&g, "one")], 0);

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, fan_out_ignores_global_edges)
{
	const char  *a[]     = { "writer", "reader" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	ElcOptions   opts    = { 0 };

	/* A global links the two, and no call does. Fan-out counts
	 * subroutines invoked; coupling through shared state is a different
	 * fact and must not be counted as a call. */
	add_global(fa, "shared", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "shared", 0, GLOBAL_WRITE);
	add_global(fa, "shared", 1, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_gt(g.edge_count, 0, "the global edge exists");

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.fan_out[node_of(&g, "writer")], 0,
	             "writing a global that another function reads is not "
	             "calling it");

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ----------------------------------------------------------------- fan-in */

Test(calltree, fan_in_counts_distinct_callers)
{
	const char  *a[]     = { "one", "two", "shared" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 3) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	ElcOptions   opts    = { 0 };

	/* Three call sites, two callers. Fan-in is the count of functions
	 * that invoke it, not of times they do (HLR-156). */
	add_call(fa, "shared", 0);
	add_call(fa, "shared", 0);
	add_call(fa, "shared", 1);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.fan_in[node_of(&g, "shared")], 2);
	cr_assert_eq(tree.fan_in[node_of(&g, "one")], 0,
	             "a function nothing calls has fan-in zero");

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, fan_in_ignores_global_edges)
{
	const char  *a[]     = { "writer", "reader" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	ElcOptions   opts    = { 0 };

	/* The converse of fan_out_ignores_global_edges, and the one with more
	 * riding on it: the global edge runs *towards* the reader, so an
	 * in-degree taken over the whole SDG would inflate exactly the figure
	 * the Henry-Kafura value then squares (LLR-CTR-07, HLR-157). */
	add_global(fa, "shared", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "shared", 0, GLOBAL_WRITE);
	add_global(fa, "shared", 1, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_gt(g.edge_count, 0, "the global edge exists");

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.fan_in[node_of(&g, "reader")], 0,
	             "reading a global another function writes is not being "
	             "called by it");

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ----------------------------------------------------- information flow -- */

Test(calltree, the_degrees_are_counted_over_a_wide_hub)
{
	/* Both degrees at a size no fixture tree reaches, and at a size the
	 * bands care about: 300 callers is twelve times the fan-in warning
	 * bound and 300 callees twenty times the fan-out critical one, so a
	 * counting error here would move a finding rather than merely a figure
	 * (HLR-085, HLR-156).
	 *
	 * 300 callers and 300 callees around one hub, which is 601 functions
	 * and 600 call edges — cheap to build and impossible to write by hand.
	 */
	enum { WIDTH = 300 };

	const char **names = calloc(WIDTH * 2 + 1, sizeof *names);
	char        *owned[WIDTH * 2];
	FileMetrics *files[1];
	FactList     facts = { 0 };
	FileFacts   *fa    = facts_for("/p/a.c");
	Sdg          g     = { 0 };
	TreeResults  tree  = { 0 };
	ElcOptions   opts  = { 0 };
	Report       report;

	cr_assert_not_null(names);
	names[0] = "hub";
	for (int i = 0; i < WIDTH * 2; i++) {
		char buf[32];

		snprintf(buf, sizeof buf, "%s%d", i < WIDTH ? "in" : "out",
		         i % WIDTH);
		owned[i]       = strdup(buf);
		cr_assert_not_null(owned[i]);
		names[i + 1]   = owned[i];
	}

	files[0] = file_with("/p/a.c", names, WIDTH * 2 + 1);
	set_eloc(files[0], 7);
	report = report_of(files, 1);

	/* Node 0 is `hub`; nodes 1..WIDTH call it, and it calls the rest. */
	for (int i = 0; i < WIDTH; i++) {
		add_call(fa, "hub", (size_t)(i + 1));
		add_call(fa, owned[WIDTH + i], 0);
	}
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);

	size_t hub = node_of(&g, "hub");

	cr_assert_eq(tree.fan_in[hub], WIDTH);
	cr_assert_eq(tree.fan_out[hub], WIDTH);

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
	for (int i = 0; i < WIDTH * 2; i++)
		free(owned[i]);
	free(names);
}

/* -------------------------------------------------------------- recursion */

Test(calltree, direct_recursion_is_detected)
{
	const char  *a[]     = { "fact", "other" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	ElcOptions   opts    = { 0 };

	add_call(fa, "fact", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.cycle_count, 1, "a self-call is a cycle (HLR-089)");
	cr_assert_eq(tree.cycles[0].count, 1);
	cr_assert_eq(tree.cycles[0].members[0], node_of(&g, "fact"));

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, mutual_recursion_is_detected)
{
	const char  *a[]     = { "ping", "pong" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	ElcOptions   opts    = { 0 };

	add_call(fa, "pong", 0);
	add_call(fa, "ping", 1);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.cycle_count, 1,
	             "a mutually recursive pair is one cycle, not two");
	cr_assert_eq(tree.cycles[0].count, 2);

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, a_straight_line_program_has_no_cycles)
{
	const char  *a[]     = { "top", "middle", "bottom" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 3) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	ElcOptions   opts    = { 0 };

	add_call(fa, "middle", 0);
	add_call(fa, "bottom", 1);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.cycle_count, 0,
	             "every function is its own trivial component; reporting "
	             "those would call every program recursive");

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, a_global_cycle_is_not_recursion)
{
	const char  *a[]     = { "one", "two" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	ElcOptions   opts    = { 0 };

	/* Each writes a global the other reads, so the *SDG* has a cycle
	 * through global edges. That is shared state, not recursion: neither
	 * function calls anything, and reporting it under MISRA C Rule 17.2
	 * would be a false critical finding on ordinary code. */
	add_global(fa, "x", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "y", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "x", 0, GLOBAL_WRITE);
	add_global(fa, "x", 1, GLOBAL_READ);
	add_global(fa, "y", 1, GLOBAL_WRITE);
	add_global(fa, "y", 0, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(g.edge_count, 2, "the two global edges form a cycle");

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.cycle_count, 0,
	             "recursion is about calls; the call view is what the "
	             "decomposition must run over");

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ------------------------------------------------------------------ depth */

Test(calltree, depth_is_the_length_of_the_deepest_chain)
{
	const char  *a[]     = { "main", "parse", "lex" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 3) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	const char  *names[] = { "main" };
	ElcOptions   opts    = entries_of(names, 1);

	add_call(fa, "parse", 0);
	add_call(fa, "lex", 1);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.depth_state, DEPTH_MEASURED);
	cr_assert_eq(tree.depth, 3, "three nested layers, counting the entry "
	                            "point itself");

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, the_deepest_chain_is_an_ordered_sequence)
{
	const char  *a[]     = { "main", "shallow", "deep", "deeper" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 4) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	const char  *names[] = { "main" };
	ElcOptions   opts    = entries_of(names, 1);

	/* Two branches from main; the deeper one is what must be reported,
	 * and in order (HLR-088, LLR-LPD-03). */
	add_call(fa, "shallow", 0);
	add_call(fa, "deep", 0);
	add_call(fa, "deeper", 2);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.deepest.count, 3);
	cr_assert_eq(tree.deepest.nodes[0], node_of(&g, "main"));
	cr_assert_eq(tree.deepest.nodes[1], node_of(&g, "deep"),
	             "the branch taken is the deeper one, not the first");
	cr_assert_eq(tree.deepest.nodes[2], node_of(&g, "deeper"));

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, depth_measures_from_the_deepest_entry_point)
{
	const char  *a[]     = { "isr", "main", "one", "two" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 4) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	const char  *names[] = { "isr", "main" };
	ElcOptions   opts    = entries_of(names, 2);

	/* isr is shallow, main is deep. With both declared, the answer is the
	 * worst case across them. */
	add_call(fa, "one", 1);
	add_call(fa, "two", 2);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.depth, 3);
	cr_assert_eq(tree.deepest.nodes[0], node_of(&g, "main"));

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, recursion_yields_no_depth_and_terminates)
{
	const char  *a[]     = { "main", "ping", "pong" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 3) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	const char  *names[] = { "main" };
	ElcOptions   opts    = entries_of(names, 1);

	/* Reaching the assertion is half the result: a longest-path search
	 * over a cyclic graph does not terminate, which is why acyclicity is
	 * established first rather than discovered (HLR-090, LLR-CTR-04). */
	add_call(fa, "ping", 0);
	add_call(fa, "pong", 1);
	add_call(fa, "ping", 2);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.depth_state, DEPTH_UNBOUNDED_RECURSION);
	cr_assert_eq(tree.depth, 0, "no finite number is invented");
	cr_assert_eq(tree.deepest.count, 0, "and no chain claimed");
	cr_assert_gt(tree.cycle_count, 0, "the cycle is reported instead");

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, no_entry_points_omits_depth_with_a_reason)
{
	const char  *a[]     = { "main", "helper" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	ElcOptions   opts    = { 0 };

	add_call(fa, "helper", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0,
	             "an absent declaration is not an error (HLR-115)");
	cr_assert_eq(tree.depth_state, DEPTH_OMITTED_NO_ENTRY_POINTS,
	             "and `main` is not guessed at, however obvious it looks");
	cr_assert_eq(tree.deepest.count, 0);

	/* Fan-out and recursion do not depend on a declaration, so they are
	 * still measured — omitting one analysis must not omit the others. */
	cr_assert_eq(tree.node_count, 2);

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, an_entry_point_matching_nothing_is_a_distinct_omission)
{
	const char  *a[]     = { "helper" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	const char  *names[] = { "main" };
	ElcOptions   opts    = entries_of(names, 1);

	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.depth_state, DEPTH_OMITTED_ENTRY_UNRESOLVED,
	             "\"you declared nothing\" and \"what you declared is not "
	             "here\" call for different actions, so they are "
	             "different outcomes");

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, depth_carries_the_unresolved_count)
{
	const char  *a[]     = { "main" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	const char  *names[] = { "main" };
	ElcOptions   opts    = entries_of(names, 1);

	/* A library call the graph cannot follow. The depth is therefore a
	 * lower bound, and the count travels with it so the reader can judge
	 * how much of the program the number covers (HLR-087, LLR-CTR-06). */
	add_call(fa, "printf", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.depth, 1);
	cr_assert_eq(tree.unresolved_calls, 1);

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, a_chain_does_not_travel_along_a_global_edge)
{
	const char  *a[]     = { "main", "writer", "reader" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 3) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	TreeResults  tree    = { 0 };
	const char  *names[] = { "main" };
	ElcOptions   opts    = entries_of(names, 1);

	/* main calls writer; writer shares a global with reader. The call
	 * chain is two functions long, not three — reader is not called by
	 * anything, and stack depth is about calls. */
	add_call(fa, "writer", 0);
	add_global(fa, "shared", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "shared", 1, GLOBAL_WRITE);
	add_global(fa, "shared", 2, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_eq(tree.depth, 2);
	cr_assert_eq(tree.deepest.count, 2);

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, an_empty_project_measures_nothing_without_failing)
{
	Report      report = report_of(NULL, 0);
	FactList    facts  = { 0 };
	Sdg         g      = { 0 };
	TreeResults tree   = { 0 };
	ElcOptions  opts   = { 0 };

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0,
	             "a run that analysed nothing still produces a model "
	             "(HLR-066)");
	cr_assert_eq(tree.node_count, 0);
	cr_assert_eq(tree.cycle_count, 0);

	tree_results_free(&tree);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(calltree, tree_results_free_is_safe_on_null_and_twice)
{
	TreeResults tree = { 0 };

	tree_results_free(NULL);
	tree_results_free(&tree);
	tree_results_free(&tree);
	cr_assert(1, "teardown is unconditional on every exit path");
}

/* --------------------------------- the weighted fan-out and the index --
 *
 * The weighted degree needs a graph, because it is a sum over edges. The
 * index does not: it is a pure function of three numbers, and testing it
 * through a graph would put the graph's behaviour into every assertion about
 * the arithmetic. So these are two groups, and only the first builds one.
 */
#define TBI_EPSILON 1e-9

/* Verifies LLR-CTR-13: the weighted degree sums what the edges point at,
 * where the plain degree counts the edges. */
Test(calltree, weighted_fan_out_sums_the_burden_of_what_a_function_calls)
{
	static const char *const names[] = { "caller", "a", "b", "c", "d" };
	FileMetrics  *m     = file_with("/t/a.c", names, 5);
	FileMetrics  *files[1];
	FileFacts    *f     = facts_for("/t/a.c");
	FactList      facts = { 0 };
	Report        report;
	Sdg           g     = { 0 };
	TreeResults   tree  = { 0 };
	ElcOptions    opts  = { 0 };
	size_t        caller;

	/* Four callees, each costing the base tax and nothing more. */
	for (size_t i = 1; i < 5; i++)
		m->functions[i].mock_burden = 0.25;

	files[0] = m;
	report   = report_of(files, 1);

	add_call(f, "a", 0);
	add_call(f, "b", 0);
	add_call(f, "c", 0);
	add_call(f, "d", 0);
	facts.items = &f;
	facts.count = 1;

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);

	caller = node_of(&g, "caller");
	cr_assert_neq(caller, SIZE_MAX);
	cr_assert_eq(tree.fan_out[caller], 4,
	             "four edges leave the caller");
	cr_assert_float_eq(tree.wf_out[caller], 1.00, TBI_EPSILON,
	             "and they weigh a quarter each, which is one — not four, "
	             "and not zero, which is what an integer accumulator "
	             "would have reported");

	tree_results_free(&tree);
	graph_free(&g);
	report_free(&report);
	filefacts_free(f);
}

/* Verifies LLR-CTR-13: an unresolvable call produces no edge, so it reaches
 * neither degree. This is where the consequence HLR-222 names is pinned
 * down — a project calling the C library reads as cheaper to test than one
 * wrapping it, because the first set of calls leaves the graph. */
Test(calltree, an_unresolved_call_contributes_to_neither_degree)
{
	static const char *const names[] = { "caller", "a" };
	FileMetrics  *m     = file_with("/t/a.c", names, 2);
	FileMetrics  *files[1];
	FileFacts    *f     = facts_for("/t/a.c");
	FactList      facts = { 0 };
	Report        report;
	Sdg           g     = { 0 };
	TreeResults   tree  = { 0 };
	ElcOptions    opts  = { 0 };
	size_t        caller;

	m->functions[1].mock_burden = 0.25;
	files[0] = m;
	report   = report_of(files, 1);

	add_call(f, "a", 0);
	add_call(f, "memcpy", 0);   /* nothing analysed defines it */
	facts.items = &f;
	facts.count = 1;

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);

	caller = node_of(&g, "caller");
	cr_assert_eq(tree.fan_out[caller], 1);
	cr_assert_float_eq(tree.wf_out[caller], 0.25, TBI_EPSILON,
	             "the library call weighs nothing because it is not an "
	             "edge, which is the same reason it is not counted");

	tree_results_free(&tree);
	graph_free(&g);
	report_free(&report);
	filefacts_free(f);
}

/* Verifies LLR-CTR-13: the accumulator's type is the whole of the risk. An
 * integer one would truncate every callee below 1.0 and report the entire
 * lower half of the range as no burden at all. */
Test(calltree, fractional_burdens_accumulate_without_being_rounded)
{
	static const char *const names[] = { "caller", "a", "b" };
	FileMetrics  *m     = file_with("/t/a.c", names, 3);
	FileMetrics  *files[1];
	FileFacts    *f     = facts_for("/t/a.c");
	FactList      facts = { 0 };
	Report        report;
	Sdg           g     = { 0 };
	TreeResults   tree  = { 0 };
	ElcOptions    opts  = { 0 };

	m->functions[1].mock_burden = 0.35;
	m->functions[2].mock_burden = 0.40;
	files[0] = m;
	report   = report_of(files, 1);

	add_call(f, "a", 0);
	add_call(f, "b", 0);
	facts.items = &f;
	facts.count = 1;

	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(calltree_analyse(&g, &opts, &tree), 0);
	cr_assert_float_eq(tree.wf_out[node_of(&g, "caller")], 0.75,
	                   TBI_EPSILON,
	                   "two callees under one apiece still sum to more "
	                   "than either");

	tree_results_free(&tree);
	graph_free(&g);
	report_free(&report);
	filefacts_free(f);
}

/* Verifies LLR-TBI-01: the case the index exists for. A function many others
 * call but which calls nothing needs no mocks, so its burden is its logic.
 *
 * These are `diag_printf`'s own figures: fan-in 82, weighted fan-out 0,
 * complexity 2. The Adapted Maintainability Index scores that function 51 and
 * calls it a rigid, fragile monolith; this index scores it 2. The two
 * disagreeing about it is the intended outcome and not a defect in either.
 */
Test(calltree, a_widely_shared_leaf_collapses_to_its_cyclomatic_complexity)
{
	cr_assert_float_eq(calltree_burden(2, 82, 0.0), 2.0, TBI_EPSILON);
}

/* Verifies LLR-TBI-01: the mirror of the leaf, asserted separately because a
 * formula taking the *larger* degree would pass the leaf case and fail this
 * one. */
Test(calltree, a_coordinator_called_from_one_place_collapses_likewise)
{
	cr_assert_float_eq(calltree_burden(4, 1, 12.0), 8.0, TBI_EPSILON);
}

/* Verifies LLR-TBI-01: the God Object is the one shape this index condemns,
 * and this asserts it is the only one. */
Test(calltree, only_a_function_large_in_both_degrees_produces_a_large_index)
{
	cr_assert_float_eq(calltree_burden(5, 10, 9.0), 50.0, TBI_EPSILON,
	                   "large in both: over the critical bound");
	cr_assert_float_eq(calltree_burden(5, 10, 0.0), 5.0, TBI_EPSILON,
	                   "large in one only: nowhere near it");
	cr_assert_float_eq(calltree_burden(5, 0, 9.0), 5.0, TBI_EPSILON,
	                   "large in the other only: likewise");
}

/* Verifies LLR-TBI-01: the count is widened to compare, not the weight
 * truncated. Truncating would make every weighted fan-out below 1.0 compare
 * equal to zero and return the bare complexity across the whole lower half of
 * the range — a defect no assertion about ordering would catch. */
Test(calltree, the_degrees_are_compared_without_truncating_the_weight)
{
	cr_assert_float_eq(calltree_burden(4, 3, 0.75), 7.0, TBI_EPSILON,
	                   "0.75 is the lesser degree and must be used as "
	                   "0.75, not as 0");
}

/* Verifies LLR-TBI-02: a pure function of its three arguments, reading no
 * graph, so the figure the report prints and the figure the catalogue bands
 * are produced by one call and cannot diverge. */
Test(calltree, the_index_is_a_pure_function_of_its_three_measurements)
{
	cr_assert_float_eq(calltree_burden(7, 4, 2.5), calltree_burden(7, 4, 2.5),
	                   TBI_EPSILON);
	cr_assert_float_eq(calltree_burden(0, 9, 9.0), 0.0, TBI_EPSILON,
	                   "a complexity of zero falls out of the "
	                   "multiplication rather than needing a guard");
}
