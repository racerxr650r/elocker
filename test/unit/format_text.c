/* test/unit/format_text.c — unit tests for src/format_text.c.
 *
 * The renderer is a pure consumer of the report model, so these tests hand
 * it a model built by hand and assert on the bytes it produces. Rendering to
 * a tmpfile() keeps the assertions on content rather than on the terminal.
 */

#include <criterion/criterion.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "elc.h"
#include "format_text.h"
#include "report.h"
#include "thresholds.h"

static FileMetrics *metrics_for(const char *path, uint32_t lines)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path = strdup(path);
	cr_assert_not_null(m->path);
	m->physical_lines = lines;
	return m;
}

/* Render a model verbosely and return the bytes written, which the caller
 * frees. Verbose, because the column-width claims below are made about the
 * Files tier alongside every other, and a summary render would leave half of
 * them measuring a section that was not printed. */
static char *render(Report *report)
{
	FILE *fp = tmpfile();
	char *buf;
	long  len;

	cr_assert_not_null(fp, "could not open a temporary stream");
	cr_assert_eq(format_table(report, VERBOSITY_VERBOSE, fp), 0);

	len = ftell(fp);
	cr_assert_geq(len, 0);
	rewind(fp);

	buf = calloc(1, (size_t)len + 1);
	cr_assert_not_null(buf);
	cr_assert_eq(fread(buf, 1, (size_t)len, fp), (size_t)len);
	fclose(fp);
	return buf;
}

/* Length of the line containing `needle` within the Files section.
 *
 * Scoped to that section deliberately: a path appears in the Callouts section
 * too, and searching the whole report finds that row first — where the
 * columns are different and the widths have no reason to agree.
 */
static size_t line_length(const char *text, const char *needle)
{
	const char *section = strstr(text, "\nFiles\n");
	const char *hit     = section ? strstr(section, needle) : NULL;

	if (!hit)
		return 0;

	text = section;

	const char *start = hit;
	while (start > text && start[-1] != '\n')
		start--;

	const char *end = strchr(hit, '\n');
	end = end ? end : text + strlen(text);

	return (size_t)(end - start);
}

/* Give a file one function, so the tiers that enumerate functions have
 * something to enumerate. Since HLR-188 an empty table is not printed, so a
 * test asserting a per-function tier is present must supply a function. */
static void add_function(FileMetrics *m, const char *name, uint32_t line,
                         uint32_t complexity, uint32_t fan_in,
                         uint32_t fan_out)
{
	FunctionMetric *grown = realloc(m->functions,
	                                (m->function_count + 1) * sizeof *grown);

	cr_assert_not_null(grown);
	m->functions = grown;

	FunctionMetric *fn = &m->functions[m->function_count];

	memset(fn, 0, sizeof *fn);
	fn->name = strdup(name);
	cr_assert_not_null(fn->name);
	fn->start_line = line;
	fn->end_line   = line + 4;
	fn->eloc       = 3;
	fn->complexity = complexity;
	fn->fan_in     = fan_in;
	fn->fan_out    = fan_out;
	m->function_count++;
}

static Report report_of(FileMetrics **files, size_t count)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };

	for (size_t i = 0; i < count; i++)
		cr_assert_eq(metrics_add(&acc, files[i]), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);
	metrics_free(&acc);
	return report;
}

Test(format_text, the_table_carries_the_summary_and_every_file)
{
	FileMetrics *files[] = { metrics_for("/tree/a.c", 3),
	                         metrics_for("/tree/bb.c", 40) };
	Report       report  = report_of(files, 2);
	char        *out     = render(&report);

	cr_assert_not_null(strstr(out, "Project summary"));
	cr_assert_not_null(strstr(out, "Physical lines"));
	cr_assert_not_null(strstr(out, "/tree/a.c"));
	cr_assert_not_null(strstr(out, "/tree/bb.c"));
	cr_assert_not_null(strstr(out, "43"), "the project total is shown");

	free(out);
	report_free(&report);
}

Test(format_text, columns_are_aligned_on_the_longest_path)
{
	FileMetrics *files[] = { metrics_for("/short.c", 1),
	                         metrics_for("/a/much/longer/path.c", 1) };
	Report       report  = report_of(files, 2);
	char        *out     = render(&report);

	/* Both file rows must be the same width for the column to be aligned;
	 * the width comes from the longest path (LLR-TBL-01). */
	size_t long_row  = line_length(out, "/a/much/longer/path.c");
	size_t short_row = line_length(out, "/short.c");

	cr_assert_neq(long_row, 0);
	cr_assert_eq(short_row, long_row,
	             "the shorter path is padded to the width of the longest");

	free(out);
	report_free(&report);
}

Test(format_text, an_empty_report_still_renders_a_table)
{
	Report report = report_of(NULL, 0);
	char  *out    = render(&report);

	cr_assert_not_null(strstr(out, "Project summary"),
	                   "a run that analysed nothing still renders a "
	                   "well-formed report (HLR-066)");
	cr_assert_not_null(strstr(out, "Files"));

	free(out);
	report_free(&report);
}

Test(format_text, a_write_failure_is_reported)
{
	Report report = report_of(NULL, 0);
	FILE  *fp     = fopen("/dev/full", "w");

	cr_assert_not_null(fp, "/dev/full is needed to provoke a write failure");

	cr_assert_neq(format_table(&report, VERBOSITY_VERBOSE, fp), 0,
	              "a truncated report is never reported as success");

	fclose(fp);
	report_free(&report);
}

/* ---------------------------------------------------------- verbosity ------
 *
 * Verifies LLR-SUM-09 at the level the traversal lives at. The integration
 * suite asserts the partition section by section against a real run; what is
 * asserted here is the structural property that makes that partition hold —
 * that the verbosity filters one walk rather than selecting between two, so a
 * tier can be present in neither composition or in both, never invented for
 * one.
 */

/* Render a model at a given verbosity, in a given style. */
static char *render_as(Report *report, Style style, Verbosity verbosity)
{
	FILE *fp = tmpfile();
	char *buf;
	long  len;

	cr_assert_not_null(fp, "could not open a temporary stream");
	cr_assert_eq(render_report(report, style, verbosity, fp), 0);

	len = ftell(fp);
	cr_assert_geq(len, 0);
	rewind(fp);

	buf = calloc(1, (size_t)len + 1);
	cr_assert_not_null(buf);
	cr_assert_eq(fread(buf, 1, (size_t)len, fp), (size_t)len);
	fclose(fp);
	return buf;
}

Test(format_text, the_summary_omits_the_per_function_tier)
{
	FileMetrics *a       = metrics_for("/tree/a.c", 3);
	FileMetrics *files[] = { a };
	Report       report;
	char        *summary;
	char        *verbose;

	add_function(a, "helper", 1, 2, 0, 0);
	report  = report_of(files, 1);
	summary = render_as(&report, STYLE_TABLE, VERBOSITY_SUMMARY);
	verbose = render_as(&report, STYLE_TABLE, VERBOSITY_VERBOSE);

	/* The Files tier is a file's own totals and stays; the Functions tier
	 * is one row per analysed entity and goes (HLR-150). */
	cr_assert_not_null(strstr(summary, "\nFiles\n"));
	cr_assert_null(strstr(summary, "\nFunctions\n"));
	cr_assert_not_null(strstr(verbose, "\nFunctions\n"));

	free(summary);
	free(verbose);
	report_free(&report);
}

/* Verifies LLR-SUM-15: the findings are the section immediately after the
 * project summary, ahead of every table that supplies their evidence
 * (HLR-182).
 */
Test(format_text, the_findings_follow_the_project_summary)
{
	FileMetrics *a       = metrics_for("/tree/a.c", 3);
	FileMetrics *files[] = { a };
	Report       report;
	char        *summary;
	const char  *findings;
	const char  *callouts;

	add_function(a, "dispatch", 1, 2, 0, 16);
	report = report_of(files, 1);

	report.findings = calloc(1, sizeof *report.findings);
	cr_assert_not_null(report.findings);
	report.finding_count            = 1;
	report.findings[0].severity     = strdup("critical");
	report.findings[0].measurement  = strdup("fan-out");
	report.findings[0].subject      = strdup("dispatch");
	report.findings[0].where        = strdup("/tree/a.c");
	report.findings[0].detail       = strdup("calls 16 distinct subroutines");
	report.findings[0].source       = strdup("Henry-Kafura");

	summary  = render_as(&report, STYLE_TABLE, VERBOSITY_SUMMARY);
	findings = strstr(summary, "\nFindings\n");
	callouts = strstr(summary, "\nCallouts\n");

	cr_assert_not_null(findings);
	cr_assert_not_null(callouts);
	cr_assert_lt(findings, callouts,
	             "the findings precede every table beneath them");
	cr_assert_lt(strstr(summary, "Project summary"), findings,
	             "and follow the project summary, which heads the report");

	free(summary);
	report_free(&report);
}

/* Verifies LLR-SUM-16: a table with no rows is not printed,
 * and the closing statement names it (HLR-188, HLR-189).
 */
Test(format_text, an_empty_table_is_omitted_and_named_at_the_end)
{
	FileMetrics *a       = metrics_for("/tree/a.c", 3);
	FileMetrics *files[] = { a };
	Report       report;
	char        *verbose;

	add_function(a, "helper", 1, 2, 0, 0);
	report = report_of(files, 1);
	report.strata_state = STRATA_OMITTED_NONE_DECLARED;
	verbose = render_as(&report, STYLE_TABLE, VERBOSITY_VERBOSE);

	cr_assert_null(strstr(verbose, "\nRecursion\n"),
	               "a run with no recursion prints no recursion table");
	cr_assert_not_null(strstr(verbose, "\nNothing to report\n"));
	cr_assert_not_null(strstr(verbose, "- Recursion\n"),
	                   "and says so, by name, at the end");

	/* The heading names the reason where there is one, and that is the
	 * whole point of naming the table rather than merely counting them:
	 * HLR-115 requires the reason be stated wherever the analysis is
	 * not. */
	cr_assert_not_null(strstr(verbose,
	                          "- Layering (omitted: no architectural "
	                          "strata declared, see --stratum)\n"));

	free(verbose);
	report_free(&report);
}

/* The closing statement is emitted whether or not anything was empty: a
 * section that appears only sometimes is the problem it exists to solve. */
Test(format_text, the_closing_statement_is_present_even_with_nothing_empty)
{
	Report report = report_of(NULL, 0);
	char  *out    = render_as(&report, STYLE_MARKDOWN, VERBOSITY_SUMMARY);

	cr_assert_not_null(strstr(out, "## Nothing to report\n"));

	free(out);
	report_free(&report);
}

/* Verifies LLR-SUM-14: one function table carrying every per-function figure,
 * where there were three tables carrying them between them (HLR-183). */
Test(format_text, the_function_table_carries_the_degrees_beside_the_metrics)
{
	FileMetrics *a       = metrics_for("/tree/a.c", 30);
	FileMetrics *files[] = { a };
	Report       report;
	char        *out;
	const char  *header;

	add_function(a, "hub", 10, 7, 4, 9);
	report = report_of(files, 1);
	out    = render_as(&report, STYLE_TABLE, VERBOSITY_VERBOSE);

	header = strstr(out, "\nFunctions\n");
	cr_assert_not_null(header);
	cr_assert_not_null(strstr(header, "Fan-in"));
	cr_assert_not_null(strstr(header, "Fan-out"));
	cr_assert_not_null(strstr(header, "Complexity"));

	/* And the tables the figures used to live in are gone, not merely
	 * moved: two tables listing the same functions are two chances to
	 * disagree about which functions exist. */
	cr_assert_null(strstr(out, "\nFan-out (distinct callees)\n"));
	cr_assert_null(strstr(out, "\nInformation flow"));
	cr_assert_null(strstr(out, "Henry-Kafura"),
	               "the metric is withdrawn, per function and per project");

	free(out);
	report_free(&report);
}

Test(format_text, the_verbose_report_is_a_superset_of_the_summary)
{
	FileMetrics *files[] = { metrics_for("/tree/a.c", 3),
	                         metrics_for("/tree/b.c", 9) };
	Report       report  = report_of(files, 2);
	char        *summary = render_as(&report, STYLE_TABLE,
	                                 VERBOSITY_SUMMARY);
	char        *verbose = render_as(&report, STYLE_TABLE,
	                                 VERBOSITY_VERBOSE);

	cr_assert_gt(strlen(verbose), strlen(summary),
	             "the option must select something");

	free(summary);
	free(verbose);
	report_free(&report);
}

/* Whether `text` carries a section *heading* named `name` in the given style.
 *
 * Anchored to the start of a line, and to the style's decoration, because a
 * tier's name also occurs as a column header two spaces in — the Files tier
 * has a "Functions" column, and a loose substring search would find it and
 * report the per-function tier present in a summary that omitted it.
 */
static bool has_heading(const char *text, Style style, const char *name)
{
	char needle[128];

	snprintf(needle, sizeof needle, "%s%s",
	         style == STYLE_MARKDOWN ? "## " : "", name);

	for (const char *p = text; (p = strstr(p, needle)) != NULL; p += 1)
		if (p == text || p[-1] == '\n')
			return true;
	return false;
}

/* Whether the traversal *reached* a tier: it either printed the section, or
 * printed nothing and named it in the closing statement.
 *
 * The disjunction is the contract HLR-188 and HLR-189 make together. Before
 * them, "reached" and "printed" were the same thing; now a tier with no rows
 * is reached and not printed, and a tier a verbosity filtered out is neither.
 * Asserting on the disjunction is what keeps this test measuring the
 * traversal rather than the fixture's contents.
 */
static bool reaches(const char *text, Style style, const char *name)
{
	char        needle[160];
	const char *tail = strstr(text, style == STYLE_MARKDOWN
	                                        ? "\n## Nothing to report\n"
	                                        : "\nNothing to report\n");

	if (has_heading(text, style, name))
		return true;

	snprintf(needle, sizeof needle, "- %s", name);
	return tail && strstr(tail, needle) != NULL;
}

Test(format_text, both_styles_reach_the_same_tiers_at_each_verbosity)
{
	static const char *const summary_tiers[] = {
		"Project summary", "Callouts", "Discovery", "Languages",
		"Files", "Architecture conformance", "Findings",
		"Skipped files"
	};
	static const char *const detail_tiers[] = {
		"Functions", "Recursion", "Global state",
		"Dependency structure matrix",
		"Dead code within functions", "Custom rule matches"
	};
	FileMetrics *a       = metrics_for("/tree/a.c", 3);
	FileMetrics *files[] = { a };
	Report       report;

	add_function(a, "helper", 1, 2, 0, 0);
	report = report_of(files, 1);

	/* One traversal under two decorations and two filters: whichever tier
	 * a verbosity reaches, it reaches in both styles (LLR-SUM-02,
	 * LLR-SUM-09). */
	for (int s = 0; s < 2; s++) {
		Style style   = s ? STYLE_MARKDOWN : STYLE_TABLE;
		char *summary = render_as(&report, style, VERBOSITY_SUMMARY);
		char *verbose = render_as(&report, style, VERBOSITY_VERBOSE);

		for (size_t i = 0; i < sizeof summary_tiers / sizeof *summary_tiers;
		     i++) {
			cr_assert(reaches(summary, style, summary_tiers[i]),
			          "style %d summary omitted '%s'",
			          s, summary_tiers[i]);
			cr_assert(reaches(verbose, style, summary_tiers[i]),
			          "style %d verbose omitted '%s'",
			          s, summary_tiers[i]);
		}
		for (size_t i = 0; i < sizeof detail_tiers / sizeof *detail_tiers;
		     i++) {
			cr_assert(!reaches(summary, style, detail_tiers[i]),
			          "style %d summary presented '%s'",
			          s, detail_tiers[i]);
			cr_assert(reaches(verbose, style, detail_tiers[i]),
			          "style %d verbose omitted '%s'",
			          s, detail_tiers[i]);
		}
		free(summary);
		free(verbose);
	}

	report_free(&report);
}

/* The recovered layering, and the boundary the section is built to keep.
 *
 * **A proposal is never a baseline** (HLR-173). The rows are a description of
 * the order the graph already has; nothing above them was measured against
 * them, and the heading says so, because a table of layers printed under an
 * architecture report is otherwise easy to read as a verdict.
 */
Test(format_text, recovery_is_a_detail_tier_and_says_it_is_a_proposal)
{
	FileMetrics *files[] = { metrics_for("/tree/a.c", 3) };
	Report       report  = report_of(files, 1);

	report.recovery_state    = RECOVERY_PROPOSED;
	report.recovery_strata   = 2;
	report.recovery          = calloc(2, sizeof *report.recovery);
	cr_assert_not_null(report.recovery);
	report.recovery[0].directory = strdup("/tree/app");
	report.recovery[1].directory = strdup("/tree/hal");
	cr_assert_not_null(report.recovery[0].directory);
	cr_assert_not_null(report.recovery[1].directory);
	report.recovery[0].layer     = 0;
	report.recovery[1].layer     = 1;
	report.recovery[0].functions = 2;
	report.recovery[1].functions = 1;
	report.recovery_count        = 2;
	report.recovery_proposal =
		strdup("--stratum app:'/tree/app/*' "
		       "--stratum hal:'/tree/hal/*' "
		       "--stratum-order 'app>hal'");
	cr_assert_not_null(report.recovery_proposal);

	for (int s = 0; s < 2; s++) {
		Style style   = s ? STYLE_MARKDOWN : STYLE_TABLE;
		char *summary = render_as(&report, style, VERBOSITY_SUMMARY);
		char *verbose = render_as(&report, style, VERBOSITY_VERBOSE);

		/* One row per directory, so the partition rule of HLR-150 puts
		 * it with the other per-entity tables. */
		cr_assert_null(strstr(summary, "Architecture recovery"),
		               "style %d presented a detail tier in the "
		               "summary", s);
		cr_assert_not_null(strstr(verbose, "Architecture recovery"),
		                   "style %d", s);
		cr_assert_not_null(strstr(verbose, "never the baseline"),
		                   "style %d dropped the boundary the section "
		                   "exists to keep", s);
		cr_assert_not_null(strstr(verbose, "/tree/hal"), "style %d", s);

		/* **The proposal as arguments, not as prose** (HLR-173).
		 * Adoption is a copy rather than a transcription, and what
		 * `elc` produces is a command line that takes effect only when
		 * the user passes it back. */
		cr_assert_not_null(strstr(verbose, "--stratum-order 'app>hal'"),
		                   "style %d dropped the argument list", s);

		free(summary);
		free(verbose);
	}

	report_free(&report);
}

/* The two conformance indices are a project-level aggregate and the matrix is
 * one row per subject, so the first is a summary tier and the second a detail
 * tier — the partition rule applied to the two tiers this phase added
 * (LLR-SUM-11).
 *
 * Asserted in both styles, because a tier that reached one decoration and not
 * the other would break the uniform composition the shared traversal exists
 * to guarantee (HLR-031).
 */
Test(format_text, purification_is_a_detail_tier_and_names_elcs_own_thresholds)
{
	FileMetrics *files[] = { metrics_for("/tree/a.c", 3) };
	Report       report  = report_of(files, 1);

	report.purify_thresholds.sink_authority  = 90;
	report.purify_thresholds.sink_hub        = 10;
	report.purify_thresholds.god_betweenness = 90;
	report.purify_thresholds.god_hub         = 90;
	report.purify_thresholds.core_depth      = 2;

	for (int s = 0; s < 2; s++) {
		Style style   = s ? STYLE_MARKDOWN : STYLE_TABLE;
		char *summary = render_as(&report, style, VERBOSITY_SUMMARY);
		char *verbose = render_as(&report, style, VERBOSITY_VERBOSE);

		/* One row per classified function, so the partition rule of
		 * HLR-150 puts it with the other per-entity tables. */
		cr_assert_null(strstr(summary, "Graph purification"),
		               "style %d presented a detail tier in the "
		               "summary", s);
		cr_assert_not_null(strstr(verbose, "Graph purification"),
		                   "style %d", s);

		/* **The label is load-bearing** (HLR-171, HLR-099). None of the
		 * five thresholds is a published standard, and presenting a
		 * classification made against one without saying so would lend
		 * it an authority it has not got. */
		cr_assert_not_null(strstr(verbose, ELC_OWN_HEURISTIC),
		                   "style %d dropped the attribution", s);
		cr_assert_not_null(strstr(verbose, "peripheral below core "
		                          "depth 2"),
		                   "style %d dropped the thresholds in force",
		                   s);
		/* And no severity, because a classification does not have one:
		 * the columns are what was measured and what was done, never
		 * how bad it is (HLR-101). */
		cr_assert_null(strstr(verbose, "Severity  Class"), "style %d",
		               s);

		free(summary);
		free(verbose);
	}

	report_free(&report);
}

Test(format_text, conformance_is_a_summary_tier_and_the_matrix_a_detail_tier)
{
	FileMetrics *files[] = { metrics_for("/tree/a.c", 3) };
	Report       report  = report_of(files, 1);

	for (int s = 0; s < 2; s++) {
		Style style   = s ? STYLE_MARKDOWN : STYLE_TABLE;
		char *summary = render_as(&report, style, VERBOSITY_SUMMARY);
		char *verbose = render_as(&report, style, VERBOSITY_VERBOSE);

		cr_assert_not_null(strstr(summary, "Architecture conformance"),
		                   "style %d dropped a project-level aggregate "
		                   "from the summary", s);
		cr_assert_null(strstr(summary, "Dependency structure matrix"),
		               "style %d presented a detail tier in the "
		               "summary", s);
		cr_assert_not_null(strstr(verbose, "Architecture conformance"),
		                   "style %d", s);
		cr_assert_not_null(strstr(verbose,
		                          "Dependency structure matrix"),
		                   "style %d", s);

		free(summary);
		free(verbose);
	}

	report_free(&report);
}
