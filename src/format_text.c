/* format_text.c — the two human-facing renderers.
 *
 * The aligned ASCII table that is the default (HLR-027) and GitHub-Flavored
 * Markdown (HLR-029). Both are pure consumers of the report model: they
 * recompute nothing, mutate nothing, and — the part that is easy to get
 * wrong — sort nothing. Ordering is report.c's responsibility.
 *
 * **They are one traversal, not two.** `render_report()` walks the model
 * once and emits the same tiers in the same order; the style decides only
 * how each tier is decorated. That is what makes HLR-031's uniform
 * composition true by construction rather than by parallel maintenance — a
 * tier added here appears in both formats, and cannot be added to one and
 * forgotten in the other, because there is nowhere to forget it.
 *
 * Each tier is built into a Grid and then rendered. The two passes are what
 * the aligned style needs — a column's width is not known until its last
 * cell is in — and Markdown reuses the same widths, so the raw document is
 * readable rather than ragged.
 *
 * The shape does not vary with the type of the target either: a single file,
 * a directory, and a repository render the same sections with the same
 * columns (HLR-006).
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "format_text.h"
#include "report.h"

#define GRID_MAX_COLUMNS 8

/* The type of one cell argument.
 *
 * A typedef rather than `const char *` written out, because `va_arg` takes a
 * type and tree-sitter-c parses a multi-token one as an error — so `elc`
 * cannot read a file that spells it in full. The limitation is the grammar's
 * and is recorded in doc/notes.md §3; using a single-token name here costs
 * nothing and keeps this file analysable by the tool it belongs to.
 */
typedef const char *CellText;

/* One tier's worth of table: its heading, its columns, and its cells.
 *
 * Cells are owned copies of already-formatted strings. Formatting a value
 * once, into a cell, is what lets the measuring pass and the writing pass
 * agree — measuring a number one way and printing it another is how a column
 * comes out a character short.
 */
typedef struct {
	const char *heading;
	size_t      column_count;
	const char *columns[GRID_MAX_COLUMNS];
	bool        numeric[GRID_MAX_COLUMNS]; /* right-aligned when aligned */
	int         width[GRID_MAX_COLUMNS];
	char      **cells;                     /* row-major, owned          */
	size_t      row_count;
	size_t      capacity;                  /* in rows                   */
	bool        failed;                    /* an allocation gave out    */
} Grid;

static void grid_begin(Grid *grid, const char *heading, size_t columns,
                       const char *const *names, const bool *numeric)
{
	memset(grid, 0, sizeof *grid);
	grid->heading      = heading;
	grid->column_count = columns;

	for (size_t i = 0; i < columns; i++) {
		grid->columns[i] = names[i];
		grid->numeric[i] = numeric ? numeric[i] : false;
		grid->width[i]   = (int)strlen(names[i]);
	}
}

/* Append one row from a NULL-terminated list of cell strings.
 *
 * Every cell arrives already formatted. The alternative — a printf-style
 * variadic that formats each cell in turn — cannot be written portably,
 * because there is no way to ask `vsnprintf` how many arguments a format
 * consumed, and stepping over them by hand is undefined behaviour the moment
 * a cell is not pointer-sized.
 *
 * A failed allocation is recorded rather than returned: the caller is a
 * traversal of half a dozen tiers, and threading a status through every row
 * would bury it. `grid_render` reports the failure once.
 */
static void grid_row(Grid *grid, ...)
{
	va_list args;

	if (grid->failed)
		return;

	if (grid->row_count == grid->capacity) {
		size_t next   = grid->capacity ? grid->capacity * 2 : 16;
		char **bigger = realloc(grid->cells,
		                        next * grid->column_count * sizeof *bigger);

		if (!bigger) {
			grid->failed = true;
			return;
		}
		grid->cells    = bigger;
		grid->capacity = next;
	}

	char **row = &grid->cells[grid->row_count * grid->column_count];

	for (size_t i = 0; i < grid->column_count; i++)
		row[i] = NULL;

	va_start(args, grid);
	for (size_t i = 0; i < grid->column_count; i++) {
		CellText value = va_arg(args, CellText);
		int         length;

		if (!value) {
			grid->failed = true;
			break;
		}

		row[i] = strdup(value);
		if (!row[i]) {
			grid->failed = true;
			break;
		}

		length = (int)strlen(value);
		if (length > grid->width[i])
			grid->width[i] = length;
	}
	va_end(args);

	if (grid->failed) {
		for (size_t i = 0; i < grid->column_count; i++)
			free(row[i]);
		return;
	}

	grid->row_count++;
}

static void grid_free(Grid *grid)
{
	for (size_t r = 0; r < grid->row_count; r++)
		for (size_t c = 0; c < grid->column_count; c++)
			free(grid->cells[r * grid->column_count + c]);
	free(grid->cells);
	grid->cells     = NULL;
	grid->row_count = 0;
	grid->capacity  = 0;
}

static void rule(FILE *out, int width, char fill)
{
	for (int i = 0; i < width; i++)
		fputc(fill, out);
}

/* Emit the grid in the requested style, then release it.
 *
 * Both styles emit the heading, the column names, a rule, and every row —
 * including no rows at all. A heading with an empty body says "nothing
 * here"; an absent heading is indistinguishable from a renderer that forgot,
 * and would make the report's shape vary with its content.
 */
static int grid_render(Grid *grid, Style style, FILE *out)
{
	if (grid->failed) {
		grid_free(grid);
		return -1;
	}

	if (style == STYLE_MARKDOWN) {
		fprintf(out, "\n## %s\n\n", grid->heading);

		fputc('|', out);
		for (size_t c = 0; c < grid->column_count; c++)
			fprintf(out, " %-*s |", grid->width[c], grid->columns[c]);
		fputc('\n', out);

		fputc('|', out);
		for (size_t c = 0; c < grid->column_count; c++) {
			/* GFM marks a right-aligned column with a trailing
			 * colon, so a renderer downstream aligns numbers the
			 * way this one does. */
			fputc(' ', out);
			rule(out, grid->width[c] - (grid->numeric[c] ? 1 : 0), '-');
			fputs(grid->numeric[c] ? ": |" : " |", out);
		}
		fputc('\n', out);

		for (size_t r = 0; r < grid->row_count; r++) {
			fputc('|', out);
			for (size_t c = 0; c < grid->column_count; c++)
				fprintf(out, " %*s |",
				        grid->numeric[c] ? grid->width[c]
				                         : -grid->width[c],
				        grid->cells[r * grid->column_count + c]);
			fputc('\n', out);
		}
	} else {
		fprintf(out, "\n%s\n", grid->heading);

		fputs("  ", out);
		for (size_t c = 0; c < grid->column_count; c++) {
			if (c)
				fputs("  ", out);
			/* A left-aligned final column is not padded: padding it
			 * puts trailing whitespace on every line, which shows
			 * up in a diff and in any tool that strips it. */
			if (c + 1 == grid->column_count && !grid->numeric[c])
				fputs(grid->columns[c], out);
			else
				fprintf(out, "%*s",
				        grid->numeric[c] ? grid->width[c]
				                         : -grid->width[c],
				        grid->columns[c]);
		}
		fputc('\n', out);

		fputs("  ", out);
		for (size_t c = 0; c < grid->column_count; c++) {
			if (c)
				fputs("  ", out);
			rule(out, grid->width[c], '-');
		}
		fputc('\n', out);

		for (size_t r = 0; r < grid->row_count; r++) {
			const char *const *row =
				(const char *const *)&grid->cells[r * grid->column_count];

			fputs("  ", out);
			for (size_t c = 0; c < grid->column_count; c++) {
				if (c)
					fputs("  ", out);
				if (c + 1 == grid->column_count &&
				    !grid->numeric[c])
					fputs(row[c], out);
				else
					fprintf(out, "%*s",
					        grid->numeric[c] ? grid->width[c]
					                         : -grid->width[c],
					        row[c]);
			}
			fputc('\n', out);
		}
	}

	grid_free(grid);
	return 0;
}

static int width_of(uint64_t value)
{
	int digits = 1;

	while (value >= 10) {
		value /= 10;
		digits++;
	}
	return digits;
}

/* The project summary is a list of pairs rather than a table of rows, and
 * reads as one in both styles. */
static void summary_pair(FILE *out, Style style, int label, int value,
                         const char *name, uint64_t number)
{
	if (style == STYLE_MARKDOWN)
		fprintf(out, "| %-*s | %*" PRIu64 " |\n", label, name, value,
		        number);
	else
		fprintf(out, "  %-*s  %*" PRIu64 "\n", label, name, value,
		        number);
}

static void summary_section(const Report *report, Style style, FILE *out)
{
	const ProjectSummary *sum   = &report->summary;
	const int             label = (int)strlen("Physical lines");
	int                   value = width_of(sum->physical_lines);

	if (width_of(sum->eloc) > value)
		value = width_of(sum->eloc);
	if (width_of(sum->function_count) > value)
		value = width_of(sum->function_count);
	if (width_of((uint64_t)sum->file_count) > value)
		value = width_of((uint64_t)sum->file_count);

	if (style == STYLE_MARKDOWN) {
		fputs("\n## Project summary\n\n", out);
		fprintf(out, "| %-*s | %*s |\n", label, "Metric", value, "Value");
		fputc('|', out);
		rule(out, label + 2, '-');
		fputc('|', out);
		rule(out, value + 1, '-');
		fputs(": |\n", out);
	} else {
		fputs("Project summary\n", out);
	}

	summary_pair(out, style, label, value, "Files",
	             (uint64_t)sum->file_count);
	summary_pair(out, style, label, value, "Physical lines",
	             sum->physical_lines);
	summary_pair(out, style, label, value, "ELOC", sum->eloc);
	summary_pair(out, style, label, value, "Functions",
	             sum->function_count);
	summary_pair(out, style, label, value, "Skipped",
	             (uint64_t)report->skipped_files.count);
}

/* -------------------------------------------------------- the traversal --
 *
 * One walk of the model, emitting every tier the uniform-composition rule
 * requires, in a fixed order (HLR-031, LLR-SUM-01, LLR-SUM-02).
 */
int render_report(const Report *report, Style style, FILE *out)
{
	Grid grid;
	char a[32], b[32], c[32];

	summary_section(report, style, out);

	{
		static const char *const names[]   = { "What", "Value", "Where" };
		static const bool        numeric[] = { false, true, false };
		const ProjectSummary    *sum       = &report->summary;
		char                     where[2048];

		grid_begin(&grid, "Callouts", 3, names, numeric);
		if (sum->largest_file) {
			snprintf(a, sizeof a, "%" PRIu32, sum->largest_file_eloc);
			grid_row(&grid, "Largest file", a, sum->largest_file);
		}
		if (sum->most_complex) {
			snprintf(a, sizeof a, "%" PRIu32, sum->most_complex_value);
			snprintf(where, sizeof where, "%s in %s",
			         sum->most_complex, sum->most_complex_file);
			grid_row(&grid, "Most complex", a, where);
		}
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
		static const char *const names[]   = { "Target", "Route" };

		grid_begin(&grid, "Discovery", 2, names, NULL);
		for (size_t i = 0; i < report->routes.count; i++)
			grid_row(&grid, report->routes.items[i].target,
			         report->routes.items[i].route == ROUTE_REPOSITORY
			                 ? "repository" : "filesystem");
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
		static const char *const names[]   = { "Language", "Files",
		                                       "Lines", "ELOC" };
		static const bool        numeric[] = { false, true, true, true };

		grid_begin(&grid, "Languages", 4, names, numeric);
		for (size_t i = 0; i < report->languages.count; i++) {
			const LanguageTotals *l = &report->languages.items[i];

			snprintf(a, sizeof a, "%zu", l->file_count);
			snprintf(b, sizeof b, "%" PRIu64, l->physical_lines);
			snprintf(c, sizeof c, "%" PRIu64, l->eloc);
			grid_row(&grid, l->language, a, b, c);
		}
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
		static const char *const names[]   = { "File", "Language",
		                                       "Lines", "ELOC",
		                                       "Functions" };
		static const bool        numeric[] = { false, false, true,
		                                       true, true };

		grid_begin(&grid, "Files", 5, names, numeric);
		for (size_t i = 0; i < report->file_count; i++) {
			const FileMetrics *f = report->files[i];

			snprintf(a, sizeof a, "%" PRIu32, f->physical_lines);
			snprintf(b, sizeof b, "%" PRIu32, f->eloc);
			snprintf(c, sizeof c, "%zu", f->function_count);
			grid_row(&grid, f->path, f->language ? f->language : "",
			         a, b, c);
		}
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
		static const char *const names[]   = { "File", "Function",
		                                       "Lines", "ELOC",
		                                       "Complexity" };
		static const bool        numeric[] = { false, false, true,
		                                       true, true };

		grid_begin(&grid, "Functions", 5, names, numeric);
		for (size_t i = 0; i < report->file_count; i++) {
			const FileMetrics *f = report->files[i];

			for (size_t j = 0; j < f->function_count; j++) {
				const FunctionMetric *fn = &f->functions[j];

				snprintf(a, sizeof a, "%" PRIu32 "-%" PRIu32,
				         fn->start_line, fn->end_line);
				snprintf(b, sizeof b, "%" PRIu32, fn->eloc);
				snprintf(c, sizeof c, "%" PRIu32, fn->complexity);
				grid_row(&grid, f->path, fn->name, a, b, c);
			}
		}
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
		static const char *const names[]   = { "File", "Function",
		                                       "Complexity" };
		static const bool        numeric[] = { false, false, true };
		char                     heading[64];

		snprintf(heading, sizeof heading,
		         "At or over the complexity threshold (%" PRIu32 ")",
		         report->complexity_threshold);

		grid_begin(&grid, heading, 3, names, numeric);
		for (size_t i = 0; i < report->over_threshold.count; i++) {
			const ThresholdEntry *e = &report->over_threshold.items[i];

			snprintf(a, sizeof a, "%" PRIu32, e->function->complexity);
			grid_row(&grid, e->file, e->function->name, a);
		}
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
		static const char *const names[] = { "File" };

		grid_begin(&grid, "Skipped files (no language module)", 1,
		           names, NULL);
		for (size_t i = 0; i < report->skipped_files.count; i++)
			grid_row(&grid, report->skipped_files.paths[i]);
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	if (fflush(out) != 0 || ferror(out))
		return -1;

	return 0;
}

int format_table(const Report *report, FILE *out)
{
	return render_report(report, STYLE_TABLE, out);
}

int format_markdown(const Report *report, FILE *out)
{
	return render_report(report, STYLE_MARKDOWN, out);
}
