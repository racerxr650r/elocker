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

#include "cfg.h"

#define CFG_NO_BLOCK UINT32_MAX

/* The marks found in the body, as byte offsets, so a statement can be asked
 * what it contains without re-running the query per statement. */
typedef struct {
	Cfg            *g;
	const CfgMarks *marks;
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
static int statement_delta(const CfgMarks *m, TSNode node)
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

/* An `if`, with both arms.
 *
 * The missing `else` goes to the statement after, and that matters more than
 * it looks: an `if` with no `else` is a path that skips the body entirely, and
 * it is frequently the path on which a lock is not released.
 */
static uint32_t build_branch(Builder *b, TSNode node, uint32_t next)
{
	TSNode   then_n = ts_node_child_by_field_name(node, "consequence", 11);
	TSNode   else_n = ts_node_child_by_field_name(node, "alternative", 11);
	uint32_t here   = block_new(b->g);
	uint32_t t, e;

	if (here == CFG_NO_BLOCK)
		return CFG_NO_BLOCK;

	t = ts_node_is_null(then_n) ? next : build_statement(b, then_n, next);
	e = ts_node_is_null(else_n) ? next : build_statement(b, else_n, next);

	if (block_link(b->g, here, t) != 0 || block_link(b->g, here, e) != 0)
		return CFG_NO_BLOCK;
	return here;
}

/* `while` and `for`: a head that goes into the body and past it. A loop whose
 * condition is false on the first test runs no iteration, and that is a path
 * like any other. */
static uint32_t build_loop(Builder *b, TSNode node, uint32_t next)
{
	TSNode   body_n = ts_node_child_by_field_name(node, "body", 4);
	uint32_t here   = block_new(b->g);
	uint32_t saved_break, saved_cont, body_entry;

	if (here == CFG_NO_BLOCK)
		return CFG_NO_BLOCK;

	saved_break    = b->break_to;
	saved_cont     = b->continue_to;
	b->break_to    = next;
	b->continue_to = here;
	body_entry = ts_node_is_null(body_n) ? here
	                                     : build_statement(b, body_n, here);
	b->break_to    = saved_break;
	b->continue_to = saved_cont;

	if (block_link(b->g, here, body_entry) != 0 ||
	    block_link(b->g, here, next) != 0)
		return CFG_NO_BLOCK;
	return here;
}

/* `do`: the body runs before the test, so the entry is the body. */
static uint32_t build_do_loop(Builder *b, TSNode node, uint32_t next)
{
	TSNode   body_n = ts_node_child_by_field_name(node, "body", 4);
	uint32_t head   = block_new(b->g);
	uint32_t saved_break, saved_cont, here;

	if (head == CFG_NO_BLOCK)
		return CFG_NO_BLOCK;

	saved_break    = b->break_to;
	saved_cont     = b->continue_to;
	b->break_to    = next;
	b->continue_to = head;
	here = ts_node_is_null(body_n) ? head
	                               : build_statement(b, body_n, head);
	b->break_to    = saved_break;
	b->continue_to = saved_cont;

	if (block_link(b->g, head, here) != 0 ||
	    block_link(b->g, head, next) != 0)
		return CFG_NO_BLOCK;
	return here;
}

/* `switch`: every case is reachable from the head, and so is the path that
 * matches none of them. Fallthrough falls out of the sequence chaining. */
static uint32_t build_switch(Builder *b, TSNode node, uint32_t next)
{
	TSNode   body_n = ts_node_child_by_field_name(node, "body", 4);
	uint32_t here   = block_new(b->g);
	uint32_t saved_break, entry;

	if (here == CFG_NO_BLOCK)
		return CFG_NO_BLOCK;

	saved_break = b->break_to;
	b->break_to = next;
	entry = ts_node_is_null(body_n) ? next
	                                : build_sequence(b, body_n, next);
	b->break_to = saved_break;

	if (block_link(b->g, here, entry) != 0 ||
	    block_link(b->g, here, next) != 0)
		return CFG_NO_BLOCK;
	return here;
}

/* A statement-shaped wrapper, so every entry in the dispatch table below has
 * one signature and the table needs no special cases. */
static uint32_t build_sequence_stmt(Builder *b, TSNode node, uint32_t next)
{
	return build_sequence(b, node, next);
}

/* `return`: control leaves the function here. */
static uint32_t build_return(Builder *b, TSNode node, uint32_t next)
{
	uint32_t here = block_new(b->g);

	(void)next;
	if (here == CFG_NO_BLOCK)
		return CFG_NO_BLOCK;
	b->g->blocks[here].delta   = statement_delta(b->marks, node);
	b->g->blocks[here].is_exit = true;
	return here;
}

/* `break` and `continue`: control goes where the enclosing construct said. */
static uint32_t build_goes_to(Builder *b, uint32_t to)
{
	uint32_t here = block_new(b->g);

	if (here == CFG_NO_BLOCK)
		return CFG_NO_BLOCK;
	if (to != CFG_NO_BLOCK && block_link(b->g, here, to) != 0)
		return CFG_NO_BLOCK;
	return here;
}

static uint32_t build_break(Builder *b, TSNode node, uint32_t next)
{
	(void)node; (void)next;
	return build_goes_to(b, b->break_to);
}

static uint32_t build_continue(Builder *b, TSNode node, uint32_t next)
{
	(void)node; (void)next;
	return build_goes_to(b, b->continue_to);
}

/* `case` bodies chain like any other statement list. */
static uint32_t build_case(Builder *b, TSNode node, uint32_t next)
{
	return build_sequence(b, node, next);
}

/* One statement, and where control goes after it.
 *
 * A table rather than a chain of comparisons, and not only for tidiness: as a
 * chain this function was one decision point per construct and stood at
 * thirty-seven against the threshold `elc` holds its own source to. The table
 * makes adding a construct an entry rather than a branch (LLR-BLD-23).
 */
static uint32_t build_statement(Builder *b, TSNode node, uint32_t next)
{
	static const struct {
		const char *type;
		uint32_t  (*build)(Builder *, TSNode, uint32_t);
	} FORMS[] = {
		{ "compound_statement", build_sequence_stmt },
		{ "case_statement",     build_case          },
		{ "if_statement",       build_branch        },
		{ "while_statement",    build_loop          },
		{ "for_statement",      build_loop          },
		{ "do_statement",       build_do_loop       },
		{ "switch_statement",   build_switch        },
		{ "return_statement",   build_return        },
		{ "break_statement",    build_break         },
		{ "continue_statement", build_continue      },
	};
	const char *type = ts_node_type(node);
	uint32_t    here;

	for (size_t i = 0; i < sizeof FORMS / sizeof *FORMS; i++)
		if (strcmp(type, FORMS[i].type) == 0)
			return FORMS[i].build(b, node, next);

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

/* -------------------------------------------------------------- the build */

int cfg_build(TSNode body, const CfgMarks *marks, Cfg *out)
{
	Builder  b;
	uint32_t exit_block;
	int      status = -1;

	memset(out, 0, sizeof *out);
	out->complete = true;
	out->entry    = CFG_NO_BLOCK;

	exit_block = block_new(out);
	if (exit_block == CFG_NO_BLOCK)
		goto cleanup;
	out->blocks[exit_block].is_exit = true;

	b.g           = out;
	b.marks       = marks;
	b.break_to    = CFG_NO_BLOCK;
	b.continue_to = CFG_NO_BLOCK;

	out->entry = build_sequence(&b, body, exit_block);
	if (out->entry == CFG_NO_BLOCK)
		goto cleanup;

	status = 0;

cleanup:
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
