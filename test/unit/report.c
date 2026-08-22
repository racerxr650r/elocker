/* test/unit/report.c — unit tests for src/report.c.
 *
 * report.c is where every sort lives, so these tests are the audit point for
 * HLR-032's byte-identical guarantee at the unit level: a model assembled
 * from files in one order must be indistinguishable from the same files in
 * another (doc/STP.md §2.2).
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "elc.h"
#include "report.h"

/* --------------------------------------------------------------- --wrap ---
 *
 * `volatile` is load-bearing: without it the compiler judges the arming
 * store dead and removes it, and the interception silently does not happen
 * (doc/STP.md §2.2).
 */
extern void *__real_realloc(void *, size_t);

static volatile int realloc_should_fail;

void *__wrap_realloc(void *p, size_t n)
{
	if (realloc_should_fail)
		return NULL;
	return __real_realloc(p, n);
}

/* ------------------------------------------------------------- scaffolding */

static FileMetrics *metrics_for(const char *path, uint32_t lines)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path = strdup(path);
	cr_assert_not_null(m->path);
	m->physical_lines = lines;
	return m;
}

/* ------------------------------------------------------------------ tests */

Test(report, totals_sum_across_every_file)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };

	cr_assert_eq(metrics_add(&acc, metrics_for("/a.c", 10)), 0);
	cr_assert_eq(metrics_add(&acc, metrics_for("/b.c", 32)), 0);

	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);
	cr_assert_eq(report.summary.file_count, 2);
	cr_assert_eq(report.summary.physical_lines, 42);

	report_free(&report);
	metrics_free(&acc);
}

Test(report, the_henry_kafura_total_is_the_sum_of_the_per_function_values)
{
	Report report = { 0 };

	/* Built directly rather than through an analysis, because the property
	 * under test is the summation and not the measurement: the total is
	 * the sum of the rows, never the formula applied to project aggregates
	 * — a project has no fan-in (HLR-158). */
	report.fan_out = calloc(3, sizeof *report.fan_out);
	cr_assert_not_null(report.fan_out);
	report.fan_out_count = 3;
	report.fan_out[0].henry_kafura = 144;
	report.fan_out[1].henry_kafura = 0;      /* an end of the graph */
	report.fan_out[2].henry_kafura = UINT64_C(56700000000);

	report_total_henry_kafura(&report);
	cr_assert_eq(report.summary.henry_kafura, UINT64_C(56700000144),
	             "the total accumulates in the same width the values do");

	/* Idempotent, because both the live path and the regeneration path
	 * call it and a run reading a record does both. */
	report_total_henry_kafura(&report);
	cr_assert_eq(report.summary.henry_kafura, UINT64_C(56700000144));

	report_free(&report);
}

Test(report, a_project_with_no_functions_totals_zero_rather_than_nothing)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };

	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);
	report_total_henry_kafura(&report);
	cr_assert_eq(report.summary.henry_kafura, 0);

	report_free(&report);
	metrics_free(&acc);
}

Test(report, files_are_presented_in_ascending_path_order)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };

	cr_assert_eq(metrics_add(&acc, metrics_for("/z.c", 1)), 0);
	cr_assert_eq(metrics_add(&acc, metrics_for("/a.c", 1)), 0);
	cr_assert_eq(metrics_add(&acc, metrics_for("/m.c", 1)), 0);

	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);
	cr_assert_eq(report.file_count, 3);
	cr_assert_str_eq(report.files[0]->path, "/a.c");
	cr_assert_str_eq(report.files[1]->path, "/m.c");
	cr_assert_str_eq(report.files[2]->path, "/z.c",
	                 "presentation order is a property of the model, not "
	                 "of the order files were discovered (HLR-033)");

	report_free(&report);
	metrics_free(&acc);
}

Test(report, an_empty_run_yields_a_complete_model_with_zero_totals)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };

	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0,
	             "a run that analysed nothing still produces a model "
	             "(HLR-066)");
	cr_assert_eq(report.file_count, 0);
	cr_assert_eq(report.summary.file_count, 0);
	cr_assert_eq(report.summary.physical_lines, 0);

	report_free(&report);
	metrics_free(&acc);
}

Test(report, assembly_leaves_the_accumulator_empty)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };

	cr_assert_eq(metrics_add(&acc, metrics_for("/a.c", 1)), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);

	cr_assert_eq(acc.count, 0,
	             "ownership moves to the report, so releasing both cannot "
	             "free the same metrics twice (HLR-124)");
	cr_assert_null(acc.files);

	report_free(&report);
	metrics_free(&acc);
}

Test(report, a_failed_growth_leaves_the_accumulator_intact)
{
	MetricsAccumulator acc = { 0 };
	FileMetrics       *m   = metrics_for("/a.c", 1);

	realloc_should_fail = 1;
	int rc = metrics_add(&acc, m);
	realloc_should_fail = 0;

	cr_assert_neq(rc, 0, "a failed reallocation must be reported");
	cr_assert_eq(acc.count, 0);
	cr_assert_null(acc.files,
	               "the original allocation is not overwritten by a failed "
	               "realloc (HLR-125)");

	/* The caller still owns the metrics on the failure path. */
	filemetrics_free(m);
	metrics_free(&acc);
}

Test(report, free_is_safe_on_null)
{
	report_free(NULL);
	metrics_free(NULL);
	cr_assert(1, "releasing a null model must not fault");
}

/* --------------------------------------------------- component directory -- */

/* The directory a component belongs to, derived once so that every analysis
 * grouping by directory reads the same answer (HLR-160).
 *
 * The two edge cases are the ones a naive split on the last separator gets
 * wrong: a file directly under the root has an empty prefix, and a path with
 * no separator at all has no directory to name.
 */
Test(report, a_components_directory_is_the_path_above_it)
{
	char *dir = component_directory("/p/app/a.c");

	cr_assert_not_null(dir);
	cr_assert_str_eq(dir, "/p/app", "no trailing separator, so that two "
	                 "files in one directory compare equal");
	free(dir);
}

Test(report, a_file_at_the_root_belongs_to_the_root)
{
	char *dir = component_directory("/a.c");

	cr_assert_not_null(dir);
	cr_assert_str_eq(dir, "/", "the empty prefix is the root, not nothing");
	free(dir);
}

Test(report, a_path_with_no_separator_belongs_to_the_working_directory)
{
	char *dir = component_directory("a.c");

	cr_assert_not_null(dir);
	cr_assert_str_eq(dir, ".");
	free(dir);
}

/* A measured file carries its directory, rather than every consumer slicing
 * the path for itself. Asserted on the assembled model, because that is where
 * the layering, the matrix, and the edge densities read it from. */
Test(report, an_assembled_file_carries_its_directory)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };
	FileMetrics       *m      = metrics_for("/p/app/a.c", 1);

	m->directory = component_directory("/p/app/a.c");
	cr_assert_not_null(m->directory);
	cr_assert_eq(metrics_add(&acc, m), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);

	cr_assert_eq(report.file_count, 1);
	cr_assert_str_eq(report.files[0]->directory, "/p/app");

	report_free(&report);
	metrics_free(&acc);
}
