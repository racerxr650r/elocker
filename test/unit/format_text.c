/* test/unit/format_text.c — unit tests for src/format_text.c.
 *
 * The renderer is a pure consumer of the report model, so these tests hand
 * it a model built by hand and assert on the bytes it produces. Rendering to
 * a tmpfile() keeps the assertions on content rather than on the terminal.
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "elc.h"
#include "format_text.h"
#include "report.h"

static FileMetrics *metrics_for(const char *path, uint32_t lines)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path = strdup(path);
	cr_assert_not_null(m->path);
	m->physical_lines = lines;
	return m;
}

/* Render a model and return the bytes written, which the caller frees. */
static char *render(Report *report)
{
	FILE *fp = tmpfile();
	char *buf;
	long  len;

	cr_assert_not_null(fp, "could not open a temporary stream");
	cr_assert_eq(format_table(report, fp), 0);

	len = ftell(fp);
	cr_assert_geq(len, 0);
	rewind(fp);

	buf = calloc(1, (size_t)len + 1);
	cr_assert_not_null(buf);
	cr_assert_eq(fread(buf, 1, (size_t)len, fp), (size_t)len);
	fclose(fp);
	return buf;
}

/* Length of the whole line containing `needle`, or 0 if it is absent. */
static size_t line_length(const char *text, const char *needle)
{
	const char *hit = strstr(text, needle);

	if (!hit)
		return 0;

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
	cr_assert_eq(report_assemble(&acc, &opts, &report), 0);
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

	cr_assert_neq(format_table(&report, fp), 0,
	              "a truncated report is never reported as success");

	fclose(fp);
	report_free(&report);
}
