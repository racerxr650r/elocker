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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>
#include <jansson.h>

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
const char *purify_action_name(PurifyClass klass, bool masked)
{
	/* **A class without its action.** A manifest may agree that a function
	 * is a dispatcher and disagree that it should be set aside, and that
	 * disagreement is the reason HLR-175 exists. The classification stays
	 * reportable — a reader still learns where the function sits in the
	 * graph — and the view keeps its edges (HLR-177). */
	if (!masked)
		return klass == PURIFY_ORDINARY ? "" : "kept in the view";

	switch (klass) {
	case PURIFY_UTILITY_SINK: return "incoming edges masked";
	case PURIFY_GOD_OBJECT:   return "all edges masked";
	case PURIFY_PERIPHERAL:   return "excluded from the view";
	case PURIFY_ORDINARY:
	default:                  return "";
	}
}

bool purify_class_from_name(const char *name, PurifyClass *out)
{
	static const struct {
		const char *name;
		PurifyClass klass;
	} NAMES[] = {
		{ "ordinary",     PURIFY_ORDINARY     },
		{ "utility sink", PURIFY_UTILITY_SINK },
		{ "god object",   PURIFY_GOD_OBJECT   },
		{ "peripheral",   PURIFY_PERIPHERAL   }
	};

	if (!name)
		return false;

	for (size_t i = 0; i < sizeof NAMES / sizeof *NAMES; i++)
		if (strcmp(NAMES[i].name, name) == 0) {
			*out = NAMES[i].klass;
			return true;
		}

	/* Not a class this build knows. The statement is not guessed at and
	 * not dropped: the file is rejected, because well-formed JSON that is
	 * not a manifest is what HLR-176 asks be refused. */
	return false;
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

/* The three measurements themselves, over an initialised set of vectors.
 *
 * Separated from the vector lifetime above it because the two are different
 * concerns: which centralities `elc` asks igraph for is the design decision the
 * comments below argue, and initialising four vectors and destroying them again
 * is bookkeeping that would otherwise be read as part of it.
 */
static int measure_centralities(const igraph_t *call, igraph_vector_t *hub,
                                igraph_vector_t *authority,
                                igraph_vector_t *betweenness,
                                igraph_vector_int_t *coreness,
                                igraph_arpack_options_t *arpack)
{
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
	    igraph_hub_and_authority_scores(call, hub, authority, NULL, NULL,
	                                    arpack) != IGRAPH_SUCCESS)
		return -1;

	/* Unnormalised, because the figure is compared by rank and a reader
	 * checking the report against the graph counts shortest paths rather
	 * than fractions of them. */
	if (igraph_betweenness(call, NULL, betweenness, igraph_vss_all(),
	                       true, false) != IGRAPH_SUCCESS)
		return -1;

	/* Coreness over the **undirected** neighbourhood: the k-core is the
	 * mutually connected centre of the program, and a leaf hanging off it
	 * is peripheral whichever way its one edge points (HLR-170). */
	if (igraph_coreness(call, coreness, IGRAPH_ALL) != IGRAPH_SUCCESS)
		return -1;

	return 0;
}

/* Copy the measurements onto the classifications, one node at a time. */
static void record_centralities(const Sdg *g, Classification *out,
                                const igraph_vector_t *hub,
                                const igraph_vector_t *authority,
                                const igraph_vector_t *betweenness,
                                const igraph_vector_int_t *coreness)
{
	for (size_t i = 0; i < g->node_count; i++) {
		/* Zero where the decomposition was not run, which is what an
		 * edgeless graph leaves behind. */
		out[i].hub         = igraph_vector_size(hub) > (igraph_integer_t)i
		                             ? VECTOR(*hub)[i] : 0.0;
		out[i].authority   = igraph_vector_size(authority) >
		                     (igraph_integer_t)i
		                             ? VECTOR(*authority)[i] : 0.0;
		out[i].betweenness = VECTOR(*betweenness)[i];
		out[i].coreness    = (uint32_t)VECTOR(*coreness)[i];
	}
}

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

	if (measure_centralities(call, &hub, &authority, &betweenness,
	                         &coreness, &arpack) != 0)
		goto cleanup;

	record_centralities(g, out, &hub, &authority, &betweenness, &coreness);
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
	/* The action travels with the class, so an ordinary function is
	 * unmasked by construction and a classified one is masked unless a
	 * manifest says otherwise. */
	c->masked = klass != PURIFY_ORDINARY;
}

/* Whether one manifest statement names this node.
 *
 * The file is compared where the manifest gave one and ignored where it did
 * not. Naming it is the precise form and is what `elc` writes; omitting it is
 * the convenience a person hand-editing the file reaches for, and a project
 * with two static functions of one name is the case that makes the difference
 * matter.
 */
static bool names_node(const ManifestEntry *e, const SdgNode *n)
{
	if (!n->name || strcmp(e->function, n->name) != 0)
		return false;
	if (!e->file)
		return true;
	return n->file && strcmp(e->file, n->file) == 0;
}

/* Let the manifest overrule what the centralities concluded (HLR-177).
 *
 * **The statement governs and is not recomputed.** A user who has read the
 * transparency report and disagreed with it has said something `elc` has no
 * grounds to overrule: the classifications are heuristics (HLR-171), and only
 * the user knows whether the function at the centre of their graph is a
 * monolith or a state machine's dispatcher doing exactly its job.
 *
 * A statement matching nothing is *recorded as unmatched* rather than acted
 * on, and the caller reports it. Ending the run there would make a manifest
 * unusable exactly where a large code base most needs one — analysing a single
 * directory of a project the manifest covers whole.
 */
static void apply_manifest(const Sdg *g, Manifest *m, Classification *out)
{
	if (!m)
		return;

	for (size_t e = 0; e < m->count; e++)
		for (size_t i = 0; i < g->node_count; i++) {
			if (!names_node(&m->entries[e], &g->nodes[i]))
				continue;

			out[i].klass          = m->entries[e].klass;
			out[i].masked         = m->entries[e].mask &&
			                        m->entries[e].klass !=
			                                PURIFY_ORDINARY;
			out[i].from_manifest  = true;
			/* No metric triggered this one, and saying otherwise
			 * would present a measurement as the reason for a
			 * decision the user made. The value column is empty
			 * and the source column says where the class came
			 * from (HLR-174, HLR-177). */
			out[i].metric         = PURIFY_METRIC_NONE;
			out[i].value          = 0.0;
			out[i].rank           = 0;
			m->entries[e].matched = true;
		}
}

/* Rank all three centralities, each over the whole node set.
 *
 * One scratch vector serves all three: a rank is taken from a copy of the
 * scores, and the copy is dead the moment `rank_of` returns. Returns 0 on
 * success, -1 on allocation failure with the three rank arrays untouched.
 */
static int rank_centralities(const Classification *c, size_t n,
                             size_t *hub_below, size_t *auth_below,
                             size_t *bet_below)
{
	double *scores = calloc(n, sizeof *scores);
	int     status = -1;

	if (!scores)
		return -1;

	for (size_t i = 0; i < n; i++)
		scores[i] = c[i].hub;
	if (rank_of(scores, n, hub_below) != 0)
		goto cleanup;
	for (size_t i = 0; i < n; i++)
		scores[i] = c[i].authority;
	if (rank_of(scores, n, auth_below) != 0)
		goto cleanup;
	for (size_t i = 0; i < n; i++)
		scores[i] = c[i].betweenness;
	if (rank_of(scores, n, bet_below) != 0)
		goto cleanup;
	status = 0;

cleanup:
	free(scores);
	return status;
}

/* The three tests, applied to one node in the order that settles which class
 * a function satisfying more than one of them is reported as.
 */
static void classify_one(Classification *c, const PurifyThresholds *t,
                         size_t n, size_t hub_below, size_t auth_below,
                         size_t bet_below)
{
	c->hub_rank         = rank_percent(hub_below, n);
	c->authority_rank   = rank_percent(auth_below, n);
	c->betweenness_rank = rank_percent(bet_below, n);

	/* **A god object first** (HLR-169). Where one function satisfies both
	 * centrality tests it is a god object and is reported as one: masking
	 * its edges subsumes masking its incoming edges, and the more specific
	 * claim is the more useful one to a reader. */
	if (rank_at_or_above(bet_below, n, t->god_betweenness) &&
	    rank_at_or_above(hub_below, n, t->god_hub)) {
		triggered_by(c, PURIFY_GOD_OBJECT, PURIFY_METRIC_BETWEENNESS,
		             c->betweenness, c->betweenness_rank);
		return;
	}

	if (rank_at_or_above(auth_below, n, t->sink_authority) &&
	    rank_at_or_below(hub_below, n, t->sink_hub)) {
		triggered_by(c, PURIFY_UTILITY_SINK, PURIFY_METRIC_AUTHORITY,
		             c->authority, c->authority_rank);
		return;
	}

	/* Peripheral last, and only where neither centrality test spoke. Both
	 * of those name a function's part in *fusing* domains, and a function
	 * they named is by construction part of the connected centre this test
	 * is asking about. */
	if (c->coreness < t->core_depth)
		triggered_by(c, PURIFY_PERIPHERAL, PURIFY_METRIC_CORENESS,
		             (double)c->coreness, 0);
}

int classify_nodes(const Sdg *g, const PurifyThresholds *t, Manifest *manifest,
                   Classification *out)
{
	size_t  n = g->node_count;
	size_t *hub_below = NULL, *auth_below = NULL, *bet_below = NULL;
	int     status = -1;

	if (n == 0)
		return 0;

	if (centralities(g, out) != 0)
		return -1;

	hub_below  = calloc(n, sizeof *hub_below);
	auth_below = calloc(n, sizeof *auth_below);
	bet_below  = calloc(n, sizeof *bet_below);
	if (!hub_below || !auth_below || !bet_below)
		goto cleanup;

	if (rank_centralities(out, n, hub_below, auth_below, bet_below) != 0)
		goto cleanup;

	for (size_t i = 0; i < n; i++)
		classify_one(&out[i], t, n, hub_below[i], auth_below[i],
		             bet_below[i]);

	/* **Last, so that it overrules rather than competes** (HLR-177). Every
	 * computed class is in place before a manifest statement is applied,
	 * which is what makes "the statement governs" a property of the order
	 * rather than of a condition scattered through the three tests above. */
	apply_manifest(g, manifest, out);
	status = 0;

cleanup:
	free(bet_below);
	free(auth_below);
	free(hub_below);
	return status;
}

/* ------------------------------------------------------ the masked copy -- */

/* Whether the view keeps one call edge.
 *
 * The three rules, in one place, so that the asymmetry between them is read
 * rather than reconstructed from three call sites (HLR-168 – HLR-170).
 */
static bool masked_as(const Classification *c, PurifyClass klass)
{
	/* The class and the action are two facts, and a manifest may state the
	 * first and withhold the second (HLR-175, HLR-177). Every rule below
	 * asks about the action, so a classification the user kept in the view
	 * changes what is reported and not what is masked. */
	return c->klass == klass && c->masked;
}

bool purify_edge_retained(const Classification *c, uint32_t from, uint32_t to)
{
	/* An excluded node holds no edge: it is not in the view, so nothing
	 * reaches it and nothing leaves it. */
	if (masked_as(&c[from], PURIFY_PERIPHERAL) ||
	    masked_as(&c[to], PURIFY_PERIPHERAL))
		return false;

	/* A god object short-circuits in both directions, so it loses both. */
	if (masked_as(&c[from], PURIFY_GOD_OBJECT) ||
	    masked_as(&c[to], PURIFY_GOD_OBJECT))
		return false;

	/* A utility sink loses its **incoming** edges only. Its outgoing edges
	 * harm nothing: the fusion is between its callers, and masking the node
	 * rather than the edges into it would remove its own position from view
	 * along with the fusion. */
	return !masked_as(&c[to], PURIFY_UTILITY_SINK);
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
		out->included[i] = !masked_as(&c[i], PURIFY_PERIPHERAL);
		if (out->included[i])
			out->included_count++;
	}

	for (size_t i = 0; i < g->edge_count; i++) {
		if (g->edges[i].kind != EDGE_CALL)
			continue;
		calls++;
		if (purify_edge_retained(c, g->edges[i].from, g->edges[i].to))
			out->edge_count++;
	}
	out->masked_edges = calls - out->edge_count;

	if (igraph_vector_int_init(&edges,
	                           (igraph_integer_t)(out->edge_count * 2)) !=
	    IGRAPH_SUCCESS)
		return -1;

	for (size_t i = 0; i < g->edge_count; i++) {
		if (g->edges[i].kind != EDGE_CALL ||
		    !purify_edge_retained(c, g->edges[i].from,
		                          g->edges[i].to))
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

/* ------------------------------------------------------------ the manifest --
 *
 * The one artefact `elc` both writes and reads back, and the only place a
 * third-party library *emits* a format rather than merely parsing one. The two
 * facts are the same fact: a hand-rolled writer paired with a library reader
 * would be two implementations of one format with `elc` on both ends of the
 * disagreement, and the round trip is what makes that disagreement reachable
 * (SDD §20.2.3).
 *
 * What the module owes Jansson is bounded. Jansson decides whether the file is
 * JSON; whether it is a *manifest* — every class name one this build knows, the
 * version one it reads — is decided here, and both failures are refused the
 * same way (HLR-176).
 */

/* One node's place in the written order.
 *
 * Sorted by file, then by line, then by name — the order the report presents
 * the same rows in, so a person reading the report and editing the manifest
 * finds them in the same place, and two runs over one tree produce identical
 * files (HLR-032).
 */
typedef struct {
	const SdgNode *node;
	uint32_t       index;
} WriteOrder;

static int by_file_then_line(const void *a, const void *b)
{
	const WriteOrder *x = a;
	const WriteOrder *y = b;
	const char       *xf = x->node->file ? x->node->file : "";
	const char       *yf = y->node->file ? y->node->file : "";
	int               c  = strcmp(xf, yf);

	if (c != 0)
		return c;
	if (x->node->line_start != y->node->line_start)
		return x->node->line_start < y->node->line_start ? -1 : 1;
	return strcmp(x->node->name ? x->node->name : "",
	              y->node->name ? y->node->name : "");
}

/* Which classifications the manifest carries, in the order it carries them.
 *
 * Returns 0 with `*out` owning `*count_out` entries — possibly none, and
 * possibly NULL where the graph is empty — or -1 with the diagnostic written.
 */
static int manifest_order(const PurifyResults *r, const Sdg *g,
                          WriteOrder **out, size_t *count_out)
{
	WriteOrder *order;
	size_t      count = 0;

	order = calloc(g->node_count ? g->node_count : 1, sizeof *order);
	if (!order) {
		fputs("elc: out of memory writing the purification manifest\n",
		      stderr);
		return -1;
	}

	for (size_t i = 0; i < r->node_count && i < g->node_count; i++) {
		/* Every classification the run made, and no ordinary function
		 * `elc` said nothing about — the same set the transparency
		 * report carries, for the same reason (HLR-174, HLR-175). A
		 * manifest statement counts even where it says "ordinary",
		 * since it is the user's own and round-tripping it is the
		 * point. */
		if (r->classes[i].klass == PURIFY_ORDINARY &&
		    !r->classes[i].from_manifest)
			continue;
		order[count].node  = &g->nodes[i];
		order[count].index = (uint32_t)i;
		count++;
	}
	if (count > 1)
		qsort(order, count, sizeof *order, by_file_then_line);

	*out       = order;
	*count_out = count;
	return 0;
}

/* One row of the `classifications` array, or NULL. */
static json_t *manifest_row(const Classification *c, const SdgNode *node)
{
	json_t *row = json_object();

	if (!row)
		return NULL;
	if (json_object_set_new(row, "function",
	                        json_string(node->name)) != 0 ||
	    json_object_set_new(row, "file",
	                        json_string(node->file ? node->file : "")) != 0 ||
	    json_object_set_new(row, "class",
	                        json_string(purify_class_name(c->klass))) != 0 ||
	    json_object_set_new(row, "mask", json_boolean(c->masked)) != 0) {
		json_decref(row);
		return NULL;
	}
	return row;
}

/* The whole document, built in memory, or NULL on allocation failure. */
static json_t *manifest_document(const PurifyResults *r,
                                 const WriteOrder *order, size_t count)
{
	json_t *root = json_object();
	json_t *rows = json_array();

	if (!root || !rows) {
		json_decref(rows);
		json_decref(root);
		return NULL;
	}

	/* Versioned in the manner of the XML record (HLR-061), so a manifest
	 * written by a later build is rejected by an earlier one rather than
	 * half-understood. */
	if (json_object_set_new(root, "manifest-version",
	                        json_integer(ELC_MANIFEST_VERSION)) != 0 ||
	    json_object_set_new(root, "classifications", rows) != 0) {
		json_decref(rows);
		json_decref(root);
		return NULL;
	}
	/* `root` owns `rows` from here, and frees it with itself. */

	for (size_t i = 0; i < count; i++) {
		json_t *row = manifest_row(&r->classes[order[i].index],
		                           order[i].node);

		if (!row || json_array_append_new(rows, row) != 0) {
			json_decref(row);
			json_decref(root);
			return NULL;
		}
	}
	return root;
}

/* Write the document to `path`, diagnosing every failure itself.
 *
 * Written through a stream of our own rather than `json_dump_file`, for one
 * byte: a text file this project writes ends with a newline, and Jansson's file
 * writer does not add one. A manifest is meant to be hand-edited and put under
 * version control, and a file without a final newline is one every such tool
 * complains about.
 */
static int manifest_emit(json_t *root, const char *path)
{
	FILE *file = fopen(path, "w");

	if (!file) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (json_dumpf(root, file, JSON_INDENT(2)) != 0 ||
	    fputc('\n', file) == EOF) {
		fclose(file);
		fprintf(stderr, "elc: %s: the purification manifest could not "
		        "be written\n", path);
		return -1;
	}
	if (fclose(file) != 0) {
		fprintf(stderr, "elc: %s: the purification manifest could not "
		        "be written\n", path);
		return -1;
	}
	return 0;
}

int manifest_write(const PurifyResults *r, const Sdg *g, const char *path)
{
	WriteOrder *order = NULL;
	size_t      count = 0;
	json_t     *root;
	int         status;

	if (!r || !g || !path)
		return -1;

	if (manifest_order(r, g, &order, &count) != 0)
		return -1;

	root = manifest_document(r, order, count);
	free(order);
	if (!root) {
		fputs("elc: out of memory writing the purification manifest\n",
		      stderr);
		return -1;
	}

	status = manifest_emit(root, path);
	json_decref(root);
	return status;
}

/* Refuse the file, naming what is wrong with it.
 *
 * One exit for every rejection, so that a syntax fault and a semantic one are
 * refused in the same words and with the same status: the difference between
 * them is Jansson's business, and to a user who hand-edited the file both are
 * "this is not a manifest I can read" (HLR-176).
 */
static int manifest_reject(const char *path, const char *why)
{
	fprintf(stderr, "elc: %s: %s\n", path, why);
	return -1;
}

/* Whether a JSON object is a statement `elc` can act on, and what class it
 * names.
 *
 * Split from the construction below because the two answer different questions:
 * this one is the whole of what a hand-edited file is checked against, and
 * every one of its refusals is a message a user has to be able to act on
 * (HLR-176). Returns 0 with the four members and the parsed class published,
 * -1 with the diagnostic already written.
 */
static int entry_fields(const char *path, json_t *item, json_t **function,
                        json_t **file, json_t **mask, PurifyClass *parsed)
{
	json_t *klass;

	if (!json_is_object(item))
		return manifest_reject(path, "a classification is not an object");

	*function = json_object_get(item, "function");
	*file     = json_object_get(item, "file");
	klass     = json_object_get(item, "class");
	*mask     = json_object_get(item, "mask");

	if (!json_is_string(*function) ||
	    json_string_value(*function)[0] == '\0')
		return manifest_reject(path,
		                       "a classification names no function");
	if (!json_is_string(klass))
		return manifest_reject(path,
		                       "a classification names no class");
	if (!purify_class_from_name(json_string_value(klass), parsed))
		return manifest_reject(path,
		                       "a classification names a class this "
		                       "build does not know");
	if (*file && !json_is_string(*file))
		return manifest_reject(path, "a classification's file is not "
		                       "a string");
	if (*mask && !json_is_boolean(*mask))
		return manifest_reject(path, "a classification's mask is not "
		                       "true or false");
	return 0;
}

/* Read one statement out of a JSON object.
 *
 * Returns 0 on success, -1 with the diagnostic already written. `function` is
 * required and `class` is required; `file` may be absent, in which case the
 * statement matches by name alone; `mask` may be absent and defaults to true,
 * which is what a user adding a classification by hand means by writing one.
 */
static int read_entry(const char *path, json_t *item, ManifestEntry *out)
{
	json_t       *function, *file, *mask;
	PurifyClass   parsed;
	ManifestEntry entry = { 0 };

	if (entry_fields(path, item, &function, &file, &mask, &parsed) != 0)
		return -1;

	/* Built complete and published in one step, so a statement that runs
	 * out of memory halfway leaves nothing behind for the caller's teardown
	 * to miss. */
	entry.function = strdup(json_string_value(function));
	if (!entry.function)
		return manifest_reject(path, "out of memory");

	/* An empty file is the same statement as an absent one: `elc` writes
	 * the field for every row, and a user who clears it means "wherever
	 * this function is defined". */
	if (file && json_string_value(file)[0] != '\0') {
		entry.file = strdup(json_string_value(file));
		if (!entry.file) {
			free(entry.function);
			return manifest_reject(path, "out of memory");
		}
	}

	entry.klass = parsed;
	entry.mask  = mask ? json_boolean_value(mask)
	                   : parsed != PURIFY_ORDINARY;
	*out = entry;
	return 0;
}

int manifest_read(const char *path, Manifest *out)
{
	json_error_t error;
	json_t      *root = NULL;
	json_t      *rows;
	json_t      *version;
	int          status = -1;

	memset(out, 0, sizeof *out);

	/* **The path, and nowhere else** (HLR-176). Nothing here searches the
	 * working directory, the analysis target, an ancestor of either, or a
	 * dotfile: the manifest is read because the user named it, exactly as a
	 * custom rule file is, and two people running the same command on the
	 * same tree still obtain the same result (HLR-039). */
	root = json_load_file(path, 0, &error);
	if (!root) {
		/* Jansson carries the line and column of a syntax fault, and
		 * the diagnostic quotes them: a person who hand-edited the file
		 * needs to be told *where* they broke it, not merely that they
		 * did. */
		fprintf(stderr, "elc: %s:%d:%d: %s\n", path, error.line,
		        error.column, error.text);
		return -1;
	}

	if (!json_is_object(root)) {
		manifest_reject(path, "not a purification manifest");
		goto cleanup;
	}

	version = json_object_get(root, "manifest-version");
	if (!json_is_integer(version)) {
		manifest_reject(path, "not a purification manifest: it states "
		                "no manifest-version");
		goto cleanup;
	}
	if (json_integer_value(version) != ELC_MANIFEST_VERSION) {
		fprintf(stderr, "elc: %s: manifest version %lld is not one this "
		        "build reads (expected %d)\n", path,
		        (long long)json_integer_value(version),
		        ELC_MANIFEST_VERSION);
		goto cleanup;
	}

	rows = json_object_get(root, "classifications");
	if (!json_is_array(rows)) {
		manifest_reject(path, "not a purification manifest: it holds "
		                "no classifications array");
		goto cleanup;
	}

	out->entries = calloc(json_array_size(rows) ? json_array_size(rows) : 1,
	                      sizeof *out->entries);
	if (!out->entries) {
		manifest_reject(path, "out of memory");
		goto cleanup;
	}

	for (size_t i = 0; i < json_array_size(rows); i++) {
		if (read_entry(path, json_array_get(rows, i),
		               &out->entries[out->count]) != 0)
			goto cleanup;
		out->count++;
	}
	status = 0;

cleanup:
	/* **Rejected rather than partly applied.** A run governed by half of
	 * what its author wrote is worse than one that stops: the half that
	 * took effect is invisible, and the user has no way to tell which. */
	if (status != 0)
		manifest_free(out);
	json_decref(root);
	return status;
}

void manifest_free(Manifest *m)
{
	if (!m)
		return;

	for (size_t i = 0; i < m->count; i++) {
		free(m->entries[i].function);
		free(m->entries[i].file);
	}
	free(m->entries);
	memset(m, 0, sizeof *m);
}

/* ------------------------------------------------------------- the pass -- */

/* Report every manifest statement that named no analysed function (HLR-177).
 *
 * A diagnostic and nothing more. Analysing one directory of a project whose
 * manifest covers all of it is ordinary use, and rejecting the file there would
 * make a manifest unusable exactly where a large code base most needs one —
 * the rule a declared entry point matching nothing already follows
 * (LLR-CTR-08). It is *reported* rather than passed over in silence because a
 * statement that matched nothing is far more often a typo than a deliberate
 * partial run, and a user who never hears about it goes on believing their
 * correction took effect.
 */
static void warn_unmatched(const Manifest *m)
{
	if (!m)
		return;

	for (size_t e = 0; e < m->count; e++) {
		if (m->entries[e].matched)
			continue;
		fprintf(stderr,
		        "elc: the manifest names '%s'%s%s, which no analysed "
		        "file defines; the statement is ignored\n",
		        m->entries[e].function,
		        m->entries[e].file ? " in " : "",
		        m->entries[e].file ? m->entries[e].file : "");
	}
}

int purify_analyse(const Sdg *g, const ElcOptions *opts, Manifest *manifest,
                   PurifyResults *out)
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

	if (classify_nodes(g, &out->thresholds, manifest, out->classes) != 0)
		return -1;

	warn_unmatched(manifest);

	/* A manifest statement counts as a classification even where it says
	 * "ordinary", so that a reader of the report can see that the tool was
	 * overruled here rather than that it concluded nothing (HLR-177). */
	for (size_t i = 0; i < g->node_count; i++)
		if (out->classes[i].klass != PURIFY_ORDINARY ||
		    out->classes[i].from_manifest)
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
