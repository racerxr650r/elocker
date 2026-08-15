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
                             FileMetrics *metrics)
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

		metrics->function_count++;
	}

	return 0;
}

int analyze_file(Registry *reg, const char *path, FileMetrics **out)
{
	const LanguageModule *module;
	FileMetrics          *metrics = NULL;
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

	if (collect_functions(module, reg, map, root, metrics) != 0) {
		fprintf(stderr, "elc: out of memory analysing %s\n", path);
		goto cleanup;
	}

	*out    = metrics;
	metrics = NULL;
	status  = ANALYZE_OK;

cleanup:
	/* Every acquired resource released on every path, in order: the tree
	 * before the mapping it points into, and the mapping in the same
	 * function that made it. */
	if (tree)
		ts_tree_delete(tree);
	if (map != MAP_FAILED)
		munmap(map, len);
	filemetrics_free(metrics);
	if (fd >= 0)
		close(fd);
	return status;
}
