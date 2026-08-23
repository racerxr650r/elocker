/* test/unit/recover.c — unit tests for src/recover.c.
 *
 * Built from hand-made facts and reports, as the purify tests are: the fold is
 * what is under test, and a fixture that had to be parsed first would put a
 * query file's behaviour into every assertion about a layering.
 *
 * Four cases are worth reading first, because each is a way the module can be
 * wrong while looking right:
 *
 *   * `an_outlier_member_does_not_move_its_directory` — a topological order is
 *     not a layering, and folding at a directory's earliest or latest member
 *     is the mistake that makes it look like one (HLR-172).
 *   * `a_cyclic_view_reports_the_cycles_instead_of_an_order` — where no
 *     ordering exists the cycles are the finding, not an excuse to order the
 *     graph arbitrarily (HLR-172).
 *   * `an_excluded_function_is_given_no_layer` — a function purification did
 *     not consider is not one placed at the bottom (HLR-170).
 *   * `the_proposal_is_an_argument_list` — what this module produces is a
 *     command line the user passes back, which is the boundary HLR-173 draws
 *     made visible.
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>

#include "analyze.h"
#include "elc.h"
#include "graph.h"
#include "purify.h"
#include "recover.h"
#include "report.h"

/* ------------------------------------------------------------ scaffolding */

static FileMetrics *file_with(const char *path, const char *directory,
                              const char *const *names, size_t count)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path      = strdup(path);
	m->directory = strdup(directory);
	m->language  = strdup("c");
	cr_assert_not_null(m->path);
	cr_assert_not_null(m->directory);
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

static uint32_t node_of(const Sdg *g, const char *name)
{
	for (size_t i = 0; i < g->node_count; i++)
		if (strcmp(g->nodes[i].name, name) == 0)
			return (uint32_t)i;
	cr_assert_fail("no node named %s", name);
	return 0;
}

typedef struct {
	Report report;
	Sdg    sdg;
} Fixture;

/* Three directories in a plain layering, and one function that is not where
 * its directory is.
 *
 *   app/  app_start, app_stop   -> svc_open, svc_close
 *   svc/  svc_open, svc_close   -> hal_init, hal_stop
 *         svc_leaf              (called from hal, calling nothing)
 *   hal/  hal_init, hal_stop    -> svc_leaf
 *
 * `svc_leaf` is the whole reason the tree is shaped this way. It sits at the
 * very bottom of the topological order while the rest of its directory sits
 * near the top, so a fold that placed a directory at its latest member would
 * put `svc/` below `hal/`. The graph stays a DAG — `svc_leaf` calls nothing —
 * so a layering still exists to get wrong.
 */
static void fixture_build(Fixture *f)
{
	static const char *const app[] = { "app_start", "app_stop" };
	static const char *const hal[] = { "hal_init", "hal_stop" };
	static const char *const svc[] = { "svc_open", "svc_close",
	                                   "svc_leaf" };
	FileMetrics *files[3];
	FactList     facts = { 0 };
	FileFacts   *a     = facts_for("/p/app/app.c");
	FileFacts   *h     = facts_for("/p/hal/hal.c");
	FileFacts   *s     = facts_for("/p/svc/svc.c");

	files[0] = file_with("/p/app/app.c", "/p/app", app, 2);
	files[1] = file_with("/p/hal/hal.c", "/p/hal", hal, 2);
	files[2] = file_with("/p/svc/svc.c", "/p/svc", svc, 3);

	add_call(a, "svc_open", 0);
	add_call(a, "svc_close", 0);
	add_call(a, "svc_open", 1);
	add_call(a, "svc_close", 1);
	add_call(h, "svc_leaf", 0);
	add_call(h, "svc_leaf", 1);
	add_call(s, "hal_init", 0);
	add_call(s, "hal_stop", 0);
	add_call(s, "hal_init", 1);
	add_call(s, "hal_stop", 1);

	cr_assert_eq(factlist_add(&facts, a), 0);
	cr_assert_eq(factlist_add(&facts, h), 0);
	cr_assert_eq(factlist_add(&facts, s), 0);

	{
		MetricsAccumulator acc  = { 0 };
		ElcOptions         opts = { 0 };

		for (size_t i = 0; i < 3; i++)
			cr_assert_eq(metrics_add(&acc, files[i]), 0);
		cr_assert_eq(report_assemble(&acc, NULL, &opts, &f->report), 0);
		metrics_free(&acc);
	}
	cr_assert_eq(graph_build(&facts, &f->report, &f->sdg), 0);
	factlist_free(&facts);
}

static void fixture_free(Fixture *f)
{
	graph_free(&f->sdg);
	report_free(&f->report);
}

/* The recovery view over a hand-made classification.
 *
 * Hand-made rather than computed, so that a test about the *fold* does not
 * depend on what the centralities happen to conclude about a seven-function
 * graph. `purify_results_free` owns everything this leaves behind.
 */
static void view_of(Fixture *f, PurifyResults *p)
{
	memset(p, 0, sizeof *p);
	p->node_count = f->sdg.node_count;
	p->classes    = calloc(f->sdg.node_count, sizeof *p->classes);
	cr_assert_not_null(p->classes);
	cr_assert_eq(build_recovery_view(&f->sdg, p->classes, &p->view), 0);
}

/* Rebuild the view after a test has revised the classifications.
 *
 * The view is a copy, so revising a class does not revise it — which is the
 * containment of HLR-167 showing up in a test as well as in the product.
 */
static void rebuild_view(Fixture *f, PurifyResults *p)
{
	igraph_destroy((igraph_t *)p->view.graph);
	free(p->view.graph);
	free(p->view.included);
	cr_assert_eq(build_recovery_view(&f->sdg, p->classes, &p->view), 0);
}

/* The layer one directory was placed at, or SIZE_MAX where it was not
 * placed. */
static size_t layer_of(const RecoveryResults *r, const char *directory)
{
	for (size_t i = 0; i < r->layer_count; i++)
		if (strcmp(r->layers[i].directory, directory) == 0)
			return r->layers[i].layer;
	return SIZE_MAX;
}

/* ------------------------------------------------------------- the fold -- */

Test(recover, a_layered_graph_recovers_its_layers)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults r;

	fixture_build(&f);
	view_of(&f, &p);

	/* An architecture orders *directories*, and this is the whole of what
	 * the fold produces: three of them, in the order the calls run
	 * (HLR-172, HLR-160). */
	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &r), 0);
	cr_assert_eq(r.state, RECOVERY_PROPOSED);
	cr_assert_eq(r.strata, 3);
	cr_assert_eq(r.layer_count, 3);
	cr_assert_eq(layer_of(&r, "/p/app"), 0);
	cr_assert_eq(layer_of(&r, "/p/svc"), 1);
	cr_assert_eq(layer_of(&r, "/p/hal"), 2);

	recovery_results_free(&r);
	purify_results_free(&p);
	fixture_free(&f);
}

Test(recover, an_outlier_member_does_not_move_its_directory)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults r;

	fixture_build(&f);
	view_of(&f, &p);
	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &r), 0);

	/* **A topological order is not a layering** (HLR-172). `svc_leaf` is
	 * last in the order — the hardware layer calls it — and it is the only
	 * member of `svc/` that is anywhere near the bottom. A fold placing a
	 * directory at its latest member would put `svc/` below `hal/`; one
	 * asking where the bulk of its edges point does not, because
	 * `svc_leaf` carries two of that directory's ten edge ends and the
	 * other eight are where the layer really is. */
	cr_assert_lt(layer_of(&r, "/p/svc"), layer_of(&r, "/p/hal"),
	             "one function reaching far down the order dragged its "
	             "whole directory with it");
	cr_assert_eq(r.layers[2].functions, 2, "hal keeps its two functions");

	recovery_results_free(&r);
	purify_results_free(&p);
	fixture_free(&f);
}

Test(recover, an_excluded_function_is_given_no_layer)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults r;

	fixture_build(&f);
	view_of(&f, &p);

	/* Both of `hal/`'s functions are peripheral, so the directory is not
	 * considered at all. **It is not therefore the bottom layer**: a
	 * function `elc` did not consider is not one it placed at the edge of
	 * the architecture, and a fold that read the excluded vertices back in
	 * would put every leaf in the lowest layer (HLR-170). */
	p.classes[node_of(&f.sdg, "hal_init")].klass  = PURIFY_PERIPHERAL;
	p.classes[node_of(&f.sdg, "hal_init")].masked = true;
	p.classes[node_of(&f.sdg, "hal_stop")].klass  = PURIFY_PERIPHERAL;
	p.classes[node_of(&f.sdg, "hal_stop")].masked = true;
	rebuild_view(&f, &p);

	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &r), 0);
	cr_assert_eq(r.state, RECOVERY_PROPOSED);
	cr_assert_eq(layer_of(&r, "/p/hal"), SIZE_MAX);
	cr_assert_eq(r.excluded, 2);

	recovery_results_free(&r);
	purify_results_free(&p);
	fixture_free(&f);
}

Test(recover, nothing_surviving_purification_proposes_nothing)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults r;

	fixture_build(&f);
	view_of(&f, &p);
	for (size_t i = 0; i < f.sdg.node_count; i++) {
		p.classes[i].klass  = PURIFY_PERIPHERAL;
		p.classes[i].masked = true;
	}
	rebuild_view(&f, &p);

	/* Not an error, and not an empty proposal: an analysis short of its
	 * inputs is omitted with its reason stated (HLR-115). */
	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &r), 0);
	cr_assert_eq(r.state, RECOVERY_OMITTED_EMPTY);
	cr_assert_eq(r.layer_count, 0);
	cr_assert_null(r.proposal);

	recovery_results_free(&r);
	purify_results_free(&p);
	fixture_free(&f);
}

/* ----------------------------------------------------------- the cycles -- */

Test(recover, a_cyclic_view_reports_the_cycles_instead_of_an_order)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults r;
	FileFacts      *back;
	FactList        facts = { 0 };

	fixture_build(&f);
	graph_free(&f.sdg);

	/* Rebuild the graph with the hardware layer calling back into the one
	 * above it, which is what makes the view cyclic. */
	{
		FileFacts *a = facts_for("/p/app/app.c");
		FileFacts *s = facts_for("/p/svc/svc.c");

		back = facts_for("/p/hal/hal.c");
		add_call(a, "svc_open", 0);
		add_call(s, "hal_init", 0);
		add_call(back, "svc_open", 0);
		cr_assert_eq(factlist_add(&facts, a), 0);
		cr_assert_eq(factlist_add(&facts, back), 0);
		cr_assert_eq(factlist_add(&facts, s), 0);
	}
	cr_assert_eq(graph_build(&facts, &f.report, &f.sdg), 0);
	factlist_free(&facts);
	view_of(&f, &p);

	/* **The cycles are the finding** (HLR-172). No topological ordering
	 * exists, and ordering the graph arbitrarily would present an
	 * invention as a reading — the rule HLR-090 applies to call depth over
	 * a cyclic graph, applied to the same impossibility. */
	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &r), 0);
	cr_assert_eq(r.state, RECOVERY_CYCLIC);
	cr_assert_eq(r.layer_count, 0);
	cr_assert_null(r.proposal);
	cr_assert_eq(r.cycle_count, 1);
	/* Members, comma-separated. A strongly connected component is a set:
	 * every member reaches every other, but the decomposition yields no
	 * order, and an arrow chain would assert a path that may not exist. */
	cr_assert_not_null(strstr(r.cycles[0], "svc_open"));
	cr_assert_not_null(strstr(r.cycles[0], "hal_init"));
	cr_assert_null(strstr(r.cycles[0], "->"));

	recovery_results_free(&r);
	purify_results_free(&p);
	fixture_free(&f);
}

Test(recover, a_self_call_does_not_block_the_layering)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults r;
	FactList        facts = { 0 };

	fixture_build(&f);
	graph_free(&f.sdg);

	/* The same layered tree with one function calling itself. A self-call
	 * makes the graph cyclic in the strict sense and orders *nothing*: it
	 * is an edge from a node to itself and says nothing about where that
	 * node sits relative to any other. Reporting it in place of a layering
	 * would repeat a fact the recursion section already states and cost
	 * every project with one recursive function the whole of this
	 * analysis. */
	{
		FileFacts *a = facts_for("/p/app/app.c");
		FileFacts *h = facts_for("/p/hal/hal.c");
		FileFacts *s = facts_for("/p/svc/svc.c");

		add_call(a, "svc_open", 0);
		add_call(a, "svc_open", 1);
		add_call(s, "hal_init", 0);
		add_call(s, "hal_init", 1);
		add_call(h, "hal_init", 0);   /* hal_init calls itself */
		cr_assert_eq(factlist_add(&facts, a), 0);
		cr_assert_eq(factlist_add(&facts, h), 0);
		cr_assert_eq(factlist_add(&facts, s), 0);
	}
	cr_assert_eq(graph_build(&facts, &f.report, &f.sdg), 0);
	factlist_free(&facts);
	view_of(&f, &p);

	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &r), 0);
	cr_assert_eq(r.state, RECOVERY_PROPOSED);
	cr_assert_eq(r.cycle_count, 0);
	cr_assert_lt(layer_of(&r, "/p/app"), layer_of(&r, "/p/hal"));

	recovery_results_free(&r);
	purify_results_free(&p);
	fixture_free(&f);
}

/* --------------------------------------------------------- the proposal -- */

Test(recover, the_proposal_is_an_argument_list)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults r;

	fixture_build(&f);
	view_of(&f, &p);
	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &r), 0);

	/* **Arguments, not prose** (HLR-173). Adoption is then a copy rather
	 * than a transcription, and the boundary the requirement draws is
	 * visible in the form of the thing: `elc` produces a command line, and
	 * it takes effect only when the user passes it back. */
	cr_assert_not_null(r.proposal);
	cr_assert_not_null(strstr(r.proposal, "--stratum app:'/p/app/*'"));
	cr_assert_not_null(strstr(r.proposal, "--stratum svc:'/p/svc/*'"));
	cr_assert_not_null(strstr(r.proposal, "--stratum hal:'/p/hal/*'"));
	/* The order is quoted because `>` is a shell redirection. An unquoted
	 * one would not merely fail to be adopted — it would create files
	 * named after the layers and hand `elc` a partial order. */
	cr_assert_not_null(strstr(r.proposal,
	                          "--stratum-order 'app>svc>hal'"));

	recovery_results_free(&r);
	purify_results_free(&p);
	fixture_free(&f);
}

Test(recover, the_declarations_run_deepest_directory_first)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults r;
	const char     *shallow, *deep;

	fixture_build(&f);
	/* Move the top layer's file into an ancestor of the others, so that a
	 * `--stratum` pattern for it would match their files too. */
	free(f.report.files[0]->directory);
	f.report.files[0]->directory = strdup("/p");
	cr_assert_not_null(f.report.files[0]->directory);
	view_of(&f, &p);
	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &r), 0);

	/* `stratum_of_components` takes the *first* declared layer whose
	 * pattern matches a file, and a directory wildcard matches everything
	 * beneath it. Declaring `/p` before `/p/svc` would hand every file to
	 * the top layer, so the deepest is declared first — which costs
	 * nothing, since the ordinals come from `--stratum-order` beside them
	 * and not from the order the declarations appear in. */
	shallow = strstr(r.proposal, ":'/p/*'");
	deep    = strstr(r.proposal, ":'/p/svc/*'");
	cr_assert_not_null(shallow);
	cr_assert_not_null(deep);
	cr_assert_lt(deep, shallow,
	             "an ancestor directory was declared before its child");

	recovery_results_free(&r);
	purify_results_free(&p);
	fixture_free(&f);
}

Test(recover, two_runs_over_one_graph_propose_one_layering)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults first, second;

	fixture_build(&f);
	view_of(&f, &p);

	/* **Identical, not merely equivalent** (HLR-179, HLR-032). The
	 * ordering is the graph library's and the fold's tie-break is the
	 * directory path, so neither the rows nor the argument list may depend
	 * on the order anything was enumerated in. */
	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &first), 0);
	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &second), 0);
	cr_assert_eq(first.layer_count, second.layer_count);
	for (size_t i = 0; i < first.layer_count; i++) {
		cr_assert_str_eq(first.layers[i].directory,
		                 second.layers[i].directory);
		cr_assert_eq(first.layers[i].layer, second.layers[i].layer);
	}
	cr_assert_str_eq(first.proposal, second.proposal);

	recovery_results_free(&second);
	recovery_results_free(&first);
	purify_results_free(&p);
	fixture_free(&f);
}

/* ------------------------------------------------------------ the report -- */

Test(recover, the_proposal_reaches_the_report_and_nothing_else)
{
	Fixture         f = { 0 };
	PurifyResults   p;
	RecoveryResults r;
	Report          out = { 0 };

	fixture_build(&f);
	view_of(&f, &p);
	cr_assert_eq(recover_layers(&p, &f.sdg, &f.report, &r), 0);
	cr_assert_eq(report_set_recovery(&out, &r), 0);

	/* The rows and the argument list, copied onto the model the renderers
	 * read. **Nothing measures against them** — the conformance analyses
	 * take their layer index from the declared strata and from nothing
	 * else, and `arch.c` is given no path to a `RecoveryResults`
	 * (HLR-173). */
	cr_assert_eq(out.recovery_state, RECOVERY_PROPOSED);
	cr_assert_eq(out.recovery_count, 3);
	cr_assert_eq(out.recovery_strata, 3);
	cr_assert_not_null(out.recovery_proposal);

	/* And **nothing else on the model was touched**. The conformance
	 * fields are exactly as a report with no declared strata leaves them:
	 * no layering rows, no indices, no subjects read off the proposal. A
	 * tool measuring conformance against its own proposal would find every
	 * code base conformant, because the standard would have been read off
	 * the thing it judged (HLR-173, HLR-115). */
	cr_assert_eq(out.layering_count, 0);
	cr_assert_null(out.back_call.index);
	cr_assert_null(out.skip_call.index);
	cr_assert_eq(out.dsm.count, 0);

	report_free(&out);
	recovery_results_free(&r);
	purify_results_free(&p);
	fixture_free(&f);
}

Test(recover, recovery_results_free_is_safe_on_null_and_twice)
{
	RecoveryResults r = { 0 };

	recovery_results_free(NULL);
	recovery_results_free(&r);
	recovery_results_free(&r);
	cr_assert_eq(r.layer_count, 0);
}
