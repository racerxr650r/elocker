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

#include "diag.h"
#include "preproc.h"
#include "repair.h"
#include "analyze.h"
#include "elc.h"
#include "elfsyms.h"
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
static int  span_add(SpanList *spans, uint32_t start, uint32_t end,
                     uint32_t start_line, uint32_t end_line);

void filemetrics_free(FileMetrics *metrics)
{
	if (!metrics)
		return;

	for (size_t i = 0; i < metrics->function_count; i++)
		free(metrics->functions[i].name);
	free(metrics->functions);
	for (size_t i = 0; i < metrics->absent_count; i++)
		free(metrics->absent[i].name);
	free(metrics->absent);
	for (size_t i = 0; i < metrics->stdlib_count; i++)
		free(metrics->stdlib_headers[i]);
	free(metrics->stdlib_headers);
	free(metrics->stdlib_kinds);
	free(metrics->language);
	free(metrics->directory);
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
static int analyze_grow(void **items, size_t *capacity, size_t item_size)
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

/* Every region the grammar could not follow, with the source itself, into the
 * debug companion (HLR-195).
 *
 * A separate walk from `tally_errors` rather than a hook inside it, and the
 * reason is that the two count different things: the tally coalesces regions
 * so a line is counted once, which is right for the figure a reader sees and
 * wrong for a log meant to reproduce a parser defect. Here every region is
 * recorded as the grammar reported it.
 *
 * Guarded by `diag_active`, so a run without `--dbg` walks nothing: this is
 * work whose only purpose is to fill a file that does not exist.
 */
static void log_parse_failures(TSNode node, const char *path, const char *data,
                               size_t length)
{
	if (ts_node_is_error(node) || ts_node_is_missing(node)) {
		diag_parse_failure(path, data, length,
		                   ts_node_start_point(node).row + 1,
		                   ts_node_end_point(node).row + 1);
		return;
	}

	if (!ts_node_has_error(node))
		return;

	for (uint32_t i = 0; i < ts_node_child_count(node); i++)
		log_parse_failures(ts_node_child(node, i), path, data, length);
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

/* A directive is not a filter. `#set!` and its relatives attach metadata for a
 * consumer that wants it, and rejecting a match for carrying one would turn a
 * note into a deletion. */
static bool is_directive(const char *name, uint32_t length)
{
	size_t n = length;

	while (n > 0 && name[n - 1] == '\0')
		n--;
	return n > 0 && name[n - 1] == '!';
}

/* `#eq?`, `#not-eq?` and `#any-of?`: does the captured text equal any of the
 * remaining steps? */
static bool any_step_equals(const TSQuery *query, const TSQueryMatch *match,
                            const char *data,
                            const TSQueryPredicateStep *steps, size_t count,
                            const char *text)
{
	for (size_t i = 2; i < count; i++) {
		const char *value = step_string(query, &steps[i]);
		TSNode      other;

		if (value) {
			if (strcmp(text, value) == 0)
				return true;
			continue;
		}

		/* A capture on the right-hand side compares two spans of the
		 * source against each other, which is how a query asks whether
		 * two identifiers are the same. */
		if (match_capture(match, steps[i].value_id, &other)) {
			char *rhs = node_text(data, other);
			bool  hit = rhs && strcmp(text, rhs) == 0;

			free(rhs);
			if (hit)
				return true;
		}
	}
	return false;
}

/* `#match?` and `#not-match?`, with `negated` selecting between them. */
static bool regex_holds(const TSQuery *query,
                        const TSQueryPredicateStep *steps, size_t count,
                        const char *text, bool negated)
{
	const char *pattern = count >= 3 ? step_string(query, &steps[2]) : NULL;
	regex_t     re;
	bool        hit;

	/* An uncompilable pattern is the query author's defect, and the safe
	 * reading of a filter that cannot be applied is that nothing passes
	 * it — which is why a bad pattern fails the negated form too. */
	if (!pattern || regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB) != 0)
		return false;

	hit = regexec(&re, text, 0, NULL, 0) == 0;
	regfree(&re);
	return negated ? !hit : hit;
}

/* One predicate's steps, minus its trailing sentinel, evaluated against the
 * match. `steps[0]` is the predicate name. */
/* Evaluate the named filter against the captured text.
 *
 * The five are the ones Tree-sitter's query language defines; a predicate
 * naming anything else rejects the match rather than being ignored, so a
 * misspelling in a query file is visible as a query that matches nothing
 * rather than as one that matches everything.
 */
static bool filter_holds(const TSQuery *query, const TSQueryMatch *match,
                         const char *data, const TSQueryPredicateStep *steps,
                         size_t count, const char *name, uint32_t length,
                         const char *text)
{
	bool not_eq  = text_is(name, length, "not-eq?");
	bool not_mat = text_is(name, length, "not-match?");

	if (text_is(name, length, "eq?") || not_eq ||
	    text_is(name, length, "any-of?")) {
		bool found = any_step_equals(query, match, data, steps, count,
		                             text);

		return not_eq ? !found : found;
	}

	if (text_is(name, length, "match?") || not_mat)
		return regex_holds(query, steps, count, text, not_mat);

	return false;
}

static bool predicate_holds(const TSQuery *query, const TSQueryMatch *match,
                            const char *data,
                            const TSQueryPredicateStep *steps, size_t count)
{
	uint32_t    length = 0;
	const char *name;
	char       *text;
	bool        result;
	TSNode      node;

	if (count == 0 || steps[0].type != TSQueryPredicateStepTypeString)
		return false;

	name = ts_query_string_value_for_id(query, steps[0].value_id, &length);
	if (!name || length == 0)
		return false;

	if (is_directive(name, length))
		return true;

	if (count < 2 || steps[1].type != TSQueryPredicateStepTypeCapture)
		return false;

	if (!match_capture(match, steps[1].value_id, &node))
		return false;

	text = node_text(data, node);
	if (!text)
		return false;

	result = filter_holds(query, match, data, steps, count, name, length,
	                      text);

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

/* Both halves of the function contract from one match, or false where the
 * match supplies only one of them.
 *
 * A pattern capturing only a name or only a body contributes no function.
 * That is deliberate: the pair is what identifies a function, and silently
 * accepting half of it would report a function with no line range or a line
 * range with no name.
 *
 * Shared by the two passes over `functions.scm` so that "what counts as a
 * function" is decided once. A filtered run answers the question twice — which
 * functions the image lacks, then which functions to measure — and the two
 * must not be able to disagree about what a function is.
 */
static bool function_match(const TSQuery *query, const TSQueryMatch *match,
                           TSNode *name_node, TSNode *body_node)
{
	bool have_name = false;
	bool have_body = false;

	for (uint16_t i = 0; i < match->capture_count; i++) {
		uint32_t index = match->captures[i].index;

		if (capture_is(query, index, CAPTURE_FUNCTION_NAME)) {
			*name_node = match->captures[i].node;
			have_name  = true;
		} else if (capture_is(query, index, CAPTURE_FUNCTION_BODY)) {
			*body_node = match->captures[i].node;
			have_body  = true;
		}
	}

	return have_name && have_body;
}

/* Append one absent function to the file's list, and its whole extent to the
 * span list the caller will merge into the excluded set. */
static int absent_add(FileMetrics *metrics, SpanList *spans, char *name,
                      TSNode name_node, TSNode body_node)
{
	if (metrics->absent_count == metrics->absent_capacity) {
		size_t          next   = metrics->absent_capacity
		                                 ? metrics->absent_capacity * 2 : 8;
		AbsentFunction *bigger = realloc(metrics->absent,
		                                 next * sizeof *bigger);

		if (!bigger)
			return -1;
		metrics->absent          = bigger;
		metrics->absent_capacity = next;
	}

	/* Both allocations before either record, so a failure of the second
	 * cannot leave the name owned by the list and freed by the caller
	 * too (HLR-125). */
	if (spans->count == spans->capacity &&
	    analyze_grow((void **)&spans->items, &spans->capacity,
	         sizeof *spans->items) != 0)
		return -1;

	uint32_t name_start = ts_node_start_byte(name_node);
	uint32_t body_start = ts_node_start_byte(body_node);
	uint32_t name_row   = ts_node_start_point(name_node).row;
	uint32_t body_row   = ts_node_start_point(body_node).row;
	uint32_t start_line = (name_row < body_row ? name_row : body_row) + 1;

	metrics->absent[metrics->absent_count].name = name;
	metrics->absent[metrics->absent_count].line = start_line;
	metrics->absent_count++;

	spans->items[spans->count].start_byte =
		name_start < body_start ? name_start : body_start;
	spans->items[spans->count].end_byte   = ts_node_end_byte(body_node);
	spans->items[spans->count].start_line = start_line;
	spans->items[spans->count].end_line   =
		ts_node_end_point(body_node).row + 1;
	spans->count++;
	return 0;
}

/* Record every function the image does not define, and exclude its bytes.
 *
 * **The filter joins the exclusion set rather than gating `collect_functions`
 * alone**, and the difference is not presentational. Dropping the function
 * from the reported set alone would leave its statements attributed to no
 * function, which is to say counted as file-scope ELOC — inflating the one
 * figure HLR-145 keeps separate with lines that are plainly inside a function.
 * Excluding the extent instead is what makes HLR-144 fall out: no statement,
 * decision point, call site, global access, dead span, or rule match inside an
 * omitted function reaches any later stage, because every collector already
 * asks whether a byte is measured (LLR-ANL-51, LLR-ANL-52).
 *
 * A separate pass rather than a test inside `collect_functions`, because query
 * matches arrive in no source order: a nested function reported before the
 * omitted function containing it would be recorded and only then excluded.
 * Running the exclusion to completion first makes the result independent of
 * the order the library matched, which is the rule HLR-032 draws everywhere
 * else.
 */
static int collect_absent_functions(const LanguageModule *module, Registry *reg,
                                    const SymbolSet *image, const char *data,
                                    TSNode root, SpanList *excluded,
                                    SpanList *kept, FileMetrics *metrics)
{
	TSQuery     *query  = module->queries[QUERY_FUNCTIONS];
	SpanList     absent = { 0 };
	TSQueryMatch match;
	int          status = -1;

	if (!image)
		return 0;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		TSNode name_node;
		TSNode body_node;

		if (!predicates_hold(query, &match, data))
			continue;
		if (!function_match(query, &match, &name_node, &body_node))
			continue;

		/* A function inside a region this configuration does not
		 * compile is already gone, and is not a function the image
		 * failed to keep: reporting it absent would answer a question
		 * about the linker with a fact about the preprocessor. */
		if (byte_is_excluded(excluded, ts_node_start_byte(name_node)))
			continue;

		char *name = name_from(data, name_node);

		if (!name)
			goto cleanup;

		if (elfsyms_defines_in(image, name, metrics->path)) {
			/* Kept by the link, so its extent is where the finer
			 * filter may look. HLR-154 confines line pruning to
			 * within a function the image defines: a line outside
			 * every one of them is file-scope code, which the
			 * image's *function* set says nothing about and which
			 * HLR-145 requires be measured and reported on its own
			 * (LLR-ANL-60). */
			if (span_add(kept, ts_node_start_byte(body_node),
			             ts_node_end_byte(body_node),
			             ts_node_start_point(body_node).row + 1,
			             ts_node_end_point(body_node).row + 1) != 0) {
				free(name);
				goto cleanup;
			}
			free(name);
			continue;
		}

		if (absent_add(metrics, &absent, name, name_node,
		               body_node) != 0) {
			free(name);
			goto cleanup;
		}
	}

	/* Merged into the excluded set only now the pass is over. Appending to
	 * it as we went would have left `byte_is_excluded` reading an unsorted
	 * tail — its early exit assumes the list is ordered — and would have
	 * made the answer for one function depend on which functions the query
	 * happened to report before it. */
	for (size_t i = 0; i < absent.count; i++) {
		if (excluded->count == excluded->capacity &&
		    analyze_grow((void **)&excluded->items, &excluded->capacity,
		         sizeof *excluded->items) != 0)
			goto cleanup;
		excluded->items[excluded->count++] = absent.items[i];
	}
	merge_comment_spans(excluded);
	status = 0;

cleanup:
	free(absent.items);
	return status;
}

/* Run functions.scm over the tree and record every match that supplies both
 * halves of the contract.
 */
/* The visibility each function's name node was captured with.
 *
 * Keyed on the name node's byte offset, which is exactly the node
 * `functions.scm` captures — so the two queries agree on what a function *is*
 * without either needing to know how the other identifies one.
 */
typedef struct {
	uint32_t   offset;
	Visibility visibility;
} VisibilityMark;

typedef struct {
	VisibilityMark *items;
	size_t          count;
	size_t          capacity;
} VisibilitySet;

static int by_visibility_offset(const void *a, const void *b)
{
	uint32_t x = ((const VisibilityMark *)a)->offset;
	uint32_t y = ((const VisibilityMark *)b)->offset;

	return x < y ? -1 : x > y ? 1 : 0;
}

/* Record one capture, keeping the **earliest pattern** that claimed this node.
 *
 * That rule is what lets a language state the specific case before the general
 * one — `static` before "every function" in C, `pub` before "every function"
 * in Rust — and it is why the two languages need opposite orderings and no
 * other difference (HLR-209). It is the rule `collect_inactive_regions`
 * already applies to overlapping conditional patterns.
 */
static int visibility_add(VisibilitySet *set, uint32_t offset, Visibility v)
{
	for (size_t i = 0; i < set->count; i++)
		if (set->items[i].offset == offset)
			return 0;   /* an earlier pattern already decided */

	if (set->count == set->capacity &&
	    analyze_grow((void **)&set->items, &set->capacity,
	                 sizeof *set->items) != 0)
		return -1;

	set->items[set->count].offset     = offset;
	set->items[set->count].visibility = v;
	set->count++;
	return 0;
}

/* Run the language's visibility query, where it has one.
 *
 * A module supplying none leaves the set empty, and every function then reports
 * unknown — which is a different claim from public and is rendered as one
 * (HLR-209, HLR-138).
 */
static int collect_visibility(const LanguageModule *module, Registry *reg,
                              const char *data, TSNode root,
                              VisibilitySet *out)
{
	TSQuery      *query = module->queries[QUERY_VISIBILITY];
	TSQueryMatch  match;

	if (!query)
		return 0;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			uint32_t    len;
			const char *cap = ts_query_capture_name_for_id(
				query, match.captures[i].index, &len);
			Visibility  v;

			if (len == sizeof "function.public" - 1 &&
			    memcmp(cap, "function.public", len) == 0)
				v = VISIBILITY_PUBLIC;
			else if (len == sizeof "function.private" - 1 &&
			         memcmp(cap, "function.private", len) == 0)
				v = VISIBILITY_PRIVATE;
			else
				continue;   /* an internal `@_` capture */

			if (visibility_add(out, ts_node_start_byte(
					match.captures[i].node), v) != 0)
				return -1;
		}
	}

	if (out->count > 1)
		qsort(out->items, out->count, sizeof *out->items,
		      by_visibility_offset);
	return 0;
}

/* What the query said about the function whose name starts here. */
static Visibility visibility_of(const VisibilitySet *set, uint32_t offset)
{
	size_t lo = 0;
	size_t hi = set->count;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;

		if (set->items[mid].offset < offset)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < set->count && set->items[lo].offset == offset
	               ? set->items[lo].visibility : VISIBILITY_UNKNOWN;
}

/* Build one function's record and its attribution range.
 *
 * The two together, because the range must be the reported one: a statement is
 * attributed to the narrowest range containing it, and attributing by a
 * different extent from the one shown would let a statement count for a
 * function whose printed location does not contain it.
 */
static int record_function(FileMetrics *metrics, FnRangeIndex *ranges,
                           const char *data, TSNode name_node,
                           TSNode body_node, const VisibilitySet *visible)
{
	FunctionMetric *fn = &metrics->functions[metrics->function_count];

	memset(fn, 0, sizeof *fn);

	fn->name = name_from(data, name_node);
	if (!fn->name)
		return -1;

	fn->visibility = visibility_of(visible,
	                               ts_node_start_byte(name_node));

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
	    analyze_grow((void **)&ranges->items, &ranges->capacity,
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
	return 0;
}

static int collect_functions(const LanguageModule *module, Registry *reg,
                             const char *data, TSNode root,
                             const SpanList *excluded, FileMetrics *metrics,
                             FnRangeIndex *ranges)
{
	TSQuery      *query    = module->queries[QUERY_FUNCTIONS];
	size_t        capacity = 0;
	TSQueryMatch  match;
	VisibilitySet visible  = { 0 };
	int           status   = -1;

	if (collect_visibility(module, reg, data, root, &visible) != 0)
		goto done;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		TSNode name_node;
		TSNode body_node;

		if (!function_match(query, &match, &name_node, &body_node))
			continue;

		/* A function the configuration does not compile is not
		 * measured, and is not reported: it belongs to no build, and
		 * counting it would report the union of every configuration the
		 * source can express (HLR-132). The same test now answers for a
		 * function the linked image does not define, whose extent the
		 * pass above added to this set (HLR-144). */
		if (byte_is_excluded(excluded, ts_node_start_byte(name_node)))
			continue;

		if (metrics->function_count == capacity &&
		    functions_grow(metrics, &capacity) != 0)
			goto done;

		if (record_function(metrics, ranges, data, name_node,
		                    body_node, &visible) != 0)
			goto done;
	}

	status = 0;

done:
	free(visible.items);
	return status;
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
			    analyze_grow((void **)&spans->items, &spans->capacity,
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
	/* The same three spans in lines, 1-based.
	 *
	 * Bytes are what the exclusion works in and lines are what the image
	 * answers in, so both are carried rather than converted at the point of
	 * use — a conversion needing the buffer would put the buffer into every
	 * signature between here and the evidence (HLR-211). */
	uint32_t start_line;
	uint32_t end_line;
	uint32_t alt_line;       /* the `#else` line; 0 with no alternative */
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
	    analyze_grow((void **)&spans->items, &spans->capacity,
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
	    analyze_grow((void **)&list->items, &list->capacity,
	         sizeof *list->items) != 0)
		return -1;
	list->items[list->count++] = *region;
	return 0;
}

/* Read one conditional match into a region.
 *
 * Returns false for a pattern that captures no region: it describes nothing
 * this can act on, and is skipped rather than guessed at.
 */
static bool read_cond_region(const TSQuery *query, const TSQueryMatch *match,
                             CondRegion *region)
{
	bool seen = false;

	region->pattern = match->pattern_index;

	for (uint16_t i = 0; i < match->capture_count; i++) {
		uint32_t index = match->captures[i].index;
		TSNode   node  = match->captures[i].node;

		if (capture_is(query, index, CAPTURE_COND_REGION)) {
			region->region_start = ts_node_start_byte(node);
			region->region_end   = ts_node_end_byte(node);
			region->start_line   = ts_node_start_point(node).row + 1;
			region->end_line     = ts_node_end_point(node).row + 1;
			seen                 = true;
		} else if (capture_is(query, index, CAPTURE_COND_ALT)) {
			region->alt_start = ts_node_start_byte(node);
			region->alt_end   = ts_node_end_byte(node);
			region->alt_line  = ts_node_start_point(node).row + 1;
			region->has_alt   = true;
		} else if (capture_is(query, index, CAPTURE_COND_TRUE)) {
			region->decided = true;
			region->holds   = true;
		} else if (capture_is(query, index, CAPTURE_COND_FALSE)) {
			region->decided = true;
			region->holds   = false;
		} else if (capture_is(query, index, CAPTURE_COND_SYMBOL)) {
			region->symbol     = node;
			region->has_symbol = true;
		} else if (capture_is(query, index, CAPTURE_COND_NEGATED)) {
			region->negated = true;
		}
	}

	return seen;
}

/* Every region the conditional query matched, in the order the cursor produced
 * them.
 */
static int gather_cond_regions(const TSQuery *query, Registry *reg,
                               const char *data, TSNode root, CondList *regions)
{
	TSQueryMatch match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		CondRegion region = { 0 };

		if (!predicates_hold(query, &match, data))
			continue;

		if (!read_cond_region(query, &match, &region))
			continue;

		if (condlist_add(regions, &region) != 0)
			return -1;
	}

	return 0;
}

/* What the image's line information says about one conditional region.
 *
 * The third answer to a definedness test, after the query's own constants and
 * the user's -D set, and the only one that describes the build rather than the
 * source: a region the compiler emitted no instruction for is a region that
 * build did not compile (HLR-211).
 *
 * **Coverage governs, exactly as it governs the line pruning** (HLR-154). A
 * file the line information never described contributes no entries at all, so
 * a rule keyed on absence alone would find every region of it inactive and
 * delete the file. The coverage test comes first here for the same reason it
 * comes first there, and a run with no image reaches neither.
 *
 * **Absence is evidence between two presences, and nowhere else.** HLR-154
 * already states the limit this works within: an optimiser folds one line into
 * a neighbour, so one absent line proves nothing. What is asked instead is
 * whether an *entire* region is absent while the code around it is present —
 * a region with an alternative is judged against that alternative, which is
 * the strongest form of the question, and one without is judged against the
 * lines before and after it in the same file. Neither is proof, which is why
 * the disposition is counted separately from the ones a -D settled.
 */
typedef enum {
	EVIDENCE_NONE = 0,   /* no answer; the region stays undecidable   */
	EVIDENCE_ACTIVE,     /* this build compiled it                    */
	EVIDENCE_INACTIVE    /* this build compiled none of it            */
} RegionEvidence;

/* The two branches disagreeing, which is the whole of the evidence where a
 * region has an alternative — and it is self-contained: exactly one of them was
 * compiled, and the image says which.
 *
 * Both, or neither, is a condition this rule cannot read — a function the build
 * never emitted reads as "neither" — and the answer is no answer rather than
 * one of two resolved by the order they were tested in.
 */
static RegionEvidence evidence_from_alternative(const LineCoverage *lines,
                                                const char *path,
                                                const CondRegion *region,
                                                bool body)
{
	bool alt = dwarfline_compiled_between(lines, path, region->alt_line,
	                                      region->end_line);

	if (body != alt)
		return body ? EVIDENCE_ACTIVE : EVIDENCE_INACTIVE;

	return EVIDENCE_NONE;
}

/* Whether compiled code stands on both sides of the region, in the same file.
 *
 * What makes an absent region's absence mean something where it has no
 * alternative to be judged against: the line information was being written
 * across this stretch of the file, so the gap is a gap rather than the edge of
 * what the build described.
 */
static bool compiled_either_side(const LineCoverage *lines, const char *path,
                                 const CondRegion *region)
{
	return region->start_line > 1 &&
	       dwarfline_compiled_between(lines, path, 1,
	                                  region->start_line - 1) &&
	       dwarfline_compiled_between(lines, path, region->end_line + 1,
	                                  UINT32_MAX);
}

static RegionEvidence region_evidence(const SymbolSet *image, const char *path,
                                      const CondRegion *region)
{
	const LineCoverage *lines;
	uint32_t            body_end;
	bool                body;

	if (!image || !dwarfline_covers(&image->lines, path))
		return EVIDENCE_NONE;

	lines = &image->lines;

	/* The region proper stops where its alternative begins: `#else` is the
	 * other branch, and evidence about it is evidence about the other
	 * decision rather than this one. The directive lines are inside both
	 * spans and produce no instruction either way, so including them costs
	 * nothing and keeps the arithmetic off by nobody. */
	body_end = region->has_alt ? region->alt_line : region->end_line;
	if (region->start_line == 0 || region->start_line > body_end)
		return EVIDENCE_NONE;

	body = dwarfline_compiled_between(lines, path, region->start_line,
	                                  body_end);

	if (region->has_alt)
		return evidence_from_alternative(lines, path, region, body);

	/* With no alternative there is nothing to compare against but the rest
	 * of the file. Compiled is compiled — and deciding so excludes nothing,
	 * since a region with no alternative that holds loses none of itself. */
	if (body)
		return EVIDENCE_ACTIVE;

	return compiled_either_side(lines, path, region) ? EVIDENCE_INACTIVE
	                                                 : EVIDENCE_NONE;
}

/* Exclude what one region's condition settles, or count it undecided.
 *
 * Three dispositions and not two, and they are counted apart because they are
 * different claims: the source or a -D settled it, the image's line
 * information is evidence about the build that settled it, or nothing settled
 * it and both branches stay (HLR-133, HLR-211).
 *
 * Returns 0, or -1 when a span cannot be recorded.
 */
static int apply_cond_region(const ElcOptions *opts, const SymbolSet *image,
                             const char *path, const char *data,
                             const CondRegion *region, SpanList *spans,
                             uint32_t *undecided, uint32_t *from_image)
{
	bool decided = region->decided;
	bool holds   = region->holds;

	if (!decided && region->has_symbol &&
	    symbol_defined(opts, data, region->symbol)) {
		/* Mentioned by a -D, so the definedness test has an answer. The
		 * negated form is active while the symbol is *un*defined, so
		 * being defined excludes it. */
		decided = true;
		holds   = !region->negated;
	}

	if (!decided) {
		/* Nothing in the source or the -D set settled it, so the image
		 * is asked. Asked last, and only here: a -D is what the user
		 * said this configuration is, and evidence about one build must
		 * not overrule it (HLR-132). */
		RegionEvidence evidence = region_evidence(image, path, region);

		if (evidence != EVIDENCE_NONE) {
			decided = true;
			holds   = evidence == EVIDENCE_ACTIVE;
			(*from_image)++;
		}
	}

	if (!decided) {
		/* Either the query recognised nothing it could settle, or the
		 * symbol was named by no -D. Both branches stay and the count
		 * says how often that happened — the over-counting direction,
		 * which is visible, rather than the under-counting one, which is
		 * not (HLR-133). */
		(*undecided)++;
		return 0;
	}

	if (holds) {
		/* Only the alternative goes; a region with none loses
		 * nothing. */
		if (region->has_alt &&
		    span_add(spans, region->alt_start, region->alt_end, 0, 0) != 0)
			return -1;
		return 0;
	}

	/* Everything up to the alternative, or the whole region where there is
	 * none. The directive lines go with it, which costs nothing: a directive
	 * is not a statement, a decision, a call, or an access. */
	uint32_t end = region->has_alt ? region->alt_start : region->region_end;

	return span_add(spans, region->region_start, end, 0, 0);
}

/* Every region this configuration excludes, appended to `spans`, and the count
 * of regions left active because their condition could not be decided.
 *
 * A language with no `conditionals.scm` has no conditional compilation, which
 * is the truth for a language that has none; the required six are unchanged
 * and a module omitting this file is not a broken one (HLR-134, HLR-121).
 */
static int collect_inactive_regions(const LanguageModule *module, Registry *reg,
                                    const ElcOptions *opts,
                                    const SymbolSet *image, const char *path,
                                    const char *data,
                                    TSNode root, SpanList *spans,
                                    uint32_t *undecided, uint32_t *from_image)
{
	TSQuery *query = module->queries[QUERY_CONDITIONALS];
	CondList regions = { 0 };
	int      status  = -1;

	*undecided  = 0;
	*from_image = 0;
	if (!query)
		return 0;

	if (gather_cond_regions(query, reg, data, root, &regions) != 0)
		goto done;

	/* Guarded, and not as an optimisation: passing a null pointer to qsort
	 * is undefined even with a count of zero, and a language whose module
	 * supplies no conditional query reaches here with exactly that. */
	if (regions.count > 1)
		qsort(regions.items, regions.count, sizeof *regions.items,
		      by_region);

	for (size_t i = 0; i < regions.count; i++) {
		const CondRegion *region = &regions.items[i];

		/* One region, one decision: the earliest pattern that matched it
		 * has already been sorted to the front. */
		if (i > 0 && regions.items[i - 1].region_start ==
		                     region->region_start)
			continue;

		/* Nested inside something already excluded. Not compiled, so
		 * neither pruned again nor counted undecided. */
		if (span_excluded(spans, region->region_start))
			continue;

		if (apply_cond_region(opts, image, path, data, region, spans,
		                      undecided, from_image) != 0)
			goto done;
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
			    analyze_grow((void **)&sites->items, &sites->capacity,
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
				    analyze_grow((void **)&facts->calls,
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
				    analyze_grow((void **)&facts->address_taken,
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
			    analyze_grow((void **)&facts->globals,
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
	    analyze_grow((void **)&list->items, &list->capacity, sizeof *list->items) != 0)
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
	    analyze_grow((void **)&facts->dead, &facts->dead_capacity,
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

/* The three capture sets the dead-code query yields, collected in full.
 *
 * In full before anything is walked: the sibling walk asks "is this one a
 * re-entry point?", and a query cursor answers only in the order it matched —
 * which is not the order the walk needs.
 */
static int collect_dead_captures(const TSQuery *query, Registry *reg,
                                 const char *data, TSNode root,
                                 NodeList *terminators, NodeList *reentries,
                                 NodeList *branches)
{
	TSQueryMatch match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
		if (!predicates_hold(query, &match, data))
			continue;

		for (uint16_t i = 0; i < match.capture_count; i++) {
			uint32_t index = match.captures[i].index;
			TSNode   node  = match.captures[i].node;
			int      rc    = 0;

			if (capture_is(query, index, CAPTURE_DEAD_TERM))
				rc = nodelist_add(terminators, node);
			else if (capture_is(query, index, CAPTURE_DEAD_REENTRY))
				rc = nodelist_add(reentries, node);
			else if (capture_is(query, index, CAPTURE_DEAD_BRANCH))
				rc = nodelist_add(branches, node);

			if (rc != 0)
				return -1;
		}
	}
	return 0;
}

/* The named siblings following one terminator, up to the first that can be
 * entered without falling into it (LLR-DED-01).
 */
static int record_after_terminator(FileFacts *facts, const FnRangeIndex *ranges,
                                   const SpanList *comments,
                                   const NodeList *reentries, TSNode terminator)
{
	TSNode sibling = ts_node_next_named_sibling(terminator);

	while (!ts_node_is_null(sibling)) {
		if (is_reentry(reentries, sibling))
			return 0;

		/* **A comment is a named sibling**, and skipping it is not
		 * cosmetic. Without this the walk records the trailing comment
		 * on the terminator's own line as dead code — so a `return`
		 * annotated in passing reports itself, and every commented line
		 * after one becomes a finding. Skipped rather than stopped at:
		 * a note between two dead statements does not make the second
		 * one run.
		 *
		 * What a comment *is* stays where it already lives. This asks
		 * the merged set `comments.scm` produced, the same set the ELOC
		 * exclusion consults, so no node type enters this file and one
		 * answer serves both (HLR-016). */
		if (!byte_is_excluded(comments, ts_node_start_byte(sibling)) &&
		    dead_add(facts, ranges, sibling, DEAD_AFTER_TERMINATOR) != 0)
			return -1;

		sibling = ts_node_next_named_sibling(sibling);
	}
	return 0;
}

int collect_dead_code(const LanguageModule *module, Registry *reg,
                      const char *data, TSNode root,
                      const FnRangeIndex *ranges, const SpanList *comments,
                      FileFacts *facts)
{
	TSQuery  *query = module->queries[QUERY_DEADCODE];
	NodeList  terminators = { 0 };
	NodeList  reentries   = { 0 };
	NodeList  branches    = { 0 };
	int       status = -1;

	/* A language with no dead-code query is not a failure and is not
	 * clean. The flag stays false and the report says the analysis was not
	 * performed for that language — a different claim from "none found",
	 * and the only honest one (HLR-139, LLR-DED-05). */
	if (!query) {
		facts->dead_analysed = false;
		return 0;
	}
	facts->dead_analysed = true;

	if (collect_dead_captures(query, reg, data, root, &terminators,
	                          &reentries, &branches) != 0)
		goto cleanup;

	/* The branch a literal condition excludes. The query decided both
	 * which condition is literal and which branch it excludes, because
	 * both are language-specific: `0` is false in C, `false` in Rust,
	 * `False` in Python (LLR-DED-02). */
	for (size_t i = 0; i < branches.count; i++)
		if (dead_add(facts, ranges, branches.items[i],
		             DEAD_LITERAL_CONDITION) != 0)
			goto cleanup;

	for (size_t i = 0; i < terminators.count; i++)
		if (record_after_terminator(facts, ranges, comments, &reentries,
		                            terminators.items[i]) != 0)
			goto cleanup;

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
				    analyze_grow((void **)&facts->rule_matches,
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
		 * the file and to nothing else — and, when a filter is in
		 * force, to the one figure the filter did not narrow, since
		 * the image's function set says nothing about code that is not
		 * a function (HLR-145, LLR-ANL-53). Counted whichever way the
		 * run was made; only the reporting of it is conditional. */
		if (function != NO_FUNCTION)
			metrics->functions[function].eloc = count;
		else
			metrics->scope_eloc = count;
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

/* The directory containing `path`, as a fresh allocation (HLR-160).
 *
 * Written once and called from both places a FileMetrics is constructed —
 * the analysis of a source file, and the reader that rebuilds a model from a
 * saved record — so that a component's directory is the same string whichever
 * way the model arrived. Deriving it at each *use* is what HLR-160 forbids;
 * deriving it once per component, here, is what the field is for.
 *
 * The last separator is the split point, and the two edge cases are the ones
 * that make a naive rsplit wrong: a file directly under the root has an empty
 * prefix and its directory is "/", and a path carrying no separator at all has
 * no directory to name and yields "." — the working directory, which is what
 * the path is relative to. Discovery canonicalises every analysed path
 * (HLR-072), so the second case reaches this function only from a record
 * someone wrote by hand.
 */
char *component_directory(const char *path)
{
	const char *slash;

	if (!path)
		return NULL;

	slash = strrchr(path, '/');
	if (!slash)
		return strdup(".");
	if (slash == path)
		return strdup("/");

	return strndup(path, (size_t)(slash - path));
}

/* The two records a measured file produces, and the strings they own outright.
 *
 * Returns 0 with both populated, or -1 after a diagnostic. On failure the
 * caller's cleanup releases whatever was allocated.
 */
static int prepare_records(const LanguageModule *module, const char *path,
                           FileMetrics **metrics_out, FileFacts **facts_out)
{
	FileMetrics *metrics = calloc(1, sizeof *metrics);
	FileFacts   *facts   = calloc(1, sizeof *facts);

	*metrics_out = metrics;
	*facts_out   = facts;

	if (metrics && facts) {
		facts->path       = strdup(path);
		metrics->path     = strdup(path);
		metrics->language = strdup(module->language_name);
		/* Recorded here, once, rather than sliced off the path by
		 * every analysis that groups by directory (HLR-160). */
		metrics->directory = component_directory(path);
	}

	if (!metrics || !facts || !facts->path || !metrics->path ||
	    !metrics->directory || !metrics->language) {
		diag_printf("elc: out of memory measuring %s\n", path);
		return -1;
	}
	return 0;
}

/* What every collector below must be able to ask about, gathered first.
 *
 * Comments, then the regions this configuration does not compile, and both into
 * one set — because every collector asks the same question of it, and asking
 * twice would be two mechanisms that could disagree (HLR-132).
 *
 * Before the functions, which is the ordering change conditional compilation
 * forced: a function inside an inactive region must never reach the report, and
 * the exclusion has to exist before anything consults it.
 *
 * The image is last of the three because it is the only one that needs the
 * others settled — a function inside an inactive region was never built and is
 * not one the linker discarded (HLR-144).
 */
/* True when a byte lies inside one of the spans, which here are function
 * extents rather than exclusions and so are in no particular order: this is a
 * plain scan, not `byte_is_excluded`'s early-exiting one. */
static bool span_covers(const SpanList *spans, uint32_t byte)
{
	for (size_t i = 0; i < spans->count; i++)
		if (byte >= spans->items[i].start_byte &&
		    byte < spans->items[i].end_byte)
			return true;
	return false;
}

/* Whether a line holds anything a measurement could rest on.
 *
 * A blank line produces no instruction in any build, so its absence from the
 * mapping says nothing about this one. Pruning it removes nothing and counting
 * it would inflate the figure of HLR-155 — a report claiming four hundred
 * lines pruned, most of them empty, would misstate how far the image narrowed
 * it.
 */
static bool line_is_prunable(const char *data, size_t start, size_t end)
{
	for (size_t i = start; i < end; i++)
		if (data[i] != ' ' && data[i] != '\t' && data[i] != '\r')
			return true;
	return false;
}

/* Exclude the lines this build compiled no instruction for (HLR-153).
 *
 * The fourth and last exclusion, and last for the reason the third is third:
 * each needs the ones before it settled. A line inside a comment, inside a
 * region this configuration does not compile, or inside a function the linker
 * discarded is already gone, and pruning it again would count it twice in a
 * figure a reader is meant to act on (LLR-ANL-58).
 *
 * **Two tests, and the first governs the second.** Coverage is established per
 * file before any line within it is judged. A translation unit compiled
 * without debug information contributes no line entries at all, so a rule
 * keyed on absence alone would find every line of it uncompiled and delete the
 * file — evidence of nothing at all read as evidence of everything. Where
 * coverage is not established the file is counted and nothing in it is touched
 * (HLR-154, HLR-155).
 *
 * **Confined to within functions the image defines.** Code at file scope has
 * few line entries to its name, and it is the one figure HLR-145 requires be
 * kept separate and honest; a rule that pruned uncovered lines everywhere
 * would delete precisely that. The kept extents come from the pass above,
 * which already had the image in hand.
 *
 * A line that is blank or already excluded is skipped rather than counted.
 * Pruning it removes nothing, and counting it would inflate the figure of
 * HLR-155 with lines no measurement rested on.
 */
static int prune_uncompiled_lines(const SymbolSet *image, const char *data,
                                  size_t len, const char *path,
                                  const SpanList *kept, SpanList *excluded,
                                  FileMetrics *metrics)
{
	SpanList pruned = { 0 };
	uint32_t line   = 1;
	size_t   start  = 0;
	int      status = -1;

	if (!image)
		return 0;

	/* Only asked where an image was named: with none, the question does
	 * not arise and the file is not counted as uncovered. */
	if (!dwarfline_covers(&image->lines, path)) {
		metrics->coverage_unestablished = true;
		return 0;
	}

	for (size_t i = 0; i <= len; i++) {
		if (i < len && data[i] != '\n')
			continue;

		if (line_is_prunable(data, start, i) &&
		    span_covers(kept, (uint32_t)start) &&
		    !byte_is_excluded(excluded, (uint32_t)start) &&
		    !dwarfline_compiled(&image->lines, path, line)) {
			if (span_add(&pruned, (uint32_t)start, (uint32_t)i,
			             line, line) != 0)
				goto cleanup;
			metrics->pruned_lines++;
		}

		start = i + 1;
		line++;
	}

	/* Merged once the pass is over, for the reason the absent functions
	 * are: `byte_is_excluded` exits early on the first span starting past
	 * the byte it was asked about, which is correct exactly while the list
	 * is ordered. */
	for (size_t i = 0; i < pruned.count; i++) {
		if (excluded->count == excluded->capacity &&
		    analyze_grow((void **)&excluded->items, &excluded->capacity,
		         sizeof *excluded->items) != 0)
			goto cleanup;
		excluded->items[excluded->count++] = pruned.items[i];
	}
	merge_comment_spans(excluded);
	status = 0;

cleanup:
	free(pruned.items);
	return status;
}

static int build_exclusions(const LanguageModule *module, Registry *reg,
                            const ElcOptions *opts, const SymbolSet *image,
                            const char *data, size_t len, TSNode root,
                            const char *path, bool expanded,
                            FileMetrics *metrics, SpanList *comments)
{
	SpanList kept = { 0 };

	if (collect_comments(module, reg, data, root, comments) != 0) {
		diag_printf("elc: out of memory analysing %s\n", path);
		return -1;
	}

	/* **Skipped on an expanded buffer, where it would answer zero and
	 * overwrite the answer.**
	 *
	 * The conditional figures are the source-as-written's, settled before
	 * anything was expanded and recorded then, because the preprocessor
	 * removes the very directives they are read from. Running the query
	 * here would find no region in an expanded file, count none of the
	 * three dispositions, and replace a measurement with a zero — which
	 * matters now that one of the three can be non-zero on a file that
	 * went on to expand (HLR-208, HLR-211). */
	if (!expanded &&
	    collect_inactive_regions(module, reg, opts, image, path, data, root,
	                             comments, &metrics->undecided_regions,
	                             &metrics->image_decided_regions) != 0) {
		diag_printf("elc: out of memory analysing %s\n", path);
		return -1;
	}
	merge_comment_spans(comments);

	if (collect_absent_functions(module, reg, image, data, root, comments,
	                             &kept, metrics) != 0) {
		diag_printf("elc: out of memory analysing %s\n", path);
		free(kept.items);
		return -1;
	}

	if (prune_uncompiled_lines(image, data, len, path, &kept, comments,
	                           metrics) != 0) {
		diag_printf("elc: out of memory analysing %s\n", path);
		free(kept.items);
		return -1;
	}

	free(kept.items);
	return 0;
}

/* Every measurement the file yields, each collector reading the same tree and
 * the same exclusion set. The functions come first because the rest are located
 * against their ranges.
 */
static int collect_all(const LanguageModule *module, Registry *reg,
                       const char *data, TSNode root, const char *path,
                       const SpanList *comments, FnRangeIndex *ranges,
                       SiteList *sites, FileMetrics *metrics, FileFacts *facts)
{
	if (collect_functions(module, reg, data, root, comments, metrics,
	                      ranges) != 0 ||
	    collect_statements(module, reg, data, root, ranges, comments,
	                       sites) != 0 ||
	    collect_complexity(module, reg, data, root, ranges, comments,
	                       metrics) != 0 ||
	    collect_calls(module, reg, data, root, ranges, comments,
	                  facts) != 0 ||
	    collect_globals(module, reg, data, root, ranges, comments,
	                    facts) != 0 ||
	    collect_dead_code(module, reg, data, root, ranges, comments,
	                      facts) != 0 ||
	    collect_rule_matches(module, reg, data, root, comments,
	                         facts) != 0) {
		diag_printf("elc: out of memory analysing %s\n", path);
		return -1;
	}
	return 0;
}

/* Tree-sitter always returns a tree, and error recovery keeps the well-formed
 * parts of a damaged one intact. What cannot be parsed is measured and set
 * aside; everything around it is analysed normally (HLR-035).
 *
 * **This used to discard the whole file, and that was too blunt by two orders
 * of magnitude.** A single macro the grammar cannot follow — the
 * `printf(BOLD FG_BLUE "%s")` idiom is one, since `tree-sitter-c` accepts only
 * one identifier before the first string literal of a concatenation — damages a
 * fraction of a percent of a file and cost every metric in it. On one embedded
 * project that turned 0.1%–1.4% damage into the loss of half the codebase and
 * 137 perfectly parsed functions.
 *
 * The original objection stands and is answered rather than dismissed: metrics
 * from a damaged tree must not be mistakable for sound ones. So the damage is
 * measured in lines, carried on the file, and reported beside the figures it
 * qualifies — the same way the call depth is presented beside its
 * unresolved-call count. A reader can see exactly how much of the file `elc`
 * could not see.
 */
static void measure_damage(TSNode root, const char *path, const char *data,
                           size_t length, FileMetrics *metrics)
{
	metrics->unparsed_lines = count_unparsed_lines(root);

	if (!metrics->unparsed_lines)
		return;

	/* The source of every damaged region into the debug companion, where
	 * one is open. The terminal gets the count and a line to look at; a
	 * log meant to reproduce a parser defect on a tree nobody else has
	 * gets the construct itself (HLR-195). */
	if (diag_active())
		log_parse_failures(root, path, data, length);

	/* Names what was lost and where, rather than only that something was:
	 * "skipped" told a reader nothing about the scale, and the scale is
	 * usually a line or two. */
	diag_printf("elc: %s:%" PRIu32 ": %" PRIu32 " line%s could not be "
	        "parsed; the rest of the file is measured\n",
	        path, first_unparsed_line(root), metrics->unparsed_lines,
	        metrics->unparsed_lines == 1 ? "" : "s");
}

/* Expand with the run's configuration: the flags the user gave the
 * preprocessor, and the -D definitions they gave `elc` itself.
 *
 * The two must agree. `elc` decides a condition from its own definitions and
 * the preprocessor from the ones it was given, and a file expanded under a
 * different configuration from the one reported is measured in a build nobody
 * asked for (HLR-132).
 */
static int preproc_expand_configured(const char *path, const char *language,
                                     const ElcOptions *opts,
                                     PreprocResult *out)
{
	const char **flags;
	size_t       n = 0;
	int          rc;

	if (opts->define_count == 0)
		return preproc_expand(path, language, opts->cc,
		                      opts->cc_flags, opts->cc_flag_count,
		                      out);

	flags = calloc(opts->cc_flag_count + opts->define_count,
	               sizeof *flags);
	if (!flags)
		return -1;

	for (size_t i = 0; i < opts->cc_flag_count; i++)
		flags[n++] = opts->cc_flags[i];

	/* `elc` records a definition as the user wrote it — `NAME` or
	 * `NAME=VALUE` — and the preprocessor wants it prefixed. Built here
	 * rather than stored prefixed, because the bare form is what the
	 * conditional evaluation compares against. */
	for (size_t i = 0; i < opts->define_count; i++) {
		size_t want = strlen(opts->defines[i]) + 3;
		char  *flag = malloc(want);

		if (!flag) {
			for (size_t k = opts->cc_flag_count; k < n; k++)
				free((char *)flags[k]);
			free(flags);
			return -1;
		}
		snprintf(flag, want, "-D%s", opts->defines[i]);
		flags[n++] = flag;
	}

	rc = preproc_expand(path, language, opts->cc, flags, n, out);
	for (size_t i = opts->cc_flag_count; i < n; i++)
		free((char *)flags[i]);
	free(flags);
	return rc;
}

/* How many conditional regions the source as written leaves undecided.
 *
 * A parse of its own, and the second one an expanded file costs. The figure
 * cannot be taken from the expanded tree — the preprocessor has already
 * removed the directives it would be read from — and it cannot be skipped,
 * because it is what decides whether expanding this file is honest at all
 * (HLR-133, HLR-208).
 */
static uint32_t undecided_in(const LanguageModule *module, Registry *reg,
                             const ElcOptions *opts, const SymbolSet *image,
                             const char *path, const char *data,
                             size_t len, TSParser *parser,
                             FileMetrics *metrics)
{
	TSTree   *raw = ts_parser_parse_string(parser, NULL, data,
	                                       (uint32_t)len);
	SpanList  comments = { 0 };
	uint32_t  undecided = 0;
	uint32_t  from_image = 0;

	if (!raw)
		return 0;

	if (collect_comments(module, reg, data, ts_tree_root_node(raw),
	                     &comments) == 0)
		collect_inactive_regions(module, reg, opts, image, path, data,
		                         ts_tree_root_node(raw), &comments,
		                         &undecided, &from_image);

	/* Recorded here and not only returned, because this pass is the only
	 * one that will see these directives if the file goes on to expand.
	 * `build_exclusions` skips its own conditional pass on an expanded
	 * buffer for exactly that reason. */
	metrics->undecided_regions     = undecided;
	metrics->image_decided_regions = from_image;

	free(comments.items);
	ts_tree_delete(raw);
	return undecided;
}

/* Everything taken from one parsed tree, in the order the later steps need.
 *
 * Damage first, because a figure qualified by it must be recorded before
 * anything is counted; then the exclusions, since what is comment or inactive
 * decides what the collection sees; then the collection; then the effective
 * lines the collection's sites imply.
 */
static int measure_tree(const LanguageModule *module, Registry *reg,
                        const ElcOptions *opts, const SymbolSet *image,
                        const char *map, size_t len, TSTree *tree,
                        const char *path, bool expanded, FileMetrics *metrics,
                        FileFacts *facts, SpanList *comments,
                        FnRangeIndex *ranges, SiteList *sites)
{
	TSNode root = ts_tree_root_node(tree);

	measure_damage(root, path, map, len, metrics);

	if (build_exclusions(module, reg, opts, image, map, len, root, path,
	                     expanded, metrics, comments) != 0)
		return -1;

	if (collect_all(module, reg, map, root, path, comments, ranges,
	                sites, metrics, facts) != 0)
		return -1;

	apply_eloc(metrics, sites);
	return 0;
}

/* Give the caller the records and give up the cleanup path's claim on them.
 *
 * Both at once, because the two are handed over together or not at all: a
 * `cleanup` that freed one of a matched pair would leave the report holding
 * metrics whose facts had been released.
 */
static void hand_over(FileMetrics **out, FileFacts **facts_out,
                      FileMetrics **metrics, FileFacts **facts)
{
	*out       = *metrics;
	*facts_out = *facts;
	*metrics   = NULL;
	*facts     = NULL;
}

/* Open a file and learn its size, or diagnose why neither happened.
 *
 * The two together because a caller needs both or neither, and the diagnostic
 * is the same either way: the file was named and could not be read (HLR-035).
 */
static int open_and_stat(const char *path, struct stat *st)
{
	int fd = open(path, O_RDONLY);

	if (fd < 0) {
		diag_printf("elc: %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (fstat(fd, st) != 0) {
		diag_printf("elc: %s: %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

/* Release every buffer this file's analysis acquired, in the order that keeps
 * each pointer valid until the last thing pointing into it has gone.
 *
 * The tree is released through whichever owns it: a repaired file's result
 * holds both the tree and the buffer it was parsed from, and freeing the tree
 * separately would free it twice.
 */
static void release_buffers(RepairResult *repaired, PreprocResult *expanded,
                            TSTree *tree, void *mapping, size_t maplen)
{
	if (repaired->tree)
		repair_result_free(repaired);
	else if (tree)
		ts_tree_delete(tree);

	preproc_result_free(expanded);
	if (mapping != MAP_FAILED)
		munmap(mapping, maplen);
}

/* Map a file for reading, or diagnose why it could not be.
 *
 * Private and read-only: nothing elc does to the buffer may reach the file,
 * and both the repair path and the expansion path work in copies of their own
 * (HLR-043).
 */
static int map_file(const char *path, int fd, size_t len, void **out)
{
	*out = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (*out == MAP_FAILED) {
		diag_printf("elc: %s: %s\n", path, strerror(errno));
		return -1;
	}
	return 0;
}

/* The tree to measure, from whichever path this file took.
 *
 * Expanded text is parsed as it stands; text that could not be expanded is
 * repaired first. The two are not alternatives, which is what the withdrawal
 * of Phase 25 got wrong. Expansion is exact and needs a toolchain, the build's
 * include paths, and conditions `elc` can decide — on a cross-compiled tree it
 * reaches almost nothing. Repair needs none of those and is a guess about the
 * shape of a failure. Where the first applies the second is unnecessary; where
 * it does not, the second is all there is (HLR-196).
 *
 * The length is explicit throughout because the mapping is not NUL-terminated
 * (LLR-ANL-05).
 */
static TSTree *parse_for_metrics(TSParser *parser, const char *path,
                                 const PreprocResult *expanded,
                                 RepairResult *repaired, FileMetrics *metrics,
                                 const char **map, size_t *len)
{
	if (expanded->text)
		return ts_parser_parse_string(parser, NULL, *map,
		                              (uint32_t)*len);

	if (repair_parse(parser, *map, *len, path, repaired) != 0)
		return NULL;

	*map = repaired->buffer;
	*len = repaired->length;
	metrics->repairs = repaired->total;
	for (size_t k = 0; k < REPAIR_RULE_COUNT; k++)
		metrics->repair_counts[k] = repaired->counts[k];
	return repaired->tree;
}

/* Expand this file, and hand back the buffer to measure.
 *
 * The conditional analysis is answered from the source as written, before
 * anything is expanded, because a preprocessor destroys the very thing it
 * reports: `gcc -E` reads an undefined identifier in an `#if` as 0 and
 * discards the branch it did not take — a guess, made silently, where HLR-133
 * requires the guess be declared and both branches kept.
 *
 * So the raw text decides the conditional figures, and the expansion supplies
 * the rest. A file the raw pass could not decide is not expanded at all:
 * expanding it would let the preprocessor resolve what elc had just declared
 * unresolvable, and the effective-line count would silently become that of one
 * branch (HLR-208).
 *
 * **The image is passed in here, and that is the interaction, not an
 * accident.** The gate asks whether anything was left undecided, so a region
 * the image decided is not left — and a file whose only undecidable region was
 * one the build answers now expands, where it was refused before (HLR-211).
 * What HLR-208 guards against is the preprocessor *guessing* where elc had
 * declared no answer available; here an answer is available, read off the
 * build the image records, and the preprocessor is run against the flags and
 * definitions describing that same build (HLR-132). A user who names an image
 * from one build and flags from another has already asked for two answers to
 * one question, which is a condition no analysis can rescue.
 */
static void expand_for_metrics(const LanguageModule *module, Registry *reg,
                               const ElcOptions *opts, const SymbolSet *image,
                               const char *path,
                               const char *raw, size_t raw_len,
                               FileMetrics *metrics, PreprocResult *out,
                               const char **map, size_t *len)
{
	if (preproc_expand_configured(path, module->language_name, opts,
	                              out) != 0 || !out->text)
		return;

	if (undecided_in(module, reg, opts, image, path, raw, raw_len,
	                 reg->parser, metrics) != 0) {
		/* Declined, and the buffer goes with the decision.
		 *
		 * A non-NULL `text` is what every caller reads as "this file
		 * was expanded" — it selects the parse and it suppresses the
		 * repair that would otherwise run (HLR-196). Leaving it set on
		 * a file elc has just refused to expand meant the file was
		 * parsed as written *and* denied the repair, which is neither
		 * of the two paths and worse than both. */
		free(out->text);
		out->text   = NULL;
		out->length = 0;
		out->status = PREPROC_UNDECIDED;
		return;
	}

	*map = out->text;
	*len = out->length;
}

/* Carry the expansion's header list onto the file's metrics.
 *
 * Copied rather than borrowed because the PreprocResult dies with this
 * function and the metrics outlive the run that made them (HLR-207).
 */
static int record_stdlib(FileMetrics *m, const PreprocResult *p)
{
	if (p->header_count == 0)
		return 0;

	m->stdlib_headers = calloc(p->header_count, sizeof *m->stdlib_headers);
	m->stdlib_kinds   = calloc(p->header_count, sizeof *m->stdlib_kinds);
	if (!m->stdlib_headers || !m->stdlib_kinds)
		return -1;

	for (size_t i = 0; i < p->header_count; i++) {
		m->stdlib_headers[i] = strdup(p->headers[i].name);
		if (!m->stdlib_headers[i])
			return -1;
		m->stdlib_kinds[i] = (unsigned char)p->headers[i].kind;
	}
	m->stdlib_count = p->header_count;
	m->stdlib_cxx   = p->cxx_count;
	return 0;
}

int analyze_file(Registry *reg, const ElcOptions *opts, const SymbolSet *image,
                 const char *path, FileMetrics **out, FileFacts **facts_out)
{
	const LanguageModule *module;
	FileMetrics          *metrics  = NULL;
	FileFacts            *facts    = NULL;
	FnRangeIndex          ranges   = { 0 };
	SpanList              comments = { 0 };
	SiteList              sites    = { 0 };
	struct stat           st;
	/* The mapping, kept apart from the text that is parsed: expansion
	 * hands back its own buffer, and `munmap` must still receive what
	 * `mmap` returned. */
	void                 *mapping = MAP_FAILED;
	size_t                maplen  = 0;
	const char           *map     = NULL;
	size_t                len     = 0;
	/* Explicitly not-attempted, because a zeroed struct would read as
	 * PREPROC_EXPANDED and a run with --no-expand would report every file
	 * as expanded — the provenance of HLR-206 saying the opposite of what
	 * happened. */
	PreprocResult         expanded = { .status = PREPROC_OFF };
	RepairResult          repaired = { 0 };
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

	fd = open_and_stat(path, &st);
	if (fd < 0)
		goto cleanup;

	if (prepare_records(module, path, &metrics, &facts) != 0)
		goto cleanup;

	/* A zero-length file is short-circuited rather than mapped: mmap of an
	 * empty file fails with EINVAL, and an empty file is not an error
	 * (LLR-ANL-04). */
	if (st.st_size == 0) {
		hand_over(out, facts_out, &metrics, &facts);
		status = ANALYZE_OK;
		goto cleanup;
	}

	maplen = (size_t)st.st_size;
	if (map_file(path, fd, maplen, &mapping) != 0)
		goto cleanup;
	map = mapping;
	len = maplen;

	/* The physical-line count comes from the file, never from the parsed
	 * buffer. Expansion pads with blank lines to hold every location in
	 * place (HLR-204), and those are lines the file does not have. */
	metrics->physical_lines = count_lines(map, len);

	/* Macros expanded by the compiler, and the result filtered back down
	 * to this file at its own line numbers (HLR-202, HLR-203). Where that
	 * does not happen — no toolchain, a header the host cannot find, a
	 * cross-compiled tree — the file is parsed as written and measured
	 * exactly as it was before expansion existed (HLR-205). */
	ts_parser_set_language(reg->parser, module->ts_lang);

	if (!opts->no_expand)
		expand_for_metrics(module, reg, opts, image, path, map, len,
		                   metrics, &expanded, &map, &len);

	metrics->preproc_status = (int)expanded.status;
	if (record_stdlib(metrics, &expanded) != 0)
		goto cleanup;

	/* Expanded text is parsed as it stands; text that could not be
	 * expanded is repaired first.
	 *
	 * The two are not alternatives, which is what the withdrawal of Phase
	 * 25 got wrong. Expansion is exact and needs a toolchain, the build's
	 * include paths, and conditions `elc` can decide — on a cross-compiled
	 * tree it reaches almost nothing. Repair needs none of those and is a
	 * guess about the shape of a failure. Where the first applies the
	 * second is unnecessary; where it does not, the second is all there is
	 * (HLR-196).
	 *
	 * The length is explicit because the mapping is not NUL-terminated
	 * (LLR-ANL-05). */
	tree = parse_for_metrics(reg->parser, path, &expanded, &repaired,
	                         metrics, &map, &len);
	if (!tree) {
		diag_printf("elc: %s: parse failed\n", path);
		goto cleanup;
	}

	if (measure_tree(module, reg, opts, image, map, len, tree, path,
	                 expanded.text != NULL, metrics, facts, &comments,
	                 &ranges, &sites) != 0)
		goto cleanup;

	status = metrics->unparsed_lines ? ANALYZE_DAMAGED : ANALYZE_OK;
	hand_over(out, facts_out, &metrics, &facts);

cleanup:
	/* Every acquired resource released on every path, in order: the tree
	 * before the mapping it points into, and the mapping in the same
	 * function that made it. */
	free(sites.items);
	free(comments.items);
	free(ranges.items);
	release_buffers(&repaired, &expanded, tree, mapping, maplen);
	filemetrics_free(metrics);
	filefacts_free(facts);
	if (fd >= 0)
		close(fd);
	return status;
}
