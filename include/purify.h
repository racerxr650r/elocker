/* purify.h — the recovery view of the call graph.
 *
 * A raw call graph rarely sorts into layers. A logger every module calls and a
 * dispatcher that calls everything each join parts of a program that have
 * nothing to do with one another, and a topological ordering computed over such
 * a graph collapses into one tangled stratum describing nothing. This module
 * sets those nodes aside so that the structure underneath them can be seen
 * (doc/SDD.md §20).
 *
 * **The recovery view is a second graph, and that is structural rather than
 * remembered** (HLR-167). Masking produces a *copy* of the call view; the `Sdg`
 * every other stage reads is not modified, and `purify_analyse` takes it by
 * const pointer so that it cannot be. A stage that cannot reach a masked graph
 * cannot accidentally measure one — where masking in place and unmasking
 * afterwards would make every analysis order-dependent and leave the run one
 * early return away from reporting a fan-out that omits real calls.
 *
 * The copy is of the **call view** alone. A global-state edge takes no part in
 * a layering: writing an object another function reads is coupling and not
 * invocation, and including such edges would join every pair of functions
 * sharing a variable into the layer structure.
 *
 * **Nothing here judges.** A classification carries no severity and becomes no
 * finding (HLR-171, HLR-101): "god object" states where a function sits in a
 * graph, not that the design is wrong. The thresholds behind the
 * classifications are `elc`'s own heuristics and say so wherever one is
 * reported.
 */
#ifndef ELC_PURIFY_H
#define ELC_PURIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elc.h"
#include "graph.h"

/* What purification concluded about one function.
 *
 * Ordinary is the zero, so a zeroed table reads as "nothing was concluded",
 * which is what a graph too small to rank leaves behind.
 */
typedef enum {
	PURIFY_ORDINARY = 0,
	PURIFY_UTILITY_SINK,   /* high authority, hub near zero  (HLR-168) */
	PURIFY_GOD_OBJECT,     /* high betweenness and high hub  (HLR-169) */
	PURIFY_PERIPHERAL      /* outside the configured core    (HLR-170) */
} PurifyClass;

/* Which measurement triggered a classification.
 *
 * Carried so that the transparency report can name it beside the value
 * (HLR-174). A classification a reader cannot trace back to a number is an
 * assertion, which is the thing that requirement exists to prevent.
 */
typedef enum {
	PURIFY_METRIC_NONE = 0,
	PURIFY_METRIC_AUTHORITY,
	PURIFY_METRIC_BETWEENNESS,
	PURIFY_METRIC_CORENESS
} PurifyMetric;

/* One function's centralities, its position in each distribution, and what
 * followed from them.
 *
 * The ranks are carried beside the raw scores because the *rank* is what the
 * thresholds are compared against and the *score* is what a reader recognises.
 * A report naming only the rank could not be checked against the graph, and one
 * naming only the score could not be checked against the threshold that acted
 * on it.
 *
 * Each rank is the percentage of the **other** nodes scoring strictly below
 * this one — so the top of any distribution is 100 whatever the size of the
 * graph, which is what lets one default threshold serve a nine-function fixture
 * and a nine-hundred-function project alike (LLR-CLS-01).
 */
typedef struct {
	double       hub;
	double       authority;
	double       betweenness;
	uint32_t     coreness;
	uint32_t     hub_rank;          /* percent, 0 – 100 */
	uint32_t     authority_rank;
	uint32_t     betweenness_rank;
	PurifyClass  klass;
	PurifyMetric metric;            /* the one that triggered the class */
	double       value;             /* its value                        */
	uint32_t     rank;              /* its rank; unused for coreness    */
	/* Whether the recovery view applies this class's masking action.
	 *
	 * Computed classifications set it with the class, so the two travel
	 * together and an ordinary function is unmasked by construction. A
	 * manifest statement may set it false while keeping the class, which is
	 * the case HLR-175 exists for: a user who agrees that a function is a
	 * dispatcher and disagrees that it should be set aside says so here,
	 * and the classification stays reportable while the masking stops. */
	bool         masked;
	/* True where the class above was stated by a manifest rather than
	 * computed (HLR-177).
	 *
	 * Carried so the report can say which of the assumptions in front of a
	 * reader are `elc`'s and which are their own team's. Without it the two
	 * are indistinguishable in the one section whose purpose is to be
	 * inspected. */
	bool         from_manifest;
} Classification;

/* The masked copy of the call view.
 *
 * **Vertex identifiers are the `Sdg`'s own**, so a result read off this graph
 * indexes the node table directly and a tie broken by vertex identifier is a
 * tie broken by the stable node identifier of HLR-033. A peripheral node is
 * therefore excluded by `included[i] == false` and by holding no edge, rather
 * than by being renumbered out of existence — renumbering would put the
 * determinism of HLR-179 on a mapping instead of on the identifier the rest of
 * the run already agrees about.
 *
 * A node this view excludes is given **no layer at all** (HLR-170). A function
 * `elc` did not consider is not a function `elc` placed at the bottom of the
 * architecture, and a consumer that conflated the two would drop every leaf
 * into the lowest layer.
 */
typedef struct {
	void   *graph;          /* igraph_t * over the retained call edges  */
	bool   *included;       /* node_count entries; owned                */
	size_t  node_count;     /* the Sdg's, unchanged                     */
	size_t  included_count;
	size_t  edge_count;     /* call edges the view retained             */
	size_t  masked_edges;   /* call edges the masking removed           */
} RecoveryView;

typedef struct {
	Classification  *classes;     /* one per node; owned            */
	size_t           node_count;
	size_t           classified;  /* the non-ordinary among them    */
	RecoveryView     view;
	PurifyThresholds thresholds;  /* the values that were in force  */
} PurifyResults;

/* The manifest format this build reads and writes (HLR-061's rule, applied to
 * a second artefact).
 *
 * Versioned so that a manifest written by a later build is rejected by an
 * earlier one rather than half-understood — the failure a reader would never
 * see, because a file that parses as JSON and means something else looks like
 * a working manifest right up to the point where it silently classifies
 * nothing.
 */
#define ELC_MANIFEST_VERSION 1

/* One statement a manifest makes about one function (HLR-175, HLR-177).
 *
 * `file` is optional and NULL where the manifest omitted it, in which case the
 * statement matches by function name alone. Naming the file is the precise
 * form and is what `elc` writes; omitting it is the convenience a person
 * hand-editing the file will reach for, and a project with two static
 * functions of one name is the case that makes the distinction matter.
 */
typedef struct {
	char        *function;   /* owned                                   */
	char        *file;       /* owned; NULL matches any file            */
	PurifyClass  klass;
	bool         mask;       /* whether the view applies the action     */
	/* Whether this statement named a function the run analysed.
	 *
	 * Tracked rather than assumed, because a statement that matched
	 * nothing is reported and ignored rather than ending the run:
	 * analysing one directory of a project whose manifest covers all of it
	 * is ordinary use, exactly as a declared entry point matching nothing
	 * is (HLR-177, LLR-CTR-08). */
	bool         matched;
} ManifestEntry;

typedef struct {
	ManifestEntry *entries;   /* owned */
	size_t         count;
} Manifest;

/* Classify every function and build the masked recovery view.
 *
 * `g` is read and never written: the recovery view is a copy, and this
 * signature is where that is enforced rather than remembered (HLR-167). The
 * manifest of HLR-175 – HLR-177 enters as a further argument with the module
 * that reads it.
 *
 * Returns 0 on success; non-zero on allocation failure or on a failure inside
 * the graph library. A graph too small to rank is not a failure — it classifies
 * nothing and yields an empty view (HLR-115).
 */
int purify_analyse(const Sdg *g, const ElcOptions *opts, Manifest *manifest,
                   PurifyResults *out);

/* Assign each node its class against the thresholds in force.
 *
 * `out` must hold `g->node_count` entries. Exposed because the three
 * classifications and the precedence between them are the whole of what this
 * module decides, and are worth pinning without a report in the way.
 */
int classify_nodes(const Sdg *g, const PurifyThresholds *t, Manifest *manifest,
                   Classification *out);

/* Copy the call view, omitting the masked edges and the peripheral nodes.
 *
 * Returns 0 on success. The `Sdg` is untouched on every path.
 */
int build_recovery_view(const Sdg *g, const Classification *c,
                        RecoveryView *out);

/* Whether the recovery view keeps one call edge.
 *
 * Exposed so that the purified drawing of HLR-178 asks the same question the
 * view was built from rather than reimplementing the three masking rules. Two
 * answers to one question is how a drawing comes to show a graph the analysis
 * did not read — which would defeat the whole purpose of drawing it.
 */
bool purify_edge_retained(const Classification *c, uint32_t from, uint32_t to);

/* Compare two scores to the tolerance HLR-179 requires be stated.
 *
 * Returns -1, 0, or 1. HITS is iterative and its scores are approximations, so
 * two nodes whose scores differ in the last bits hold the *same* position in
 * the distribution; without a stated tolerance the same source classifies
 * differently on two machines and HLR-032 fails in a way no fixture reliably
 * catches.
 */
int purify_score_cmp(double a, double b);

/* The names a report prints. One definition each, so that four renderers and
 * the saved record cannot disagree about what a class is called or about what
 * masking one did. */
const char *purify_class_name(PurifyClass klass);
const char *purify_metric_name(PurifyMetric metric);
const char *purify_action_name(PurifyClass klass, bool masked);

/* The inverse of `purify_class_name`, for the manifest read path.
 *
 * Returns true and sets `*out` where the name is one this build knows. A name
 * it does not know is not a class it can guess at: the manifest is rejected
 * rather than the statement silently dropped, because well-formed JSON that is
 * not a manifest is exactly what HLR-176 asks be refused.
 */
bool purify_class_from_name(const char *name, PurifyClass *out);

/* Copy the classifications onto an assembled report.
 *
 * Declared here rather than in `report.h`, exactly as `report_set_arch` is
 * declared in `arch.h`: the report model must not have to know what a
 * `PurifyResults` is in order to be included.
 *
 * Each node identifier is resolved to the name and location a reader can act
 * on, for the reason every other row does it — an index into a table that no
 * longer exists is not something a reader can open a file with (HLR-174).
 */
int report_set_purify(Report *report, const PurifyResults *purify,
                      const Sdg *g, const ElcOptions *opts);

/* Parse the manifest at `path`, which the user named on the command line.
 *
 * **Read only when named** (HLR-176). Nothing here searches: not the working
 * directory, not the analysis target, not an ancestor of either, and no
 * dotfile. The path arrives from `argv` and is the whole of where a manifest
 * can come from, which is what leaves the zero-configuration guarantee of
 * HLR-039 exactly as it was.
 *
 * Returns 0 on success, -1 with a diagnostic already on stderr otherwise. A
 * file that is not JSON and a file that is JSON but not a *manifest* fail the
 * same way and for the same reason: the user named the file, so the failure is
 * theirs to correct, and a partly applied manifest would be a run governed by
 * half of what its author wrote.
 */
int manifest_read(const char *path, Manifest *out);

/* Write the classifications to `path` in the documented format, ready to be
 * edited and handed back (HLR-175).
 *
 * The `Sdg` is needed and the `PurifyResults` alone is not: a classification
 * is held against a node identifier, and an identifier is not something a
 * person can edit. The rows are written in the report's own order — by file,
 * then by line — so that two runs over one tree produce byte-identical
 * manifests (HLR-032).
 *
 * Returns 0 on success; -1 with a diagnostic naming the file otherwise.
 */
int manifest_write(const PurifyResults *r, const Sdg *g, const char *path);

/* Release a manifest's statements. Safe on NULL and on a zeroed record. */
void manifest_free(Manifest *m);

/* Release the classifications and the recovery view. Safe on NULL. */
void purify_results_free(PurifyResults *r);

#endif /* ELC_PURIFY_H */
