/* analyze.c — the single parse.
 *
 * Opens each source file read-only, maps it, parses it exactly once, and
 * extracts from that one tree everything any later stage needs. No other
 * module reads source text (doc/SDD.md §7).
 *
 * Phase 2 extracts function identity. ELOC, complexity, and the graph facts
 * are added to this same traversal by later phases — added to it, never
 * beside it: re-parsing for a second consumer is what HLR-076 forbids.
 *
 * Nothing here names a grammar node type. What counts as a function is
 * decided by `functions.scm`; this file knows only the capture names, which
 * are the published contract (HLR-121).
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <tree_sitter/api.h>

#include "analyze.h"
#include "elc.h"
#include "registry.h"

/* Capture names from runtime/queries/README.md. These are a contract with
 * every language module, not a property of any language. */
#define CAPTURE_FUNCTION_NAME "function.name"
#define CAPTURE_FUNCTION_BODY "function.body"
#define CAPTURE_COMMENT       "comment"
#define CAPTURE_ELOC          "eloc.statement"
#define CAPTURE_COMPLEXITY    "complexity.decision"
#define CAPTURE_CALL_NAME     "call.name"
#define CAPTURE_CALL_ADDRESS  "call.address_taken"
#define CAPTURE_GLOBAL_DECL   "global.declaration"
#define CAPTURE_GLOBAL_READ   "global.read"
#define CAPTURE_GLOBAL_WRITE  "global.write"
#define CAPTURE_DEAD_TERM     "dead.terminator"
#define CAPTURE_DEAD_REENTRY  "dead.reentry"
#define CAPTURE_DEAD_BRANCH   "dead.branch"
#define CAPTURE_COND_REGION   "conditional.region"
#define CAPTURE_COND_ALT      "conditional.alternative"
#define CAPTURE_COND_TRUE     "conditional.true"
#define CAPTURE_COND_FALSE    "conditional.false"
#define CAPTURE_COND_SYMBOL   "conditional.symbol"
#define CAPTURE_COND_NEGATED  "conditional.negated"

/* One counted statement: the line it starts on, and the function it belongs
 * to. `function` is SIZE_MAX for a statement outside every reported function,
 * which contributes to the file's ELOC and to no function's. */
typedef struct {
	uint32_t line;
	size_t   function;
} StatementSite;

typedef struct {
	StatementSite *items;
	size_t         count;
	size_t         capacity;
} SiteList;

#define NO_FUNCTION SIZE_MAX

/* Declared here because the collectors above its definition consult it:
 * every one of them asks whether a byte is measured, and the exclusion set
 * is built before any of them runs. */
static bool byte_is_excluded(const SpanList *spans, uint32_t byte);

void filemetrics_free(FileMetrics *metrics)
{
	if (!metrics)
		return;

	for (size_t i = 0; i < metrics->function_count; i++)
		free(metrics->functions[i].name);
	free(metrics->functions);
	free(metrics->language);
	free(metrics->path);
	free(metrics);
}

void filefacts_free(FileFacts *facts)
{
	if (!facts)
		return;

	for (size_t i = 0; i < facts->call_count; i++)
		free(facts->calls[i].callee);
	free(facts->calls);
	for (size_t i = 0; i < facts->global_count; i++)
		free(facts->globals[i].name);
	free(facts->globals);
	for (size_t i = 0; i < facts->address_taken_count; i++)
		free(facts->address_taken[i]);
	free(facts->address_taken);
	free(facts->dead);
	for (size_t i = 0; i < facts->rule_match_count; i++)
		free(facts->rule_matches[i].rule);
	free(facts->rule_matches);
	free(facts->path);
	free(facts);
}

int factlist_add(FactList *list, FileFacts *facts)
{
	if (list->count == list->capacity) {
		size_t      next   = list->capacity ? list->capacity * 2 : 32;
		FileFacts **bigger = realloc(list->items, next * sizeof *bigger);

		if (!bigger)
			return -1;
		list->items    = bigger;
		list->capacity = next;
	}
	list->items[list->count++] = facts;
	return 0;
}

void factlist_free(FactList *list)
{
	if (!list)
		return;

	for (size_t i = 0; i < list->count; i++)
		filefacts_free(list->items[i]);
	free(list->items);
	list->items    = NULL;
	list->count    = 0;
	list->capacity = 0;
}

/* Grow any of the analyser's arrays by doubling, with the realloc result
 * checked in a temporary before the original is overwritten (HLR-125). */
static int grow(void **items, size_t *capacity, size_t item_size)
{
	size_t next   = *capacity ? *capacity * 2 : 32;
	void  *bigger = realloc(*items, next * item_size);

	if (!bigger)
		return -1;

	*items    = bigger;
	*capacity = next;
	return 0;
}

/* --------------------------------------------------- comment-span merging --
 *
 * The canonical bug in this class of tool lives here. Captured spans overlap
 * and nest, and excluding them one at a time removes a shared line more than
 * once — enough to drive a file's ELOC below zero. Sorting and coalescing
 * first makes the exclusion idempotent (HLR-016).
 */

static int by_start_byte(const void *a, const void *b)
{
	const CommentSpan *x = a;
	const CommentSpan *y = b;

	if (x->start_byte != y->start_byte)
		return x->start_byte < y->start_byte ? -1 : 1;
	/* A tie leaves the wider span first, so the coalescing pass absorbs
	 * the narrower one immediately rather than depending on qsort's
	 * choice between equal elements. */
	if (x->end_byte != y->end_byte)
		return x->end_byte > y->end_byte ? -1 : 1;
	return 0;
}

uint32_t merge_comment_spans(SpanList *spans)
{
	uint32_t lines        = 0;
	uint32_t last_counted = 0;   /* 0 = no line counted yet */
	size_t   kept         = 0;

	if (spans->count == 0)
		return 0;

	qsort(spans->items, spans->count, sizeof *spans->items, by_start_byte);

	for (size_t i = 0; i < spans->count; ) {
		CommentSpan merged = spans->items[i];
		size_t      j      = i + 1;

		/* `j < spans->count` is the whole of LLR-MRG-04: the loop reads
		 * the next element, and without the bound it reads one past the
		 * last span whenever the final run is more than one long. */
		while (j < spans->count &&
		       spans->items[j].start_byte <= merged.end_byte) {
			if (spans->items[j].end_byte > merged.end_byte) {
				merged.end_byte = spans->items[j].end_byte;
				merged.end_line = spans->items[j].end_line;
			}
			j++;
		}

		spans->items[kept++] = merged;

		/* Count lines, not spans. Two comments sitting on one line
		 * are two disjoint byte ranges: neither contains the other, so
		 * they do not coalesce, and summing their line counts would
		 * report that single line twice. Spans arrive in byte order, so
		 * their start lines are non-decreasing and it is enough to skip
		 * whatever the previous span already covered (LLR-MRG-03).
		 */
		uint32_t from = merged.start_line;

		if (last_counted && from <= last_counted)
			from = last_counted + 1;
		if (merged.end_line >= from)
			lines += merged.end_line - from + 1;
		if (merged.end_line > last_counted)
			last_counted = merged.end_line;

		i = j;
	}

	spans->count = kept;
	return lines;
}

/* ------------------------------------------------------ statement attribution */

const FnRange *innermost_enclosing(const FnRangeIndex *index, uint32_t byte)
{
	const FnRange *best = NULL;

	for (size_t i = 0; i < index->count; i++) {
		const FnRange *candidate = &index->items[i];

		if (byte < candidate->start_byte || byte >= candidate->end_byte)
			continue;

		/* Narrowest wins. A nested function's range lies entirely
		 * within its parent's, so comparing extents is what stops a
		 * statement counting for both (HLR-068). */
		if (!best ||
		    (candidate->end_byte - candidate->start_byte) <
		    (best->end_byte - best->start_byte))
			best = candidate;
	}

	return best;
}

/* ------------------------------------------------------- parse damage --
 *
 * How much of a file the grammar could not follow, in lines.
 *
 * Lines rather than bytes or node counts, because every other figure in the
 * report is a line count and the reader's question is "how much of this file
 * did you not see". A region spanning four lines is four lines whether the
 * parser produced one error node for it or three.
 */

/* Add an error node's extent to the running total, without descending into it:
 * whatever the parser built inside a region it could not follow is not a
 * second, separate piece of damage. */
static void tally_errors(TSNode node, uint32_t *first_line, uint32_t *last_line,
                         uint32_t *lines)
{
	if (ts_node_is_error(node) || ts_node_is_missing(node)) {
		uint32_t from = ts_node_start_point(node).row + 1;
		uint32_t to   = ts_node_end_point(node).row + 1;

		/* Counted as *distinct* lines, for the reason the comment
		 * exclusion counts distinct lines: two errors on one line are
		 * one line a reader has to look at, and error regions arrive in
		 * document order so remembering the last one covered is
		 * enough. */
		if (*last_line && from <= *last_line)
			from = *last_line + 1;
		if (to >= from)
			*lines += to - from + 1;
		if (to > *last_line)
			*last_line = to;
		if (!*first_line)
			*first_line = ts_node_start_point(node).row + 1;
		return;
	}

	/* `has_error` is true for every ancestor of an error, so descending
	 * only where it holds walks straight to the damage and skips the
	 * sound majority of the tree. */
	if (!ts_node_has_error(node))
		return;

	for (uint32_t i = 0; i < ts_node_child_count(node); i++)
		tally_errors(ts_node_child(node, i), first_line, last_line, lines);
}

static uint32_t count_unparsed_lines(TSNode root)
{
	uint32_t first = 0;
	uint32_t last  = 0;
	uint32_t lines = 0;

	if (!ts_node_has_error(root))
		return 0;

	tally_errors(root, &first, &last, &lines);
	return lines;
}

/* The line the first unparsed region starts on, for the diagnostic: a reader
 * told a file is partly unparsed wants somewhere to look. */
static uint32_t first_unparsed_line(TSNode root)
{
	uint32_t first = 0;
	uint32_t last  = 0;
	uint32_t lines = 0;

	if (!ts_node_has_error(root))
		return 0;

	tally_errors(root, &first, &last, &lines);
	return first;
}

/* Count the lines in a mapping.
 *
 * A trailing fragment with no final newline is a line a reader sees, so it
 * counts. Every scan is bounded by the length from fstat(2); the mapping is
 * not NUL-terminated.
 */
static uint32_t count_lines(const char *data, size_t len)
{
	size_t lines = 0;

	for (const char *p = data, *end = data + len; p < end; ) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));

		if (!nl) {
			lines++;         /* final line, unterminated */
			break;
		}
		lines++;
		p = nl + 1;
	}

	return lines > UINT32_MAX ? UINT32_MAX : (uint32_t)lines;
}

/* True when a capture's name is `name`. The length is explicit because
 * tree-sitter returns the name unterminated. */
static bool capture_is(const TSQuery *query, uint32_t index, const char *name)
{
	uint32_t    length = 0;
	const char *actual = ts_query_capture_name_for_id(query, index, &length);

	return actual && strlen(name) == length &&
	       memcmp(actual, name, length) == 0;
}

/* --------------------------------------------------- query predicates --
 *
 * **Tree-sitter's C library does not evaluate predicates.** It parses
 * `(#eq? @c "0")` into a step list and hands it back; deciding whether the
 * match survives is the caller's job, and a caller that never asks accepts
 * every match as though the predicate were not written.
 *
 * That is a silent failure with teeth. A `deadcode.scm` distinguishes the
 * literal `0` from the variable `x` with exactly such a predicate, so ignoring
 * one turns "`if (0)` is dead" into "every `if` is dead" — the false claim
 * HLR-138 forbids outright. The evaluation belongs here because it is generic:
 * it compares the text a capture spans against a string the query file wrote,
 * and knows no language.
 *
 * An unrecognised `?`-predicate **rejects** the match rather than being
 * ignored. The query author wrote a filter and this build cannot apply it, so
 * the honest outcome is no match: under-reporting is the safe direction, and
 * accepting would apply a filter's *inverse*. A `!`-directive carries
 * information rather than filtering, and is ignored as tree-sitter intends.
 */

/* The bytes a node spans, as a NUL-terminated string the caller frees, or
 * NULL on allocation failure. Nodes reaching here are literals and
 * identifiers, so the copy is short. */
static char *node_text(const char *data, TSNode node)
{
	uint32_t start = ts_node_start_byte(node);
	uint32_t end   = ts_node_end_byte(node);
	size_t   len   = end > start ? (size_t)(end - start) : 0;
	char    *copy  = malloc(len + 1);

	if (!copy)
		return NULL;
	memcpy(copy, data + start, len);
	copy[len] = '\0';
	return copy;
}

/* The first node a match captured under `capture`, or a zeroed node.
 *
 * The first rather than all of them: a predicate over a quantified capture has
 * no defined meaning in the contract, and testing one representative is what
 * every other implementation does.
 */
static bool match_capture(const TSQueryMatch *match, uint32_t capture,
                          TSNode *out)
{
	for (uint16_t i = 0; i < match->capture_count; i++)
		if (match->captures[i].index == capture) {
			*out = match->captures[i].node;
			return true;
		}
	return false;
}

/* Compare a counted string from the query against a literal.
 *
 * The trailing NULs are trimmed first, because tree-sitter counts the
 * terminator in a *string value*'s length and not in a capture name's. Making
 * the comparison independent of which is cheaper than depending on a detail of
 * the library's symbol table.
 */
static bool text_is(const char *text, uint32_t length, const char *want)
{
	size_t n = length;

	while (n > 0 && text[n - 1] == '\0')
		n--;
	return strlen(want) == n && memcmp(text, want, n) == 0;
}

/* A string step's value, NUL-terminated, or NULL if the step is a capture. */
static const char *step_string(const TSQuery *query,
                               const TSQueryPredicateStep *step)
{
	uint32_t length = 0;

	if (step->type != TSQueryPredicateStepTypeString)
		return NULL;
	return ts_query_string_value_for_id(query, step->value_id, &length);
}

/* One predicate's steps, minus its trailing sentinel, evaluated against the
 * match. `steps[0]` is the predicate name. */
static bool predicate_holds(const TSQuery *query, const TSQueryMatch *match,
                            const char *data,
                            const TSQueryPredicateStep *steps, size_t count)
{
	uint32_t    length = 0;
	const char *name;
	char       *text   = NULL;
	bool        result = false;

	if (count == 0 || steps[0].type != TSQueryPredicateStepTypeString)
		return false;

	name = ts_query_string_value_for_id(query, steps[0].value_id, &length);
	if (!name || length == 0)
		return false;

	/* A directive is not a filter. `#set!` and its relatives attach
	 * metadata for a consumer that wants it, and rejecting a match for
	 * carrying one would turn a note into a deletion. */
	{
		size_t n = length;

		while (n > 0 && name[n - 1] == '\0')
			n--;
		if (n > 0 && name[n - 1] == '!')
			return true;
	}

	if (count < 2 || steps[1].type != TSQueryPredicateStepTypeCapture)
		return false;

	TSNode node;

	if (!match_capture(match, steps[1].value_id, &node))
		return false;

	text = node_text(data, node);
	if (!text)
		return false;

	bool eq      = text_is(name, length, "eq?");
	bool not_eq  = text_is(name, length, "not-eq?");
	bool any_of  = text_is(name, length, "any-of?");
	bool match_p = text_is(name, length, "match?");
	bool not_mat = text_is(name, length, "not-match?");

	if (eq || not_eq || any_of) {
		bool found = false;

		for (size_t i = 2; i < count && !found; i++) {
			const char *value = step_string(query, &steps[i]);

			if (value) {
				found = strcmp(text, value) == 0;
				continue;
			}

			/* A capture on the right-hand side compares two spans
			 * of the source against each other, which is how a
			 * query asks whether two identifiers are the same. */
			TSNode other;

			if (match_capture(match, steps[i].value_id, &other)) {
				char *rhs = node_text(data, other);

				found = rhs && strcmp(text, rhs) == 0;
				free(rhs);
			}
		}
		result = not_eq ? !found : found;
	} else if (match_p || not_mat) {
		const char *pattern = count >= 3 ? step_string(query, &steps[2])
		                                 : NULL;
		regex_t     re;

		if (pattern &&
		    regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB) == 0) {
			bool hit = regexec(&re, text, 0, NULL, 0) == 0;

			regfree(&re);
			result = match_p ? hit : !hit;
		} else {
			/* An uncompilable pattern is the query author's
			 * defect, and the safe reading of a filter that cannot
			 * be applied is that nothing passes it. */
			result = false;
		}
	} else {
		result = false;   /* an unrecognised filter rejects the match */
	}

	free(text);
	return result;
}

/* Whether every predicate on the match's pattern holds. */
static bool predicates_hold(const TSQuery *query, const TSQueryMatch *match,
                            const char *data)
{
	uint32_t                    step_count = 0;
	const TSQueryPredicateStep *steps =
		ts_query_predicates_for_pattern(query, match->pattern_index,
		                                &step_count);

	if (!steps || step_count == 0)
		return true;

	for (uint32_t i = 0; i < step_count; ) {
		uint32_t end = i;

		while (end < step_count &&
		       steps[end].type != TSQueryPredicateStepTypeDone)
			end++;

		if (!predicate_holds(query, match, data, &steps[i],
		                     (size_t)(end - i)))
			return false;

		i = end + 1;   /* past the sentinel */
	}

	return true;
}

static int functions_grow(FileMetrics *metrics, size_t *capacity)
{
	size_t          next   = *capacity ? *capacity * 2 : 8;
	FunctionMetric *bigger = realloc(metrics->functions,
	                                 next * sizeof *bigger);

	if (!bigger)
		return -1;

	metrics->functions = bigger;
	*capacity          = next;
	return 0;
}

/* Copy an identifier out of the mapping into its own NUL-terminated
 * allocation. The name outlives the mapping, so it cannot point into it. */
static char *name_from(const char *data, TSNode node)
{
	uint32_t start = ts_node_start_byte(node);
	uint32_t end   = ts_node_end_byte(node);
	size_t   len   = (size_t)(end - start);
	char    *copy  = malloc(len + 1);

	if (!copy)
		return NULL;

	memcpy(copy, data + start, len);
	copy[len] = '\0';
	return copy;
}

/* Run functions.scm over the tree and record every match that supplies both
 * halves of the contract.
 *
 * A pattern capturing only a name or only a body contributes no function.
 * That is deliberate: the pair is what identifies a function, and silently
 * accepting half of it would report a function with no line range or a line
 * range with no name.
 */
static int collect_functions(const LanguageModule *module, Registry *reg,
                             const char *data, TSNode root,
                             const SpanList *excluded, FileMetrics *metrics,
                             FnRangeIndex *ranges)
{
	TSQuery      *query    = module->queries[QUERY_FUNCTIONS];
	size_t        capacity = 0;
	TSQueryMatch  match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		TSNode name_node;
		TSNode body_node;
		bool   have_name = false;
		bool   have_body = false;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			uint32_t index = match.captures[i].index;

			if (capture_is(query, index, CAPTURE_FUNCTION_NAME)) {
				name_node = match.captures[i].node;
				have_name = true;
			} else if (capture_is(query, index,
			                      CAPTURE_FUNCTION_BODY)) {
				body_node = match.captures[i].node;
				have_body = true;
			}
		}

		if (!have_name || !have_body)
			continue;

		/* A function the configuration does not compile is not
		 * measured, and is not reported: it belongs to no build, and
		 * counting it would report the union of every configuration the
		 * source can express (HLR-132). */
		if (byte_is_excluded(excluded, ts_node_start_byte(name_node)))
			continue;

		if (metrics->function_count == capacity &&
		    functions_grow(metrics, &capacity) != 0)
			return -1;

		FunctionMetric *fn = &metrics->functions[metrics->function_count];
		memset(fn, 0, sizeof *fn);

		fn->name = name_from(data, name_node);
		if (!fn->name)
			return -1;

		/* The reported span runs from the name to the end of the body,
		 * not from the body's opening brace. A reader asked where
		 * `foo` starts points at its signature, and a fixture that had
		 * to hand-count from the brace would be encoding an artefact
		 * of the query rather than a property of the code.
		 *
		 * The minimum guards a language whose query captures the name
		 * after the body; the span is then the body's alone rather
		 * than inverted.
		 *
		 * TSPoint.row is 0-based; start_line and end_line are what a
		 * user reads in an editor. Converted here, once, and never
		 * again (LLR-ANL-07). */
		uint32_t name_row = ts_node_start_point(name_node).row;
		uint32_t body_row = ts_node_start_point(body_node).row;

		fn->start_line = (name_row < body_row ? name_row : body_row) + 1;
		fn->end_line   = ts_node_end_point(body_node).row + 1;

		/* The same extent in bytes, for attribution. A statement is
		 * attributed to the narrowest range containing it, so the range
		 * must be the reported one: attributing by a different extent
		 * from the one shown would let a statement count for a function
		 * whose printed line range does not contain it. */
		if (ranges->count == ranges->capacity &&
		    grow((void **)&ranges->items, &ranges->capacity,
		         sizeof *ranges->items) != 0)
			return -1;

		uint32_t name_start = ts_node_start_byte(name_node);
		uint32_t body_start = ts_node_start_byte(body_node);

		ranges->items[ranges->count].start_byte =
			name_start < body_start ? name_start : body_start;
		ranges->items[ranges->count].end_byte =
			ts_node_end_byte(body_node);
		ranges->items[ranges->count].index = metrics->function_count;
		ranges->count++;

		metrics->function_count++;
	}

	return 0;
}

/* Collect every comment span, then sort and coalesce them. */
static int collect_comments(const LanguageModule *module, Registry *reg,
                            const char *data, TSNode root, SpanList *spans)
{
	TSQuery      *query = module->queries[QUERY_COMMENTS];
	TSQueryMatch  match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			if (!capture_is(query, match.captures[i].index,
			                CAPTURE_COMMENT))
				continue;

			if (spans->count == spans->capacity &&
			    grow((void **)&spans->items, &spans->capacity,
			         sizeof *spans->items) != 0)
				return -1;

			TSNode node = match.captures[i].node;

			spans->items[spans->count].start_byte =
				ts_node_start_byte(node);
			spans->items[spans->count].end_byte =
				ts_node_end_byte(node);
			spans->items[spans->count].start_line =
				ts_node_start_point(node).row + 1;
			spans->items[spans->count].end_line =
				ts_node_end_point(node).row + 1;
			spans->count++;
		}
	}

	merge_comment_spans(spans);
	return 0;
}

/* True when a byte offset lies inside the merged excluded set.
 *
 * Two things are excluded and they share one set deliberately: comment spans,
 * and the regions this configuration does not compile (HLR-132). Keeping them
 * apart would mean two mechanisms to understand and two chances to remove a
 * range twice; merged, a byte is either measured or it is not, and one
 * question answers both.
 *
 * **Byte-granular, deliberately.** The first version of this asked whether a
 * statement's *line* touched a comment span, which is a different question
 * with a wrong answer. On a line reading
 *
 *     int n = 0;           // a trailing note
 *
 * the line touches a comment and the statement does not, and the
 * line-granular form silently deleted a line of code. Comment spans are byte
 * ranges; the exclusion has to be one too.
 *
 * With statements taken from the syntax tree the two sets are then disjoint —
 * a statement inside a comment is not something a parser produces — so this
 * is a guard rather than a subtraction. It is the guard HLR-016 asks for, and
 * merging first is what stops it removing anything twice.
 */
static bool byte_is_excluded(const SpanList *spans, uint32_t byte)
{
	for (size_t i = 0; i < spans->count; i++) {
		if (byte < spans->items[i].start_byte)
			return false;   /* sorted; no later span can contain it */
		if (byte < spans->items[i].end_byte)
			return true;
	}
	return false;
}


/* ------------------------------------------------ conditional compilation --
 *
 * Which regions of a file this configuration does not compile (HLR-131).
 *
 * **elc runs no preprocessor** (HLR-135). There is no macro expansion, no
 * include resolution, and no arithmetic over macro values, because each needs
 * a toolchain whose presence and configuration elc cannot reproduce — and the
 * answer would then depend on the machine rather than on the source. What
 * happens instead is narrower and honest: a region is decided when its
 * condition is a constant the *query* recognised, or when it tests the
 * definedness of a symbol the user named with -D.
 *
 * **Undecidable is not false**, and the asymmetry is the whole safety
 * argument (HLR-133). Treating an unrecognised condition as false silently
 * deletes code and produces a report that is confidently wrong and looks
 * exactly like a correct one. Treating it as true over-counts, which is
 * visible in the undecided figure printed beside the metrics.
 *
 * It follows that a symbol no -D mentions is undecidable rather than
 * undefined: a build may define it in a header or on a command line elc never
 * sees. That single rule is also what delivers HLR-131's "with no definitions,
 * nothing changes" — with an empty set every definedness test is undecidable,
 * so nothing prunes, and no special case says so.
 */

/* One conditional region, as the query described it. Nothing here is decided
 * yet: the query says what the shape is and elc says what the configuration
 * makes of it, which is what keeps a C `#if` and a Rust `#[cfg]` one
 * mechanism (HLR-134). */
typedef struct {
	uint32_t region_start;
	uint32_t region_end;
	uint32_t alt_start;      /* 0 when the region has no alternative */
	uint32_t alt_end;
	TSNode   symbol;
	bool     has_alt;
	bool     decided;        /* the query settled it, with no -D      */
	bool     holds;          /* and this is what it settled on        */
	bool     has_symbol;
	bool     negated;
	uint32_t pattern;        /* lower wins where two patterns match   */
} CondRegion;

typedef struct {
	CondRegion *items;
	size_t      count;
	size_t      capacity;
} CondList;

/* Ascending by region, and within a region the earlier pattern first.
 *
 * The tie-break is a contract, not an implementation detail: a query file
 * writes its specific patterns before its catch-all, and the specific one must
 * win. Without it a `#if 0` matched by both a literal pattern and a fallback
 * would be decided by whichever tree-sitter happened to report first. */
static int by_region(const void *a, const void *b)
{
	const CondRegion *x = a;
	const CondRegion *y = b;

	if (x->region_start != y->region_start)
		return x->region_start < y->region_start ? -1 : 1;
	if (x->pattern != y->pattern)
		return x->pattern < y->pattern ? -1 : 1;
	return 0;
}

/* Whether any byte of this span already lies in an excluded one. Used to skip
 * a region nested inside a region already pruned: it is not compiled either,
 * and counting it undecided would inflate a figure a reader acts on. */
static bool span_excluded(const SpanList *spans, uint32_t byte)
{
	for (size_t i = 0; i < spans->count; i++)
		if (byte >= spans->items[i].start_byte &&
		    byte < spans->items[i].end_byte)
			return true;
	return false;
}

/* Was this symbol named by a -D?
 *
 * Definedness is the only thing a definition can assert, so "mentioned" and
 * "defined" are the same question — there is no -U. A symbol that was not
 * mentioned is therefore undecidable rather than undefined, which is the rule
 * HLR-133 turns on.
 */
static bool symbol_defined(const ElcOptions *opts, const char *data, TSNode node)
{
	uint32_t start = ts_node_start_byte(node);
	size_t   len   = ts_node_end_byte(node) - start;

	for (size_t i = 0; opts && i < opts->define_count; i++) {
		const char *define = opts->defines[i];
		const char *equals = strchr(define, '=');
		size_t      length = equals ? (size_t)(equals - define)
		                            : strlen(define);

		if (length == len && memcmp(define, data + start, len) == 0)
			return true;
	}
	return false;
}

static int span_add(SpanList *spans, uint32_t start, uint32_t end,
                    uint32_t start_line, uint32_t end_line)
{
	if (spans->count == spans->capacity &&
	    grow((void **)&spans->items, &spans->capacity,
	         sizeof *spans->items) != 0)
		return -1;

	spans->items[spans->count].start_byte = start;
	spans->items[spans->count].end_byte   = end;
	spans->items[spans->count].start_line = start_line;
	spans->items[spans->count].end_line   = end_line;
	spans->count++;
	return 0;
}

static int condlist_add(CondList *list, const CondRegion *region)
{
	if (list->count == list->capacity &&
	    grow((void **)&list->items, &list->capacity,
	         sizeof *list->items) != 0)
		return -1;
	list->items[list->count++] = *region;
	return 0;
}

/* Every region this configuration excludes, appended to `spans`, and the count
 * of regions left active because their condition could not be decided.
 *
 * A language with no `conditionals.scm` has no conditional compilation, which
 * is the truth for a language that has none; the required six are unchanged
 * and a module omitting this file is not a broken one (HLR-134, HLR-121).
 */
static int collect_inactive_regions(const LanguageModule *module, Registry *reg,
                                    const ElcOptions *opts, const char *data,
                                    TSNode root, SpanList *spans,
                                    uint32_t *undecided)
{
	TSQuery      *query = module->queries[QUERY_CONDITIONALS];
	TSQueryMatch  match;
	CondList      regions = { 0 };
	int           status  = -1;

	*undecided = 0;
	if (!query)
		return 0;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		CondRegion region = { 0 };
		bool       seen   = false;

		if (!predicates_hold(query, &match, data))
			continue;

		region.pattern = match.pattern_index;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			uint32_t index = match.captures[i].index;
			TSNode   node  = match.captures[i].node;

			if (capture_is(query, index, CAPTURE_COND_REGION)) {
				region.region_start = ts_node_start_byte(node);
				region.region_end   = ts_node_end_byte(node);
				seen                = true;
			} else if (capture_is(query, index, CAPTURE_COND_ALT)) {
				region.alt_start = ts_node_start_byte(node);
				region.alt_end   = ts_node_end_byte(node);
				region.has_alt   = true;
			} else if (capture_is(query, index,
			                      CAPTURE_COND_TRUE)) {
				region.decided = true;
				region.holds   = true;
			} else if (capture_is(query, index,
			                      CAPTURE_COND_FALSE)) {
				region.decided = true;
				region.holds   = false;
			} else if (capture_is(query, index,
			                      CAPTURE_COND_SYMBOL)) {
				region.symbol     = node;
				region.has_symbol = true;
			} else if (capture_is(query, index,
			                      CAPTURE_COND_NEGATED)) {
				region.negated = true;
			}
		}

		/* A pattern that captures no region describes nothing this can
		 * act on. Skipped rather than guessed at. */
		if (seen && condlist_add(&regions, &region) != 0)
			goto done;
	}

	/* Guarded, and not as an optimisation: passing a null pointer to qsort
	 * is undefined even with a count of zero, and a language whose module
	 * supplies no conditional query reaches here with exactly that. */
	if (regions.count > 1)
		qsort(regions.items, regions.count, sizeof *regions.items,
		      by_region);

	for (size_t i = 0; i < regions.count; i++) {
		const CondRegion *region = &regions.items[i];

		/* One region, one decision: the earliest pattern that matched
		 * it has already been sorted to the front. */
		if (i > 0 && regions.items[i - 1].region_start ==
		                     region->region_start)
			continue;

		/* Nested inside something already excluded. Not compiled, so
		 * neither pruned again nor counted undecided. */
		if (span_excluded(spans, region->region_start))
			continue;

		bool decided = region->decided;
		bool holds   = region->holds;

		if (!decided && region->has_symbol &&
		    symbol_defined(opts, data, region->symbol)) {
			/* Mentioned by a -D, so the definedness test has an
			 * answer. The negated form is active while the symbol
			 * is *un*defined, so being defined excludes it. */
			decided = true;
			holds   = !region->negated;
		}

		if (!decided) {
			/* Either the query recognised nothing it could settle,
			 * or the symbol was named by no -D. Both branches stay
			 * and the count says how often that happened — the
			 * over-counting direction, which is visible, rather
			 * than the under-counting one, which is not
			 * (HLR-133). */
			(*undecided)++;
			continue;
		}

		if (holds) {
			/* Only the alternative goes; a region with none loses
			 * nothing. */
			if (region->has_alt &&
			    span_add(spans, region->alt_start, region->alt_end,
			             0, 0) != 0)
				goto done;
		} else {
			/* Everything up to the alternative, or the whole
			 * region where there is none. The directive lines go
			 * with it, which costs nothing: a directive is not a
			 * statement, a decision, a call, or an access. */
			uint32_t end = region->has_alt ? region->alt_start
			                               : region->region_end;

			if (span_add(spans, region->region_start, end, 0, 0) != 0)
				goto done;
		}
	}

	status = 0;

done:
	free(regions.items);
	return status;
}

/* Record every counted statement with the line it starts on and the function
 * it belongs to. */
static int collect_statements(const LanguageModule *module, Registry *reg,
                              const char *data, TSNode root,
                              const FnRangeIndex *ranges,
                              const SpanList *comments, SiteList *sites)
{
	TSQuery      *query = module->queries[QUERY_ELOC];
	TSQueryMatch  match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			if (!capture_is(query, match.captures[i].index,
			                CAPTURE_ELOC))
				continue;

			TSNode   node = match.captures[i].node;
			uint32_t byte = ts_node_start_byte(node);

			/* Counted once, at its start line: a statement spread
			 * over four lines is the same statement as one written
			 * on a single line, and style must not move the number
			 * (HLR-053). */
			uint32_t line = ts_node_start_point(node).row + 1;

			if (byte_is_excluded(comments, byte))
				continue;

			if (sites->count == sites->capacity &&
			    grow((void **)&sites->items, &sites->capacity,
			         sizeof *sites->items) != 0)
				return -1;

			const FnRange *owner = innermost_enclosing(ranges, byte);

			sites->items[sites->count].line     = line;
			sites->items[sites->count].function =
				owner ? owner->index : NO_FUNCTION;
			sites->count++;
		}
	}

	return 0;
}

/* Count each decision point against the function it belongs to.
 *
 * Complexity is one plus the decision points in a function, and the `1 +` is
 * added here rather than in the query: a query that captured the function
 * itself would make a straight-line function 2, and every language module
 * would have to remember not to.
 *
 * The query runs over the whole tree, not over each function body in turn,
 * and the attribution is `innermost_enclosing` — the same rule ELOC uses.
 * That is what makes both halves of the requirement fall out of one pass: a
 * nested *named* function is reported, so it is its own innermost enclosing
 * function and owns its decision points (HLR-068); an anonymous lambda is
 * *not* reported, so the nearest reported function containing it is the named
 * one, and its decision points land there (HLR-018). Running the query
 * against each body separately would give the enclosing function everything
 * its nested functions do, and need a subtraction to undo it.
 */
static int collect_complexity(const LanguageModule *module, Registry *reg,
                              const char *data, TSNode root,
                              const FnRangeIndex *ranges,
                              const SpanList *excluded, FileMetrics *metrics)
{
	TSQuery      *query = module->queries[QUERY_COMPLEXITY];
	TSQueryMatch  match;

	/* Every reported function starts at one: a function with no branch
	 * still has one path through it. */
	for (size_t i = 0; i < metrics->function_count; i++)
		metrics->functions[i].complexity = 1;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			if (!capture_is(query, match.captures[i].index,
			                CAPTURE_COMPLEXITY))
				continue;

			/* Not compiled, not counted (HLR-132). */
			if (byte_is_excluded(excluded,
			                     ts_node_start_byte(
				                     match.captures[i].node)))
				continue;

			const FnRange *owner = innermost_enclosing(
				ranges, ts_node_start_byte(match.captures[i].node));

			/* A decision point outside every reported function —
			 * in a file-scope initialiser — belongs to no function
			 * and is not counted anywhere. */
			if (owner)
				metrics->functions[owner->index].complexity++;
		}
	}

	return 0;
}

/* ------------------------------------------------------- the graph facts --
 *
 * Recorded, not resolved. A call site names an identifier; whether that
 * identifier is a function this project defines cannot be known until every
 * file has been analysed, so the decision belongs to graph.c and the parse
 * records only what it saw (HLR-073, HLR-076).
 */

/* Record every call site and every identifier used as a value.
 *
 * The address-taken captures are deliberately over-broad — most of what they
 * match is an ordinary variable — and the query files say so. Filtering them
 * here would need to know which identifiers are functions, which is exactly
 * the whole-project knowledge this stage does not have. graph.c discards the
 * ones that resolve to nothing, and the cost of carrying them this far is a
 * few strings.
 */
static int collect_calls(const LanguageModule *module, Registry *reg,
                         const char *data, TSNode root,
                         const FnRangeIndex *ranges, const SpanList *excluded,
                         FileFacts *facts)
{
	TSQuery      *query = module->queries[QUERY_CALLS];
	TSQueryMatch  match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			uint32_t index = match.captures[i].index;
			TSNode   node  = match.captures[i].node;
			uint32_t byte  = ts_node_start_byte(node);

			/* A call site the configuration does not compile is not
			 * a call site of this build, and an edge drawn from one
			 * would put a function in the graph that never runs
			 * (HLR-132). */
			if (byte_is_excluded(excluded, byte))
				continue;

			if (capture_is(query, index, CAPTURE_CALL_NAME)) {
				if (facts->call_count == facts->call_capacity &&
				    grow((void **)&facts->calls,
				         &facts->call_capacity,
				         sizeof *facts->calls) != 0)
					return -1;

				const FnRange *owner =
					innermost_enclosing(ranges, byte);
				CallSite      *site =
					&facts->calls[facts->call_count];

				site->callee = name_from(data, node);
				if (!site->callee)
					return -1;
				site->caller = owner ? owner->index
				                     : ELC_NO_FUNCTION;
				site->line   = ts_node_start_point(node).row + 1;
				facts->call_count++;
			} else if (capture_is(query, index,
			                      CAPTURE_CALL_ADDRESS)) {
				if (facts->address_taken_count ==
				        facts->address_taken_capacity &&
				    grow((void **)&facts->address_taken,
				         &facts->address_taken_capacity,
				         sizeof *facts->address_taken) != 0)
					return -1;

				char *name = name_from(data, node);

				if (!name)
					return -1;
				facts->address_taken[
					facts->address_taken_count++] = name;
			}
		}
	}

	return 0;
}

/* Record every global declaration, read and write.
 *
 * Same division of labour as the calls: the read and write patterns capture
 * identifiers wherever they appear, and which of them name globals is settled
 * against the declarations — by graph.c, which can see the declarations of
 * every file rather than only this one. A global declared in a header and
 * written in three translation units is the case that makes the difference.
 */
static int collect_globals(const LanguageModule *module, Registry *reg,
                           const char *data, TSNode root,
                           const FnRangeIndex *ranges,
                           const SpanList *excluded, FileFacts *facts)
{
	TSQuery      *query = module->queries[QUERY_GLOBALS];
	TSQueryMatch  match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			uint32_t         index = match.captures[i].index;
			TSNode           node  = match.captures[i].node;
			GlobalAccessKind kind;

			/* Not compiled, not a fact about this build
			 * (HLR-132). */
			if (byte_is_excluded(excluded, ts_node_start_byte(node)))
				continue;

			if (capture_is(query, index, CAPTURE_GLOBAL_DECL))
				kind = GLOBAL_DECLARATION;
			else if (capture_is(query, index, CAPTURE_GLOBAL_READ))
				kind = GLOBAL_READ;
			else if (capture_is(query, index, CAPTURE_GLOBAL_WRITE))
				kind = GLOBAL_WRITE;
			else
				continue;

			if (facts->global_count == facts->global_capacity &&
			    grow((void **)&facts->globals,
			         &facts->global_capacity,
			         sizeof *facts->globals) != 0)
				return -1;

			uint32_t       byte  = ts_node_start_byte(node);
			const FnRange *owner = innermost_enclosing(ranges, byte);
			GlobalAccess  *access =
				&facts->globals[facts->global_count];

			access->name = name_from(data, node);
			if (!access->name)
				return -1;
			access->function = owner ? owner->index
			                         : ELC_NO_FUNCTION;
			access->line     = ts_node_start_point(node).row + 1;
			access->kind     = kind;
			facts->global_count++;
		}
	}

	return 0;
}

/* ----------------------------------------------------------- dead code --
 *
 * The intra-procedural half of the dead-code question: statements that cannot
 * execute whatever the call graph says (HLR-137). Two shapes, and the split
 * between this file and the query file is the design.
 *
 * **What ends control flow, what can be re-entered, and what counts as a false
 * literal are language knowledge and live in `deadcode.scm`.** Walking the
 * siblings of a node is structural and lives here. There is no node type in
 * this function, and if one ever appears the split has gone wrong.
 *
 * **Nothing is evaluated.** A branch is dead only where the source writes a
 * literal, which is why `if (0)` is found and `x = 0; if (x)` is not — that
 * needs data flow, and data flow is how this analysis would begin to be wrong
 * (HLR-138, LLR-DED-03).
 */

/* The captured nodes of one file's dead-code query, kept apart because the
 * three are used differently: branches are recorded outright, terminators are
 * walked from, and re-entry points are only ever asked about. */
typedef struct {
	TSNode   *items;
	size_t    count;
	size_t    capacity;
} NodeList;

static int nodelist_add(NodeList *list, TSNode node)
{
	if (list->count == list->capacity &&
	    grow((void **)&list->items, &list->capacity, sizeof *list->items) != 0)
		return -1;
	list->items[list->count++] = node;
	return 0;
}

/* Whether a node was captured as a re-entry point.
 *
 * Compared by **start byte alone**, not by the whole extent. A query may
 * capture the label rather than the labelled statement, in which case the
 * sibling the walk is looking at is the outer node and the two agree only on
 * where they begin. Matching on the start is the forgiving comparison, and
 * forgiving is the right direction here: a re-entry point missed produces a
 * false claim of dead code, which HLR-138 forbids, while one recognised too
 * eagerly merely stops the walk early.
 */
static bool is_reentry(const NodeList *reentries, TSNode node)
{
	uint32_t start = ts_node_start_byte(node);

	for (size_t i = 0; i < reentries->count; i++)
		if (ts_node_start_byte(reentries->items[i]) == start)
			return true;
	return false;
}

static int dead_add(FileFacts *facts, const FnRangeIndex *ranges, TSNode node,
                    DeadCause cause)
{
	uint32_t       byte  = ts_node_start_byte(node);
	const FnRange *owner = innermost_enclosing(ranges, byte);

	/* HLR-137 asks about statements *within a function*. A span outside
	 * every reported one — module-level code in a language that allows it
	 * — has no enclosing function to report it against, and inventing a
	 * placeholder would put a row in the table that answers no question
	 * the requirement asks. */
	if (!owner)
		return 0;

	if (facts->dead_count == facts->dead_capacity &&
	    grow((void **)&facts->dead, &facts->dead_capacity,
	         sizeof *facts->dead) != 0)
		return -1;

	DeadSpan *span = &facts->dead[facts->dead_count++];

	span->function   = owner->index;
	span->start_line = ts_node_start_point(node).row + 1;
	span->end_line   = ts_node_end_point(node).row + 1;
	span->cause      = cause;
	return 0;
}

static int by_dead_span(const void *a, const void *b)
{
	const DeadSpan *x = a;
	const DeadSpan *y = b;

	if (x->function != y->function)
		return x->function < y->function ? -1 : 1;
	if (x->start_line != y->start_line)
		return x->start_line < y->start_line ? -1 : 1;
	if (x->end_line != y->end_line)
		return x->end_line < y->end_line ? -1 : 1;
	return x->cause < y->cause ? -1 : x->cause > y->cause;
}

/* Collapse spans that name the same lines of the same function.
 *
 * Two terminators in one block record their shared tail twice — `return 1;
 * return 2; n++;` reaches `n++` from both — and two statements written on one
 * line are one line to a reader either way. Sorting first is what makes the
 * collapse independent of the order the query matched, so the report is the
 * same on every run (HLR-032).
 */
static void dedupe_dead(FileFacts *facts)
{
	size_t kept = 0;

	if (facts->dead_count < 2) {
		return;
	}

	qsort(facts->dead, facts->dead_count, sizeof *facts->dead,
	      by_dead_span);

	for (size_t i = 0; i < facts->dead_count; i++) {
		const DeadSpan *previous = kept ? &facts->dead[kept - 1] : NULL;

		if (previous && previous->function == facts->dead[i].function &&
		    previous->start_line == facts->dead[i].start_line &&
		    previous->end_line == facts->dead[i].end_line)
			continue;
		facts->dead[kept++] = facts->dead[i];
	}
	facts->dead_count = kept;
}

int collect_dead_code(const LanguageModule *module, Registry *reg,
                      const char *data, TSNode root,
                      const FnRangeIndex *ranges, const SpanList *comments,
                      FileFacts *facts)
{
	TSQuery      *query = module->queries[QUERY_DEADCODE];
	NodeList      terminators = { 0 };
	NodeList      reentries   = { 0 };
	NodeList      branches    = { 0 };
	TSQueryMatch  match;
	int           status = -1;

	/* A language with no dead-code query is not a failure and is not
	 * clean. The flag stays false and the report says the analysis was not
	 * performed for that language — a different claim from "none found",
	 * and the only honest one (HLR-139, LLR-DED-05). */
	if (!query) {
		facts->dead_analysed = false;
		return 0;
	}
	facts->dead_analysed = true;

	/* Collected in full before anything is walked. The sibling walk asks
	 * "is this one a re-entry point?", and a query cursor answers only in
	 * the order it matched — which is not the order the walk needs. */
	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			uint32_t index = match.captures[i].index;
			TSNode   node  = match.captures[i].node;
			int      rc    = 0;

			if (capture_is(query, index, CAPTURE_DEAD_TERM))
				rc = nodelist_add(&terminators, node);
			else if (capture_is(query, index, CAPTURE_DEAD_REENTRY))
				rc = nodelist_add(&reentries, node);
			else if (capture_is(query, index, CAPTURE_DEAD_BRANCH))
				rc = nodelist_add(&branches, node);

			if (rc != 0)
				goto cleanup;
		}
	}

	/* The branch a literal condition excludes. The query decided both
	 * which condition is literal and which branch it excludes, because
	 * both are language-specific: `0` is false in C, `false` in Rust,
	 * `False` in Python (LLR-DED-02). */
	for (size_t i = 0; i < branches.count; i++)
		if (dead_add(facts, ranges, branches.items[i],
		             DEAD_LITERAL_CONDITION) != 0)
			goto cleanup;

	/* The named siblings following each terminator, up to the first that
	 * can be entered without falling into it (LLR-DED-01). */
	for (size_t i = 0; i < terminators.count; i++) {
		TSNode sibling = ts_node_next_named_sibling(terminators.items[i]);

		while (!ts_node_is_null(sibling)) {
			if (is_reentry(&reentries, sibling))
				break;

			/* **A comment is a named sibling**, and skipping it is
			 * not cosmetic. Without this the walk records the
			 * trailing comment on the terminator's own line as
			 * dead code — so a `return` annotated in passing
			 * reports itself, and every commented line after one
			 * becomes a finding. Skipped rather than stopped at: a
			 * note between two dead statements does not make the
			 * second one run.
			 *
			 * What a comment *is* stays where it already lives.
			 * This asks the merged set `comments.scm` produced,
			 * the same set the ELOC exclusion consults, so no node
			 * type enters this file and one answer serves both
			 * (HLR-016). */
			if (byte_is_excluded(comments,
			                    ts_node_start_byte(sibling))) {
				sibling = ts_node_next_named_sibling(sibling);
				continue;
			}

			if (dead_add(facts, ranges, sibling,
			             DEAD_AFTER_TERMINATOR) != 0)
				goto cleanup;
			sibling = ts_node_next_named_sibling(sibling);
		}
	}

	dedupe_dead(facts);
	status = 0;

cleanup:
	free(terminators.items);
	free(reentries.items);
	free(branches.items);
	return status;
}

/* ---------------------------------------------------------- custom rules -- */

/* Every match of every rule bound to this file's language.
 *
 * The same query mechanism the built-in metrics use, which is what HLR-107
 * asks for and is not merely convenient: the predicate evaluation above
 * applies here unchanged, so a rule author's `#eq?` and `#match?` behave as
 * they do in `elc`'s own query files rather than being quietly ignored.
 *
 * **Nothing here judges.** A match is recorded with its identity and its line
 * range, and no severity, because `elc` has no view about whether a rule was
 * worth writing (HLR-111). That is the whole difference between this section
 * of the report and the findings.
 */
static int collect_rule_matches(const LanguageModule *module, Registry *reg,
                                const char *data, TSNode root,
                                const SpanList *excluded, FileFacts *facts)
{
	for (size_t r = 0; r < reg->rule_count; r++) {
		const CustomRule *rule = &reg->rules[r];
		TSQueryMatch      match;

		/* A query compiles against one grammar, so a rule bound to
		 * another language is not merely irrelevant here — running it
		 * would read a node table it was not compiled for. */
		if (strcmp(rule->language, module->language_name) != 0)
			continue;

		ts_query_cursor_exec(reg->cursor, rule->query, root);

		while (ts_query_cursor_next_match(reg->cursor, &match)) {
			if (!predicates_hold(rule->query, &match, data))
				continue;

			for (uint16_t i = 0; i < match.capture_count; i++) {
				uint32_t    index  = match.captures[i].index;
				TSNode      node   = match.captures[i].node;
				uint32_t    length = 0;

				/* A rule matching code this configuration does
				 * not compile has matched code that is not in
				 * the build, and reporting it would send a
				 * reader to a line that is not there
				 * (HLR-132). */
				if (byte_is_excluded(excluded,
				                     ts_node_start_byte(node)))
					continue;

				const char *capture =
					ts_query_capture_name_for_id(
						rule->query, index, &length);

				if (!capture)
					continue;

				if (facts->rule_match_count ==
				        facts->rule_match_capacity &&
				    grow((void **)&facts->rule_matches,
				         &facts->rule_match_capacity,
				         sizeof *facts->rule_matches) != 0)
					return -1;

				RuleMatch *hit =
					&facts->rule_matches[
						facts->rule_match_count];
				size_t stem = strlen(rule->stem);

				/* The identity is the file and the capture
				 * joined, so one file expresses several named
				 * rules and a reader can tell which of them
				 * matched (HLR-109). */
				hit->rule = malloc(stem + 1 + length + 1);
				if (!hit->rule)
					return -1;
				memcpy(hit->rule, rule->stem, stem);
				hit->rule[stem] = '.';
				memcpy(hit->rule + stem + 1, capture, length);
				hit->rule[stem + 1 + length] = '\0';

				hit->start_line =
					ts_node_start_point(node).row + 1;
				hit->end_line = ts_node_end_point(node).row + 1;
				facts->rule_match_count++;
			}
		}
	}

	return 0;
}

static int by_function_then_line(const void *a, const void *b)
{
	const StatementSite *x = a;
	const StatementSite *y = b;

	if (x->function != y->function)
		return x->function < y->function ? -1 : 1;
	if (x->line != y->line)
		return x->line < y->line ? -1 : 1;
	return 0;
}

static int by_line(const void *a, const void *b)
{
	const StatementSite *x = a;
	const StatementSite *y = b;

	if (x->line != y->line)
		return x->line < y->line ? -1 : 1;
	return 0;
}

/* Turn the recorded sites into counts.
 *
 * Both counts are of distinct *lines*, not of statements: two statements
 * written on one line are one line of code, and counting the captures would
 * make `a = 1; b = 2;` worth twice what the same two statements are worth on
 * separate lines — which is the same error HLR-053 forbids, in the other
 * direction.
 */
static void apply_eloc(FileMetrics *metrics, SiteList *sites)
{
	if (sites->count == 0)
		return;

	qsort(sites->items, sites->count, sizeof *sites->items,
	      by_function_then_line);

	for (size_t i = 0; i < sites->count; ) {
		size_t   function = sites->items[i].function;
		uint32_t previous = 0;
		uint32_t count    = 0;

		while (i < sites->count && sites->items[i].function == function) {
			if (count == 0 || sites->items[i].line != previous) {
				previous = sites->items[i].line;
				count++;
			}
			i++;
		}

		/* A statement outside every reported function contributes to
		 * the file and to nothing else. */
		if (function != NO_FUNCTION)
			metrics->functions[function].eloc = count;
	}

	qsort(sites->items, sites->count, sizeof *sites->items, by_line);

	uint32_t previous = 0;
	uint32_t total    = 0;

	for (size_t i = 0; i < sites->count; i++) {
		if (total == 0 || sites->items[i].line != previous) {
			previous = sites->items[i].line;
			total++;
		}
	}
	metrics->eloc = total;
}

int analyze_file(Registry *reg, const ElcOptions *opts, const char *path,
                 FileMetrics **out, FileFacts **facts_out)
{
	const LanguageModule *module;
	FileMetrics          *metrics  = NULL;
	FileFacts            *facts    = NULL;
	FnRangeIndex          ranges   = { 0 };
	SpanList              comments = { 0 };
	SiteList              sites    = { 0 };
	struct stat           st;
	void                 *map    = MAP_FAILED;
	size_t                len    = 0;
	TSTree               *tree   = NULL;
	int                   fd     = -1;
	int                   status = ANALYZE_FAILED;

	*out       = NULL;
	*facts_out = NULL;

	/* No module for this extension is a skip, not a failure: the caller
	 * records it and the exit status stays 0 (HLR-012, HLR-037). */
	module = registry_for_path(reg, path);
	if (!module)
		return ANALYZE_SKIPPED;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		goto cleanup;
	}

	if (fstat(fd, &st) != 0) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		goto cleanup;
	}

	metrics = calloc(1, sizeof *metrics);
	facts   = calloc(1, sizeof *facts);
	if (!metrics || !facts) {
		fprintf(stderr, "elc: out of memory measuring %s\n", path);
		goto cleanup;
	}

	facts->path = strdup(path);
	if (!facts->path) {
		fprintf(stderr, "elc: out of memory measuring %s\n", path);
		goto cleanup;
	}

	metrics->path = strdup(path);
	if (!metrics->path) {
		fprintf(stderr, "elc: out of memory measuring %s\n", path);
		goto cleanup;
	}
	metrics->language = strdup(module->language_name);
	if (!metrics->language) {
		fprintf(stderr, "elc: out of memory measuring %s\n", path);
		goto cleanup;
	}

	/* A zero-length file is short-circuited rather than mapped: mmap of an
	 * empty file fails with EINVAL, and an empty file is not an error
	 * (LLR-ANL-04). */
	if (st.st_size == 0) {
		*out       = metrics;
		*facts_out = facts;
		metrics    = NULL;
		facts      = NULL;
		status     = ANALYZE_OK;
		goto cleanup;
	}

	len = (size_t)st.st_size;
	map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		goto cleanup;
	}

	metrics->physical_lines = count_lines(map, len);

	ts_parser_set_language(reg->parser, module->ts_lang);

	/* The length is explicit because the mapping is not NUL-terminated
	 * (LLR-ANL-05). */
	tree = ts_parser_parse_string(reg->parser, NULL, map, (uint32_t)len);
	if (!tree) {
		fprintf(stderr, "elc: %s: parse failed\n", path);
		goto cleanup;
	}

	TSNode root = ts_tree_root_node(tree);

	/* Tree-sitter always returns a tree, and error recovery keeps the
	 * well-formed parts of a damaged one intact. What cannot be parsed is
	 * measured and set aside; everything around it is analysed normally
	 * (HLR-035).
	 *
	 * **This used to discard the whole file, and that was too blunt by two
	 * orders of magnitude.** A single macro the grammar cannot follow — the
	 * `printf(BOLD FG_BLUE "%s")` idiom is one, since `tree-sitter-c`
	 * accepts only one identifier before the first string literal of a
	 * concatenation — damages a fraction of a percent of a file and cost
	 * every metric in it. On one embedded project that turned 0.1%–1.4%
	 * damage into the loss of half the codebase and 137 perfectly parsed
	 * functions.
	 *
	 * The original objection stands and is answered rather than dismissed:
	 * metrics from a damaged tree must not be mistakable for sound ones. So
	 * the damage is measured in lines, carried on the file, and reported
	 * beside the figures it qualifies — the same way the call depth is
	 * presented beside its unresolved-call count. A reader can see exactly
	 * how much of the file `elc` could not see. */
	metrics->unparsed_lines = count_unparsed_lines(root);

	if (metrics->unparsed_lines) {
		/* Names what was lost and where, rather than only that
		 * something was: "skipped" told a reader nothing about the
		 * scale, and the scale is usually a line or two. */
		fprintf(stderr,
		        "elc: %s:%" PRIu32 ": %" PRIu32 " line%s could not be "
		        "parsed; the rest of the file is measured\n",
		        path, first_unparsed_line(root), metrics->unparsed_lines,
		        metrics->unparsed_lines == 1 ? "" : "s");
	}

	/* Comments first, then the regions this configuration does not
	 * compile, and both into one set — because every collector below asks
	 * the same question of it, and asking twice would be two mechanisms
	 * that could disagree (HLR-132).
	 *
	 * Before the functions, which is the ordering change conditional
	 * compilation forced: a function inside an inactive region must never
	 * reach the report, and the exclusion has to exist before anything
	 * consults it. */
	if (collect_comments(module, reg, map, root, &comments) != 0 ||
	    collect_inactive_regions(module, reg, opts, map, root, &comments,
	                             &metrics->undecided_regions) != 0) {
		fprintf(stderr, "elc: out of memory analysing %s\n", path);
		goto cleanup;
	}
	merge_comment_spans(&comments);

	if (collect_functions(module, reg, map, root, &comments, metrics,
	                      &ranges) != 0 ||
	    collect_statements(module, reg, map, root, &ranges, &comments,
	                       &sites) != 0 ||
	    collect_complexity(module, reg, map, root, &ranges, &comments,
	                       metrics) != 0 ||
	    collect_calls(module, reg, map, root, &ranges, &comments,
	                  facts) != 0 ||
	    collect_globals(module, reg, map, root, &ranges, &comments,
	                    facts) != 0 ||
	    collect_dead_code(module, reg, map, root, &ranges, &comments,
	                      facts) != 0 ||
	    collect_rule_matches(module, reg, map, root, &comments,
	                         facts) != 0) {
		fprintf(stderr, "elc: out of memory analysing %s\n", path);
		goto cleanup;
	}

	apply_eloc(metrics, &sites);

	*out       = metrics;
	*facts_out = facts;
	status     = metrics->unparsed_lines ? ANALYZE_DAMAGED : ANALYZE_OK;
	metrics    = NULL;
	facts      = NULL;

cleanup:
	/* Every acquired resource released on every path, in order: the tree
	 * before the mapping it points into, and the mapping in the same
	 * function that made it. */
	free(sites.items);
	free(comments.items);
	free(ranges.items);
	if (tree)
		ts_tree_delete(tree);
	if (map != MAP_FAILED)
		munmap(map, len);
	filemetrics_free(metrics);
	filefacts_free(facts);
	if (fd >= 0)
		close(fd);
	return status;
}
