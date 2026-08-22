/* test/unit/purify.c — unit tests for src/purify.c.
 *
 * Built from hand-made facts and reports, as the graph, arch, call-tree and
 * state tests are: the classification rules are what is under test, and a
 * fixture that had to be parsed first would put a query file's behaviour into
 * every assertion about them. The `purify/` fixture group covers the
 * source-to-report path.
 *
 * Three cases are worth reading first, because each is a way the module can be
 * wrong while looking right:
 *
 *   * `the_call_view_is_not_modified` — the recovery view is a *copy*, and
 *     that is what every other analysis depends on (HLR-167).
 *   * `a_sink_keeps_its_outgoing_edges` — the asymmetry between a utility sink
 *     and a god object, which the fixture tree cannot show because its sink
 *     calls nothing (HLR-168, HLR-169).
 *   * `a_rank_is_a_position_not_a_score` — a threshold compared against the
 *     ordered distribution rather than against the number itself, which is
 *     what makes one default serve graphs of any size.
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>

#include "analyze.h"
#include "elc.h"
#include "graph.h"
#include "purify.h"
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

static uint32_t node_of(const Sdg *g, const char *name)
{
	for (size_t i = 0; i < g->node_count; i++)
		if (strcmp(g->nodes[i].name, name) == 0)
			return (uint32_t)i;
	cr_assert_fail("no node named %s", name);
	return 0;
}

/* The defaults `cli_parse` leaves behind. A zeroed structure would classify
 * every function as everything, since every rank is at or above zero. */
static PurifyThresholds default_thresholds(void)
{
	PurifyThresholds t = { 0 };

	t.sink_authority  = ELC_DEFAULT_SINK_AUTHORITY;
	t.sink_hub        = ELC_DEFAULT_SINK_HUB;
	t.god_betweenness = ELC_DEFAULT_GOD_BETWEENNESS;
	t.god_hub         = ELC_DEFAULT_GOD_HUB;
	t.core_depth      = ELC_DEFAULT_CORE_DEPTH;
	return t;
}

/* One graph, built once per test and torn down with it: a dispatcher every
 * path runs through, a sink everything calls, and a leaf hanging off the
 * side — the smallest shape holding all three classifications, with six
 * functions it holds none of.
 *
 *   main -> boot, dispatch                    feat_a -> store, log
 *   boot -> dispatch                          feat_b -> log
 *   dispatch -> feat_a, feat_b, feat_c, log   feat_c -> helper, log
 *   store -> log
 *
 * Three feature functions rather than two, and that is not padding: with two,
 * `dispatch` and `feat_a` reach the same hub score and neither outranks the
 * other, so no god object exists to find. The dispatcher has to call *wider*
 * than anything it calls, which is the property HLR-169 is about.
 */
typedef struct {
	Report report;
	Sdg    sdg;
} Fixture;

static void fixture_build(Fixture *f)
{
	static const char *const app[]  = { "main", "boot", "dispatch" };
	static const char *const feat[] = { "feat_a", "feat_b", "feat_c",
	                                    "helper" };
	static const char *const low[]  = { "store", "log" };
	FileMetrics *files[3];
	FactList     facts = { 0 };
	FileFacts   *a     = facts_for("/p/app.c");
	FileFacts   *b     = facts_for("/p/feat.c");
	FileFacts   *c     = facts_for("/p/low.c");

	files[0] = file_with("/p/app.c", app, 3);
	files[1] = file_with("/p/feat.c", feat, 4);
	files[2] = file_with("/p/low.c", low, 2);

	add_call(a, "boot", 0);
	add_call(a, "dispatch", 0);
	add_call(a, "dispatch", 1);
	add_call(a, "feat_a", 2);
	add_call(a, "feat_b", 2);
	add_call(a, "feat_c", 2);
	add_call(a, "log", 2);
	add_call(b, "store", 0);
	add_call(b, "log", 0);
	add_call(b, "log", 1);
	add_call(b, "helper", 2);
	add_call(b, "log", 2);
	add_call(c, "log", 0);

	cr_assert_eq(factlist_add(&facts, a), 0);
	cr_assert_eq(factlist_add(&facts, b), 0);
	cr_assert_eq(factlist_add(&facts, c), 0);

	f->report = report_of(files, 3);
	cr_assert_eq(graph_build(&facts, &f->report, &f->sdg), 0);
	factlist_free(&facts);
}

static void fixture_free(Fixture *f)
{
	graph_free(&f->sdg);
	report_free(&f->report);
}

/* ----------------------------------------------------- the classifications */

Test(purify, the_sink_and_the_dispatcher_are_classified)
{
	Fixture          f = { 0 };
	PurifyThresholds t = default_thresholds();
	Classification  *c;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);
	cr_assert_eq(classify_nodes(&f.sdg, &t, c), 0);

	/* `log` is called by five functions and calls nothing, so its hub score
	 * is exactly zero and its authority the highest (HLR-168). */
	cr_assert_eq(c[node_of(&f.sdg, "log")].klass, PURIFY_UTILITY_SINK);
	/* `dispatch` lies on every shortest path into the feature layer and
	 * calls widest (HLR-169). */
	cr_assert_eq(c[node_of(&f.sdg, "dispatch")].klass, PURIFY_GOD_OBJECT);
	/* `helper` has one undirected neighbour, so it lies in the first core
	 * and outside the mutually connected centre (HLR-170). */
	cr_assert_eq(c[node_of(&f.sdg, "helper")].klass, PURIFY_PERIPHERAL);

	free(c);
	fixture_free(&f);
}

Test(purify, an_ordinary_function_is_classified_as_nothing)
{
	Fixture          f = { 0 };
	PurifyThresholds t = default_thresholds();
	Classification  *c;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);
	cr_assert_eq(classify_nodes(&f.sdg, &t, c), 0);

	/* The half of the fixture that would pass against an implementation
	 * classifying everything. `main` and `boot` are the sharpest of these:
	 * each has a hub rank of zero and so passes the *hub* half of the
	 * utility-sink test, and neither is a sink because the authority half
	 * fails. An implementation testing either half alone reports them. */
	cr_assert_eq(c[node_of(&f.sdg, "main")].klass, PURIFY_ORDINARY);
	cr_assert_eq(c[node_of(&f.sdg, "boot")].klass, PURIFY_ORDINARY);
	cr_assert_eq(c[node_of(&f.sdg, "store")].klass, PURIFY_ORDINARY);
	cr_assert_eq(c[node_of(&f.sdg, "main")].metric, PURIFY_METRIC_NONE);

	free(c);
	fixture_free(&f);
}

Test(purify, a_classification_names_the_metric_and_the_value)
{
	Fixture          f = { 0 };
	PurifyThresholds t = default_thresholds();
	Classification  *c;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);
	cr_assert_eq(classify_nodes(&f.sdg, &t, c), 0);

	/* HLR-174: the metric and the value beside the class, or the reader has
	 * no grounds to trust the masking that follows from it. */
	const Classification *sink = &c[node_of(&f.sdg, "log")];
	const Classification *god  = &c[node_of(&f.sdg, "dispatch")];

	cr_assert_eq(sink->metric, PURIFY_METRIC_AUTHORITY);
	cr_assert_float_eq(sink->value, sink->authority, 1e-12);
	cr_assert_eq(god->metric, PURIFY_METRIC_BETWEENNESS);
	cr_assert_float_eq(god->value, god->betweenness, 1e-12);
	cr_assert_eq(c[node_of(&f.sdg, "helper")].metric,
	             PURIFY_METRIC_CORENESS);

	free(c);
	fixture_free(&f);
}

Test(purify, a_rank_is_a_position_not_a_score)
{
	Fixture          f = { 0 };
	PurifyThresholds t = default_thresholds();
	Classification  *c;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);
	cr_assert_eq(classify_nodes(&f.sdg, &t, c), 0);

	/* The top of any distribution is 100 whatever the size of the graph,
	 * because the rank is over the *other* nodes. That is what lets one
	 * default classify an eight-function graph and an eight-hundred one:
	 * betweenness scales with size and a rank does not. */
	cr_assert_eq(c[node_of(&f.sdg, "dispatch")].betweenness_rank, 100);
	cr_assert_eq(c[node_of(&f.sdg, "log")].authority_rank, 100);
	cr_assert_eq(c[node_of(&f.sdg, "log")].hub_rank, 0);
	/* And the raw score it was reached from is nowhere near the threshold
	 * as a number, which is the whole point of not comparing it. */
	cr_assert(c[node_of(&f.sdg, "dispatch")].betweenness < 90.0);

	free(c);
	fixture_free(&f);
}

Test(purify, a_god_object_outranks_a_utility_sink)
{
	Fixture          f = { 0 };
	PurifyThresholds t = default_thresholds();
	Classification  *c;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);

	/* Thresholds loose enough that `dispatch` satisfies both tests at once.
	 * HLR-169 settles it: the god object is the stronger and more useful
	 * claim, and masking its edges subsumes masking its incoming ones. */
	t.sink_authority  = 0;
	t.sink_hub        = 100;
	cr_assert_eq(classify_nodes(&f.sdg, &t, c), 0);
	cr_assert_eq(c[node_of(&f.sdg, "dispatch")].klass, PURIFY_GOD_OBJECT);

	free(c);
	fixture_free(&f);
}

Test(purify, a_centrality_class_outranks_a_peripheral_one)
{
	Fixture          f = { 0 };
	PurifyThresholds t = default_thresholds();
	Classification  *c;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);

	/* A core depth above every coreness in the graph would make every
	 * function peripheral, and the two the centrality tests named keep
	 * their classes: a function those tests spoke about is part of the
	 * connected centre by construction. */
	t.core_depth = 99;
	cr_assert_eq(classify_nodes(&f.sdg, &t, c), 0);
	cr_assert_eq(c[node_of(&f.sdg, "dispatch")].klass, PURIFY_GOD_OBJECT);
	cr_assert_eq(c[node_of(&f.sdg, "log")].klass, PURIFY_UTILITY_SINK);
	cr_assert_eq(c[node_of(&f.sdg, "main")].klass, PURIFY_PERIPHERAL);

	free(c);
	fixture_free(&f);
}

Test(purify, a_graph_with_no_nodes_classifies_nothing)
{
	Sdg              empty = { 0 };
	PurifyThresholds t     = default_thresholds();

	/* Not an error, as an analysis short of its inputs never is (HLR-115).
	 * There is no distribution to rank against and nothing to rank. */
	cr_assert_eq(classify_nodes(&empty, &t, NULL), 0);
}

Test(purify, an_edgeless_graph_scores_zero_and_classifies_no_centrality)
{
	static const char *const names[] = { "alone", "apart", "aside" };
	FileMetrics     *files[1];
	FactList         facts = { 0 };
	Report           report = { 0 };
	Sdg              sdg    = { 0 };
	PurifyThresholds t      = default_thresholds();
	Classification  *c;

	files[0] = file_with("/p/lone.c", names, 3);
	cr_assert_eq(factlist_add(&facts, facts_for("/p/lone.c")), 0);
	report = report_of(files, 1);
	cr_assert_eq(graph_build(&facts, &report, &sdg), 0);
	factlist_free(&facts);

	c = calloc(sdg.node_count, sizeof *c);
	cr_assert_not_null(c);
	cr_assert_eq(classify_nodes(&sdg, &t, c), 0);

	/* The hub-and-authority decomposition is not defined on a graph with no
	 * edges, and the library reports the fact on standard error naming one
	 * of its own source files — which is neither a diagnostic a user can
	 * act on nor `elc`'s own, the only thing HLR-038 admits to that stream.
	 * A program whose functions call nothing has no hub-and-authority
	 * structure to find, and the zero says exactly that. */
	for (size_t i = 0; i < sdg.node_count; i++) {
		cr_assert_float_eq(c[i].hub, 0.0, 1e-12);
		cr_assert_float_eq(c[i].authority, 0.0, 1e-12);
		cr_assert_neq(c[i].klass, PURIFY_UTILITY_SINK);
		cr_assert_neq(c[i].klass, PURIFY_GOD_OBJECT);
		/* Every one of them is outside the connected centre, because
		 * there is no centre. That is a coreness answer, and it is
		 * still given. */
		cr_assert_eq(c[i].klass, PURIFY_PERIPHERAL);
	}

	free(c);
	graph_free(&sdg);
	report_free(&report);
}

/* --------------------------------------------------------- the tolerance -- */

Test(purify, scores_within_the_tolerance_hold_one_position)
{
	/* HITS is iterative and its scores are approximations, so a comparison
	 * at the boundary is made to a stated tolerance. Without one the same
	 * source classifies differently on two machines, and HLR-032 fails in a
	 * way no fixture reliably catches (HLR-179). */
	cr_assert_eq(purify_score_cmp(1.0, 1.0 + 1e-15), 0);
	cr_assert_eq(purify_score_cmp(0.0, 1e-15), 0);
	cr_assert_eq(purify_score_cmp(1.0, 1.0), 0);
}

Test(purify, scores_outside_the_tolerance_are_ordered)
{
	cr_assert_eq(purify_score_cmp(1.0, 2.0), -1);
	cr_assert_eq(purify_score_cmp(2.0, 1.0), 1);
	/* Relative above one, so that a betweenness in the thousands is not
	 * swallowed by an absolute epsilon chosen for a HITS score. */
	cr_assert_eq(purify_score_cmp(1000.0, 1001.0), -1);
	cr_assert_eq(purify_score_cmp(1e-6, 2e-6), -1);
}

Test(purify, functions_with_equal_scores_hold_one_position)
{
	Fixture          f = { 0 };
	PurifyThresholds t = default_thresholds();
	Classification  *c;
	uint32_t         a, b;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);
	cr_assert_eq(classify_nodes(&f.sdg, &t, c), 0);

	/* `feat_a` and `feat_c` are the same shape: each is called only by the
	 * dispatcher, each calls the sink and one thing besides. Their scores
	 * are equal, so they hold one position in every distribution and
	 * classify alike — whichever order the library enumerated them in, and
	 * whichever of them the sort visited first.
	 *
	 * That is the observable half of the ordering rule. The ordering itself
	 * is exact with the node identifier breaking equal scores, and the
	 * tolerance is applied over it afterwards; the two together are what
	 * make this assertion hold on any machine rather than on this one
	 * (HLR-179, HLR-033). */
	a = node_of(&f.sdg, "feat_a");
	b = node_of(&f.sdg, "feat_c");

	cr_assert_eq(purify_score_cmp(c[a].hub, c[b].hub), 0);
	cr_assert_eq(purify_score_cmp(c[a].betweenness, c[b].betweenness), 0);
	cr_assert_eq(c[a].hub_rank, c[b].hub_rank);
	cr_assert_eq(c[a].authority_rank, c[b].authority_rank);
	cr_assert_eq(c[a].betweenness_rank, c[b].betweenness_rank);
	cr_assert_eq(c[a].klass, c[b].klass);
	/* And the position they share is one below the top, not at it: a tie
	 * does not promote either of them into a threshold neither meets. */
	cr_assert_lt(c[a].betweenness_rank, 100);

	free(c);
	fixture_free(&f);
}

/* ------------------------------------------------------- the masked copy -- */

Test(purify, the_call_view_is_not_modified)
{
	Fixture       f = { 0 };
	ElcOptions    opts = { 0 };
	PurifyResults r    = { 0 };
	igraph_integer_t before_v, before_e, after_v, after_e;

	fixture_build(&f);
	opts.purify = default_thresholds();

	before_v = igraph_vcount((const igraph_t *)f.sdg.call_graph);
	before_e = igraph_ecount((const igraph_t *)f.sdg.call_graph);

	cr_assert_eq(purify_analyse(&f.sdg, &opts, &r), 0);

	after_v = igraph_vcount((const igraph_t *)f.sdg.call_graph);
	after_e = igraph_ecount((const igraph_t *)f.sdg.call_graph);

	/* **The requirement the rest of the module is built on** (HLR-167).
	 * Masking into a second graph rather than into this one is what keeps
	 * every fan-out, every coupling figure, and every matrix cell exactly
	 * what it would have been had no purification run. The in-place version
	 * is one early return away from reporting a fan-out that omits real
	 * calls. */
	cr_assert_eq(before_v, after_v);
	cr_assert_eq(before_e, after_e);
	cr_assert_neq(r.view.graph, f.sdg.call_graph);
	cr_assert_eq((size_t)before_e, f.sdg.edge_count);

	purify_results_free(&r);
	fixture_free(&f);
}

Test(purify, a_sink_keeps_its_outgoing_edges)
{
	Fixture         f = { 0 };
	RecoveryView    v = { 0 };
	Classification *c;
	uint32_t        store, log;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);

	/* Hand-built rather than computed, because this is the asymmetry the
	 * fixture tree cannot show: its sink calls nothing. `store` is declared
	 * a sink here, and it calls `log` (HLR-168). */
	store = node_of(&f.sdg, "store");
	log   = node_of(&f.sdg, "log");
	c[store].klass = PURIFY_UTILITY_SINK;

	cr_assert_eq(build_recovery_view(&f.sdg, c, &v), 0);

	/* feat_a -> store is masked, store -> log is not. The fusion a sink
	 * causes is between its *callers*, so its outgoing edges harm nothing
	 * and masking them would remove its own position from view along with
	 * the fusion. */
	igraph_bool_t into = false, out_of = false;

	cr_assert_eq(igraph_are_adjacent((const igraph_t *)v.graph,
	                                 node_of(&f.sdg, "feat_a"), store,
	                                 &into), IGRAPH_SUCCESS);
	cr_assert_eq(igraph_are_adjacent((const igraph_t *)v.graph, store, log,
	                                 &out_of), IGRAPH_SUCCESS);
	cr_assert_not(into, "a sink's incoming edges are masked");
	cr_assert(out_of, "a sink's outgoing edges are not");
	cr_assert(v.included[store], "a sink stays in the view");

	igraph_destroy((igraph_t *)v.graph);
	free(v.graph);
	free(v.included);
	free(c);
	fixture_free(&f);
}

Test(purify, a_god_object_loses_both_directions)
{
	Fixture         f = { 0 };
	RecoveryView    v = { 0 };
	Classification *c;
	uint32_t        dispatch;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);

	dispatch = node_of(&f.sdg, "dispatch");
	c[dispatch].klass = PURIFY_GOD_OBJECT;

	cr_assert_eq(build_recovery_view(&f.sdg, c, &v), 0);

	/* It short-circuits in both directions, so it loses both (HLR-169). */
	igraph_bool_t into = false, out_of = false;

	cr_assert_eq(igraph_are_adjacent((const igraph_t *)v.graph,
	                                 node_of(&f.sdg, "main"), dispatch,
	                                 &into), IGRAPH_SUCCESS);
	cr_assert_eq(igraph_are_adjacent((const igraph_t *)v.graph, dispatch,
	                                 node_of(&f.sdg, "feat_a"), &out_of),
	             IGRAPH_SUCCESS);
	cr_assert_not(into);
	cr_assert_not(out_of);
	cr_assert(v.included[dispatch],
	          "a god object is masked, not excluded");

	igraph_destroy((igraph_t *)v.graph);
	free(v.graph);
	free(v.included);
	free(c);
	fixture_free(&f);
}

Test(purify, a_peripheral_node_is_excluded_rather_than_placed)
{
	Fixture         f = { 0 };
	RecoveryView    v = { 0 };
	Classification *c;
	uint32_t        helper;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);

	helper = node_of(&f.sdg, "helper");
	c[helper].klass = PURIFY_PERIPHERAL;

	cr_assert_eq(build_recovery_view(&f.sdg, c, &v), 0);

	/* **Excluded, not placed** (HLR-170). It is marked out of the view and
	 * holds no edge, so nothing downstream can give it a recovered layer. A
	 * function elc did not consider is not a function elc put at the bottom
	 * of the architecture, and conflating the two would drop every leaf
	 * into the lowest layer. */
	igraph_bool_t into = false;

	cr_assert_not(v.included[helper]);
	cr_assert_eq(v.included_count, f.sdg.node_count - 1);
	cr_assert_eq(igraph_are_adjacent((const igraph_t *)v.graph,
	                                 node_of(&f.sdg, "feat_c"), helper,
	                                 &into), IGRAPH_SUCCESS);
	cr_assert_not(into);

	igraph_destroy((igraph_t *)v.graph);
	free(v.graph);
	free(v.included);
	free(c);
	fixture_free(&f);
}

Test(purify, the_view_keeps_the_graphs_own_node_identifiers)
{
	Fixture         f = { 0 };
	RecoveryView    v = { 0 };
	Classification *c;

	fixture_build(&f);
	c = calloc(f.sdg.node_count, sizeof *c);
	cr_assert_not_null(c);
	c[node_of(&f.sdg, "helper")].klass = PURIFY_PERIPHERAL;

	cr_assert_eq(build_recovery_view(&f.sdg, c, &v), 0);

	/* An excluded node is marked rather than renumbered away, so a vertex
	 * of the view indexes the node table directly and a tie broken by
	 * vertex identifier is a tie broken by the stable node identifier of
	 * HLR-033 (HLR-179). Renumbering would put the determinism on a
	 * mapping instead of on the identifier the rest of the run agrees
	 * about. */
	cr_assert_eq((size_t)igraph_vcount((const igraph_t *)v.graph),
	             f.sdg.node_count);
	cr_assert_eq(v.node_count, f.sdg.node_count);

	igraph_destroy((igraph_t *)v.graph);
	free(v.graph);
	free(v.included);
	free(c);
	fixture_free(&f);
}

Test(purify, the_view_counts_what_the_masking_removed)
{
	Fixture       f    = { 0 };
	ElcOptions    opts = { 0 };
	PurifyResults r    = { 0 };

	fixture_build(&f);
	opts.purify = default_thresholds();
	cr_assert_eq(purify_analyse(&f.sdg, &opts, &r), 0);

	/* Thirteen call edges in, and the retained count plus the masked count
	 * must be all of them: a view that dropped an edge without counting it
	 * would report a masking a reader could not check (HLR-174). */
	cr_assert_eq(r.view.edge_count + r.view.masked_edges,
	             f.sdg.edge_count);
	cr_assert_eq((size_t)igraph_ecount((const igraph_t *)r.view.graph),
	             r.view.edge_count);
	cr_assert_eq(r.classified, 3);

	purify_results_free(&r);
	fixture_free(&f);
}

/* ------------------------------------------------------------- the names -- */

Test(purify, every_class_names_itself_and_the_action_it_took)
{
	/* One definition each, so that four renderers and the saved record
	 * cannot disagree about what a class is called or about what masking
	 * one did (HLR-174). */
	cr_assert_str_eq(purify_class_name(PURIFY_UTILITY_SINK), "utility sink");
	cr_assert_str_eq(purify_class_name(PURIFY_GOD_OBJECT), "god object");
	cr_assert_str_eq(purify_class_name(PURIFY_PERIPHERAL), "peripheral");

	cr_assert_str_eq(purify_action_name(PURIFY_UTILITY_SINK),
	                 "incoming edges masked");
	cr_assert_str_eq(purify_action_name(PURIFY_GOD_OBJECT),
	                 "all edges masked");
	cr_assert_str_eq(purify_action_name(PURIFY_PERIPHERAL),
	                 "excluded from the view");

	cr_assert_str_eq(purify_metric_name(PURIFY_METRIC_AUTHORITY),
	                 "authority");
	cr_assert_str_eq(purify_metric_name(PURIFY_METRIC_BETWEENNESS),
	                 "betweenness");
	cr_assert_str_eq(purify_metric_name(PURIFY_METRIC_CORENESS),
	                 "coreness");
}

/* ------------------------------------------------------------ the report -- */

Test(purify, the_report_carries_one_row_per_classification)
{
	Fixture       f    = { 0 };
	ElcOptions    opts = { 0 };
	PurifyResults r    = { 0 };

	fixture_build(&f);
	opts.purify = default_thresholds();
	cr_assert_eq(purify_analyse(&f.sdg, &opts, &r), 0);
	cr_assert_eq(report_set_purify(&f.report, &r, &f.sdg, &opts), 0);

	/* Three classifications and three rows. The ordinary functions are
	 * absent: HLR-174 asks for the classifications that were made, and
	 * "elc concluded nothing about this function" is not one of them. */
	cr_assert_eq(f.report.purification_count, 3);
	cr_assert_eq(f.report.purified_nodes, r.view.included_count);
	cr_assert_eq(f.report.purified_edges, r.view.masked_edges);
	cr_assert_eq(f.report.purify_thresholds.core_depth,
	             ELC_DEFAULT_CORE_DEPTH);

	for (size_t i = 0; i < f.report.purification_count; i++) {
		const PurificationRow *row = &f.report.purification[i];

		cr_assert_not_null(row->function);
		cr_assert_not_null(row->file);
		cr_assert_neq(strlen(row->metric), 0,
		              "every row names the metric that triggered it");
		cr_assert_neq(strlen(row->value), 0,
		              "and the value it took");
		cr_assert_neq(strlen(row->action), 0,
		              "and what was done about it");
	}

	purify_results_free(&r);
	fixture_free(&f);
}

Test(purify, the_report_rows_are_ordered_by_file_and_line)
{
	Fixture       f    = { 0 };
	ElcOptions    opts = { 0 };
	PurifyResults r    = { 0 };

	fixture_build(&f);
	opts.purify = default_thresholds();
	cr_assert_eq(purify_analyse(&f.sdg, &opts, &r), 0);
	cr_assert_eq(report_set_purify(&f.report, &r, &f.sdg, &opts), 0);

	/* Every collection is ordered by an explicit key before a renderer sees
	 * it, so no library's enumeration order reaches the output (HLR-032,
	 * LLR-RPT-10). */
	for (size_t i = 1; i < f.report.purification_count; i++) {
		const PurificationRow *prev = &f.report.purification[i - 1];
		const PurificationRow *this = &f.report.purification[i];
		int                    c    = strcmp(prev->file, this->file);

		cr_assert(c < 0 || (c == 0 && prev->line <= this->line));
	}

	purify_results_free(&r);
	fixture_free(&f);
}
