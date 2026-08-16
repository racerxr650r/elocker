/* test/unit/calltree.c — unit tests for src/calltree.c.
 *
 * Built from hand-made facts and reports, as the graph tests are: the
 * measurement rules are what is under test, and a fixture that had to be
 * parsed first would put a query file's behaviour into every assertion about
 * them. The `calltree/` fixture group covers the source-to-report path.
 */

#include <criterion/criterion.h>
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
