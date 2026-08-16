/* test/unit/state.c — unit tests for src/state.c.
 *
 * Built from hand-made facts and reports, as the graph and call-tree tests
 * are: the traversal rules are what is under test, and a fixture that had to
 * be parsed first would put a query file's behaviour into every assertion
 * about them. The `reachability/` fixture group covers the source-to-report
 * path.
 *
 * Several of these assert an *absence*, and that is the point of the module.
 * A false claim of death invites deleting code that runs, so the tests that
 * pin what must **not** be reported are the load-bearing half.
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "cli.h"
#include "elc.h"
#include "graph.h"
#include "report.h"
#include "state.h"

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

static ElcOptions entries_of(const char **names, size_t count)
{
	ElcOptions opts = { 0 };

	opts.entry_points      = names;
	opts.entry_point_count = count;
	return opts;
}

static bool is_unreachable(const StateResults *s, const Sdg *g,
                           const char *name)
{
	size_t node = node_of(g, name);

	for (size_t i = 0; i < s->unreachable_count; i++)
		if (s->unreachable[i] == node)
			return true;
	return false;
}

static const GlobalRow *row_of(const StateResults *s, const char *object)
{
	for (size_t i = 0; i < s->global_count; i++)
		if (strcmp(s->globals[i].object, object) == 0)
			return &s->globals[i];
	return NULL;
}

/* --------------------------------------------------------- the root set -- */

Test(state, roots_are_entry_points_and_address_taken_functions)
{
	const char  *names[] = { "entry", "handler", "plain" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 3) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	const char  *decl[]  = { "entry" };
	ElcOptions   opts    = entries_of(decl, 1);
	uint32_t    *roots   = NULL;
	size_t       count   = 0;
	size_t       resolved = 0;

	add_address_taken(fa, "handler");
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(collect_roots(&g, &opts, &roots, &count, &resolved), 0);

	/* The union, and nothing else: `plain` is neither declared nor
	 * address-taken (LLR-RTS-01). */
	cr_assert_eq(count, 2);
	cr_assert_eq(resolved, 1);

	free(roots);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, a_declared_symbol_naming_nothing_is_skipped_not_fatal)
{
	const char  *names[] = { "entry" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	Sdg          g       = { 0 };
	const char  *decl[]  = { "entry", "absent" };
	ElcOptions   opts    = entries_of(decl, 2);
	uint32_t    *roots   = NULL;
	size_t       count   = 0;
	size_t       resolved = 0;

	cr_assert_eq(factlist_add(&facts, facts_for("/p/a.c")), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	/* Analysing one directory of a project whose entry point lives in
	 * another is ordinary; failing the run would make it unusable. */
	cr_assert_eq(collect_roots(&g, &opts, &roots, &count, &resolved), 0);
	cr_assert_eq(count, 1);
	cr_assert_eq(resolved, 1);

	free(roots);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, a_root_declared_twice_is_one_root)
{
	const char  *names[] = { "entry" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	const char  *decl[]  = { "entry", "entry" };
	ElcOptions   opts    = entries_of(decl, 2);
	uint32_t    *roots   = NULL;
	size_t       count   = 0;
	size_t       resolved = 0;

	/* Declared twice *and* address-taken: three ways to the same node. */
	add_address_taken(fa, "entry");
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(collect_roots(&g, &opts, &roots, &count, &resolved), 0);
	cr_assert_eq(count, 1);

	free(roots);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ------------------------------------------------------------ traversal -- */

Test(state, a_clique_of_unused_functions_is_unreachable)
{
	const char  *names[] = { "entry", "clique_a", "clique_b" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 3) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	const char  *decl[]  = { "entry" };
	ElcOptions   opts    = entries_of(decl, 1);
	StateResults state   = { 0 };

	/* Each has a caller, so a "no caller" rule finds neither. No path
	 * reaches the pair from the root, so traversal finds both (HLR-097). */
	add_call(fa, "clique_b", 1);
	add_call(fa, "clique_a", 2);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.reach_state, REACH_MEASURED);
	cr_assert(is_unreachable(&state, &g, "clique_a"));
	cr_assert(is_unreachable(&state, &g, "clique_b"));
	cr_assert_not(is_unreachable(&state, &g, "entry"));

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, an_address_taken_function_and_its_callees_are_reachable)
{
	const char  *names[] = { "entry", "handler", "helper" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 3) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	const char  *decl[]  = { "entry" };
	ElcOptions   opts    = entries_of(decl, 1);
	StateResults state   = { 0 };

	/* `handler` is never called by name; `helper` is called only by it.
	 * An implementation that merely *excluded* address-taken functions
	 * from the report rather than traversing from them would report
	 * `helper` dead, which is a false claim of exactly the kind HLR-096
	 * exists to prevent. */
	add_address_taken(fa, "handler");
	add_call(fa, "helper", 1);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_not(is_unreachable(&state, &g, "handler"));
	cr_assert_not(is_unreachable(&state, &g, "helper"));

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, reachability_does_not_travel_along_a_global_edge)
{
	const char  *names[] = { "entry", "orphan_reader" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	const char  *decl[]  = { "entry" };
	ElcOptions   opts    = entries_of(decl, 1);
	StateResults state   = { 0 };

	/* `entry` writes an object `orphan_reader` reads. That is coupling,
	 * not a call: control never travels along the edge, so the reader has
	 * not been reached. Following it would quietly rescue genuinely dead
	 * code from the report. */
	add_global(fa, "flag", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "flag", 0, GLOBAL_WRITE);
	add_global(fa, "flag", 1, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert(is_unreachable(&state, &g, "orphan_reader"));

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, with_no_entry_points_nothing_is_unreachable)
{
	const char  *names[] = { "one", "two" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	Sdg          g       = { 0 };
	ElcOptions   opts    = { 0 };
	StateResults state   = { 0 };

	cr_assert_eq(factlist_add(&facts, facts_for("/p/a.c")), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	/* Not "everything is dead" — the analysis did not run (HLR-115). */
	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.reach_state, REACH_OMITTED_NO_ENTRY_POINTS);
	cr_assert_eq(state.unreachable_count, 0);

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, a_declaration_matching_nothing_is_its_own_omission)
{
	const char  *names[] = { "one" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	Sdg          g       = { 0 };
	const char  *decl[]  = { "absent" };
	ElcOptions   opts    = entries_of(decl, 1);
	StateResults state   = { 0 };

	cr_assert_eq(factlist_add(&facts, facts_for("/p/a.c")), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.reach_state, REACH_OMITTED_ENTRY_UNRESOLVED);
	cr_assert_eq(state.unreachable_count, 0);

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* --------------------------------------------------------- global state -- */

Test(state, a_global_touched_by_one_function_is_a_scope_reduction_candidate)
{
	const char  *names[] = { "owner", "bystander" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = { 0 };
	StateResults state   = { 0 };

	/* One function writes and reads it, so no writer-to-reader edge
	 * exists anywhere in the graph. An analysis reading the edge table
	 * would find no candidate at all (HLR-092). */
	add_global(fa, "owned", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "owned", 0, GLOBAL_WRITE);
	add_global(fa, "owned", 0, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);
	cr_assert_eq(g.edge_count, 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);

	const GlobalRow *row = row_of(&state, "owned");

	cr_assert_not_null(row);
	cr_assert_eq(row->verdict, GLOBAL_SCOPE_REDUCTION);
	cr_assert_eq(row->toucher_count, 1);
	cr_assert(row->touchers[0].writes);
	cr_assert(row->touchers[0].reads);

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, a_global_spanning_disconnected_regions_is_a_hidden_channel)
{
	const char  *names[] = { "island_a", "island_b" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = { 0 };
	StateResults state   = { 0 };

	add_global(fa, "channel", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "channel", 0, GLOBAL_WRITE);
	add_global(fa, "channel", 1, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);

	const GlobalRow *row = row_of(&state, "channel");

	cr_assert_not_null(row);
	cr_assert_eq(row->verdict, GLOBAL_HIDDEN_CHANNEL);
	cr_assert_eq(row->region_count, 2);

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, shared_state_within_one_region_is_ordinary)
{
	const char  *names[] = { "producer", "consumer" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = { 0 };
	StateResults state   = { 0 };

	/* The one difference from the case above: a call edge joins the two,
	 * so they lie in one region and the sharing is a design. Without this
	 * distinction the finding would fire on every shared variable. */
	add_call(fa, "consumer", 0);
	add_global(fa, "shared", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "shared", 0, GLOBAL_WRITE);
	add_global(fa, "shared", 1, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);

	const GlobalRow *row = row_of(&state, "shared");

	cr_assert_not_null(row);
	cr_assert_eq(row->verdict, GLOBAL_ORDINARY);
	cr_assert_eq(row->region_count, 1);

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, an_undeclared_identifier_is_not_global_state)
{
	const char  *names[] = { "one" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = { 0 };
	StateResults state   = { 0 };

	/* The read and write patterns are deliberately over-broad in every
	 * language module, because one file cannot tell a global from a local.
	 * A name no file declares at file scope is not global state. */
	add_global(fa, "a_local", 0, GLOBAL_WRITE);
	add_global(fa, "a_local", 0, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.global_count, 0);

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ---------------------------------------------------- unreachable data -- */

Test(state, a_global_touched_only_by_dead_functions_is_unreachable)
{
	const char  *names[] = { "entry", "dead_writer" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	const char  *decl[]  = { "entry" };
	ElcOptions   opts    = entries_of(decl, 1);
	StateResults state   = { 0 };

	add_global(fa, "orphaned", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "live", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "orphaned", 1, GLOBAL_WRITE);
	add_global(fa, "live", 0, GLOBAL_WRITE);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.dead_global_count, 1);
	cr_assert_str_eq(state.dead_globals[0], "orphaned");

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, a_global_no_function_touches_is_not_claimed_dead)
{
	const char  *names[] = { "entry" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	const char  *decl[]  = { "entry" };
	ElcOptions   opts    = entries_of(decl, 1);
	StateResults state   = { 0 };

	/* Declared and never touched by an analysed function. It may be
	 * written from file scope or from a translation unit outside the
	 * target, and the asymmetry that governs the functions governs the
	 * storage: an object wrongly called dead invites deleting memory
	 * something writes (LLR-UGL-01). */
	add_global(fa, "untouched", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.dead_global_count, 0);

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* -------------------------------------------------------- scope isolation */

Test(state, an_edge_crossing_a_declared_boundary_is_reported)
{
	const char  *a[]     = { "host_fn" };
	const char  *b[]     = { "target_fn" };
	FileMetrics *files[] = { file_with("/p/host/a.c", a, 1),
	                         file_with("/p/target/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/host/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = { 0 };
	StateResults state   = { 0 };

	add_call(fa, "target_fn", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, facts_for("/p/target/b.c")), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(parse_scope("host:/p/host/*", &opts), 0);
	cr_assert_eq(parse_scope("target:/p/target/*", &opts), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.scope_state, SCOPES_MEASURED);
	cr_assert_eq(state.violation_count, 1);
	cr_assert_eq(state.violations[0].kind, EDGE_CALL);

	state_results_free(&state);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, a_shared_global_crossing_a_boundary_is_reported)
{
	const char  *a[]     = { "host_fn" };
	const char  *b[]     = { "target_fn" };
	FileMetrics *files[] = { file_with("/p/host/a.c", a, 1),
	                         file_with("/p/target/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/host/a.c");
	FileFacts   *fb      = facts_for("/p/target/b.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = { 0 };
	StateResults state   = { 0 };

	/* No call between them at all. A scope that only shares a variable has
	 * not been isolated, and an implementation checking call edges alone
	 * would report this arrangement clean (HLR-094). */
	add_global(fa, "mailbox", ELC_NO_FUNCTION, GLOBAL_DECLARATION);
	add_global(fa, "mailbox", 0, GLOBAL_WRITE);
	add_global(fb, "mailbox", 0, GLOBAL_READ);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, fb), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(parse_scope("host:/p/host/*", &opts), 0);
	cr_assert_eq(parse_scope("target:/p/target/*", &opts), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.violation_count, 1);
	cr_assert_eq(state.violations[0].kind, EDGE_GLOBAL);
	cr_assert_str_eq(state.violations[0].object, "mailbox");

	state_results_free(&state);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, a_component_matching_no_declaration_is_outside_the_partition)
{
	const char  *a[]     = { "host_fn" };
	const char  *b[]     = { "other_fn" };
	FileMetrics *files[] = { file_with("/p/host/a.c", a, 1),
	                         file_with("/p/unnamed/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/host/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = { 0 };
	StateResults state   = { 0 };

	/* The user said nothing about `/p/unnamed`, so inventing a boundary
	 * around it would report a violation against a partition nobody drew. */
	add_call(fa, "other_fn", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, facts_for("/p/unnamed/b.c")), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(parse_scope("host:/p/host/*", &opts), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.violation_count, 0);

	state_results_free(&state);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(state, with_no_scopes_declared_the_analysis_is_omitted)
{
	const char  *a[]     = { "one" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	Sdg          g       = { 0 };
	ElcOptions   opts    = { 0 };
	StateResults state   = { 0 };

	cr_assert_eq(factlist_add(&facts, facts_for("/p/a.c")), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.scope_state, SCOPES_OMITTED_NONE_DECLARED);
	cr_assert_eq(state.violation_count, 0);

	state_results_free(&state);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ------------------------------------------------------- the empty graph -- */

Test(state, an_empty_graph_analyses_without_incident)
{
	Sdg          g     = { 0 };
	ElcOptions   opts  = { 0 };
	StateResults state = { 0 };
	FactList     facts = { 0 };
	Report       report = { 0 };
	MetricsAccumulator acc = { 0 };

	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(state_analyse(&g, &opts, &state), 0);
	cr_assert_eq(state.global_count, 0);
	cr_assert_eq(state.unreachable_count, 0);

	state_results_free(&state);
	metrics_free(&acc);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}
