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
Test(analyze, physical_lines_are_counted)
{
	Registry     reg;
	FileMetrics *m = NULL;

	registry_for_tests(&reg);
	cr_assert_eq(analyze_file(&reg, source_holding("int a;\nint b;\nint c;\n"),
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
	cr_assert_eq(analyze_file(&reg, source_holding("int a;\nint b;"), &m),
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
	cr_assert_eq(analyze_file(&reg, source_holding(""), &m), ANALYZE_OK,
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
	cr_assert_eq(analyze_file(&reg, path, &m), ANALYZE_OK);
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
	cr_assert_eq(analyze_file(&reg, source_holding(
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
	cr_assert_eq(analyze_file(&reg, source_holding(
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
	cr_assert_eq(analyze_file(&reg, source_holding(
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
	cr_assert_eq(analyze_file(&reg, source_holding(
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
	int rc = analyze_file(&reg, path, &m);
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
	cr_assert_eq(analyze_file(&reg, source_holding(
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

	cr_assert_eq(analyze_file(&reg, path, &m), ANALYZE_SKIPPED);
	cr_assert_null(m);

	registry_close(&reg);
}

Test(analyze, filemetrics_free_is_safe_on_null)
{
	filemetrics_free(NULL);
	cr_assert(1, "releasing a null metrics structure must not fault");
}
