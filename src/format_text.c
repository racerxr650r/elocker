/* format_text.c — the aligned ASCII table.
 *
 * A pure consumer of the report model. It walks the model in the order
 * report.c put it in and emits the tiers in the order the uniform
 * composition rule fixes: project summary, per-file totals, per-function
 * detail, then what was skipped (doc/SDD.md §14, HLR-031). The shape does
 * not vary with the type of the target — a single file, a directory, and a
 * repository all render the same sections with the same columns (HLR-006).
 *
 * Every section is emitted whether or not it has rows. A heading with an
 * empty body says "nothing here"; an absent heading is indistinguishable
 * from a renderer that forgot.
 *
 * Later phases add columns to this traversal. They do not add a second one:
 * sharing it is what keeps the table and the Markdown renderer from drifting
 * apart.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "format_text.h"
#include "report.h"

static int width_of(uint64_t value)
{
	int digits = 1;

	while (value >= 10) {
		value /= 10;
		digits++;
	}
	return digits;
}

/* Widest of a column's header and every cell beneath it, so a column is
 * exactly as wide as it needs to be and never wider. */
static int widest(int width, const char *candidate)
{
	int n = (int)strlen(candidate);

	return n > width ? n : width;
}

static int widest_number(int width, uint64_t value)
{
	int n = width_of(value);

	return n > width ? n : width;
}

static void rule(FILE *out, int width)
{
	for (int i = 0; i < width; i++)
		fputc('-', out);
}

/* One "1234-1250" line range, rendered into a caller-owned buffer so the
 * column can be measured before anything is written. */
static void span_of(char *buffer, size_t size, const FunctionMetric *fn)
{
	snprintf(buffer, size, "%" PRIu32 "-%" PRIu32, fn->start_line,
	         fn->end_line);
}

static void summary_section(const Report *report, FILE *out)
{
	int value_width = width_of(report->summary.physical_lines);

	value_width = widest_number(value_width,
	                            (uint64_t)report->summary.file_count);
	value_width = widest_number(value_width, report->summary.function_count);
	value_width = widest_number(value_width,
	                            (uint64_t)report->skipped_files.count);

	fputs("Project summary\n", out);
	fprintf(out, "  %-14s  %*zu\n", "Files", value_width,
	        report->summary.file_count);
	fprintf(out, "  %-14s  %*" PRIu64 "\n", "Physical lines", value_width,
	        report->summary.physical_lines);
	fprintf(out, "  %-14s  %*" PRIu64 "\n", "Functions", value_width,
	        report->summary.function_count);
	fprintf(out, "  %-14s  %*zu\n", "Skipped", value_width,
	        report->skipped_files.count);
}

static void files_section(const Report *report, FILE *out)
{
	int path = widest(0, "File");
	int lang = widest(0, "Language");
	int line = widest(0, "Lines");
	int func = widest(0, "Functions");

	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		path = widest(path, f->path);
		lang = widest(lang, f->language ? f->language : "");
		line = widest_number(line, f->physical_lines);
		func = widest_number(func, (uint64_t)f->function_count);
	}

	fputs("\nFiles\n", out);
	fprintf(out, "  %-*s  %-*s  %*s  %*s\n", path, "File", lang, "Language",
	        line, "Lines", func, "Functions");
	fputs("  ", out);
	rule(out, path);
	fputs("  ", out);
	rule(out, lang);
	fputs("  ", out);
	rule(out, line);
	fputs("  ", out);
	rule(out, func);
	fputc('\n', out);

	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		fprintf(out, "  %-*s  %-*s  %*" PRIu32 "  %*zu\n", path, f->path,
		        lang, f->language ? f->language : "", line,
		        f->physical_lines, func, f->function_count);
	}
}

static void functions_section(const Report *report, FILE *out)
{
	char span[32];
	int  path = widest(0, "File");
	int  name = widest(0, "Function");
	int  line = widest(0, "Lines");

	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		for (size_t j = 0; j < f->function_count; j++) {
			path = widest(path, f->path);
			name = widest(name, f->functions[j].name);
			span_of(span, sizeof span, &f->functions[j]);
			line = widest(line, span);
		}
	}

	fputs("\nFunctions\n", out);
	fprintf(out, "  %-*s  %-*s  %*s\n", path, "File", name, "Function",
	        line, "Lines");
	fputs("  ", out);
	rule(out, path);
	fputs("  ", out);
	rule(out, name);
	fputs("  ", out);
	rule(out, line);
	fputc('\n', out);

	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		for (size_t j = 0; j < f->function_count; j++) {
			span_of(span, sizeof span, &f->functions[j]);
			fprintf(out, "  %-*s  %-*s  %*s\n", path, f->path, name,
			        f->functions[j].name, line, span);
		}
	}
}

/* Every discovered file is accounted for: one that no language module could
 * be found for is listed here rather than silently absent (HLR-012). */
static void skipped_section(const Report *report, FILE *out)
{
	fputs("\nSkipped files (no language module)\n", out);

	for (size_t i = 0; i < report->skipped_files.count; i++)
		fprintf(out, "  %s\n", report->skipped_files.paths[i]);
}

int format_table(const Report *report, FILE *out)
{
	summary_section(report, out);
	files_section(report, out);
	functions_section(report, out);
	skipped_section(report, out);

	if (fflush(out) != 0 || ferror(out))
		return -1;

	return 0;
}
