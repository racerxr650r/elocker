/* src/concurrency.c — the second thread of control (HLR-227 – HLR-231).
 *
 * See include/concurrency.h for what the four analyses are. This file is
 * mostly about one decision, which is the root set: everything downstream
 * inherits it, so a root set that is wrong produces findings that are wrong
 * while reading exactly like findings that were measured.
 */
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include "concurrency.h"
#include "diag.h"
#include "state.h"

/* ------------------------------------------------------- asynchronous roots */

static int root_add(RootSet *set, uint32_t node, RootOrigin origin)
{
	if (set->count == set->capacity) {
		size_t     next = set->capacity ? set->capacity * 2 : 8;
		AsyncRoot *grown = realloc(set->items, next * sizeof *grown);

		if (!grown)
			return -1;
		set->items    = grown;
		set->capacity = next;
	}

	set->items[set->count].node   = node;
	set->items[set->count].origin = origin;
	set->count++;
	return 0;
}

/* Every function some call edge points at. In-degree zero is the second of
 * HLR-227's conditions and is read from the edge table rather than from the
 * call view's degrees, because the two are built from the same edges and one
 * fewer traversal is one fewer place they can disagree. */
static bool *called_set(const Sdg *g)
{
	bool *called = calloc(g->node_count ? g->node_count : 1,
	                      sizeof *called);

	if (!called)
		return NULL;

	for (size_t i = 0; i < g->edge_count; i++)
		if (g->edges[i].kind == EDGE_CALL &&
		    g->edges[i].to < g->node_count)
			called[g->edges[i].to] = true;

	return called;
}

static bool is_declared_entry(const Sdg *g, const ElcOptions *opts,
                              uint32_t node)
{
	for (size_t i = 0; i < opts->entry_point_count; i++)
		if (strcmp(opts->entry_points[i], g->nodes[node].name) == 0)
			return true;
	return false;
}

/* HLR-227's four conditions, in one place.
 *
 * Split from the loop below because the four are the requirement and the loop
 * is bookkeeping — and because with them inline the caller stood at seventeen
 * against the threshold `elc` enforces on everyone else (LLR-BLD-23).
 */
static bool admits_root(const Sdg *g, const ElcOptions *opts,
                        const bool *called, const SymbolSet *image,
                        const regex_t *pattern, uint32_t node,
                        RootOrigin *origin)
{
	const SdgNode *n = &g->nodes[node];

	if (is_declared_entry(g, opts, node))
		return false;
	if (called[node])
		return false;

	/* The image says what survived the linker. A function it does not
	 * define is entered by nothing, whatever its in-degree.
	 *
	 * **Asked of the linkage name where there is one.** A macro that writes
	 * a definition renames it, so the source spells `TCB0_INT_vect` where
	 * the image spells `__vector_12`; comparing the source name alone would
	 * reject every handler on this target as absent from its own image —
	 * the single condition that most has to be right, because rejecting a
	 * root silently empties HLR-228 through HLR-231 (HLR-233). */
	if (image && !elfsyms_defines(image, n->linkage_name ? n->linkage_name
	                                                     : n->name))
		return false;

	/* **The fourth condition, and the one that makes the set worth
	 * reporting from.** The three above are equally true of an exported
	 * function nothing in the library calls, of a function reached only
	 * through a pointer, and of one whose caller was not among the files
	 * analysed.
	 *
	 * Three shapes satisfy it, and they are tried strongest first because
	 * the origin is reported and a reader acts on which one they have.
	 *
	 * A definition written through a function-shaped macro that nothing
	 * calls is a handler: the macro exists to attach the body to a table
	 * the source never names, and it is why the body has no caller to find.
	 * Taking an address without calling is the weaker claim — it is equally
	 * what registering an ordinary callback looks like, and a callback may
	 * be dispatched from the application's own loop. */
	if (n->macro_defined) {
		*origin = ROOT_BY_WRAPPER;
		return true;
	}
	if (pattern &&
	    regexec(pattern, n->name, 0, NULL, 0) == 0) {
		*origin = ROOT_BY_PATTERN;
		return true;
	}
	if (n->address_taken) {
		*origin = ROOT_BY_ADDRESS;
		return true;
	}

	return false;
}

int concurrency_roots(const Sdg *g, const ElcOptions *opts,
                      const SymbolSet *image, RootSet *out)
{
	bool    *called;
	regex_t  pattern;
	bool     have_pattern = false;
	int      status       = -1;

	memset(out, 0, sizeof *out);

	if (opts->isr_regex) {
		if (regcomp(&pattern, opts->isr_regex,
		            REG_EXTENDED | REG_NOSUB) != 0) {
			diag_printf("elc: --isr-regex: not a usable "
			            "expression: %s\n", opts->isr_regex);
			return -1;
		}
		have_pattern = true;
	}

	called = called_set(g);
	if (!called)
		goto cleanup;

	for (size_t i = 0; i < g->node_count; i++) {
		RootOrigin origin;

		if (!admits_root(g, opts, called, image,
		                 have_pattern ? &pattern : NULL,
		                 (uint32_t)i, &origin))
			continue;
		if (root_add(out, (uint32_t)i, origin) != 0)
			goto cleanup;
	}

	/* **Three outcomes, and the third is the one LLR-ASY-02 exists for.**
	 *
	 * A function whose address is taken is evidence in itself, and it is
	 * evidence the source carries — the image only says what survived the
	 * linker, so a run without one can still identify handlers, just less
	 * confidently. So the walk always runs.
	 *
	 * What must never happen is a root set invented from in-degree alone,
	 * which would put a library's whole public interface into the
	 * asynchronous tree and leave every finding of HLR-228 through
	 * HLR-231 inheriting the error while reading as though it had been
	 * measured. Where nothing was found *and* nothing was supplied to look
	 * with, the state says so and the caller reports an omission a reader
	 * can see (HLR-115). */
	if (out->count)
		out->state = ROOTS_IDENTIFIED;
	else if (!image && !opts->isr_regex)
		out->state = ROOTS_NO_EVIDENCE;
	else
		out->state = ROOTS_NONE_FOUND;
	status     = 0;

cleanup:
	free(called);
	if (have_pattern)
		regfree(&pattern);
	return status;
}

void rootset_free(RootSet *r)
{
	if (!r)
		return;
	free(r->items);
	r->items    = NULL;
	r->count    = 0;
	r->capacity = 0;
}

/* ------------------------------------------------------------- re-entrancy */

/* The root each marked function is reached from, strongest evidence first.
 *
 * One walk per root rather than one walk over all of them, because the question
 * is *which* root — and a walk from the whole set answers only that some root
 * reaches it. Handlers are walked first and a function keeps the first root that
 * claims it, so a function a handler and a callback both reach is attributed to
 * the handler: the mark rests on the stronger of the two, which is the one a
 * reader would act on (HLR-233).
 */
static int attribute_roots(const Sdg *g, const RootSet *async,
                           const bool *marked, uint32_t *via)
{
	bool *seen = calloc(g->node_count ? g->node_count : 1, sizeof *seen);

	if (!seen)
		return -1;

	for (size_t i = 0; i < g->node_count; i++)
		via[i] = UINT32_MAX;

	for (int pass = 0; pass < 2; pass++)
		for (size_t r = 0; r < async->count; r++) {
			uint32_t root = async->items[r].node;

			if (root_is_handler(async->items[r].origin) !=
			    (pass == 0))
				continue;

			memset(seen, 0, g->node_count * sizeof *seen);
			if (state_reachable(g, &root, 1, seen) != 0) {
				free(seen);
				return -1;
			}
			for (size_t i = 0; i < g->node_count; i++)
				if (marked[i] && seen[i] &&
				    via[i] == UINT32_MAX)
					via[i] = root;
		}

	free(seen);
	return 0;
}

/* The functions both root sets reach, over call edges alone (LLR-RNT-01).
 *
 * One walk, twice. `state_reachable` is the traversal HLR-096 already uses; a
 * second implementation here would be a second answer to "what does this
 * reach", and the one that drifts is the one nothing else checks. Both follow
 * call edges alone: a global-state edge joins a writer to a reader and is not
 * an invocation, so sharing an object with a handler is not being entered by it
 * (LLR-RNT-02).
 *
 * Split from its caller because the caller stood at sixteen against the
 * threshold `elc` enforces on everyone else, which `elc` reported of its own
 * source before this existed (LLR-BLD-23).
 */
static int intersect_trees(const Sdg *g, const uint32_t *entries,
                           size_t entry_count, const uint32_t *roots,
                           size_t root_count, bool *out)
{
	size_t n          = g->node_count ? g->node_count : 1;
	bool  *from_main  = calloc(n, sizeof *from_main);
	bool  *from_async = calloc(n, sizeof *from_async);
	int    status     = -1;

	if (!from_main || !from_async)
		goto cleanup;

	if (state_reachable(g, entries, entry_count, from_main) != 0)
		goto cleanup;
	if (state_reachable(g, roots, root_count, from_async) != 0)
		goto cleanup;

	/* **The intersection, not the union.** A function only a handler
	 * reaches is not re-entered — nothing interrupts it in the middle of
	 * itself. It is being reachable from *both* that means one thread can
	 * begin a function while another is already inside it (LLR-RNT-03). */
	for (size_t i = 0; i < g->node_count; i++)
		out[i] = from_main[i] && from_async[i];

	status = 0;

cleanup:
	free(from_main);
	free(from_async);
	return status;
}

int concurrency_reentrant(const Sdg *g, const uint32_t *entries,
                          size_t entry_count, const RootSet *async,
                          bool *out, uint32_t *via)
{
	uint32_t *roots  = NULL;
	int       status = -1;

	memset(out, 0, g->node_count * sizeof *out);
	if (via)
		for (size_t i = 0; i < g->node_count; i++)
			via[i] = UINT32_MAX;
	if (!async->count || !entry_count)
		return 0;

	roots = calloc(async->count, sizeof *roots);
	if (!roots)
		return -1;
	for (size_t i = 0; i < async->count; i++)
		roots[i] = async->items[i].node;

	if (intersect_trees(g, entries, entry_count, roots, async->count,
	                    out) != 0)
		goto cleanup;
	if (via && attribute_roots(g, async, out, via) != 0)
		goto cleanup;

	status = 0;

cleanup:
	free(roots);
	return status;
}

/* -------------------------------------------------------- the qualifier -- */

/* Which trees touch the object at `name_index`, over the touch table. */
static void touched_by(const Sdg *g, size_t object, const bool *main_tree,
                       const bool *async_tree, bool *in_main, bool *in_async)
{
	*in_main  = false;
	*in_async = false;

	for (size_t i = 0; i < g->touch_count; i++) {
		const GlobalTouch *t = &g->touches[i];

		if (strcmp(t->object, g->global_names[object]) != 0)
			continue;
		if (t->node >= g->node_count)
			continue;
		if (main_tree[t->node])
			*in_main = true;
		if (async_tree[t->node])
			*in_async = true;
	}
}

/* The function on each side that reaches the object, so the finding names the
 * pair that makes it shared rather than only saying that it is (HLR-091). */
static void naming_pair(const Sdg *g, size_t object, const bool *main_tree,
                        const bool *async_tree, const char **from_main,
                        const char **from_async)
{
	*from_main  = NULL;
	*from_async = NULL;

	for (size_t i = 0; i < g->touch_count; i++) {
		const GlobalTouch *t = &g->touches[i];

		if (strcmp(t->object, g->global_names[object]) != 0)
			continue;
		if (t->node >= g->node_count)
			continue;
		if (!*from_main && main_tree[t->node])
			*from_main = g->nodes[t->node].name;
		if (!*from_async && async_tree[t->node])
			*from_async = g->nodes[t->node].name;
	}
}

/* The critical half of the classification: shared and unqualified (HLR-230).
 *
 * Split from its converse because the two are not the same kind of claim —
 * this one is sound whatever else is true of the object, and the other is
 * hedged — and because together they put the caller at fifteen against the
 * threshold `elc` holds its own source to (LLR-BLD-23).
 */
static int report_shared(const Sdg *g, size_t object, const bool *main_tree,
                         const bool *async_tree, FindingList *out)
{
	char        detail[256];
	const char *a, *b;

	naming_pair(g, object, main_tree, async_tree, &a, &b);
	snprintf(detail, sizeof detail,
	         "reached from %s and from %s, and not declared volatile",
	         a ? a : "the application", b ? b : "an asynchronous handler");

	return findings_add(out, MEASURE_SHARED_UNQUALIFIED, SEVERITY_CRITICAL,
	                    g->global_names[object], "", 0, detail);
}

/* The qualifier is unnecessary only where the object is provably confined
 * (HLR-231).
 *
 * **Which needs the callback tree to be empty of it, and that is the whole of
 * this refinement.** A root admitted by an address alone may be a handler or may
 * be a callback the application dispatches from its own loop, and `elc` cannot
 * tell which from the source. An object such a function touches is therefore
 * not shown to be confined to one thread of control *or* shared between two:
 * warning that its qualifier is unnecessary would advise removing a `volatile`
 * whose absence is a miscompile, on evidence that does not reach the claim.
 *
 * Measured on `avrOS`, this is not a corner: a tick counter written by the
 * timer interrupt and drained by a state machine the scheduler resumes was
 * reported as reachable from an asynchronous root alone, and its qualifier —
 * the one thing keeping the drain loop correct — as unnecessary.
 */
static bool confined_to_one_tree(bool in_main, bool in_handler,
                                 bool in_callback)
{
	if (in_callback)
		return false;

	return in_main != in_handler;
}

static int report_confined(const Sdg *g, size_t object, bool in_main,
                           FindingList *out)
{
	char detail[256];

	snprintf(detail, sizeof detail,
	         "declared volatile and reached only from %s; "
	         "a cause outside elc's view may still require it",
	         in_main ? "the application" : "an asynchronous handler");

	return findings_add(out, MEASURE_VOLATILE_CONFINED, SEVERITY_WARNING,
	                    g->global_names[object], "", 0, detail);
}

/* Shared with a callback, unqualified: a defect if the callback runs on a
 * second thread of control, and nothing at all if it does not (HLR-230).
 *
 * Reported, because the case it covers is real — an interrupt-dispatched
 * callback is exactly this shape — and reported at warning rather than at
 * critical, because `elc` has not established the second thread. The detail
 * says which half is missing so the reader can settle it in a way `elc` cannot.
 */
static int report_maybe_shared(const Sdg *g, size_t object,
                               const bool *main_tree, const bool *cb_tree,
                               FindingList *out)
{
	char        detail[256];
	const char *a, *b;

	naming_pair(g, object, main_tree, cb_tree, &a, &b);
	snprintf(detail, sizeof detail,
	         "reached from %s and from %s, a registered callback whose "
	         "thread of control elc cannot place, and not declared volatile",
	         a ? a : "the application", b ? b : "a callback");

	return findings_add(out, MEASURE_SHARED_UNQUALIFIED, SEVERITY_WARNING,
	                    g->global_names[object], "", 0, detail);
}

/* One object against the three trees. Split out because the classification is
 * four exclusive outcomes and the loop below is bookkeeping — and because
 * together they stand at seventeen against the threshold `elc` enforces on
 * everyone else (LLR-BLD-23). */
static int classify_object(Sdg *g, size_t o, const bool *main_tree,
                           const bool *handler_tree, const bool *cb_tree,
                           FindingList *out)
{
	bool in_main, in_handler, in_cb, unused;

	touched_by(g, o, main_tree, handler_tree, &in_main, &in_handler);
	touched_by(g, o, main_tree, cb_tree, &unused, &in_cb);

	if (!in_main && !in_handler && !in_cb)
		return 0;

	/* **Sound whatever else is true of the object.** Two threads of
	 * control sharing an unqualified object is a defect independent of
	 * target, compiler and optimisation level: the compiler may cache it in
	 * a register across the very sequence the other thread modifies it in,
	 * and the failure is intermittent and frequently absent under a
	 * debugger. Only the *handler* tree carries that claim; the callback
	 * tree may be the application's own thread under another name. */
	g->global_shared[o] = in_main && in_handler;

	if (g->global_volatile[o]) {
		/* The exemption, and it is here rather than a refinement for
		 * later. The qualifier is also how a memory-mapped peripheral
		 * register is declared and how an object surviving a non-local
		 * jump is declared, and neither involves two threads of
		 * control. A register is confined to one tree *by
		 * construction*, so without the exemption this finding would
		 * fire on every one of them and advise removing a qualifier
		 * whose absence is a miscompile.
		 *
		 * The shape that says "this is an address, not a variable" is
		 * recognised by the language's own query, not here: it is a
		 * fact about how C spells a register. */
		if (g->global_mmio[o] ||
		    !confined_to_one_tree(in_main, in_handler, in_cb))
			return 0;

		g->global_status[o] = GLOBAL_QUALIFIER_UNNECESSARY_WARNING;
		return report_confined(g, o, in_main, out);
	}

	if (in_main && in_handler) {
		g->global_status[o] = GLOBAL_QUALIFIER_MISSING_CRITICAL;
		return report_shared(g, o, main_tree, handler_tree, out);
	}

	if (in_main && in_cb)
		return report_maybe_shared(g, o, main_tree, cb_tree, out);

	return 0;
}

int concurrency_qualifiers(Sdg *g, const bool *main_tree,
                           const bool *handler_tree, const bool *callback_tree,
                           FindingList *out)
{
	for (size_t o = 0; o < g->global_name_count; o++)
		if (classify_object(g, o, main_tree, handler_tree,
		                    callback_tree, out) != 0)
			return -1;

	return 0;
}

/* ---------------------------------------------------- the critical section */

int concurrency_sections(const Sdg *g, const bool *reentrant,
                         FindingList *out)
{
	for (size_t i = 0; i < g->node_count; i++) {
		const SdgNode *n = &g->nodes[i];

		/* **Only the re-entrant ones.** A lock released on one path
		 * and not another is a defect wherever it is, but the finding
		 * this requirement makes is about a function two threads can
		 * be inside: reporting every function that ever locks would
		 * bury the ones that matter under the ones that cannot race
		 * (HLR-229). */
		if (!reentrant[i])
			continue;

		/* **Not analysed is not safe.** A function whose control flow
		 * could not be built is reported as such, because a silent
		 * pass here claims a proof that was never attempted — which
		 * is the failure mode this analysis most has to avoid
		 * (HLR-138, LLR-CFG-02). */
		if (!n->cfg_complete) {
			if (findings_add(out, MEASURE_CRITICAL_SECTION,
			                 SEVERITY_INFO, n->name,
			                 n->file ? n->file : "", n->line_start,
			                 "re-entrant, and its critical sections "
			                 "were not analysed: its control flow "
			                 "could not be built") != 0)
				return -1;
			continue;
		}

		if (n->leaks_lock &&
		    findings_add(out, MEASURE_CRITICAL_SECTION,
		                 SEVERITY_CRITICAL, n->name,
		                 n->file ? n->file : "", n->line_start,
		                 "re-entrant, and a path leaves it holding a "
		                 "critical section it acquired") != 0)
			return -1;
	}

	return 0;
}
