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

void filemetrics_free(FileMetrics *metrics)
{
	if (!metrics)
		return;

	for (size_t i = 0; i < metrics->function_count; i++)
		free(metrics->functions[i].name);
	free(metrics->functions);
	free(metrics->path);
	free(metrics);
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
                             FileMetrics *metrics, FnRangeIndex *ranges)
{
	TSQuery      *query    = module->queries[QUERY_FUNCTIONS];
	size_t        capacity = 0;
	TSQueryMatch  match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
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
                            TSNode root, SpanList *spans)
{
	TSQuery      *query = module->queries[QUERY_COMMENTS];
	TSQueryMatch  match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
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

/* True when a byte offset lies inside the merged comment set.
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
static bool byte_is_comment(const SpanList *spans, uint32_t byte)
{
	for (size_t i = 0; i < spans->count; i++) {
		if (byte < spans->items[i].start_byte)
			return false;   /* sorted; no later span can contain it */
		if (byte < spans->items[i].end_byte)
			return true;
	}
	return false;
}

/* Record every counted statement with the line it starts on and the function
 * it belongs to. */
static int collect_statements(const LanguageModule *module, Registry *reg,
                              TSNode root, const FnRangeIndex *ranges,
                              const SpanList *comments, SiteList *sites)
{
	TSQuery      *query = module->queries[QUERY_ELOC];
	TSQueryMatch  match;

	ts_query_cursor_exec(reg->cursor, query, root);

	while (ts_query_cursor_next_match(reg->cursor, &match)) {
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

			if (byte_is_comment(comments, byte))
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

int analyze_file(Registry *reg, const char *path, FileMetrics **out)
{
	const LanguageModule *module;
	FileMetrics          *metrics  = NULL;
	FnRangeIndex          ranges   = { 0 };
	SpanList              comments = { 0 };
	SiteList              sites    = { 0 };
	struct stat           st;
	void                 *map    = MAP_FAILED;
	size_t                len    = 0;
	TSTree               *tree   = NULL;
	int                   fd     = -1;
	int                   status = ANALYZE_FAILED;

	*out = NULL;

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
	if (!metrics) {
		fprintf(stderr, "elc: out of memory measuring %s\n", path);
		goto cleanup;
	}

	metrics->path = strdup(path);
	if (!metrics->path) {
		fprintf(stderr, "elc: out of memory measuring %s\n", path);
		goto cleanup;
	}
	metrics->language = module->language_name;   /* borrowed from the module */

	/* A zero-length file is short-circuited rather than mapped: mmap of an
	 * empty file fails with EINVAL, and an empty file is not an error
	 * (LLR-ANL-04). */
	if (st.st_size == 0) {
		*out    = metrics;
		metrics = NULL;
		status  = ANALYZE_OK;
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

	/* Tree-sitter always returns a tree, so a parse failure means the root
	 * reports an error node. Any error node skips the whole file: metrics
	 * from a damaged tree are indistinguishable from sound ones once
	 * rendered, and a silently undercounted file is worse than a visibly
	 * skipped one (HLR-035). This is the single place that tolerance would
	 * be relaxed, if experience shows it too blunt. */
	if (ts_node_has_error(root)) {
		fprintf(stderr, "elc: %s: parse error; file skipped\n", path);
		goto cleanup;
	}

	if (collect_functions(module, reg, map, root, metrics, &ranges) != 0 ||
	    collect_comments(module, reg, root, &comments) != 0 ||
	    collect_statements(module, reg, root, &ranges, &comments,
	                       &sites) != 0) {
		fprintf(stderr, "elc: out of memory analysing %s\n", path);
		goto cleanup;
	}

	apply_eloc(metrics, &sites);

	*out    = metrics;
	metrics = NULL;
	status  = ANALYZE_OK;

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
	if (fd >= 0)
		close(fd);
	return status;
}
