/* recover.c — a layering read off the purified recovery view.
 *
 * Topological ordering of the view `purify.c` built, folded into per-directory
 * layers, and rendered as the arguments a user would have typed to declare it
 * (doc/SDD.md §21).
 *
 * **Everything here is subordinate to one boundary** (HLR-173): what this
 * module produces is a *proposal*, and a proposal is never the baseline it is
 * measured against. The boundary is kept by the dependency direction rather
 * than by care — `arch.c` includes no header of this module, holds no result
 * of it, and is handed no path to one — because a rule enforced by
 * remembering is a rule one refactor away from being forgotten. A tool
 * measuring conformance against its own proposal would find every code base
 * conformant, since the standard would have been read off the thing it judged.
 *
 * The second boundary is HLR-101's, and it is why nothing below ranks or
 * scores the architecture it describes. A recovered layering states the order
 * the graph already has. It does not say the design is wrong, name a component
 * as belonging somewhere else, or compare what it found against what was
 * declared — every one of which would cross from describing a structure into
 * prescribing one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>

#include "elc.h"
#include "graph.h"
#include "purify.h"
#include "recover.h"
#include "report.h"

/* ------------------------------------------------------------- the score --
 *
 * A directory's position is a fraction — the mean topological position of its
 * functions, weighted by how many retained edges each carries — and the
 * fractions are compared **exactly**, never as floating point.
 *
 * Exactness is not fastidiousness here. Two directories whose weighted means
 * are equal belong in one layer, and a comparison that decided otherwise on
 * the last bits of a division would split them on one machine and not on
 * another; HLR-179 exists to remove precisely that.
 */

/* Compare a/b against c/d exactly, with no product large enough to overflow.
 *
 * Cross-multiplying is the obvious way and is wrong for a big project: the
 * numerator is a sum of position × degree over every retained function, and
 * the product of two such numerators leaves 64 bits behind. The continued
 * fraction below compares the integer parts, then the reciprocals of the
 * remainders with the order inverted, and never multiplies anything.
 *
 * Both denominators are non-zero by construction: a directory with no
 * retained function is not a row.
 */
static int frac_cmp(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	int sign = 1;

	for (;;) {
		uint64_t qa = a / b;
		uint64_t qc = c / d;
		uint64_t ra, rc, na, nb;

		if (qa != qc)
			return qa < qc ? -sign : sign;

		ra = a % b;
		rc = c % d;
		if (ra == 0 && rc == 0)
			return 0;
		if (ra == 0)
			return -sign;
		if (rc == 0)
			return sign;

		/* ra/b < rc/d exactly when b/ra > d/rc, so the reciprocals are
		 * compared next and the sense of the answer is flipped. */
		na = b;
		nb = ra;
		a  = d;
		b  = rc;
		c  = na;
		d  = nb;
		sign = -sign;
	}
}

/* One directory, accumulating its position before the layers are cut.
 *
 * Two sums are kept rather than one. The weighted sum is the answer the
 * requirement asks for — *where the bulk of a directory's edges point* — and
 * the unweighted one is what a directory of functions holding no retained edge
 * at all falls back to, since a weighted mean over a total weight of zero is
 * not a number.
 */
typedef struct {
	const char *directory;   /* borrowed from the report model */
	uint64_t    weighted;    /* sum of position x degree       */
	uint64_t    weight;      /* sum of degree                  */
	uint64_t    plain;       /* sum of position                */
	uint64_t    plain_count; /* functions counted              */
	size_t      functions;
	size_t      layer;
} DirScore;

static void score_of(const DirScore *d, uint64_t *num, uint64_t *den)
{
	if (d->weight > 0) {
		*num = d->weighted;
		*den = d->weight;
		return;
	}
	/* Every function here is isolated in the recovery view — its calls
	 * were masked, or it made none. The order still places them, so the
	 * plain mean is used rather than the directory being dropped: a
	 * directory `elc` retained is one it has something to say about. */
	*num = d->plain;
	*den = d->plain_count;
}

/* Ascending by position, ties broken by directory path.
 *
 * The tie-break is what makes two directories at one position render in the
 * same order on every run; without it the order would be the order the node
 * table happened to reach them in (HLR-032, HLR-179).
 */
static int by_score_then_path(const void *a, const void *b)
{
	const DirScore *x = a;
	const DirScore *y = b;
	uint64_t        xn, xd, yn, yd;
	int             c;

	score_of(x, &xn, &xd);
	score_of(y, &yn, &yd);
	c = frac_cmp(xn, xd, yn, yd);
	if (c != 0)
		return c;
	return strcmp(x->directory, y->directory);
}

/* -------------------------------------------------------------- the fold -- */

/* The directory a node belongs to, or NULL where the run cannot say.
 *
 * Read from the report's own record of it rather than sliced off the path
 * here. More than one analysis groups by directory, and two consumers each
 * slicing a path for themselves is how two of them come to disagree about
 * which directory a file is in (HLR-160).
 */
static const char *directory_of(const Sdg *g, const Report *r, size_t node)
{
	size_t component = g->nodes[node].component;

	if (component >= r->file_count || !r->files[component])
		return NULL;
	return r->files[component]->directory;
}

/* The number of retained edges one node carries, in either direction.
 *
 * This is the weight in the mean, and it is what keeps one function reaching
 * far down the order from dragging its whole directory with it: a function
 * holding one edge counts once against the ten held by the rest.
 */
static uint64_t degree_in_view(const igraph_t *view, size_t node)
{
	igraph_integer_t d = 0;

	/* Loops excluded. A function calling itself is coupled to nothing, and
	 * counting the self-call would weight it twice over towards a position
	 * it had no part in choosing. */
	if (igraph_degree_1(view, &d, (igraph_integer_t)node, IGRAPH_ALL,
	                    IGRAPH_NO_LOOPS) != IGRAPH_SUCCESS)
		return 0;
	return (uint64_t)d;
}

static DirScore *find_dir(DirScore *dirs, size_t count, const char *path)
{
	for (size_t i = 0; i < count; i++)
		if (strcmp(dirs[i].directory, path) == 0)
			return &dirs[i];
	return NULL;
}

/* Fold every included node onto the directory that defines it, accumulating
 * both the weighted and the plain position of each.
 *
 * Returns the number of distinct directories found, having filled that many
 * leading entries of `dirs`.
 */
static size_t tally_directories(const PurifyResults *p, const Sdg *g,
                                const Report *r, const size_t *order,
                                DirScore *dirs)
{
	const igraph_t *view  = (const igraph_t *)p->view.graph;
	size_t          count = 0;

	for (size_t i = 0; i < g->node_count && i < p->view.node_count; i++) {
		const char *dir;
		DirScore   *slot;
		uint64_t    weight;

		/* **An excluded node has no layer** (HLR-170). It was not
		 * considered, and a fold that read the excluded vertices back
		 * in would put every leaf in the bottom layer — the one thing
		 * that requirement names. */
		if (!p->view.included[i])
			continue;

		dir = directory_of(g, r, i);
		if (!dir)
			continue;

		slot = find_dir(dirs, count, dir);
		if (!slot) {
			slot            = &dirs[count++];
			slot->directory = dir;
		}

		weight            = degree_in_view(view, i);
		slot->weighted   += (uint64_t)order[i] * weight;
		slot->weight     += weight;
		slot->plain      += (uint64_t)order[i];
		slot->plain_count++;
		slot->functions++;
	}
	return count;
}

/* Number the layers of an already-sorted directory list.
 *
 * **Equal positions are one layer, not two.** Two directories the ordering
 * places level with one another are level with one another, and cutting
 * between them would invent a dependency direction the graph does not hold —
 * which is the kind of claim HLR-101 forbids.
 */
static void assign_layers(DirScore *dirs, size_t count)
{
	dirs[0].layer = 0;

	for (size_t i = 1; i < count; i++) {
		uint64_t an, ad, bn, bd;

		score_of(&dirs[i - 1], &an, &ad);
		score_of(&dirs[i], &bn, &bd);
		dirs[i].layer = dirs[i - 1].layer +
		                (frac_cmp(an, ad, bn, bd) == 0 ? 0 : 1);
	}
}

/* Copy the numbered directories onto the results, taking a copy of each path
 * so the results outlive the graph they were read from. Returns 0 on success.
 */
static int publish_layers(const DirScore *dirs, size_t count,
                          RecoveryResults *out)
{
	out->layers = calloc(count, sizeof *out->layers);
	if (!out->layers)
		return -1;

	for (size_t i = 0; i < count; i++) {
		out->layers[i].directory = strdup(dirs[i].directory);
		if (!out->layers[i].directory)
			return -1;
		out->layers[i].layer     = dirs[i].layer;
		out->layers[i].functions = dirs[i].functions;
		out->layer_count++;
	}
	out->strata = dirs[count - 1].layer + 1;
	out->state  = RECOVERY_PROPOSED;
	return 0;
}

int layer_by_directory(const PurifyResults *p, const Sdg *g, const Report *r,
                       const size_t *order, RecoveryResults *out)
{
	DirScore *dirs;
	size_t    count;
	int       status;

	dirs = calloc(g->node_count ? g->node_count : 1, sizeof *dirs);
	if (!dirs)
		return -1;

	count = tally_directories(p, g, r, order, dirs);
	if (count == 0) {
		out->state = RECOVERY_OMITTED_EMPTY;
		free(dirs);
		return 0;
	}

	if (count > 1)
		qsort(dirs, count, sizeof *dirs, by_score_then_path);
	assign_layers(dirs, count);
	status = publish_layers(dirs, count, out);

	free(dirs);
	return status;
}

/* ------------------------------------------------------------ the cycles -- */

/* Render one strongly connected component of the view as its membership.
 *
 * **Comma-separated, not joined with arrows**, by the rule the recursion
 * report already follows. A strongly connected component is a *set*: every
 * member can reach every other, but the decomposition yields no order, and
 * `a -> b -> c` would assert a path that may not exist. The set is the true
 * statement and the useful one — breaking any edge among these functions is
 * what would make a layering possible.
 *
 * Members in ascending node identifier, so the text is the same on every run
 * whatever order the library enumerated them in (HLR-033, HLR-179).
 */
static char *render_cycle(const Sdg *g, const size_t *members, size_t count)
{
	size_t len = 1;
	char  *text;

	for (size_t i = 0; i < count; i++)
		len += strlen(g->nodes[members[i]].name) + 2;

	text = malloc(len);
	if (!text)
		return NULL;
	text[0] = '\0';

	for (size_t i = 0; i < count; i++) {
		if (i)
			strcat(text, ", ");
		strcat(text, g->nodes[members[i]].name);
	}
	return text;
}

static int by_text(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Collect the cycles the recovery view still holds.
 *
 * **Not an error, and not worked around** (HLR-172). Purification often breaks
 * the cycles a god object created, which is much of its purpose — but where
 * cycles remain there is no topological order to have, and ordering the graph
 * arbitrarily would present an invention as a reading. The cycles are the
 * finding, exactly as HLR-090 reports them in place of a call depth.
 */
static int render_components(const PurifyResults *p, const Sdg *g,
                             const igraph_vector_int_t *membership,
                             const igraph_vector_int_t *sizes,
                             igraph_integer_t components, size_t *members,
                             RecoveryResults *out)
{
	for (igraph_integer_t c = 0; c < components; c++) {
		size_t count = 0;
		char  *text;

		/* **Two or more members.** A component of one is a single
		 * function, and the graph this runs over holds no self-call —
		 * a function calling itself orders nothing, and reporting it
		 * here would repeat a fact the recursion section already
		 * states while costing a user the analysis this section exists
		 * for. */
		if (VECTOR(*sizes)[c] < 2)
			continue;

		for (size_t i = 0; i < g->node_count &&
		     (igraph_integer_t)i < igraph_vector_int_size(membership);
		     i++)
			if (VECTOR(*membership)[i] == c && p->view.included[i])
				members[count++] = i;
		if (count == 0)
			continue;

		text = render_cycle(g, members, count);
		if (!text)
			return -1;
		out->cycles[out->cycle_count++] = text;
	}

	if (out->cycle_count > 1)
		qsort(out->cycles, out->cycle_count, sizeof *out->cycles,
		      by_text);
	return 0;
}

static int collect_cycles(const PurifyResults *p, const Sdg *g,
                          const igraph_t *view, RecoveryResults *out)
{
	igraph_vector_int_t membership;
	igraph_vector_int_t sizes;
	igraph_integer_t    components = 0;
	size_t             *members    = NULL;
	int                 status     = -1;
	bool                have_mem = false, have_size = false;

	out->state = RECOVERY_CYCLIC;

	if (igraph_vector_int_init(&membership, 0) != IGRAPH_SUCCESS)
		goto cleanup;
	have_mem = true;
	if (igraph_vector_int_init(&sizes, 0) != IGRAPH_SUCCESS)
		goto cleanup;
	have_size = true;

	if (igraph_connected_components(view, &membership, &sizes, &components,
	                                IGRAPH_STRONG) != IGRAPH_SUCCESS)
		goto cleanup;

	members = calloc(g->node_count ? g->node_count : 1, sizeof *members);
	out->cycles = calloc((size_t)components ? (size_t)components : 1,
	                     sizeof *out->cycles);
	if (!members || !out->cycles)
		goto cleanup;

	status = render_components(p, g, &membership, &sizes, components,
	                           members, out);

cleanup:
	free(members);
	if (have_size)
		igraph_vector_int_destroy(&sizes);
	if (have_mem)
		igraph_vector_int_destroy(&membership);
	return status;
}

/* ---------------------------------------------------------- the proposal -- */

/* A growable text buffer, for the one string this module builds.
 *
 * The proposal's length is a function of the project's directory count, which
 * nothing here knows in advance, and a fixed buffer would silently truncate an
 * argument list — producing something that looks adoptable and is not.
 */
typedef struct {
	char  *text;
	size_t len;
	size_t capacity;
} Buffer;

static int buffer_add(Buffer *b, const char *text)
{
	size_t add = strlen(text);

	if (b->len + add + 1 > b->capacity) {
		size_t next = b->capacity ? b->capacity * 2 : 256;
		char  *grown;

		while (next < b->len + add + 1)
			next *= 2;
		grown = realloc(b->text, next);
		if (!grown)
			return -1;
		b->text     = grown;
		b->capacity = next;
	}
	memcpy(b->text + b->len, text, add + 1);
	b->len += add;
	return 0;
}

/* The last path component, which is what a layer is named after.
 *
 * A directory basename is what a reader of the proposal already calls that
 * part of the program, so it is the name they are most likely to keep.
 */
static const char *basename_of(const char *path)
{
	const char *slash = strrchr(path, '/');

	if (!slash || slash[1] == '\0')
		return path;
	return slash + 1;
}

/* A layer name `--stratum` will accept and `--stratum-order` can separate.
 *
 * A colon ends the name in one option and a `>` separates names in the other,
 * so neither may appear in a name; anything else outside the portable
 * filename set is replaced too, since a name is copied into a shell command
 * and a proposal that needed quoting to be adopted would not be adoptable.
 */
static void sanitise(const char *from, char *out, size_t size)
{
	size_t at = 0;

	for (const char *p = from; *p && at + 1 < size; p++) {
		bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ||
		          *p == '.';

		out[at++] = ok ? *p : '_';
	}
	out[at] = '\0';
	if (at == 0)
		snprintf(out, size, "layer");
}

/* How deep a directory sits, counted in separators.
 *
 * Used to order the `--stratum` declarations, and the reason is a property of
 * the option rather than a preference. `stratum_of_components` takes the
 * *first* declared layer whose pattern matches a file, and a directory
 * wildcard matches everything beneath that directory and not merely the files
 * directly in it — so a parent directory
 * declared before its child would claim the child's files. Declaring the
 * deepest first removes that, and costs nothing: a layer's ordinal comes from
 * `--stratum-order`, which is emitted beside them, and not from the order the
 * declarations happen to appear in.
 */
static size_t depth_of(const char *path)
{
	size_t depth = 0;

	for (const char *p = path; *p; p++)
		if (*p == '/')
			depth++;
	return depth;
}

/* One `--stratum` declaration waiting to be written. */
typedef struct {
	const char *directory;
	const char *name;
	size_t      depth;
} Declaration;

static int by_depth_then_path(const void *a, const void *b)
{
	const Declaration *x = a;
	const Declaration *y = b;

	if (x->depth != y->depth)
		return x->depth > y->depth ? -1 : 1;
	return strcmp(x->directory, y->directory);
}

/* Render the proposal as the arguments that would declare it (HLR-173).
 *
 * **As arguments, not as prose.** A user who agrees with the recovered
 * layering must be able to adopt it by copying rather than by transcribing,
 * and the argument list is also the boundary the requirement draws in the one
 * form a reader cannot mistake for a measurement: `elc` produces a command
 * line, and it takes effect only when the user passes it back.
 */
/* A layer name not already taken, written into `out`.
 *
 * A repeat is suffixed, because `--stratum` merges two declarations sharing a
 * name into one layer and two layers named alike would silently become one.
 */
static void unique_layer_name(char *const *names, size_t strata,
                              const char *directory, char *out, size_t size)
{
	char candidate[64];

	sanitise(basename_of(directory), candidate, sizeof candidate);
	snprintf(out, size, "%s", candidate);

	for (unsigned n = 2; n < 1000; n++) {
		bool clash = false;

		for (size_t j = 0; j < strata; j++)
			if (names[j] && strcmp(names[j], out) == 0) {
				clash = true;
				break;
			}
		if (!clash)
			return;
		snprintf(out, size, "%s-%u", candidate, n);
	}
}

/* One name per layer, taken from the first directory in it — the rows are
 * already ordered by layer and then by path, so "first" is a property of the
 * model rather than of this loop. Returns 0 on success.
 */
static int name_layers(const RecoveryResults *out, char **names)
{
	for (size_t i = 0; i < out->layer_count; i++) {
		size_t layer = out->layers[i].layer;
		char   unique[80];

		if (names[layer])
			continue;

		unique_layer_name(names, out->strata, out->layers[i].directory,
		                  unique, sizeof unique);
		names[layer] = strdup(unique);
		if (!names[layer])
			return -1;
	}
	return 0;
}

/* The declarations themselves, in the order `--stratum` wants to read them. */
static void order_declarations(const RecoveryResults *out, char *const *names,
                               Declaration *decls)
{
	for (size_t i = 0; i < out->layer_count; i++) {
		decls[i].directory = out->layers[i].directory;
		decls[i].name      = names[out->layers[i].layer];
		decls[i].depth     = depth_of(out->layers[i].directory);
	}
	if (out->layer_count > 1)
		qsort(decls, out->layer_count, sizeof *decls,
		      by_depth_then_path);
}

/* The `--stratum name:'pattern'` clauses, one per declaration. */
static int render_declarations(Buffer *b, const Declaration *decls,
                               size_t count)
{
	for (size_t i = 0; i < count; i++)
		if (buffer_add(b, i ? " --stratum " : "--stratum ") != 0 ||
		    buffer_add(b, decls[i].name) != 0 ||
		    /* Quoted, because the pattern holds a `*` and the list is
		     * meant to be pasted into a shell. */
		    buffer_add(b, ":'") != 0 ||
		    buffer_add(b, decls[i].directory) != 0 ||
		    buffer_add(b, strcmp(decls[i].directory, "/") == 0
		                          ? "*'" : "/*'") != 0)
			return -1;
	return 0;
}

/* The single `--stratum-order` clause naming every layer in ascending order.
 *
 * Quoted, like the patterns above, and for a sharper reason: `>` is a shell
 * redirection. An unquoted order would not merely fail to be adopted — it
 * would create files named after the layers and hand `elc` a partial order. A
 * proposal that has to be repaired before it can be used is a transcription,
 * which is the thing HLR-173 asks be avoided.
 */
static int render_order(Buffer *b, char *const *names, size_t strata)
{
	if (buffer_add(b, " --stratum-order '") != 0)
		return -1;
	for (size_t i = 0; i < strata; i++)
		if ((i && buffer_add(b, ">") != 0) ||
		    buffer_add(b, names[i]) != 0)
			return -1;
	return buffer_add(b, "'");
}

static int build_proposal(RecoveryResults *out)
{
	Buffer       b = { 0 };
	char       **names = NULL;
	Declaration *decls = NULL;
	int          status = -1;

	if (out->strata == 0)
		return 0;

	names = calloc(out->strata, sizeof *names);
	decls = calloc(out->layer_count, sizeof *decls);
	if (!names || !decls)
		goto cleanup;

	if (name_layers(out, names) != 0)
		goto cleanup;
	order_declarations(out, names, decls);

	if (render_declarations(&b, decls, out->layer_count) != 0 ||
	    render_order(&b, names, out->strata) != 0)
		goto cleanup;

	out->proposal = b.text;
	b.text        = NULL;
	status        = 0;

cleanup:
	free(b.text);
	if (names)
		for (size_t i = 0; i < out->strata; i++)
			free(names[i]);
	free(names);
	free(decls);
	return status;
}

/* ---------------------------------------------------------------- the pass -- */

/* How many functions the view set aside, and under which of the two headings.
 *
 * The two are counted apart because they are different statements: a
 * peripheral function was excluded from the ordering altogether, while a sink
 * or god object stayed in it with its edges masked (HLR-170, HLR-179).
 */
static void count_set_aside(const PurifyResults *p, RecoveryResults *out)
{
	for (size_t i = 0; i < p->node_count; i++) {
		if (p->classes[i].klass == PURIFY_PERIPHERAL &&
		    p->classes[i].masked)
			out->excluded++;
		else if (p->classes[i].klass != PURIFY_ORDINARY &&
		         p->classes[i].masked)
			out->masked++;
	}
}

/* The position of every node in a topological sort of `simple`, written into
 * `order` by node identifier. Returns 0 on success.
 */
static int topological_order(const igraph_t *simple, const Sdg *g,
                             size_t *order)
{
	igraph_vector_int_t sorted;
	int                 status = -1;

	if (igraph_vector_int_init(&sorted, 0) != IGRAPH_SUCCESS)
		return -1;

	if (igraph_topological_sorting(simple, &sorted, IGRAPH_OUT) !=
	    IGRAPH_SUCCESS)
		goto cleanup;

	for (igraph_integer_t i = 0; i < igraph_vector_int_size(&sorted); i++) {
		igraph_integer_t node = VECTOR(sorted)[i];

		if (node >= 0 && (size_t)node < g->node_count)
			order[node] = (size_t)i;
	}
	status = 0;

cleanup:
	igraph_vector_int_destroy(&sorted);
	return status;
}

/* A copy of the recovery view with its self-calls removed, and whether what
 * remains is acyclic. On success the caller owns `*out` and destroys it.
 *
 * **Removing the self-calls is a judgement rather than a convenience.** A
 * function calling itself makes the graph cyclic in the strict sense, but it
 * orders nothing: it is an edge from a node to itself and says nothing about
 * where that node sits relative to any other. Reporting it in place of a
 * layering would repeat a fact the recursion section of HLR-089 already
 * states, and would cost every project with one recursive function the whole
 * of this analysis. A *mutual* cycle is different and is still reported: it
 * genuinely leaves no order to read (HLR-172).
 */
static int simplified_view(const PurifyResults *p, igraph_t *out,
                           igraph_bool_t *is_dag)
{
	if (igraph_copy(out, (const igraph_t *)p->view.graph) != IGRAPH_SUCCESS)
		return -1;

	if (igraph_simplify(out, false, true, NULL) != IGRAPH_SUCCESS ||
	    igraph_is_dag(out, is_dag) != IGRAPH_SUCCESS) {
		igraph_destroy(out);
		return -1;
	}
	return 0;
}

int recover_layers(const PurifyResults *p, const Sdg *g, const Report *r,
                   RecoveryResults *out)
{
	igraph_t      simple;
	size_t       *order  = NULL;
	igraph_bool_t is_dag = false;
	int           status = -1;

	memset(out, 0, sizeof *out);

	if (!p || !g || !r || !p->view.graph)
		return 0;

	count_set_aside(p, out);

	/* **Nothing survived, so nothing is proposed** (HLR-115). An analysis
	 * short of its inputs is omitted with its reason stated rather than
	 * reported empty, and a layering over no functions is exactly that. */
	if (p->view.included_count == 0) {
		out->state = RECOVERY_OMITTED_EMPTY;
		return 0;
	}

	if (simplified_view(p, &simple, &is_dag) != 0)
		return -1;

	if (!is_dag) {
		status = collect_cycles(p, g, &simple, out);
		goto cleanup;
	}

	order = calloc(g->node_count ? g->node_count : 1, sizeof *order);
	if (!order || topological_order(&simple, g, order) != 0)
		goto cleanup;

	if (layer_by_directory(p, g, r, order, out) != 0)
		goto cleanup;
	if (out->state == RECOVERY_PROPOSED && build_proposal(out) != 0)
		goto cleanup;
	status = 0;

cleanup:
	free(order);
	igraph_destroy(&simple);
	return status;
}

int report_set_recovery(Report *report, const RecoveryResults *rec)
{
	if (!rec)
		return 0;

	report->recovery_state    = rec->state;
	report->recovery_strata   = rec->strata;
	report->recovery_masked   = rec->masked;
	report->recovery_excluded = rec->excluded;

	if (rec->proposal) {
		report->recovery_proposal = strdup(rec->proposal);
		if (!report->recovery_proposal)
			return -1;
	}

	if (rec->layer_count > 0) {
		report->recovery = calloc(rec->layer_count,
		                          sizeof *report->recovery);
		if (!report->recovery)
			return -1;
		for (size_t i = 0; i < rec->layer_count; i++) {
			report->recovery[i].directory =
				strdup(rec->layers[i].directory);
			if (!report->recovery[i].directory)
				return -1;
			report->recovery[i].layer     = rec->layers[i].layer;
			report->recovery[i].functions = rec->layers[i].functions;
			report->recovery_count++;
		}
	}

	if (rec->cycle_count > 0) {
		report->recovery_cycles.paths =
			calloc(rec->cycle_count,
			       sizeof *report->recovery_cycles.paths);
		if (!report->recovery_cycles.paths)
			return -1;
		report->recovery_cycles.capacity = rec->cycle_count;
		for (size_t i = 0; i < rec->cycle_count; i++) {
			report->recovery_cycles.paths[i] =
				strdup(rec->cycles[i]);
			if (!report->recovery_cycles.paths[i])
				return -1;
			report->recovery_cycles.count++;
		}
	}

	return 0;
}

void recovery_results_free(RecoveryResults *r)
{
	if (!r)
		return;

	for (size_t i = 0; i < r->layer_count; i++)
		free(r->layers[i].directory);
	free(r->layers);
	for (size_t i = 0; i < r->cycle_count; i++)
		free(r->cycles[i]);
	free(r->cycles);
	free(r->proposal);
	memset(r, 0, sizeof *r);
}
