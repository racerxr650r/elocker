/* test/unit/analyze.c — unit tests for src/analyze.c.
 *
 * These run against the real grammar rather than a synthetic one. The
 * requirements under test constrain what `analyze_file` does with what a
 * query captures — pairing, line conversion, span arithmetic, the failure
 * paths — and the cheapest correct source of captures is a real module.
 * Which C construct is a function is `functions.scm`'s decision, and is
 * fixture-verified rather than asserted here.
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "analyze.h"
#include "elc.h"
#include "registry.h"

static char scratch[512];

static void remove_scratch(void)
{
	char command[600];

	if (scratch[0] == '\0')
		return;
	snprintf(command, sizeof command, "rm -rf -- '%s'", scratch);
	if (system(command) != 0)
		fprintf(stderr, "could not remove %s\n", scratch);
}

/* A .c file holding exactly `contents`, in a directory removed when this
 * test's process exits. The extension matters: it is what the registry maps
 * to a language. */
static const char *source_holding(const char *contents)
{
	static char path[1024];

	if (scratch[0] == '\0') {
		snprintf(scratch, sizeof scratch, "/tmp/elc-analyze-XXXXXX");
		cr_assert_not_null(mkdtemp(scratch), "could not create a scratch dir");
		atexit(remove_scratch);
	}

	snprintf(path, sizeof path, "%s/subject.c", scratch);

	FILE *fp = fopen(path, "w");
	cr_assert_not_null(fp, "could not write %s", path);
	fputs(contents, fp);
	fclose(fp);
	return path;
}

/* The registry over the in-tree runtime. Every test opens its own; Criterion
 * gives each its own process, so there is nothing to share. */
static void registry_for_tests(Registry *reg)
{
	ElcOptions opts;

	memset(&opts, 0, sizeof opts);
	cr_assert_eq(registry_open(&opts, reg), 0,
	             ELC_RUNTIME_DIR_ENV " must name a usable runtime");
}

static const FunctionMetric *function_named(const FileMetrics *m,
                                            const char *name)
{
	for (size_t i = 0; i < m->function_count; i++)
		if (strcmp(m->functions[i].name, name) == 0)
			return &m->functions[i];
	return NULL;
}

/* ---------------------------------------------------------- measurement */

/* Verifies LLR-ANL-06: physical lines are counted from the mapped contents. */
/* analyze_file with the graph facts released on the spot.
 *
 * Most tests in this file are about the metrics, and threading a FileFacts
 * through every one of them would add a variable to twenty-three call sites
 * to say nothing. The fact tests at the end take the other path and keep
 * them.
 */
static int analyze_metrics(Registry *reg, const char *path, FileMetrics **out)
{
	FileFacts *facts = NULL;
	int        rc    = analyze_file(reg, path, out, &facts);

	filefacts_free(facts);
	return rc;
}

Test(analyze, physical_lines_are_counted)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding("int a;\nint b;\nint c;\n"),
	                          &m), ANALYZE_OK);
	cr_assert_eq(m->physical_lines, 3);

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-06: a final line with no terminating newline is still a
 * line the reader sees, and counts. */
Test(analyze, an_unterminated_final_line_counts)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding("int a;\nint b;"), &m),
	             ANALYZE_OK);
	cr_assert_eq(m->physical_lines, 2);

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-04: a zero-length file reports zero metrics without
 * error, rather than being mapped — mmap of an empty file fails EINVAL. */
Test(analyze, a_zero_length_file_reports_zero_without_error)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(""), &m), ANALYZE_OK,
	             "an empty file is not an error");
	cr_assert_eq(m->physical_lines, 0);
	cr_assert_eq(m->function_count, 0);

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-02: the metrics carry the path and the language they were
 * measured against. */
Test(analyze, the_metrics_carry_the_path_and_language)
{
	Registry     reg;
	FileMetrics *m    = NULL;
	const char  *path = source_holding("int a;\n");

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, path, &m), ANALYZE_OK);
	cr_assert_str_eq(m->path, path);
	cr_assert_str_eq(m->language, "c");

	filemetrics_free(m);
	registry_close(&reg);
}

/* ----------------------------------------------------- function identity */

/* Verifies LLR-ANL-07: each function is reported with its name and its line
 * range, converted from the parser's zero-based rows exactly once. */
Test(analyze, a_function_is_reported_with_its_name_and_line_range)
{
	Registry     reg;
	FileMetrics *m = NULL;

	/* Line 1 is the signature; line 4 closes the body. */
	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int only(int n)\n"
		"{\n"
		"\treturn n;\n"
		"}\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->function_count, 1);
	cr_assert_str_eq(m->functions[0].name, "only");
	cr_assert_eq(m->functions[0].start_line, 1,
	             "the span starts at the signature, not the brace");
	cr_assert_eq(m->functions[0].end_line, 4);

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-09: a named function declared inside another is reported
 * in its own right rather than folded into its enclosing function. */
Test(analyze, a_nested_function_is_reported_in_its_own_right)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int outer(void)\n"
		"{\n"
		"\tint inner(int x) { return x + 1; }\n"
		"\treturn inner(1);\n"
		"}\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->function_count, 2, "both are functions (HLR-067)");
	cr_assert_not_null(function_named(m, "outer"));

	const FunctionMetric *inner = function_named(m, "inner");
	cr_assert_not_null(inner, "the nested function is not folded in");
	cr_assert_eq(inner->start_line, 3);
	cr_assert_eq(inner->end_line, 3);

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-08: what counts as a function is the query's decision. A
 * declaration without a body is not a definition and contributes nothing. */
Test(analyze, a_prototype_is_not_a_function)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int declared(void);\n"
		"int defined(void) { return 0; }\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->function_count, 1);
	cr_assert_str_eq(m->functions[0].name, "defined");

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-07: a name extracted from the mapping outlives it. The
 * mapping is released before analyze_file returns, so a name still readable
 * afterwards is one that was copied out rather than pointed at. */
Test(analyze, a_function_name_outlives_the_mapping)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int a_name_long_enough_to_notice(void) { return 0; }\n"), &m),
		ANALYZE_OK);

	cr_assert_eq(m->function_count, 1);
	cr_assert_str_eq(m->functions[0].name, "a_name_long_enough_to_notice");

	filemetrics_free(m);
	registry_close(&reg);
}

/* ------------------------------------------------------- failure paths */

/* Verifies LLR-ANL-02: a file that cannot be read is a per-file failure, not
 * a fatal one, and yields no metrics for the caller to release. */
Test(analyze, an_unreadable_file_is_a_failure_without_metrics)
{
	Registry     reg;
	FileMetrics *m    = (FileMetrics *)0x1;
	const char  *path = source_holding("int a;\n");

	registry_for_tests(&reg);
	cr_assert_eq(chmod(path, 0), 0);
	int rc = analyze_metrics(&reg, path, &m);
	cr_assert_eq(chmod(path, 0600), 0);

	cr_assert_eq(rc, ANALYZE_FAILED);
	cr_assert_null(m, "no metrics are handed back on the failure path");

	registry_close(&reg);
}

/* Verifies LLR-ANL-01: a file whose tree contains an error node is skipped
 * whole, rather than reported from a partially valid tree. */
Test(analyze, a_file_that_fails_to_parse_is_a_failure)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int fine(void) { return 0; }\n"
		"this is not C at all ((( \n"), &m), ANALYZE_FAILED,
		"metrics from a damaged tree are indistinguishable from sound "
		"ones once rendered (HLR-035)");
	cr_assert_null(m);

	registry_close(&reg);
}

/* Verifies LLR-ANL-02: a file whose extension maps to no language is a skip,
 * distinct from a failure, so the exit status stays 0 (HLR-012, HLR-037). */
Test(analyze, an_unmapped_extension_is_a_skip_not_a_failure)
{
	Registry     reg;
	FileMetrics *m = NULL;
	char         path[1024];

	registry_for_tests(&reg);
	source_holding("int a;\n");
	snprintf(path, sizeof path, "%s/notes.md", scratch);

	FILE *fp = fopen(path, "w");
	cr_assert_not_null(fp);
	fputs("# not source\n", fp);
	fclose(fp);

	cr_assert_eq(analyze_metrics(&reg, path, &m), ANALYZE_SKIPPED);
	cr_assert_null(m);

	registry_close(&reg);
}

/* ------------------------------------------------ merge_comment_spans ----
 *
 * Tested against its contract with synthetic spans rather than only through a
 * parsed file. This is the arithmetic the whole metric rests on, and the ways
 * it goes wrong — a shared line excluded twice, a run of adjacent spans read
 * one past its end — are reachable directly and awkward to provoke through C
 * source.
 */

static SpanList spans_of(CommentSpan *items, size_t count)
{
	SpanList list;

	list.items    = items;
	list.count    = count;
	list.capacity = count;
	return list;
}

/* Verifies LLR-MRG-01: spans are sorted before anything is merged. */
Test(analyze, spans_are_sorted_before_merging)
{
	CommentSpan items[] = {
		{ 100, 120, 10, 12 },
		{  10,  20,  1,  2 },
		{  50,  60,  5,  6 },
	};
	SpanList list = spans_of(items, 3);

	cr_assert_eq(merge_comment_spans(&list), 7,
	             "2 + 2 + 3 lines, whatever order they arrived in");
	cr_assert_eq(list.count, 3, "disjoint spans do not coalesce");
	cr_assert_eq(list.items[0].start_byte, 10);
	cr_assert_eq(list.items[1].start_byte, 50);
	cr_assert_eq(list.items[2].start_byte, 100);
}

/* Verifies LLR-MRG-02: overlapping spans coalesce into one. */
Test(analyze, overlapping_spans_coalesce)
{
	CommentSpan items[] = {
		{ 10, 40, 1, 4 },
		{ 30, 60, 3, 6 },
	};
	SpanList list = spans_of(items, 2);

	cr_assert_eq(merge_comment_spans(&list), 6, "lines 1 through 6, once");
	cr_assert_eq(list.count, 1);
	cr_assert_eq(list.items[0].start_byte, 10);
	cr_assert_eq(list.items[0].end_byte, 60);
}

/* Verifies LLR-MRG-02, LLR-MRG-03: a nested span is absorbed by the span
 * containing it, and contributes nothing of its own. This is the canonical
 * case — a block comment carrying inline comment syntax — and subtracting per
 * capture would count its lines twice. */
Test(analyze, a_nested_span_is_absorbed_not_counted_twice)
{
	CommentSpan items[] = {
		{ 10, 90, 1, 9 },   /* the block comment          */
		{ 30, 40, 3, 4 },   /* inline syntax inside it    */
		{ 50, 55, 5, 5 },   /* and again                  */
	};
	SpanList list = spans_of(items, 3);

	cr_assert_eq(merge_comment_spans(&list), 9,
	             "nine lines, not nine plus two plus one");
	cr_assert_eq(list.count, 1);
	cr_assert_eq(list.items[0].end_byte, 90,
	             "the outer span is not shortened by the inner one");
}

/* Verifies LLR-MRG-03: no line is excluded more than once, however many
 * spans share it. Subtracting per capture is what drives ELOC negative. */
Test(analyze, a_shared_line_is_counted_once)
{
	CommentSpan items[] = {
		{ 10, 20, 7, 7 },
		{ 21, 30, 7, 7 },
		{ 31, 40, 7, 7 },
	};
	SpanList list = spans_of(items, 3);

	cr_assert_eq(merge_comment_spans(&list), 1,
	             "three comments on one line are one line");
}

/* Verifies LLR-MRG-04: coalescing a run of adjacent spans stops at the final
 * element. The bound is the whole of the requirement — without it the loop
 * reads one past the last span whenever the trailing run is longer than one,
 * which is exactly this shape. */
Test(analyze, coalescing_a_trailing_run_stays_in_bounds)
{
	CommentSpan items[] = {
		{ 10, 20, 1, 1 },
		{ 15, 25, 1, 2 },
		{ 20, 35, 2, 3 },
		{ 30, 45, 3, 4 },
	};
	SpanList list = spans_of(items, 4);

	cr_assert_eq(merge_comment_spans(&list), 4, "lines 1 through 4");
	cr_assert_eq(list.count, 1);
}

Test(analyze, merging_an_empty_span_list_is_zero)
{
	SpanList list = { 0 };

	cr_assert_eq(merge_comment_spans(&list), 0);
	cr_assert_eq(list.count, 0);
}

/* ------------------------------------------------- innermost_enclosing ---- */

static FnRangeIndex ranges_of(FnRange *items, size_t count)
{
	FnRangeIndex index;

	index.items    = items;
	index.count    = count;
	index.capacity = count;
	return index;
}

/* Verifies LLR-INN-01: the narrowest containing range wins, not the first. */
Test(analyze, the_narrowest_enclosing_function_wins)
{
	FnRange      items[] = { { 0, 100, 0 }, { 20, 40, 1 } };
	FnRangeIndex index   = ranges_of(items, 2);

	const FnRange *hit = innermost_enclosing(&index, 30);

	cr_assert_not_null(hit);
	cr_assert_eq(hit->index, 1,
	             "a statement inside a nested function belongs to it alone "
	             "(HLR-068)");
}

/* Verifies LLR-INN-01: order of declaration does not decide the answer. */
Test(analyze, the_narrowest_wins_whatever_order_the_ranges_are_in)
{
	FnRange      items[] = { { 20, 40, 1 }, { 0, 100, 0 } };
	FnRangeIndex index   = ranges_of(items, 2);

	cr_assert_eq(innermost_enclosing(&index, 30)->index, 1);
	cr_assert_eq(innermost_enclosing(&index, 50)->index, 0,
	             "outside the nested range, the enclosing one applies");
}

/* Verifies LLR-INN-02: an offset outside every function belongs to none, so
 * file-scope code contributes to the file and to no function. */
Test(analyze, an_offset_outside_every_function_has_no_owner)
{
	FnRange      items[] = { { 20, 40, 0 } };
	FnRangeIndex index   = ranges_of(items, 1);

	cr_assert_null(innermost_enclosing(&index, 10));
	cr_assert_null(innermost_enclosing(&index, 40),
	               "the end offset is exclusive");
	cr_assert_not_null(innermost_enclosing(&index, 20),
	                   "the start offset is inclusive");
}

Test(analyze, an_empty_range_index_owns_nothing)
{
	FnRangeIndex index = { 0 };

	cr_assert_null(innermost_enclosing(&index, 0));
}

/* ------------------------------------------------------------------ ELOC -- */

/* Verifies LLR-ANL-11: a statement spread over several lines counts once, at
 * its start line — style must not move the number. */
Test(analyze, a_multi_line_statement_counts_once)
{
	Registry     reg;
	FileMetrics *spread = NULL;
	FileMetrics *dense  = NULL;

	registry_for_tests(&reg);

	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int f(int a, int b)\n"
		"{\n"
		"\treturn (a +\n"
		"\t        b +\n"
		"\t        a);\n"
		"}\n"), &spread), ANALYZE_OK);

	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int f(int a, int b)\n"
		"{\n"
		"\treturn (a + b + a);\n"
		"}\n"), &dense), ANALYZE_OK);

	cr_assert_eq(spread->functions[0].eloc, 1);
	cr_assert_eq(spread->functions[0].eloc, dense->functions[0].eloc,
	             "identical logic yields the same ELOC however it is laid "
	             "out (HLR-053)");

	filemetrics_free(spread);
	filemetrics_free(dense);
	registry_close(&reg);
}

/* Verifies LLR-ANL-10: a statement is counted, a blank line, a lone brace, a
 * bare declaration, and a directive are not. */
Test(analyze, only_statements_count_toward_eloc)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"#define N 4\n"          /* directive     — excluded */
		"int f(void)\n"
		"{\n"                    /* lone brace    — excluded */
		"\tint bare;\n"          /* declaration   — excluded */
		"\n"                     /* blank         — excluded */
		"\tint init = N;\n"      /* initialises   — counted  */
		"\treturn init;\n"       /* returns       — counted  */
		"}\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->functions[0].eloc, 2);

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-11: two statements sharing a line are one line of code.
 * Counting the captures instead would make the same two statements worth
 * twice as much on one line as on two, which is HLR-053's error inverted. */
Test(analyze, two_statements_on_one_line_count_once)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int f(void)\n"
		"{\n"
		"\tint a = 1; int b = 2;\n"
		"\treturn a + b;\n"
		"}\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->functions[0].eloc, 2);

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-INN-02: a statement in a nested function contributes to it
 * and not also to the function enclosing it. */
Test(analyze, a_nested_functions_statements_are_not_counted_twice)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int outer(void)\n"
		"{\n"
		"\tint a = 1;\n"
		"\tint inner(int x)\n"
		"\t{\n"
		"\t\tint b = 2;\n"
		"\t\treturn x + b;\n"
		"\t}\n"
		"\treturn inner(a);\n"
		"}\n"), &m), ANALYZE_OK);

	const FunctionMetric *outer = function_named(m, "outer");
	const FunctionMetric *inner = function_named(m, "inner");

	cr_assert_not_null(outer);
	cr_assert_not_null(inner);
	cr_assert_eq(inner->eloc, 2);
	cr_assert_eq(outer->eloc, 2,
	             "the enclosing function keeps its own two statements and "
	             "gains none of the nested one's (HLR-068)");
	cr_assert_eq(m->eloc, 4, "four distinct lines carry a statement");

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-10: code outside every function contributes to the file's
 * ELOC and to no function's. */
Test(analyze, file_scope_code_counts_for_the_file_only)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int global = 1;\n"
		"int bare;\n"
		"int f(void)\n"
		"{\n"
		"\treturn global;\n"
		"}\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->functions[0].eloc, 1);
	cr_assert_eq(m->eloc, 2,
	             "the initialised global counts for the file (HLR-019)");

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-04: a file with nothing executable reports zero ELOC
 * without error (HLR-020). */
Test(analyze, a_file_with_nothing_executable_reports_zero_eloc)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"/* a comment */\n"
		"#include <stdio.h>\n"
		"int declared_only;\n"
		"int prototype(void);\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->eloc, 0);

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-01: a line of code carrying a trailing comment is still a
 * line of code. The exclusion is byte-granular for exactly this reason — a
 * line-granular one deletes this statement. */
Test(analyze, a_trailing_comment_does_not_remove_its_line)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int f(void)\n"
		"{\n"
		"\tint n = 0;   /* a note */\n"
		"\treturn n;    /* another */\n"
		"}\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->functions[0].eloc, 2);

	filemetrics_free(m);
	registry_close(&reg);
}

/* ------------------------------------------------------------ complexity -- */

/* Verifies LLR-ANL-21: complexity is one plus the decision points, so a
 * function that never branches is 1. A query capturing the function itself
 * would report 2, which is the failure this asserts against. */
Test(analyze, a_function_that_never_branches_is_one)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int f(int n)\n"
		"{\n"
		"\tint a = n;\n"
		"\treturn a;\n"
		"}\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->functions[0].complexity, 1);

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-21: each decision point adds one. */
Test(analyze, each_decision_point_adds_one)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int f(int n)\n"
		"{\n"
		"\tif (n > 0)\n"
		"\t\treturn 1;\n"
		"\twhile (n--)\n"
		"\t\tn = n;\n"
		"\treturn 0;\n"
		"}\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->functions[0].complexity, 3, "1 + the if + the while");

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-22, LLR-INN-02: a nested named function owns its own
 * decision points, and the function enclosing it gains none of them. */
Test(analyze, a_nested_functions_decisions_are_not_counted_twice)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int outer(int a)\n"
		"{\n"
		"\tint inner(int b)\n"
		"\t{\n"
		"\t\tif (b)\n"
		"\t\t\treturn 1;\n"
		"\t\treturn 0;\n"
		"\t}\n"
		"\tif (a)\n"
		"\t\treturn inner(a);\n"
		"\treturn 0;\n"
		"}\n"), &m), ANALYZE_OK);

	const FunctionMetric *outer = function_named(m, "outer");
	const FunctionMetric *inner = function_named(m, "inner");

	cr_assert_not_null(outer);
	cr_assert_not_null(inner);
	cr_assert_eq(inner->complexity, 2);
	cr_assert_eq(outer->complexity, 2,
	             "running the query against outer's body without "
	             "attribution would report 3 (HLR-068)");

	filemetrics_free(m);
	registry_close(&reg);
}

/* Verifies LLR-ANL-22: a decision point inside a scope that is *not* a
 * reported function belongs to the nearest enclosing one that is.
 *
 * This is the anonymous-callable rule (HLR-018), which C cannot express — it
 * has no lambdas. The mechanism is what the requirement constrains, and the
 * mechanism is `innermost_enclosing` over the *reported* functions: an offset
 * inside an unreported scope resolves to the named function containing it,
 * because the unreported scope is not in the index at all. A fixture in the
 * language follows when C++ arrives in Phase 6.
 */
Test(analyze, an_unreported_scope_attributes_to_the_named_function_around_it)
{
	/* One reported function spanning 0..100. Bytes 40..60 stand for an
	 * anonymous callable inside it, which contributes no range of its
	 * own. */
	FnRange      items[] = { { 0, 100, 0 } };
	FnRangeIndex index   = ranges_of(items, 1);

	const FnRange *hit = innermost_enclosing(&index, 50);

	cr_assert_not_null(hit, "an unreported scope does not swallow the "
	                        "decision point");
	cr_assert_eq(hit->index, 0,
	             "it lands on the nearest enclosing named function "
	             "(HLR-018)");
}

/* Verifies LLR-ANL-21: a decision point outside every reported function is
 * counted for no function, rather than for an arbitrary one. */
Test(analyze, a_file_scope_decision_belongs_to_no_function)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_metrics(&reg, source_holding(
		"int global = 1 ? 2 : 3;\n"
		"int f(void)\n"
		"{\n"
		"\treturn global;\n"
		"}\n"), &m), ANALYZE_OK);

	cr_assert_eq(m->functions[0].complexity, 1,
	             "the conditional in the global initialiser is not f's");

	filemetrics_free(m);
	registry_close(&reg);
}

Test(analyze, filemetrics_free_is_safe_on_null)
{
	filemetrics_free(NULL);
	cr_assert(1, "releasing a null metrics structure must not fault");
}
