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

/* Verifies LLR-RPT-38: the flow degrees measured over the graph are joined
 * onto the functions they describe, matched by file, start line and name.
 *
 * Neither half of the pair suffices, and the fixture says why: two static
 * functions in two translation units share a name here, so only the one at
 * the named location may take the figures.
 */
Test(report, the_flow_degrees_are_joined_onto_their_functions)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };
	FileMetrics       *a      = metrics_for("/a.c", 40);
	FileMetrics       *b      = metrics_for("/b.c", 40);

	a->functions = calloc(2, sizeof *a->functions);
	cr_assert_not_null(a->functions);
	a->function_count       = 2;
	a->functions[0].name    = strdup("helper");
	a->functions[0].start_line = 1;
	a->functions[1].name    = strdup("hub");
	a->functions[1].start_line = 10;
	b->functions = calloc(1, sizeof *b->functions);
	cr_assert_not_null(b->functions);
	b->function_count       = 1;
	b->functions[0].name    = strdup("helper");   /* the same name */
	b->functions[0].start_line = 1;

	cr_assert_eq(metrics_add(&acc, a), 0);
	cr_assert_eq(metrics_add(&acc, b), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);

	report.fan_out = calloc(3, sizeof *report.fan_out);
	cr_assert_not_null(report.fan_out);
	report.fan_out_count = 3;
	report.fan_out[0].file = strdup("/a.c");
	report.fan_out[0].function = strdup("hub");
	report.fan_out[0].line = 10;
	report.fan_out[0].fan_in = 7;
	report.fan_out[0].fan_out = 3;
	report.fan_out[1].file = strdup("/b.c");
	report.fan_out[1].function = strdup("helper");
	report.fan_out[1].line = 1;
	report.fan_out[1].fan_in = 2;
	report.fan_out[1].fan_out = 0;
	/* A row naming a function the model does not define is dropped, not
	 * diagnosed: a hand-written record can carry one, and the rest of the
	 * record is still readable. */
	report.fan_out[2].file = strdup("/a.c");
	report.fan_out[2].function = strdup("absent");
	report.fan_out[2].line = 900;

	cr_assert_eq(report_attach_flow(&report), 0);

	cr_assert_eq(report.files[0]->functions[1].fan_in, 7);
	cr_assert_eq(report.files[0]->functions[1].fan_out, 3);
	cr_assert_eq(report.files[0]->functions[0].fan_in, 0,
	             "the /a.c helper takes no figures from the /b.c one");
	cr_assert_eq(report.files[1]->functions[0].fan_in, 2);

	report_free(&report);
	metrics_free(&acc);
}

/* Verifies LLR-RPT-39: the threshold listing unites the functions at or over
 * the configured complexity threshold with every function a band names, and
 * carries the highest band that named each (HLR-187).
 */
Test(report, the_listing_unites_the_configured_threshold_with_the_bands)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };
	FileMetrics       *a      = metrics_for("/a.c", 90);

	opts.complexity_threshold = 15;

	a->functions = calloc(4, sizeof *a->functions);
	cr_assert_not_null(a->functions);
	a->function_count = 4;
	/* Inside every band, and below the listing threshold: absent. */
	a->functions[0].name       = strdup("quiet");
	a->functions[0].start_line = 1;
	a->functions[0].complexity = 4;
	/* Complexity 11: a warning, and below the listing threshold of 15 —
	 * so it is here because of the band and nothing else. */
	a->functions[1].name       = strdup("branchy");
	a->functions[1].start_line = 10;
	a->functions[1].complexity = 11;
	/* Fan-out 16: critical. Complexity says nothing about it. */
	a->functions[2].name       = strdup("dispatch");
	a->functions[2].start_line = 20;
	a->functions[2].complexity = 2;
	/* Fan-in 26: a warning on elc's own authority. */
	a->functions[3].name       = strdup("popular");
	a->functions[3].start_line = 30;
	a->functions[3].complexity = 2;

	cr_assert_eq(metrics_add(&acc, a), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);

	report.fan_out = calloc(2, sizeof *report.fan_out);
	cr_assert_not_null(report.fan_out);
	report.fan_out_count = 2;
	report.fan_out[0].file = strdup("/a.c");
	report.fan_out[0].function = strdup("dispatch");
	report.fan_out[0].line = 20;
	report.fan_out[0].fan_out = 16;
	report.fan_out[1].file = strdup("/a.c");
	report.fan_out[1].function = strdup("popular");
	report.fan_out[1].line = 30;
	report.fan_out[1].fan_in = 26;

	cr_assert_eq(report_attach_flow(&report), 0);

	cr_assert_eq(report.over_threshold.count, 3,
	             "quiet is inside every band and below the threshold");
	cr_assert_str_eq(report.over_threshold.items[0].function->name,
	                 "branchy");
	cr_assert_eq(report.over_threshold.items[0].severity,
	             SEVERITY_WARNING);
	cr_assert_str_eq(report.over_threshold.items[1].function->name,
	                 "dispatch");
	cr_assert_eq(report.over_threshold.items[1].severity,
	             SEVERITY_CRITICAL);
	cr_assert_str_eq(report.over_threshold.items[2].function->name,
	                 "popular");
	cr_assert_eq(report.over_threshold.items[2].severity,
	             SEVERITY_WARNING);

	report_free(&report);
	metrics_free(&acc);
}

/* Verifies LLR-RPT-40: the index is derived onto the metrics in both assembly
 * paths, so the field is never read as the zero of an uninitialised value.
 *
 * For this measurement that zero is not neutral — it is the worst score on
 * the scale — so a listing built between the two derivations would report
 * every function in the project as critically unmaintainable.
 */
Test(report, the_maintainability_index_is_derived_in_both_paths)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };
	FileMetrics       *a      = metrics_for("/a.c", 40);

	opts.complexity_threshold = 15;

	a->functions = calloc(1, sizeof *a->functions);
	cr_assert_not_null(a->functions);
	a->function_count          = 1;
	a->functions[0].name       = strdup("hub");
	a->functions[0].start_line = 10;
	a->functions[0].eloc       = 20;
	a->functions[0].complexity = 5;

	cr_assert_eq(metrics_add(&acc, a), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);

	/* Assembled before any graph exists: both degrees are zero, the flow
	 * term vanishes, and the figure rests on length and branching. */
	cr_assert_eq(report.files[0]->functions[0].mi, 71);
	cr_assert_eq(report.over_threshold.count, 0,
	             "a score of 71 is inside the accepted band, so nothing "
	             "is listed — the field was not read as an unset zero");

	/* And again once the degrees are real: one caller, two callees. */
	report.fan_out = calloc(1, sizeof *report.fan_out);
	cr_assert_not_null(report.fan_out);
	report.fan_out_count       = 1;
	report.fan_out[0].file     = strdup("/a.c");
	report.fan_out[0].function = strdup("hub");
	report.fan_out[0].line     = 10;
	report.fan_out[0].fan_in   = 1;
	report.fan_out[0].fan_out  = 2;

	cr_assert_eq(report_attach_flow(&report), 0);
	cr_assert_eq(report.files[0]->functions[0].mi, 66,
	             "the flow term now costs it five points");

	report_free(&report);
	metrics_free(&acc);
}

/* Verifies LLR-RPT-39 for the case the configured threshold governs alone: a
 * function inside every band, listed because its complexity met the value
 * `--threshold` set, carries no severity. The listing threshold has never
 * moved a severity and must not start (HLR-023).
 */
Test(report, a_function_listed_by_the_configured_threshold_carries_no_severity)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };
	FileMetrics       *a      = metrics_for("/a.c", 20);

	opts.complexity_threshold = 5;

	a->functions = calloc(1, sizeof *a->functions);
	cr_assert_not_null(a->functions);
	a->function_count          = 1;
	a->functions[0].name       = strdup("modest");
	a->functions[0].start_line = 1;
	a->functions[0].complexity = 6;   /* over 5, inside every band */

	cr_assert_eq(metrics_add(&acc, a), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);

	cr_assert_eq(report.over_threshold.count, 1);
	cr_assert_eq(report.over_threshold.items[0].severity, SEVERITY_INFO);

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
