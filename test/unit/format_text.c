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
	FileMetrics *files[]  = { metrics_for("/tree/a.c", 3) };
	Report       report   = report_of(files, 1);
	char        *summary  = render_as(&report, STYLE_TABLE,
	                                  VERBOSITY_SUMMARY);
	char        *verbose  = render_as(&report, STYLE_TABLE,
	                                  VERBOSITY_VERBOSE);

	/* The Files tier is a file's own totals and stays; the Functions tier
	 * is one row per analysed entity and goes (HLR-150). */
	cr_assert_not_null(strstr(summary, "\nFiles\n"));
	cr_assert_null(strstr(summary, "\nFunctions\n"));
	cr_assert_not_null(strstr(verbose, "\nFunctions\n"));

	free(summary);
	free(verbose);
	report_free(&report);
}

Test(format_text, the_summary_keeps_the_findings)
{
	FileMetrics *files[] = { metrics_for("/tree/a.c", 3) };
	Report       report  = report_of(files, 1);
	char        *summary = render_as(&report, STYLE_TABLE,
	                                 VERBOSITY_SUMMARY);

	/* Emitted whether or not it has rows: a summary that dropped the one
	 * section a reader acts on would be shorter and useless (HLR-150). */
	cr_assert_not_null(strstr(summary, "\nFindings\n"));

	free(summary);
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

Test(format_text, both_styles_reach_the_same_tiers_at_each_verbosity)
{
	static const char *const summary_tiers[] = {
		"Project summary", "Callouts", "Discovery", "Languages",
		"Files", "Architecture conformance", "Findings",
		"Skipped files"
	};
	static const char *const detail_tiers[] = {
		"Functions", "Fan-out", "Global state",
		"Dependency structure matrix",
		"Dead code within functions", "Custom rule matches"
	};
	FileMetrics *files[] = { metrics_for("/tree/a.c", 3) };
	Report       report  = report_of(files, 1);

	/* One traversal under two decorations and two filters: whichever tier
	 * a verbosity reaches, it reaches in both styles (LLR-SUM-02,
	 * LLR-SUM-09). */
	for (int s = 0; s < 2; s++) {
		Style style   = s ? STYLE_MARKDOWN : STYLE_TABLE;
		char *summary = render_as(&report, style, VERBOSITY_SUMMARY);
		char *verbose = render_as(&report, style, VERBOSITY_VERBOSE);

		for (size_t i = 0; i < sizeof summary_tiers / sizeof *summary_tiers;
		     i++) {
			cr_assert(has_heading(summary, style, summary_tiers[i]),
			          "style %d summary omitted '%s'",
			          s, summary_tiers[i]);
			cr_assert(has_heading(verbose, style, summary_tiers[i]),
			          "style %d verbose omitted '%s'",
			          s, summary_tiers[i]);
		}
		for (size_t i = 0; i < sizeof detail_tiers / sizeof *detail_tiers;
		     i++) {
			cr_assert(!has_heading(summary, style, detail_tiers[i]),
			          "style %d summary presented '%s'",
			          s, detail_tiers[i]);
			cr_assert(has_heading(verbose, style, detail_tiers[i]),
			          "style %d verbose omitted '%s'",
			          s, detail_tiers[i]);
		}
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
