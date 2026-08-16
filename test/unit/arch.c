/* test/unit/arch.c — unit tests for src/arch.c.
 *
 * Built from hand-made facts and reports, as the graph, call-tree and state
 * tests are: the measurement rules are what is under test, and a fixture that
 * had to be parsed first would put a query file's behaviour into every
 * assertion about them. The `arch/` fixture group covers the source-to-report
 * path.
 *
 * The two cases worth reading first are the ones that separate this module
 * from `calltree.c`: mutual recursion inside one component is not a cycle
 * here, and instability with both couplings zero is undefined rather than
 * anything.
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "arch.h"
#include "cli.h"
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

static size_t component_of(const Sdg *g, const char *path)
{
	for (size_t i = 0; i < g->component_count; i++)
		if (strcmp(g->component_paths[i], path) == 0)
			return i;
	return SIZE_MAX;
}

/* Options with the default bottleneck threshold, as cli_parse would leave
 * them: a zeroed structure would flag every component, since 0 >= 0. */
static ElcOptions default_options(void)
{
	ElcOptions opts = { 0 };

	opts.bottleneck_threshold = ELC_DEFAULT_BOTTLENECK_THRESHOLD;
	return opts;
}

/* ---------------------------------------------------------- instability -- */

Test(arch, instability_is_ce_over_the_sum)
{
	bool defined = false;

	cr_assert_float_eq(instability(1, 3, &defined), 0.75, 1e-9);
	cr_assert(defined);

	cr_assert_float_eq(instability(3, 1, &defined), 0.25, 1e-9);
	cr_assert(defined);
}

Test(arch, instability_is_zero_when_nothing_is_depended_upon)
{
	bool defined = false;

	/* Maximally stable, and a real number: Ca is not zero, so the division
	 * is well formed and the answer is 0 rather than undefined. */
	cr_assert_float_eq(instability(4, 0, &defined), 0.0, 1e-9);
	cr_assert(defined);
}

Test(arch, instability_is_one_when_nothing_depends_on_it)
{
	bool defined = false;

	cr_assert_float_eq(instability(0, 4, &defined), 1.0, 1e-9);
	cr_assert(defined);
}

Test(arch, instability_is_undefined_when_both_couplings_are_zero)
{
	bool defined = true;

	/* The division the requirement forbids. A component nothing depends on
	 * that depends on nothing is ordinary — a lone file in a single-file
	 * target — and 0, 1 and NaN are all wrong answers (HLR-082,
	 * LLR-INS-02). */
	(void)instability(0, 0, &defined);
	cr_assert_not(defined);
}

Test(arch, the_instability_metric_is_attributed)
{
	cr_assert_str_eq(instability_attribution(), "Martin");
}

Test(arch, the_bottleneck_threshold_is_marked_as_elcs_own)
{
	/* Not a published standard, and presenting it beside Henry-Kafura and
	 * MISRA without saying so would lend it authority it has not got
	 * (HLR-099, LLR-ARC-02). */
	cr_assert_not_null(strstr(bottleneck_attribution(), "elc heuristic"));
	cr_assert_not_null(strstr(bottleneck_attribution(),
	                          "not a published standard"));
}

/* -------------------------------------------------------------- coupling -- */

Test(arch, coupling_counts_components_not_calls)
{
	const char  *a[]     = { "a_one", "a_two" };
	const char  *b[]     = { "b_fn" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2),
	                         file_with("/p/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = default_options();
	ArchResults  arch    = { 0 };

	/* Three calls from two functions, all into one component. That is one
	 * dependency: counting call sites would make a.c's Ce three
	 * (LLR-CPL-03). */
	add_call(fa, "b_fn", 0);
	add_call(fa, "b_fn", 0);
	add_call(fa, "b_fn", 1);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, facts_for("/p/b.c")), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);

	size_t ca_idx = component_of(&g, "/p/a.c");
	size_t cb_idx = component_of(&g, "/p/b.c");

	cr_assert_eq(arch.coupling[ca_idx].ce, 1);
	cr_assert_eq(arch.coupling[ca_idx].ca, 0);
	cr_assert_eq(arch.coupling[cb_idx].ca, 1);
	cr_assert_eq(arch.coupling[cb_idx].ce, 0);

	arch_results_free(&arch);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, a_call_within_one_component_is_no_coupling_at_all)
{
	const char  *a[]     = { "caller", "callee" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = default_options();
	ArchResults  arch    = { 0 };

	/* A file does not depend on itself, which is the same rule that keeps
	 * intra-file recursion out of the cycle report. */
	add_call(fa, "callee", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.coupling[0].ca, 0);
	cr_assert_eq(arch.coupling[0].ce, 0);
	cr_assert_not(arch.coupling[0].instability_defined);

	arch_results_free(&arch);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, a_bottleneck_needs_both_couplings_at_the_threshold)
{
	const char  *a[]     = { "hub" };
	const char  *b[]     = { "b_fn" };
	const char  *c[]     = { "c_fn" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1),
	                         file_with("/p/b.c", b, 1),
	                         file_with("/p/c.c", c, 1) };
	Report       report  = report_of(files, 3);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	FileFacts   *fb      = facts_for("/p/b.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = default_options();
	ArchResults  arch    = { 0 };

	/* a.c depends on c.c and is depended on by b.c: Ca 1, Ce 1. */
	add_call(fa, "c_fn", 0);
	add_call(fb, "hub", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, fb), 0);
	cr_assert_eq(factlist_add(&facts, facts_for("/p/c.c")), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	size_t hub = component_of(&g, "/p/a.c");
	size_t leaf = component_of(&g, "/p/c.c");

	/* At 1, the hub qualifies and the leaf — Ca 1, Ce 0 — does not. That
	 * second half is the test: the rule is *both*, not either. */
	opts.bottleneck_threshold = 1;
	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert(arch.coupling[hub].bottleneck);
	cr_assert_not(arch.coupling[leaf].bottleneck);
	arch_results_free(&arch);

	opts.bottleneck_threshold = 2;
	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_not(arch.coupling[hub].bottleneck);

	arch_results_free(&arch);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ---------------------------------------------------------------- cycles -- */

Test(arch, a_cycle_between_two_components_is_found_with_a_loop)
{
	const char  *a[]     = { "a_fn" };
	const char  *b[]     = { "b_fn" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1),
	                         file_with("/p/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	FileFacts   *fb      = facts_for("/p/b.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = default_options();
	ArchResults  arch    = { 0 };

	add_call(fa, "b_fn", 0);
	add_call(fb, "a_fn", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, fb), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.cycle_count, 1);
	cr_assert_eq(arch.cycles[0].member_count, 2);

	/* The loop, not merely the membership: the reader's next action is to
	 * cut one of its edges, and a set does not say which (LLR-CYC-02). */
	cr_assert_eq(arch.cycles[0].path_count, 2);

	arch_results_free(&arch);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, mutual_recursion_within_one_component_is_not_a_cycle)
{
	const char  *a[]     = { "ping", "pong" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 2) };
	Report       report  = report_of(files, 1);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = default_options();
	ArchResults  arch    = { 0 };

	/* The case HLR-083 names. The call view holds a cycle; the component
	 * projection does not, because a file does not depend on itself.
	 * Reporting this would tell an architect to split a file over what is
	 * a MISRA C Rule 17.2 finding about two functions. */
	add_call(fa, "pong", 0);
	add_call(fa, "ping", 1);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.cycle_count, 0);

	arch_results_free(&arch);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, an_acyclic_projection_yields_no_cycles)
{
	const char  *a[]     = { "a_fn" };
	const char  *b[]     = { "b_fn" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1),
	                         file_with("/p/b.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = default_options();
	ArchResults  arch    = { 0 };

	add_call(fa, "b_fn", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, facts_for("/p/b.c")), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.cycle_count, 0);

	arch_results_free(&arch);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, a_three_component_cycle_reports_its_whole_group)
{
	const char  *a[]     = { "a_fn" };
	const char  *b[]     = { "b_fn" };
	const char  *c[]     = { "c_fn" };
	FileMetrics *files[] = { file_with("/p/a.c", a, 1),
	                         file_with("/p/b.c", b, 1),
	                         file_with("/p/c.c", c, 1) };
	Report       report  = report_of(files, 3);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/a.c");
	FileFacts   *fb      = facts_for("/p/b.c");
	FileFacts   *fc      = facts_for("/p/c.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = default_options();
	ArchResults  arch    = { 0 };

	add_call(fa, "b_fn", 0);
	add_call(fb, "c_fn", 0);
	add_call(fc, "a_fn", 0);
	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, fb), 0);
	cr_assert_eq(factlist_add(&facts, fc), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.cycle_count, 1);
	cr_assert_eq(arch.cycles[0].member_count, 3);
	cr_assert_eq(arch.cycles[0].path_count, 3);

	arch_results_free(&arch);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* -------------------------------------------------------------- layering -- */

/* Two components in two layers, with one call between them in the direction
 * the caller chooses. */
static void layered(Sdg *g, Report *report, FactList *facts, const char *caller,
                    const char *callee, const char *caller_file)
{
	const char  *a[]     = { "app_fn" };
	const char  *b[]     = { "hal_fn" };
	const char  *c[]     = { "drv_fn" };
	FileMetrics *files[] = { file_with("/p/app/a.c", a, 1),
	                         file_with("/p/hal/b.c", b, 1),
	                         file_with("/p/drv/c.c", c, 1) };

	*report = report_of(files, 3);

	FileFacts *fa = facts_for("/p/app/a.c");
	FileFacts *fb = facts_for("/p/hal/b.c");
	FileFacts *fc = facts_for("/p/drv/c.c");
	FileFacts *from = strcmp(caller_file, "app") == 0 ? fa
	                : strcmp(caller_file, "hal") == 0 ? fb : fc;

	(void)caller;
	add_call(from, callee, 0);
	cr_assert_eq(factlist_add(facts, fa), 0);
	cr_assert_eq(factlist_add(facts, fb), 0);
	cr_assert_eq(factlist_add(facts, fc), 0);
	cr_assert_eq(graph_build(facts, report, g), 0);
}

static ElcOptions three_layers(void)
{
	ElcOptions opts = default_options();

	cr_assert_eq(parse_stratum("app:/p/app/*", &opts), 0);
	cr_assert_eq(parse_stratum("hal:/p/hal/*", &opts), 0);
	cr_assert_eq(parse_stratum("drv:/p/drv/*", &opts), 0);
	return opts;
}

Test(arch, a_call_descending_two_layers_is_skip_level_only)
{
	Sdg         g      = { 0 };
	Report      report = { 0 };
	FactList    facts  = { 0 };
	ElcOptions  opts   = three_layers();
	ArchResults arch   = { 0 };

	layered(&g, &report, &facts, "app_fn", "drv_fn", "app");

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.strata_state, STRATA_MEASURED);
	cr_assert_eq(arch.violation_count, 1);
	cr_assert_eq(arch.violations[0].kind, LAYER_SKIP_LEVEL);
	cr_assert_eq(arch.violations[0].layers_crossed, 2);

	arch_results_free(&arch);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, a_call_ascending_one_layer_is_inverted_only)
{
	Sdg         g      = { 0 };
	Report      report = { 0 };
	FactList    facts  = { 0 };
	ElcOptions  opts   = three_layers();
	ArchResults arch   = { 0 };

	layered(&g, &report, &facts, "hal_fn", "app_fn", "hal");

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.violation_count, 1);
	cr_assert_eq(arch.violations[0].kind, LAYER_INVERTED);
	cr_assert_eq(arch.violations[0].layers_crossed, 1);

	arch_results_free(&arch);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, a_call_ascending_two_layers_is_both)
{
	Sdg         g      = { 0 };
	Report      report = { 0 };
	FactList    facts  = { 0 };
	ElcOptions  opts   = three_layers();
	ArchResults arch   = { 0 };
	bool        skip   = false;
	bool        invert = false;

	/* Both statements are true of it, and each has its own remedy, so it
	 * is reported twice rather than folded into one finding (LLR-LAY-03). */
	layered(&g, &report, &facts, "drv_fn", "app_fn", "drv");

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.violation_count, 2);
	for (size_t i = 0; i < arch.violation_count; i++) {
		skip   |= arch.violations[i].kind == LAYER_SKIP_LEVEL;
		invert |= arch.violations[i].kind == LAYER_INVERTED;
	}
	cr_assert(skip);
	cr_assert(invert);

	arch_results_free(&arch);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, a_call_descending_one_layer_is_no_violation)
{
	Sdg         g      = { 0 };
	Report      report = { 0 };
	FactList    facts  = { 0 };
	ElcOptions  opts   = three_layers();
	ArchResults arch   = { 0 };

	/* The ordinary case, and the one that has to stay silent: without it
	 * an implementation flagging every inter-layer call would pass every
	 * test above. */
	layered(&g, &report, &facts, "app_fn", "hal_fn", "app");

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.violation_count, 0);

	arch_results_free(&arch);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, a_component_in_no_declared_stratum_is_outside_the_partition)
{
	Sdg         g      = { 0 };
	Report      report = { 0 };
	FactList    facts  = { 0 };
	ElcOptions  opts   = default_options();
	ArchResults arch   = { 0 };

	layered(&g, &report, &facts, "app_fn", "drv_fn", "app");

	/* Only the middle layer is declared, so the call has nothing to be
	 * compared against. Placing an unnamed file would report violations
	 * against a design nobody drew. */
	cr_assert_eq(parse_stratum("hal:/p/hal/*", &opts), 0);
	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.violation_count, 0);

	arch_results_free(&arch);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, with_no_strata_declared_layering_is_omitted)
{
	Sdg         g      = { 0 };
	Report      report = { 0 };
	FactList    facts  = { 0 };
	ElcOptions  opts   = default_options();
	ArchResults arch   = { 0 };

	layered(&g, &report, &facts, "app_fn", "drv_fn", "app");

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.strata_state, STRATA_OMITTED_NONE_DECLARED);
	cr_assert_eq(arch.violation_count, 0);

	/* And the coupling table is produced all the same: omitting one
	 * analysis for want of a declaration must not omit its neighbours. */
	cr_assert_eq(arch.component_count, 3);

	arch_results_free(&arch);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, a_state_edge_is_not_a_layering_violation)
{
	const char  *a[]     = { "app_fn" };
	const char  *b[]     = { "drv_fn" };
	FileMetrics *files[] = { file_with("/p/app/a.c", a, 1),
	                         file_with("/p/drv/c.c", b, 1) };
	Report       report  = report_of(files, 2);
	FactList     facts   = { 0 };
	FileFacts   *fa      = facts_for("/p/app/a.c");
	FileFacts   *fc      = facts_for("/p/drv/c.c");
	Sdg          g       = { 0 };
	ElcOptions   opts    = default_options();
	ArchResults  arch    = { 0 };

	/* HLR-079 and HLR-118 are about a *call* reaching into a layer it
	 * should not. A variable two layers happen to share is a different
	 * fact with its own analyses, and folding it in here would report a
	 * layering violation for it. */
	fa->globals = calloc(2, sizeof *fa->globals);
	cr_assert_not_null(fa->globals);
	fa->globals[0].name     = strdup("shared");
	fa->globals[0].function = ELC_NO_FUNCTION;
	fa->globals[0].kind     = GLOBAL_DECLARATION;
	fa->globals[1].name     = strdup("shared");
	fa->globals[1].function = 0;
	fa->globals[1].kind     = GLOBAL_WRITE;
	fa->global_count        = 2;
	fa->global_capacity     = 2;

	fc->globals = calloc(1, sizeof *fc->globals);
	cr_assert_not_null(fc->globals);
	fc->globals[0].name     = strdup("shared");
	fc->globals[0].function = 0;
	fc->globals[0].kind     = GLOBAL_READ;
	fc->global_count        = 1;
	fc->global_capacity     = 1;

	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, fc), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(parse_stratum("app:/p/app/*", &opts), 0);
	cr_assert_eq(parse_stratum("hal:/p/hal/*", &opts), 0);
	cr_assert_eq(parse_stratum("drv:/p/drv/*", &opts), 0);

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.violation_count, 0);

	/* The dependency itself is real, and the coupling table has it. */
	cr_assert_eq(arch.coupling[component_of(&g, "/p/app/a.c")].ce, 1);

	arch_results_free(&arch);
	cli_options_free(&opts);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(arch, an_empty_graph_analyses_without_incident)
{
	Sdg                g      = { 0 };
	Report             report = { 0 };
	FactList           facts  = { 0 };
	MetricsAccumulator acc    = { 0 };
	ElcOptions         opts   = default_options();
	ArchResults        arch   = { 0 };

	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(arch_analyse(&g, &opts, &arch), 0);
	cr_assert_eq(arch.component_count, 0);
	cr_assert_eq(arch.cycle_count, 0);

	arch_results_free(&arch);
	metrics_free(&acc);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}
