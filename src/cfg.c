/* src/cfg.c — a function's control flow (HLR-229).
 *
 * Tree-sitter yields a syntax tree; the graph below is built from it. The
 * construction is a recursive walk over statement lists that returns, for each
 * list, the block control enters it at — every construct being expressible as
 * "this block, and where control may go from it".
 *
 * **What this file must not do is answer confidently about a shape it does not
 * model.** A graph missing an edge does not fail to answer a path question; it
 * answers it wrongly, because the path that would have failed is the one the
 * missing edge carried. So an unmodelled construct clears `complete` and the
 * caller reports the function as not analysed (LLR-CFG-02).
 */
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "cfg.h"

#define CFG_NO_BLOCK UINT32_MAX

/* The marks found in the body, as byte offsets, so a statement can be asked
 * what it contains without re-running the query per statement. */
typedef struct {
	uint32_t *acquire;
	size_t    acquire_count;
	uint32_t *release;
	size_t    release_count;
} Marks;

typedef struct {
	Cfg         *g;
	const Marks *marks;
	const char  *data;
	/* Where `break` and `continue` go, innermost first. */
	uint32_t     break_to;
	uint32_t     continue_to;
} Builder;

/* ------------------------------------------------------------- the blocks */

static uint32_t block_new(Cfg *g)
{
	if (g->block_count == g->block_capacity) {
		size_t    next = g->block_capacity ? g->block_capacity * 2 : 16;
		CfgBlock *b    = realloc(g->blocks, next * sizeof *b);

		if (!b)
			return CFG_NO_BLOCK;
		g->blocks         = b;
		g->block_capacity = next;
	}

	memset(&g->blocks[g->block_count], 0, sizeof *g->blocks);
	return (uint32_t)g->block_count++;
}

static int block_link(Cfg *g, uint32_t from, uint32_t to)
{
	CfgBlock *b = &g->blocks[from];

	if (from == CFG_NO_BLOCK || to == CFG_NO_BLOCK)
		return 0;

	if (b->succ_count == b->succ_capacity) {
		size_t    next = b->succ_capacity ? b->succ_capacity * 2 : 4;
		uint32_t *s    = realloc(b->succ, next * sizeof *s);

		if (!s)
			return -1;
		b->succ         = s;
		b->succ_capacity = next;
	}

	b->succ[b->succ_count++] = to;
	return 0;
}

/* ------------------------------------------------------------- the marks */

static bool in_range(const uint32_t *offsets, size_t count, uint32_t from,
                     uint32_t to)
{
	for (size_t i = 0; i < count; i++)
		if (offsets[i] >= from && offsets[i] < to)
			return true;
	return false;
}

/* The net effect of one statement on the number held. A statement holding both
 * an acquisition and a release nets to zero, which is what a scoped pair
 * inside one expression is. */
static int statement_delta(const Marks *m, TSNode node)
{
	uint32_t from = ts_node_start_byte(node);
	uint32_t to   = ts_node_end_byte(node);
	int      d    = 0;

	if (in_range(m->acquire, m->acquire_count, from, to))
		d++;
	if (in_range(m->release, m->release_count, from, to))
		d--;
	return d;
}

/* ----------------------------------------------------------- the builder */

static uint32_t build_statement(Builder *b, TSNode node, uint32_t next);

/* A statement list, chained back to front so each statement knows where it
 * goes. Returns the block control enters the list at. */
static uint32_t build_sequence(Builder *b, TSNode parent, uint32_t next)
{
	uint32_t count = ts_node_named_child_count(parent);
	uint32_t entry = next;

	for (uint32_t i = count; i-- > 0; ) {
		TSNode child = ts_node_named_child(parent, i);

		entry = build_statement(b, child, entry);
		if (entry == CFG_NO_BLOCK)
			return CFG_NO_BLOCK;
	}

	return entry;
}

/* One statement, and where control goes after it. */
static uint32_t build_statement(Builder *b, TSNode node, uint32_t next)
{
	const char *type = ts_node_type(node);
	uint32_t    here;

	if (strcmp(type, "compound_statement") == 0)
		return build_sequence(b, node, next);

	if (strcmp(type, "if_statement") == 0) {
		TSNode   then_n = ts_node_child_by_field_name(node,
		                                              "consequence", 11);
		TSNode   else_n = ts_node_child_by_field_name(node,
		                                              "alternative", 11);
		uint32_t t, e;

		here = block_new(b->g);
		if (here == CFG_NO_BLOCK)
			return CFG_NO_BLOCK;

		t = ts_node_is_null(then_n) ? next
		                            : build_statement(b, then_n, next);
		/* **Both arms, and the missing one goes to `next`.** An `if`
		 * with no `else` is a path that skips the body entirely, and
		 * that is frequently the path on which a lock is not
		 * released. */
		e = ts_node_is_null(else_n) ? next
		                            : build_statement(b, else_n, next);

		if (block_link(b->g, here, t) != 0 ||
		    block_link(b->g, here, e) != 0)
			return CFG_NO_BLOCK;
		return here;
	}

	if (strcmp(type, "while_statement") == 0 ||
	    strcmp(type, "for_statement") == 0) {
		TSNode   body = ts_node_child_by_field_name(node, "body", 4);
		uint32_t body_entry;
		uint32_t saved_break = b->break_to;
		uint32_t saved_cont  = b->continue_to;

		here = block_new(b->g);
		if (here == CFG_NO_BLOCK)
			return CFG_NO_BLOCK;

		b->break_to    = next;
		b->continue_to = here;
		body_entry = ts_node_is_null(body) ? here
		                                   : build_statement(b, body,
		                                                     here);
		b->break_to    = saved_break;
		b->continue_to = saved_cont;

		/* The head goes into the body and past it: a loop whose
		 * condition is false on the first test runs no iteration, and
		 * that is a path like any other. */
		if (block_link(b->g, here, body_entry) != 0 ||
		    block_link(b->g, here, next) != 0)
			return CFG_NO_BLOCK;
		return here;
	}

	if (strcmp(type, "do_statement") == 0) {
		TSNode   body = ts_node_child_by_field_name(node, "body", 4);
		uint32_t head;
		uint32_t saved_break = b->break_to;
		uint32_t saved_cont  = b->continue_to;

		head = block_new(b->g);
		if (head == CFG_NO_BLOCK)
			return CFG_NO_BLOCK;

		b->break_to    = next;
		b->continue_to = head;
		here = ts_node_is_null(body) ? head
		                             : build_statement(b, body, head);
		b->break_to    = saved_break;
		b->continue_to = saved_cont;

		/* The body runs before the test, so the entry is the body. */
		if (block_link(b->g, head, here) != 0 ||
		    block_link(b->g, head, next) != 0)
			return CFG_NO_BLOCK;
		return here;
	}

	if (strcmp(type, "switch_statement") == 0) {
		TSNode   body = ts_node_child_by_field_name(node, "body", 4);
		uint32_t saved_break = b->break_to;
		uint32_t entry;

		here = block_new(b->g);
		if (here == CFG_NO_BLOCK)
			return CFG_NO_BLOCK;

		b->break_to = next;
		entry = ts_node_is_null(body) ? next
		                              : build_sequence(b, body, next);
		b->break_to = saved_break;

		/* Every case is reachable from the head, and so is the path
		 * that matches none of them. Fallthrough falls out of the
		 * sequence chaining for free. */
		if (block_link(b->g, here, entry) != 0 ||
		    block_link(b->g, here, next) != 0)
			return CFG_NO_BLOCK;
		return here;
	}

	if (strcmp(type, "case_statement") == 0)
		return build_sequence(b, node, next);

	if (strcmp(type, "return_statement") == 0) {
		here = block_new(b->g);
		if (here == CFG_NO_BLOCK)
			return CFG_NO_BLOCK;
		b->g->blocks[here].delta   = statement_delta(b->marks, node);
		b->g->blocks[here].is_exit = true;
		return here;
	}

	if (strcmp(type, "break_statement") == 0 ||
	    strcmp(type, "continue_statement") == 0) {
		uint32_t to = strcmp(type, "break_statement") == 0
		                      ? b->break_to : b->continue_to;

		here = block_new(b->g);
		if (here == CFG_NO_BLOCK)
			return CFG_NO_BLOCK;
		if (to != CFG_NO_BLOCK && block_link(b->g, here, to) != 0)
			return CFG_NO_BLOCK;
		return here;
	}

	/* **`goto` is not modelled, and saying so is the point.** Following it
	 * needs a label table and a second pass, and a graph that silently
	 * dropped the edge would report the path through the label as absent —
	 * which is to say it would report a function safe because it could not
	 * see the way it fails (LLR-CFG-02). */
	if (strcmp(type, "goto_statement") == 0 ||
	    strcmp(type, "labeled_statement") == 0) {
		b->g->complete   = false;
		b->g->unmodelled = "goto";
	}

	here = block_new(b->g);
	if (here == CFG_NO_BLOCK)
		return CFG_NO_BLOCK;
	b->g->blocks[here].delta = statement_delta(b->marks, node);
	if (block_link(b->g, here, next) != 0)
		return CFG_NO_BLOCK;
	return here;
}

/* ------------------------------------------------------------ the marks */

/* **A cursor of its own, and that is not an optimisation to be undone.** This
 * runs inside the caller's own iteration over the functions query, and a
 * cursor holds the position of exactly one traversal: executing the registry's
 * cursor here restarts it, and the loop that called us then walks whatever
 * this query left behind. It cost every function after the first in each file
 * — elc's own source fell from 792 functions to 27 — and it did so silently,
 * because a report of 27 functions is a perfectly well-formed report.
 */
static int marks_collect(TSNode body, const LanguageModule *lang,
                         Registry *reg, const char *data, Marks *out)
{
	TSQuery         *query = lang->queries[QUERY_SYNC];
	TSQueryCursor   *cursor;
	TSQueryMatch     match;
	int              status = -1;

	(void)reg;
	memset(out, 0, sizeof *out);
	if (!query)
		return 0;

	cursor = ts_query_cursor_new();
	if (!cursor)
		return -1;

	ts_query_cursor_exec(cursor, query, body);

	while (ts_query_cursor_next_match(cursor, &match)) {
		/* Without this every call matches both patterns and
		 * the two cancel, so nothing is ever held. */
		if (!analyze_predicates_hold(query, &match, data))
			continue;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			uint32_t     len;
			const char  *cap = ts_query_capture_name_for_id(
				query, match.captures[i].index, &len);
			uint32_t   **list;
			size_t      *count;
			uint32_t    *grown;

			if (len == sizeof "sync.acquire" - 1 &&
			    memcmp(cap, "sync.acquire", len) == 0) {
				list  = &out->acquire;
				count = &out->acquire_count;
			} else if (len == sizeof "sync.release" - 1 &&
			           memcmp(cap, "sync.release", len) == 0) {
				list  = &out->release;
				count = &out->release_count;
			} else {
				continue;
			}

			grown = realloc(*list, (*count + 1) * sizeof *grown);
			if (!grown) {
				ts_query_cursor_delete(cursor);
				return -1;
			}
			*list           = grown;
			(*list)[*count] = ts_node_start_byte(
				match.captures[i].node);
			(*count)++;
		}
	}

	status = 0;
	ts_query_cursor_delete(cursor);
	return status;
}

static void marks_free(Marks *m)
{
	free(m->acquire);
	free(m->release);
}

/* -------------------------------------------------------------- the build */

int cfg_build(TSNode body, const LanguageModule *lang, Registry *reg,
              const char *data, Cfg *out)
{
	Builder  b;
	Marks    marks;
	uint32_t exit_block;
	int      status = -1;

	memset(out, 0, sizeof *out);
	out->complete = true;
	out->entry    = CFG_NO_BLOCK;

	if (marks_collect(body, lang, reg, data, &marks) != 0)
		return -1;

	exit_block = block_new(out);
	if (exit_block == CFG_NO_BLOCK)
		goto cleanup;
	out->blocks[exit_block].is_exit = true;

	b.g           = out;
	b.marks       = &marks;
	b.data        = data;
	b.break_to    = CFG_NO_BLOCK;
	b.continue_to = CFG_NO_BLOCK;

	out->entry = build_sequence(&b, body, exit_block);
	if (out->entry == CFG_NO_BLOCK)
		goto cleanup;

	status = 0;

cleanup:
	marks_free(&marks);
	return status;
}

/* --------------------------------------------------------- the path search */

/* Depth-first, with the visited set keyed on the block *and* the number held.
 *
 * Keying on the block alone is the mistake this comment exists to prevent: a
 * lock taken inside a loop and released after it reaches the loop head twice,
 * once holding and once not, and a search that had already visited the head
 * would answer from whichever state it arrived in first — reporting a leak
 * that is not there, or missing one that is (LLR-CFG-03).
 */
static bool leaks_from(const Cfg *g, uint32_t block, int held, bool *seen,
                       int max_held)
{
	size_t key;

	if (block >= g->block_count || held < 0)
		return false;
	if (held > max_held)
		held = max_held;

	key = (size_t)block * (size_t)(max_held + 1) + (size_t)held;
	if (seen[key])
		return false;
	seen[key] = true;

	held += g->blocks[block].delta;
	if (held < 0)
		held = 0;
	if (held > max_held)
		held = max_held;

	if (g->blocks[block].is_exit)
		return held > 0;

	for (size_t i = 0; i < g->blocks[block].succ_count; i++)
		if (leaks_from(g, g->blocks[block].succ[i], held, seen,
		               max_held))
			return true;

	return false;
}

bool cfg_leaks(const Cfg *g)
{
	int    max_held = 0;
	bool  *seen;
	bool   leaked;

	if (!g->block_count || g->entry == CFG_NO_BLOCK)
		return false;

	for (size_t i = 0; i < g->block_count; i++)
		if (g->blocks[i].delta > 0)
			max_held += g->blocks[i].delta;
	if (max_held == 0)
		return false;   /* nothing is ever acquired */

	seen = calloc(g->block_count * (size_t)(max_held + 1), sizeof *seen);
	if (!seen)
		return false;

	leaked = leaks_from(g, g->entry, 0, seen, max_held);
	free(seen);
	return leaked;
}

void cfg_free(Cfg *g)
{
	if (!g)
		return;
	for (size_t i = 0; i < g->block_count; i++)
		free(g->blocks[i].succ);
	free(g->blocks);
	g->blocks      = NULL;
	g->block_count = 0;
}
