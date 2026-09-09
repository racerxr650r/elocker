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
	const char *section = strstr(text, "\nFiles (");
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

	cr_assert_not_null(strstr(out, "Project Summary"));
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

	cr_assert_not_null(strstr(out, "Project Summary"),
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
	                                        ? "\n## Nothing To Report\n"
	                                        : "\nNothing To Report\n");

	if (has_heading(text, style, name))
		return true;

	snprintf(needle, sizeof needle, "- %s", name);
	return tail && strstr(tail, needle) != NULL;
}

/* Verifies LLR-SUM-22: a cell wider than the wrap limit survives an unwrapped
 * table whole.
 *
 * The regression this exists for. The wrapping was first written by copying
 * each cell into a fixed buffer the width of the limit, which silently
 * truncated — and so wrapped — every cell wider than 128 in tables the limit
 * does not apply to at all. A `tmpfile()` is not a terminal, so nothing here
 * should be narrowed; the assertion is that the whole path comes back on one
 * line.
 */
Test(format_text, a_cell_wider_than_the_wrap_limit_survives_an_unwrapped_table)
{
	char         path[300];
	FileMetrics *a;
	FileMetrics *files[1];
	Report       report;
	char        *text;

	memset(path, 'x', sizeof path - 1);
	path[0]              = '/';
	path[sizeof path - 1] = '\0';

	a        = metrics_for(path, 3);
	files[0] = a;
	add_function(a, "helper", 1, 2, 0, 0);
	report = report_of(files, 1);
	text   = render(&report);

	cr_assert_not_null(strstr(text, path),
	                   "a cell wider than the wrap limit was divided in a "
	                   "table the limit does not apply to");

	free(text);
	report_free(&report);
}

/* Verifies LLR-SUM-23: the project summary's two columns are measured over
 * the rows it presents, not from a label or a figure named individually.
 *
 * Asserted as equal line lengths, which is what "aligned" means here and what
 * a width taken from one hard-coded label stopped being the moment a longer
 * one was added.
 */
Test(format_text, the_project_summary_columns_are_sized_from_its_rows)
{
	FileMetrics *files[] = { metrics_for("/tree/a.c", 3) };
	Report       report  = report_of(files, 1);
	char        *text    = render(&report);
	const char  *line    = strstr(text, "\n  Files");
	size_t       width   = 0;

	cr_assert_not_null(line);
	line++;

	/* Every row whose value is a *figure*, to the first whose value is a
	 * word. The figures are right-aligned against each other and must
	 * therefore share a width; a word is left-aligned and its row is as
	 * long as the word (HLR-239). Stopping at "Linked image" is stopping
	 * at the first of the three, which the traversal emits last. */
	for (; *line == ' ' && !strncmp(line, "  ", 2); ) {
		const char *end = strchr(line, '\n');

		if (strncmp(line, "  Linked image", 14) == 0)
			break;

		cr_assert_not_null(end);
		if (width == 0)
			width = (size_t)(end - line);
		cr_assert_eq((size_t)(end - line), width,
		             "the summary row '%.*s' is not the width of the "
		             "rows above it",
		             (int)(end - line), line);
		line = end + 1;
	}
	cr_assert_gt(width, 0);

	/* And the words did not widen the column the figures line up in: a
	 * value column sized from a sixty-character path would push every
	 * figure across the page to line up with nothing. */
	cr_assert_lt(width, 40);

	free(text);
	report_free(&report);
}

/* Verifies HLR-235 and LLR-SUM-19: the Markdown summary keeps the per-function
 * tier, and still omits the tiers HLR-150's rule sends to the detail side.
 *
 * The Functions table is a detail tier under that rule — one row per analysed
 * entity — and is presented anyway, which is the exception HLR-150 states. The
 * global-state tier is checked absent beside it, so this cannot pass against a
 * renderer that had lost the partition altogether.
 */
Test(format_text, the_markdown_summary_keeps_the_per_function_tier)
{
	FileMetrics *a       = metrics_for("/tree/a.c", 3);
	FileMetrics *files[] = { a };
	Report       report;
	char        *summary;
	char        *verbose;

	add_function(a, "helper", 1, 2, 0, 0);
	report  = report_of(files, 1);
	summary = render_as(&report, STYLE_MARKDOWN, VERBOSITY_SUMMARY);
	verbose = render_as(&report, STYLE_MARKDOWN, VERBOSITY_VERBOSE);

	cr_assert_not_null(strstr(summary, "\n## Files ("));
	cr_assert_not_null(strstr(summary, "\n## Functions ("));
	cr_assert_not_null(strstr(verbose, "\n## Functions ("));

	/* Still a detail tier, and still absent: one row per global object is
	 * what the rule sends away, and the exception is the function table
	 * alone. */
	cr_assert(!reaches(summary, STYLE_MARKDOWN, "Global State"));
	cr_assert(reaches(verbose, STYLE_MARKDOWN, "Global State"));

	free(summary);
	free(verbose);
	report_free(&report);
}

/* Verifies HLR-218, HLR-235 and LLR-SUM-19: the aligned table's default is the
 * project summary, the findings, the file totals and the function table, and
 * nothing else.
 *
 * The converse of the test above, against the same model, so the two together
 * say that the difference is the format's and not the fixture's. The tiers
 * checked absent are the ones HLR-150 keeps in a Markdown summary — asserting
 * the absence of a tier that was never a summary tier anywhere would pass
 * against a renderer that had lost the second partition entirely.
 */
Test(format_text, the_terminal_summary_is_the_four_shared_tiers)
{
	static const char *const gone[] = {
		"Callouts", "Discovery", "Languages",
		"Architecture Conformance"
	};
	FileMetrics *a       = metrics_for("/tree/a.c", 3);
	FileMetrics *files[] = { a };
	Report       report;
	char        *summary;
	char        *verbose;

	add_function(a, "helper", 1, 2, 0, 0);
	report  = report_of(files, 1);
	summary = render_as(&report, STYLE_TABLE, VERBOSITY_SUMMARY);
	verbose = render_as(&report, STYLE_TABLE, VERBOSITY_VERBOSE);

	cr_assert_not_null(strstr(summary, "Project Summary"));
	cr_assert(has_heading(summary, STYLE_TABLE, "Functions"),
	          "the terminal default dropped the per-function table");
	cr_assert(has_heading(summary, STYLE_TABLE, "Files"),
	          "the terminal default dropped the file totals");

	for (size_t i = 0; i < sizeof gone / sizeof *gone; i++) {
		cr_assert(!reaches(summary, STYLE_TABLE, gone[i]),
		          "the terminal default presented '%s'", gone[i]);
		cr_assert(reaches(verbose, STYLE_TABLE, gone[i]),
		          "--verbose dropped '%s'", gone[i]);
	}

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
	const char  *files_at;

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

	/* Asserted against the Files tier, which the default composition
	 * presents and which follows the findings in the one traversal. The
	 * order under test is that traversal's and is the same in both styles
	 * (HLR-182, HLR-218). */
	summary  = render_as(&report, STYLE_MARKDOWN, VERBOSITY_SUMMARY);
	findings = strstr(summary, "\n## Findings (");
	files_at = strstr(summary, "\n## Files (");

	cr_assert_not_null(findings);
	cr_assert_not_null(files_at);
	cr_assert_lt(findings, files_at,
	             "the findings precede every table beneath them");
	cr_assert_lt(strstr(summary, "Project Summary"), findings,
	             "and follow the project summary, which heads the report");

	free(summary);

	/* And ahead of the evidence tiers a verbose run restores, which is the
	 * broader claim: a finding is read without the tables and the tables
	 * are found from it. */
	{
		char       *verbose  = render_as(&report, STYLE_MARKDOWN,
		                                 VERBOSITY_VERBOSE);
		const char *at       = strstr(verbose, "\n## Findings (");
		const char *callouts = strstr(verbose, "\n## Callouts (");

		cr_assert_not_null(at);
		cr_assert_not_null(callouts);
		cr_assert_lt(at, callouts);
		free(verbose);
	}

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
	cr_assert_not_null(strstr(verbose, "\nNothing To Report\n"));
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

	cr_assert_not_null(strstr(out, "## Nothing To Report\n"));

	free(out);
	report_free(&report);
}

/* Verifies LLR-SUM-17: every Markdown table sits inside a disclosure element
 * stating its row count, beneath a heading that stays a heading (HLR-190).
 *
 * The blank lines either side of the table are asserted because they are what
 * makes the table render at all: GitHub-Flavored Markdown parses the contents
 * of an HTML block as Markdown only where a blank line separates them.
 */
Test(format_text, a_markdown_table_stands_open_under_its_heading)
{
	FileMetrics *a       = metrics_for("/tree/a.c", 30);
	FileMetrics *files[] = { a };
	Report       report;
	char        *out;

	add_function(a, "one", 1, 2, 0, 0);
	add_function(a, "two", 10, 2, 0, 0);
	report = report_of(files, 1);
	out    = render_as(&report, STYLE_MARKDOWN, VERBOSITY_VERBOSE);

	/* The table follows its heading directly. It was folded into a
	 * `<details>` element until Phase 35, which cost more than it saved: a
	 * folded table is not searchable, and a fragment pointing into one
	 * scrolls to nothing — which made every cross-reference of HLR-241 a
	 * link that did not work. */
	cr_assert_not_null(strstr(out, "\n## Files (1)\n\n|"),
	                   "the Files table follows its heading with no "
	                   "element between them");
	cr_assert_not_null(strstr(out, "\n## Functions (2)\n\n|"),
	                   "and so does the Functions table");

	/* The count the `<summary>` used to carry now rides on the heading,
	 * where the aligned table already puts it (HLR-235), so nothing was
	 * lost with the element that held it. */
	cr_assert_null(strstr(out, "<details"),
	               "no table is folded behind a disclosure element");
	cr_assert_null(strstr(out, "<summary>"),
	               "and none carries a disclosure summary");

	free(out);
	report_free(&report);
}

/* The aligned table offers no disclosure and gains none: HLR-190 governs the
 * Markdown report alone, and HTML in a terminal is noise. */
Test(format_text, the_aligned_table_carries_no_html)
{
	FileMetrics *a       = metrics_for("/tree/a.c", 30);
	FileMetrics *files[] = { a };
	Report       report;
	char        *out;

	add_function(a, "one", 1, 2, 0, 0);
	report = report_of(files, 1);
	out    = render_as(&report, STYLE_TABLE, VERBOSITY_VERBOSE);

	cr_assert_null(strstr(out, "<details>"));
	cr_assert_null(strstr(out, "<summary>"));

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

	header = strstr(out, "\nFunctions (");
	cr_assert_not_null(header);
	cr_assert_not_null(strstr(header, "In"));
	cr_assert_not_null(strstr(header, "Out"));
	cr_assert_not_null(strstr(header, "CC"));

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

/* Verifies LLR-SUM-02 and LLR-SUM-09: *every* tier is reached at the verbose
 * verbosity in both styles.
 *
 * This is the invariant HLR-218 left standing, and it is the one worth
 * asserting across the styles now that the summary partitions differ. The
 * guarantee is that a section is written down once and classified where it is
 * written, so it cannot be added to one format's list and forgotten in the
 * other's — and a verbose run of either style is where a section forgotten in
 * both columns would show up. A summary run cannot make that claim any more,
 * because the two summaries are deliberately different documents.
 */
Test(format_text, every_tier_is_reached_at_the_verbose_verbosity_in_both_styles)
{
	static const char *const tiers[] = {
		"Project Summary", "Callouts", "Discovery", "Languages",
		"Files", "Architecture Conformance", "Findings",
		"Skipped Files", "Functions", "Recursion", "Global State",
		"Dependency Structure Matrix",
		"Dead Code Within Functions", "Custom Rule Matches"
	};
	FileMetrics *a       = metrics_for("/tree/a.c", 3);
	FileMetrics *files[] = { a };
	Report       report;

	add_function(a, "helper", 1, 2, 0, 0);
	report = report_of(files, 1);

	for (int s = 0; s < 2; s++) {
		Style style   = s ? STYLE_MARKDOWN : STYLE_TABLE;
		char *verbose = render_as(&report, style, VERBOSITY_VERBOSE);

		for (size_t i = 0; i < sizeof tiers / sizeof *tiers; i++)
			cr_assert(reaches(verbose, style, tiers[i]),
			          "style %d verbose omitted '%s'",
			          s, tiers[i]);

		free(verbose);
	}

	report_free(&report);
}

/* Verifies HLR-218 and HLR-150: one composition, and it is the same one under
 * either decoration.
 *
 * The table below is the partition itself, asserted as data against both
 * styles. Every row must answer identically in the two, which is the claim
 * HLR-218 was rewritten to make — and it is a stronger claim than either
 * summary makes on its own: a section classified for one format and not the
 * other cannot exist, because there is only one classification, and this is
 * what fails if a second is ever reintroduced.
 */
Test(format_text, one_summary_composition_serves_both_formats)
{
	static const struct {
		const char *tier;
		bool        in_summary;
	} PARTITION[] = {
		/* The four of HLR-150's composition. */
		{ "Findings",                 true  },
		{ "Files",                    true  },
		{ "Functions",                true  },
		/* Evidence for one of the four, and so detail: each of these
		 * was a summary tier of a document while HLR-150 admitted
		 * every project-level aggregate. */
		{ "Callouts",                 false },
		{ "Discovery",                false },
		{ "Languages",                false },
		{ "Architecture Conformance", false },
		{ "Skipped Files",            false },
		/* Detail by every reading of the rule, then and now. */
		{ "Recursion",                false },
		{ "Global State",             false },
		{ "Custom Rule Matches",      false }
	};
	FileMetrics *a       = metrics_for("/tree/a.c", 3);
	FileMetrics *files[] = { a };
	Report       report;
	char        *markdown;
	char        *table;

	add_function(a, "helper", 1, 2, 0, 0);
	report   = report_of(files, 1);
	markdown = render_as(&report, STYLE_MARKDOWN, VERBOSITY_SUMMARY);
	table    = render_as(&report, STYLE_TABLE,    VERBOSITY_SUMMARY);

	for (size_t i = 0; i < sizeof PARTITION / sizeof *PARTITION; i++) {
		cr_assert_eq(reaches(markdown, STYLE_MARKDOWN,
		                     PARTITION[i].tier),
		             PARTITION[i].in_summary,
		             "the markdown summary disagrees about '%s'",
		             PARTITION[i].tier);
		cr_assert_eq(reaches(table, STYLE_TABLE, PARTITION[i].tier),
		             PARTITION[i].in_summary,
		             "the terminal summary disagrees about '%s'",
		             PARTITION[i].tier);
	}

	free(markdown);
	free(table);
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
		cr_assert_null(strstr(summary, "Architecture Recovery"),
		               "style %d presented a detail tier in the "
		               "summary", s);
		cr_assert_not_null(strstr(verbose, "Architecture Recovery"),
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
		cr_assert_null(strstr(summary, "Graph Purification"),
		               "style %d presented a detail tier in the "
		               "summary", s);
		cr_assert_not_null(strstr(verbose, "Graph Purification"),
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

/* Verifies HLR-150 and HLR-218: conformance and the matrix are both detail
 * tiers, in every format.
 *
 * Conformance was a summary tier while HLR-150's partition admitted every
 * project-level aggregate. It is evidence for a finding rather than one of the
 * four questions a default report answers, so it now waits for --verbose along
 * with the matrix — and it does so in both styles, there being one composition.
 */
Test(format_text, conformance_and_the_matrix_are_both_detail_tiers)
{
	FileMetrics *files[] = { metrics_for("/tree/a.c", 3) };
	Report       report  = report_of(files, 1);

	{
		char *summary = render_as(&report, STYLE_MARKDOWN,
		                          VERBOSITY_SUMMARY);
		char *verbose = render_as(&report, STYLE_MARKDOWN,
		                          VERBOSITY_VERBOSE);

		cr_assert_null(strstr(summary, "Architecture Conformance"),
		               "presented a detail tier in the summary");
		cr_assert_null(strstr(summary, "Dependency Structure Matrix"),
		               "presented a detail tier in the summary");
		cr_assert_not_null(strstr(verbose,
		                          "Architecture Conformance"));
		cr_assert_not_null(strstr(verbose,
		                          "Dependency Structure Matrix"));

		free(summary);
		free(verbose);
	}

	report_free(&report);
}
