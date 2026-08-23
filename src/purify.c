/* purify.c — the recovery view of the call graph.
 *
 * Hub-and-authority, betweenness, and coreness over the call view; the
 * classification of utility sinks, god objects, and peripheral nodes against
 * them; and the masked copy a layering can be read off (doc/SDD.md §20).
 *
 * **This is the one place `elc` forms a view of its own about a code base**,
 * and the requirements are shaped almost entirely around containing that. Two
 * containments run through every function below:
 *
 *   * **The recovery view is a copy** (HLR-167). Nothing here writes to the
 *     `Sdg`; the masking builds a second graph out of the same edge table. The
 *     alternative — masking in place and unmasking afterwards — makes every
 *     analysis order-dependent and leaves the run one early return away from
 *     reporting a fan-out that omits real calls, which would be a wrong number
 *     carrying the authority of a measured one.
 *   * **Nothing here judges** (HLR-171, HLR-101). A classification carries no
 *     severity, becomes no finding, and advises nothing. "God object" states
 *     where a function sits in a graph; it is not a measurement banded against
 *     a published range, and presenting one as a finding would put `elc`'s own
 *     opinion in the section whose whole claim is that it holds none.
 *
 * The thresholds are compared against a node's **position in the ordered
 * distribution**, never against a raw score. Betweenness scales with the size
 * of the graph, so a fixed cut-off would classify every function in a large
 * project and none in a small one; a rank is comparable across projects, which
 * is what makes one default serviceable for both.
 */

#include <stdlib.h>
#include <string.h>

#include <igraph.h>

#include "elc.h"
#include "graph.h"
#include "purify.h"

/* The tolerance a score comparison is made to (HLR-179).
 *
 * Relative, and floored at one, so that it is meaningful for a betweenness in
 * the thousands and for a HITS score in the thousandths alike. HITS is
 * iterative and its scores are approximations: without a *stated* tolerance the
 * same source classifies differently on two machines, and HLR-032 fails in a
 * way no fixture reliably catches.
 */
#define PURIFY_TOLERANCE 1e-9

/* --------------------------------------------------------------- naming -- */

const char *purify_class_name(PurifyClass klass)
{
	switch (klass) {
	case PURIFY_UTILITY_SINK: return "utility sink";
	case PURIFY_GOD_OBJECT:   return "god object";
	case PURIFY_PERIPHERAL:   return "peripheral";
	case PURIFY_ORDINARY:
	default:                  return "ordinary";
	}
}

const char *purify_metric_name(PurifyMetric metric)
{
	switch (metric) {
	case PURIFY_METRIC_AUTHORITY:   return "authority";
	case PURIFY_METRIC_BETWEENNESS: return "betweenness";
	case PURIFY_METRIC_CORENESS:    return "coreness";
	case PURIFY_METRIC_NONE:
	default:                        return "";
	}
}

/* What masking the class did to the view.
 *
 * **The asymmetry is the point** (HLR-168, HLR-169). A utility sink keeps its
 * outgoing edges, because the fusion it causes is between its *callers*: the
 * incoming edges are what join every caller to every other through it. A god
 * object loses both directions, because it short-circuits in both. A peripheral
 * node is excluded outright and given no layer at all — a function `elc` did
 * not consider is not a function `elc` placed at the bottom (HLR-170).
 */
const char *purify_action_name(PurifyClass klass)
{
	switch (klass) {
	case PURIFY_UTILITY_SINK: return "incoming edges masked";
	case PURIFY_GOD_OBJECT:   return "all edges masked";
	case PURIFY_PERIPHERAL:   return "excluded from the view";
	case PURIFY_ORDINARY:
	default:                  return "";
	}
}

/* ------------------------------------------------------------ the ranks -- */

int purify_score_cmp(double a, double b)
{
	double diff  = a - b;
	double scale = a < 0 ? -a : a;
	double other = b < 0 ? -b : b;

	if (other > scale)
		scale = other;
	if (scale < 1.0)
		scale = 1.0;
	if (diff < 0)
		diff = -diff;

	if (diff <= PURIFY_TOLERANCE * scale)
		return 0;
	return a < b ? -1 : 1;
}

/* One node's score, for the ordering the ranks are read off. */
typedef struct {
	double   score;
	uint32_t node;
} RankEntry;

/* Ascending by score, **ties broken by the stable node identifier** (HLR-179,
 * HLR-033).
 *
 * The scores are compared exactly here rather than to the tolerance, and that
 * is deliberate: an ordering built on a tolerant comparison is built on a
 * relation that is not transitive, and the result would then depend on the
 * order `qsort` happened to visit the elements in — the very property this
 * requirement exists to remove. The tolerance is applied afterwards, over the
 * exact order, where it merges neighbours into one position.
 */
static int by_score_then_node(const void *a, const void *b)
{
	const RankEntry *x = a;
	const RankEntry *y = b;

	if (x->score < y->score)
		return -1;
	if (x->score > y->score)
		return 1;
	return x->node < y->node ? -1 : x->node > y->node;
}

/* For every node, the number of *other* nodes scoring strictly below it.
 *
 * Nodes whose scores agree to the tolerance hold one position, so they classify
 * identically — which is what makes a tie a tie rather than an accident of the
 * last bits of an iterative computation. A run is delimited by comparing each
 * member against the run's **first** element rather than against its
 * predecessor, so that the grouping is a property of the sorted order and not
 * of how far a chain of near-equal neighbours happens to reach.
 *
 * Returns 0 on success, -1 on allocation failure.
 */
static int rank_of(const double *score, size_t n, size_t *below)
{
	RankEntry *entries = NULL;
	size_t     at      = 0;

	if (n == 0)
		return 0;

	entries = calloc(n, sizeof *entries);
	if (!entries)
		return -1;

	for (size_t i = 0; i < n; i++) {
		entries[i].score = score[i];
		entries[i].node  = (uint32_t)i;
	}
	qsort(entries, n, sizeof *entries, by_score_then_node);

	while (at < n) {
		size_t end = at + 1;

		while (end < n &&
		       purify_score_cmp(entries[end].score,
		                        entries[at].score) == 0)
			end++;

		for (size_t i = at; i < end; i++)
			below[entries[i].node] = at;
		at = end;
	}

	free(entries);
	return 0;
}

/* The rank as a percentage of the other nodes, for the report.
 *
 * Of the *other* nodes — `n - 1` — so that the top of a distribution is 100
 * whatever the size of the graph. Over `n` it would be 8 of 9 for the highest
 * node in a nine-function tree, and no threshold above 89 could ever be met
 * there: one default would then be unusable on small projects and unusably
 * loose on large ones, which is the failure ranking exists to avoid.
 */
static uint32_t rank_percent(size_t below, size_t n)
{
	if (n < 2)
		return 0;
	return (uint32_t)((below * 100) / (n - 1));
}

/* Whether a node's position meets a threshold, from either direction.
 *
 * Compared in integers, so the boundary is exact: the only floating-point
 * comparison purification makes is the tolerance that decides which nodes share
 * a position, and it is made in one place.
 */
static bool rank_at_or_above(size_t below, size_t n, uint32_t percent)
{
	if (n < 2)
		return false;
	return (uint64_t)below * 100u >= (uint64_t)percent * (n - 1);
}

static bool rank_at_or_below(size_t below, size_t n, uint32_t percent)
{
	if (n < 2)
		return false;
	return (uint64_t)below * 100u <= (uint64_t)percent * (n - 1);
}

/* ------------------------------------------------------- the centralities --
 *
 * All three are computed over the **call view**, and that is not a detail. A
 * global-state edge joins a writer to a reader; it is coupling and not
 * invocation, and a layering read off a graph containing them would join every
 * pair of functions sharing a variable (LLR-CTR-07).
 *
 * `igraph`'s error handler is installed non-aborting when the graph is built
 * (LLR-SDG-15), so every call below has its return checked in the same way — an
 * allocation failure inside the library must produce the diagnostic and exit
 * status main promises rather than killing the run (HLR-125).
 */
static int centralities(const Sdg *g, Classification *out)
{
	igraph_vector_t     hub;
	igraph_vector_t     authority;
	igraph_vector_t     betweenness;
	igraph_vector_int_t coreness;
	igraph_arpack_options_t arpack;
	const igraph_t     *call = (const igraph_t *)g->call_graph;
	int                 status = -1;
	bool                have_hub = false, have_auth = false;
	bool                have_bet = false, have_core = false;

	igraph_arpack_options_init(&arpack);

	if (igraph_vector_init(&hub, 0) != IGRAPH_SUCCESS)
		goto cleanup;
	have_hub = true;
	if (igraph_vector_init(&authority, 0) != IGRAPH_SUCCESS)
		goto cleanup;
	have_auth = true;
	if (igraph_vector_init(&betweenness, 0) != IGRAPH_SUCCESS)
		goto cleanup;
	have_bet = true;
	if (igraph_vector_int_init(&coreness, 0) != IGRAPH_SUCCESS)
		goto cleanup;
	have_core = true;

	/* **The hub-and-authority decomposition** (HLR-168, HLR-169). A node
	 * many parts of the program call and which calls almost nothing back
	 * scores high on authority and near zero on hub, and is domain-agnostic
	 * by construction. The hub score is what separates a monolithic
	 * dispatcher from a genuine intermediary a layering ought to keep.
	 *
	 * **Not asked of a graph with no edges.** The decomposition is not
	 * defined there — every score is zero, and the library says so on
	 * standard error, which would put a diagnostic naming one of its own
	 * source files into a stream reserved for `elc`'s (HLR-038). A project
	 * whose functions call nothing has no hub-and-authority structure to
	 * find, and leaving the scores at their zero says exactly that. */
	if (igraph_ecount(call) > 0 &&
	    igraph_hub_and_authority_scores(call, &hub, &authority, NULL, NULL,
	                                    &arpack) != IGRAPH_SUCCESS)
		goto cleanup;

	/* Unnormalised, because the figure is compared by rank and a reader
	 * checking the report against the graph counts shortest paths rather
	 * than fractions of them. */
	if (igraph_betweenness(call, NULL, &betweenness, igraph_vss_all(),
	                       true, false) != IGRAPH_SUCCESS)
		goto cleanup;

	/* Coreness over the **undirected** neighbourhood: the k-core is the
	 * mutually connected centre of the program, and a leaf hanging off it
	 * is peripheral whichever way its one edge points (HLR-170). */
	if (igraph_coreness(call, &coreness, IGRAPH_ALL) != IGRAPH_SUCCESS)
		goto cleanup;

	for (size_t i = 0; i < g->node_count; i++) {
		/* Zero where the decomposition was not run, which is what an
		 * edgeless graph leaves behind. */
		out[i].hub         = igraph_vector_size(&hub) > (igraph_integer_t)i
		                             ? VECTOR(hub)[i] : 0.0;
		out[i].authority   = igraph_vector_size(&authority) >
		                     (igraph_integer_t)i
		                             ? VECTOR(authority)[i] : 0.0;
		out[i].betweenness = VECTOR(betweenness)[i];
		out[i].coreness    = (uint32_t)VECTOR(coreness)[i];
	}
	status = 0;

cleanup:
	if (have_core)
		igraph_vector_int_destroy(&coreness);
	if (have_bet)
		igraph_vector_destroy(&betweenness);
	if (have_auth)
		igraph_vector_destroy(&authority);
	if (have_hub)
		igraph_vector_destroy(&hub);
	return status;
}

/* ---------------------------------------------------------- classifying -- */

/* Record which measurement produced a class, and at what value.
 *
 * Carried on the classification rather than recomputed by the report, so that
 * the number a reader is shown is the number the comparison was made against
 * (HLR-174).
 */
static void triggered_by(Classification *c, PurifyClass klass,
                         PurifyMetric metric, double value, uint32_t rank)
{
	c->klass  = klass;
	c->metric = metric;
	c->value  = value;
	c->rank   = rank;
}

int classify_nodes(const Sdg *g, const PurifyThresholds *t, Classification *out)
{
	size_t  n      = g->node_count;
	double *scores = NULL;
	size_t *hub_below = NULL, *auth_below = NULL, *bet_below = NULL;
	int     status = -1;

	if (n == 0)
		return 0;

	if (centralities(g, out) != 0)
		return -1;

	scores     = calloc(n, sizeof *scores);
	hub_below  = calloc(n, sizeof *hub_below);
	auth_below = calloc(n, sizeof *auth_below);
	bet_below  = calloc(n, sizeof *bet_below);
	if (!scores || !hub_below || !auth_below || !bet_below)
		goto cleanup;

	for (size_t i = 0; i < n; i++)
		scores[i] = out[i].hub;
	if (rank_of(scores, n, hub_below) != 0)
		goto cleanup;
	for (size_t i = 0; i < n; i++)
		scores[i] = out[i].authority;
	if (rank_of(scores, n, auth_below) != 0)
		goto cleanup;
	for (size_t i = 0; i < n; i++)
		scores[i] = out[i].betweenness;
	if (rank_of(scores, n, bet_below) != 0)
		goto cleanup;

	for (size_t i = 0; i < n; i++) {
		Classification *c = &out[i];

		c->hub_rank         = rank_percent(hub_below[i], n);
		c->authority_rank   = rank_percent(auth_below[i], n);
		c->betweenness_rank = rank_percent(bet_below[i], n);

		/* **A god object first** (HLR-169). Where one function satisfies
		 * both centrality tests it is a god object and is reported as
		 * one: masking its edges subsumes masking its incoming edges,
		 * and the more specific claim is the more useful one to a
		 * reader. */
		if (rank_at_or_above(bet_below[i], n, t->god_betweenness) &&
		    rank_at_or_above(hub_below[i], n, t->god_hub)) {
			triggered_by(c, PURIFY_GOD_OBJECT,
			             PURIFY_METRIC_BETWEENNESS, c->betweenness,
			             c->betweenness_rank);
			continue;
		}

		if (rank_at_or_above(auth_below[i], n, t->sink_authority) &&
		    rank_at_or_below(hub_below[i], n, t->sink_hub)) {
			triggered_by(c, PURIFY_UTILITY_SINK,
			             PURIFY_METRIC_AUTHORITY, c->authority,
			             c->authority_rank);
			continue;
		}

		/* Peripheral last, and only where neither centrality test
		 * spoke. Both of those name a function's part in *fusing*
		 * domains, and a function they named is by construction part of
		 * the connected centre this test is asking about. */
		if (c->coreness < t->core_depth)
			triggered_by(c, PURIFY_PERIPHERAL,
			             PURIFY_METRIC_CORENESS,
			             (double)c->coreness, 0);
	}
	status = 0;

cleanup:
	free(bet_below);
	free(auth_below);
	free(hub_below);
	free(scores);
	return status;
}

/* ------------------------------------------------------ the masked copy -- */

/* Whether the view keeps one call edge.
 *
 * The three rules, in one place, so that the asymmetry between them is read
 * rather than reconstructed from three call sites (HLR-168 – HLR-170).
 */
static bool edge_survives(const Classification *c, uint32_t from, uint32_t to)
{
	/* An excluded node holds no edge: it is not in the view, so nothing
	 * reaches it and nothing leaves it. */
	if (c[from].klass == PURIFY_PERIPHERAL || c[to].klass == PURIFY_PERIPHERAL)
		return false;

	/* A god object short-circuits in both directions, so it loses both. */
	if (c[from].klass == PURIFY_GOD_OBJECT || c[to].klass == PURIFY_GOD_OBJECT)
		return false;

	/* A utility sink loses its **incoming** edges only. Its outgoing edges
	 * harm nothing: the fusion is between its callers, and masking the node
	 * rather than the edges into it would remove its own position from view
	 * along with the fusion. */
	return c[to].klass != PURIFY_UTILITY_SINK;
}

int build_recovery_view(const Sdg *g, const Classification *c,
                        RecoveryView *out)
{
	igraph_vector_int_t edges;
	igraph_t           *view = NULL;
	size_t              calls = 0;
	size_t              at    = 0;

	memset(out, 0, sizeof *out);
	out->node_count = g->node_count;

	out->included = calloc(g->node_count ? g->node_count : 1,
	                       sizeof *out->included);
	if (!out->included)
		return -1;

	for (size_t i = 0; i < g->node_count; i++) {
		out->included[i] = c[i].klass != PURIFY_PERIPHERAL;
		if (out->included[i])
			out->included_count++;
	}

	for (size_t i = 0; i < g->edge_count; i++) {
		if (g->edges[i].kind != EDGE_CALL)
			continue;
		calls++;
		if (edge_survives(c, g->edges[i].from, g->edges[i].to))
			out->edge_count++;
	}
	out->masked_edges = calls - out->edge_count;

	if (igraph_vector_int_init(&edges,
	                           (igraph_integer_t)(out->edge_count * 2)) !=
	    IGRAPH_SUCCESS)
		return -1;

	for (size_t i = 0; i < g->edge_count; i++) {
		if (g->edges[i].kind != EDGE_CALL ||
		    !edge_survives(c, g->edges[i].from, g->edges[i].to))
			continue;
		VECTOR(edges)[at++] = g->edges[i].from;
		VECTOR(edges)[at++] = g->edges[i].to;
	}

	view = calloc(1, sizeof *view);
	if (!view) {
		igraph_vector_int_destroy(&edges);
		return -1;
	}

	/* **The copy** (HLR-167). Built from the same edge table the call view
	 * was built from, over the same vertex set, so that a node identifier
	 * means the same thing in both — and so that no `igraph` call this
	 * module makes has the `Sdg`'s own graph as its target. */
	if (igraph_create(view, &edges, (igraph_integer_t)g->node_count,
	                  IGRAPH_DIRECTED) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&edges);
		free(view);
		return -1;
	}
	igraph_vector_int_destroy(&edges);

	out->graph = view;
	return 0;
}

/* ------------------------------------------------------------- the pass -- */

int purify_analyse(const Sdg *g, const ElcOptions *opts, PurifyResults *out)
{
	memset(out, 0, sizeof *out);

	if (!g || !opts)
		return 0;

	out->thresholds = opts->purify;
	out->node_count = g->node_count;

	out->classes = calloc(g->node_count ? g->node_count : 1,
	                      sizeof *out->classes);
	if (!out->classes)
		return -1;

	if (classify_nodes(g, &out->thresholds, out->classes) != 0)
		return -1;

	for (size_t i = 0; i < g->node_count; i++)
		if (out->classes[i].klass != PURIFY_ORDINARY)
			out->classified++;

	return build_recovery_view(g, out->classes, &out->view);
}

void purify_results_free(PurifyResults *r)
{
	if (!r)
		return;

	if (r->view.graph) {
		igraph_destroy((igraph_t *)r->view.graph);
		free(r->view.graph);
	}
	free(r->view.included);
	free(r->classes);
	memset(r, 0, sizeof *r);
}
