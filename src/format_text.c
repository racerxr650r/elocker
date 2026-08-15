/* format_text.c — the aligned ASCII table.
 *
 * A pure consumer of the report model. It walks the model in the order
 * report.c put it in and emits the tiers in the order the uniform
 * composition rule fixes: project summary first, then the per-file detail
 * (doc/SDD.md §14, HLR-031). The shape does not vary with the type of the
 * target — a single file, a directory, and a repository all render the same
 * table with the same columns (HLR-006).
 *
 * Phase 1 has one metric to show. Later phases add columns and tiers to this
 * traversal; they do not add a second traversal, which is what keeps the
 * table and the Markdown renderer from drifting apart.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "format_text.h"
#include "report.h"

#define COL_PATH  "File"
#define COL_LINES "Lines"

/* Decimal width of a value, so a column is exactly as wide as it needs to
 * be and never wider. */
static int width_of(uint64_t value)
{
	int digits = 1;

	while (value >= 10) {
		value /= 10;
		digits++;
	}
	return digits;
}

static void rule(FILE *out, int width)
{
	for (int i = 0; i < width; i++)
		fputc('-', out);
}

int format_table(const Report *report, FILE *out)
{
	int path_width  = (int)strlen(COL_PATH);
	int lines_width = (int)strlen(COL_LINES);

	for (size_t i = 0; i < report->file_count; i++) {
		int w = (int)strlen(report->files[i]->path);

		if (w > path_width)
			path_width = w;
		w = width_of(report->files[i]->physical_lines);
		if (w > lines_width)
			lines_width = w;
	}

	int summary_width = width_of(report->summary.physical_lines);
	int w             = width_of((uint64_t)report->summary.file_count);

	if (w > summary_width)
		summary_width = w;

	fputs("Project summary\n", out);
	fprintf(out, "  %-14s  %*zu\n", "Files", summary_width,
	        report->summary.file_count);
	fprintf(out, "  %-14s  %*" PRIu64 "\n", "Physical lines", summary_width,
	        report->summary.physical_lines);

	fputs("\nFiles\n", out);
	fprintf(out, "  %-*s  %*s\n", path_width, COL_PATH, lines_width,
	        COL_LINES);
	fputs("  ", out);
	rule(out, path_width);
	fputs("  ", out);
	rule(out, lines_width);
	fputc('\n', out);

	/* A run that analysed nothing still renders this far: a well-formed
	 * report with zero totals and an empty table, rather than no output
	 * at all (HLR-066). */
	for (size_t i = 0; i < report->file_count; i++)
		fprintf(out, "  %-*s  %*" PRIu32 "\n", path_width,
		        report->files[i]->path, lines_width,
		        report->files[i]->physical_lines);

	if (fflush(out) != 0 || ferror(out))
		return -1;

	return 0;
}
