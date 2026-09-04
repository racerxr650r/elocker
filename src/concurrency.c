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
	if (is_declared_entry(g, opts, node))
		return false;
	if (called[node])
		return false;

	/* The image says what survived the linker. A function it does not
	 * define is entered by nothing, whatever its in-degree. */
	if (image && !elfsyms_defines(image, g->nodes[node].name))
		return false;

	/* **The fourth condition, and the one that makes the set worth
	 * reporting from.** The three above are equally true of an exported
	 * function nothing in the library calls, of a function reached only
	 * through a pointer, and of one whose caller was not among the files
	 * analysed. Taking an address without calling is what installing a
	 * handler in a vector table or registering a callback *is*. */
	if (g->nodes[node].address_taken) {
		*origin = ROOT_BY_ADDRESS;
		return true;
	}
	if (pattern &&
	    regexec(pattern, g->nodes[node].name, 0, NULL, 0) == 0) {
		*origin = ROOT_BY_PATTERN;
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

	/* **The refusal LLR-ASY-02 exists for.** With no image and no pattern
	 * there is no evidence that any function is asynchronous, and the
	 * alternative — treating every function of in-degree zero as one —
	 * would put a library's whole public interface into the asynchronous
	 * tree. Every finding of HLR-228 through HLR-231 would inherit that
	 * and would read as though it had been measured. An omission a reader
	 * can see is worth more than a wrong answer wearing a measured one's
	 * shape (HLR-115). */
	if (!image && !opts->isr_regex) {
		out->state = ROOTS_NO_EVIDENCE;
		return 0;
	}

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

	out->state = out->count ? ROOTS_IDENTIFIED : ROOTS_NONE_FOUND;
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

int concurrency_reentrant(const Sdg *g, const uint32_t *entries,
                          size_t entry_count, const RootSet *async,
                          bool *out)
{
	bool     *from_main  = NULL;
	bool     *from_async = NULL;
	uint32_t *roots      = NULL;
	size_t    n          = g->node_count ? g->node_count : 1;
	int       status     = -1;

	memset(out, 0, g->node_count * sizeof *out);
	if (!async->count || !entry_count)
		return 0;

	from_main  = calloc(n, sizeof *from_main);
	from_async = calloc(n, sizeof *from_async);
	roots      = calloc(async->count, sizeof *roots);
	if (!from_main || !from_async || !roots)
		goto cleanup;

	for (size_t i = 0; i < async->count; i++)
		roots[i] = async->items[i].node;

	/* One walk, twice. `state_reachable` is the traversal HLR-096 already
	 * uses; a second implementation here would be a second answer to "what
	 * does this reach", and the one that drifts is the one nothing else
	 * checks (LLR-RNT-01). Both follow call edges alone: a global-state
	 * edge joins a writer to a reader and is not an invocation, so sharing
	 * an object with a handler is not being entered by it (LLR-RNT-02). */
	if (state_reachable(g, entries, entry_count, from_main) != 0)
		goto cleanup;
	if (state_reachable(g, roots, async->count, from_async) != 0)
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
	         a ? a : "the application", b ? b : "an asynchronous root");

	return findings_add(out, MEASURE_SHARED_UNQUALIFIED, SEVERITY_CRITICAL,
	                    g->global_names[object], "", 0, detail);
}

int concurrency_qualifiers(const Sdg *g, const bool *main_tree,
                           const bool *async_tree, FindingList *out)
{
	for (size_t o = 0; o < g->global_name_count; o++) {
		bool        in_main, in_async;
		char        detail[256];

		touched_by(g, o, main_tree, async_tree, &in_main, &in_async);
		if (!in_main && !in_async)
			continue;

		/* **Sound whatever else is true of the object.** Two threads
		 * of control sharing an unqualified object is a defect
		 * independent of target, compiler and optimisation level: the
		 * compiler may cache it in a register across the very sequence
		 * the other thread modifies it in, and the failure is
		 * intermittent and frequently absent under a debugger. */
		if (in_main && in_async && !g->global_volatile[o]) {
			if (report_shared(g, o, main_tree, async_tree,
			                  out) != 0)
				return -1;
			continue;
		}

		if (g->global_volatile[o] && (in_main != in_async)) {
			/* **Not sound in the way the case above is, which is
			 * why the exemption is here and not a refinement for
			 * later.** The qualifier is also how a memory-mapped
			 * peripheral register is declared and how an object
			 * surviving a non-local jump is declared, and neither
			 * involves two threads of control. A register is
			 * confined to one tree *by construction*, so without
			 * the exemption this finding would fire on every one
			 * of them and advise removing a qualifier whose
			 * absence is a miscompile (HLR-231).
			 *
			 * The shape that says "this is an address, not a
			 * variable" is recognised by the language's own query,
			 * not here: it is a fact about how C spells a
			 * register. */
			if (g->global_mmio[o])
				continue;

			snprintf(detail, sizeof detail,
			         "declared volatile and reached only from %s; "
			         "a cause outside elc's view may still "
			         "require it",
			         in_main ? "the application"
			                 : "an asynchronous root");
			if (findings_add(out, MEASURE_VOLATILE_CONFINED,
			                 SEVERITY_WARNING, g->global_names[o],
			                 "", 0, detail) != 0)
				return -1;
		}
	}

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
