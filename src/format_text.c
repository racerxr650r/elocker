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

#include "arch.h"
#include "format_dsm.h"
#include "thresholds.h"
#include "format_text.h"
#include "report.h"
#include "preproc.h"

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

/* The headings of the tables a run had nothing to put in.
 *
 * A table with no rows is not printed (HLR-188), and something has to say so
 * or the report would be silent about the difference between a measurement
 * that found nothing and one that was never taken. The closing statement of
 * HLR-189 names them, and it names them by the *full* heading — which is what
 * keeps an analysis omitted for want of a `--stratum` or `--scope`
 * declaration stating its reason once the heading itself is no longer printed
 * (HLR-115).
 *
 * The headings are copied. Several sections build theirs into a local buffer
 * carrying a threshold or a count, and a borrowed pointer into one of those
 * would dangle by the time the statement is written.
 */
typedef struct {
	char  **headings;   /* owned, in the order the sections were walked */
	size_t  count;
	size_t  capacity;
	bool    failed;     /* an allocation gave out; reported once        */
} EmptyTables;

static void empty_tables_add(EmptyTables *empty, const char *heading)
{
	if (!empty || empty->failed)
		return;

	if (empty->count == empty->capacity) {
		size_t  next   = empty->capacity ? empty->capacity * 2 : 16;
		char  **bigger = realloc(empty->headings,
		                         next * sizeof *bigger);

		if (!bigger) {
			empty->failed = true;
			return;
		}
		empty->headings = bigger;
		empty->capacity = next;
	}

	empty->headings[empty->count] = strdup(heading);
	if (!empty->headings[empty->count]) {
		empty->failed = true;
		return;
	}
	empty->count++;
}

static void empty_tables_free(EmptyTables *empty)
{
	for (size_t i = 0; i < empty->count; i++)
		free(empty->headings[i]);
	free(empty->headings);
	memset(empty, 0, sizeof *empty);
}

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

static void grid_rule(FILE *out, int width, char fill)
{
	for (int i = 0; i < width; i++)
		fputc(fill, out);
}

/* One Markdown cell, right-aligned where the column holds numbers. */
static void grid_markdown_cell(const Grid *grid, size_t c, const char *text,
                          FILE *out)
{
	fprintf(out, " %*s |",
	        grid->numeric[c] ? grid->width[c] : -grid->width[c], text);
}

/* Open a Markdown table behind a disclosure element.
 *
 * The heading stays a real `##` heading, and the table goes inside a
 * `<details>` beneath it (HLR-190). Both halves of that are deliberate. The
 * heading is what anchors a section — GitHub derives a link target from it,
 * the table of contents is built out of it, and the report's own tests read
 * the composition off it — so folding it into the `<summary>` would trade a
 * navigable document for a tidy one. The summary therefore says what is
 * *inside* rather than repeating the name above it, which is the one thing a
 * reader deciding whether to expand actually wants to know.
 *
 * The blank line after `</summary>` is load-bearing: GitHub-Flavored Markdown
 * parses the contents of a `<details>` as Markdown only where a blank line
 * separates them from the HTML, and without it the table renders as its own
 * source text (HLR-029).
 */
static void markdown_disclosure_open(const Grid *grid, FILE *out)
{
	fprintf(out, "\n## %s\n\n", grid->heading);
	fprintf(out, "<details>\n<summary>%zu row%s (click to expand)"
	             "</summary>\n\n",
	        grid->row_count, grid->row_count == 1 ? "" : "s");
}

static void grid_render_markdown(const Grid *grid, FILE *out)
{
	markdown_disclosure_open(grid, out);

	fputc('|', out);
	for (size_t c = 0; c < grid->column_count; c++)
		fprintf(out, " %-*s |", grid->width[c], grid->columns[c]);
	fputc('\n', out);

	fputc('|', out);
	for (size_t c = 0; c < grid->column_count; c++) {
		/* GFM marks a right-aligned column with a trailing colon, so a
		 * renderer downstream aligns numbers the way this one does. */
		fputc(' ', out);
		grid_rule(out, grid->width[c] - (grid->numeric[c] ? 1 : 0), '-');
		fputs(grid->numeric[c] ? ": |" : " |", out);
	}
	fputc('\n', out);

	for (size_t r = 0; r < grid->row_count; r++) {
		fputc('|', out);
		for (size_t c = 0; c < grid->column_count; c++)
			grid_markdown_cell(grid, c,
			              grid->cells[r * grid->column_count + c],
			              out);
		fputc('\n', out);
	}

	/* The blank line before the close is the counterpart of the one after
	 * `<summary>`, and is needed for the same reason. */
	fputs("\n</details>\n", out);
}

/* One plain-table cell, with the two spaces that separate it from the one
 * before.
 *
 * A left-aligned final column is not padded: padding it puts trailing
 * whitespace on every line, which shows up in a diff and in any tool that
 * strips it.
 */
static void table_cell(const Grid *grid, size_t c, const char *text, FILE *out)
{
	bool unpadded = c + 1 == grid->column_count && !grid->numeric[c];

	/* An empty final cell contributes nothing at all — not even the
	 * separator that would precede it. Leaving the separator in put two
	 * spaces at the end of the line, which is the whole of what not
	 * padding the column was for: a row whose last column is blank is the
	 * common case in every table with an optional Finding column
	 * (LLR-SUM-04). */
	if (unpadded && text[0] == '\0')
		return;

	if (c)
		fputs("  ", out);

	if (unpadded)
		fputs(text, out);
	else
		fprintf(out, "%*s",
		        grid->numeric[c] ? grid->width[c] : -grid->width[c],
		        text);
}

static void grid_render_table(const Grid *grid, FILE *out)
{
	fprintf(out, "\n%s\n", grid->heading);

	fputs("  ", out);
	for (size_t c = 0; c < grid->column_count; c++)
		table_cell(grid, c, grid->columns[c], out);
	fputc('\n', out);

	fputs("  ", out);
	for (size_t c = 0; c < grid->column_count; c++) {
		if (c)
			fputs("  ", out);
		grid_rule(out, grid->width[c], '-');
	}
	fputc('\n', out);

	for (size_t r = 0; r < grid->row_count; r++) {
		fputs("  ", out);
		for (size_t c = 0; c < grid->column_count; c++)
			table_cell(grid, c,
			           grid->cells[r * grid->column_count + c],
			           out);
		fputc('\n', out);
	}
}

/* Emit the grid in the requested style, then release it.
 *
 * **A grid with no rows is not emitted at all** (HLR-188). The report used to
 * print the heading and the column names over an empty body, on the reasoning
 * that an absent heading is indistinguishable from a renderer that forgot.
 * Thirty sections in, that reasoning had inverted: a run over a healthy tree
 * printed a dozen headings with nothing under them, and the sections that did
 * have something to say were what got lost. So an empty grid records its
 * heading instead, and the closing statement names every one of them —
 * which answers the original objection directly, by saying in one place that
 * the section was rendered and found nothing (HLR-189).
 */
static int grid_render(Grid *grid, Style style, FILE *out, EmptyTables *empty)
{
	if (grid->failed) {
		grid_free(grid);
		return -1;
	}

	if (grid->row_count == 0) {
		empty_tables_add(empty, grid->heading);
		grid_free(grid);
		return 0;
	}

	if (style == STYLE_MARKDOWN)
		grid_render_markdown(grid, out);
	else
		grid_render_table(grid, out);

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

/* Lines the grammar could not follow, across every analysed file. */
static uint64_t unparsed_total(const Report *report)
{
	uint64_t total = 0;

	for (size_t i = 0; i < report->file_count; i++)
		total += report->files[i]->unparsed_lines;
	return total;
}

/* How many files reached the parser through the preprocessor. */
static uint64_t expanded_total(const Report *report)
{
	uint64_t n = 0;

	for (size_t i = 0; i < report->file_count; i++)
		n += (report->files[i]->preproc_status == PREPROC_EXPANDED);
	return n;
}

/* Findings carrying one severity. */
static uint64_t severity_total(const Report *report, const char *severity)
{
	uint64_t total = 0;

	for (size_t i = 0; i < report->finding_count; i++)
		if (strcmp(report->findings[i].severity, severity) == 0)
			total++;
	return total;
}

static void summary_section(const Report *report, Style style, FILE *out)
{
	const ProjectSummary *sum   = &report->summary;
	const int             label = (int)strlen("Physical lines");
	int                   value = width_of(sum->physical_lines);

	/* The figures, gathered before any are printed.
	 *
	 * Collected rather than emitted one call at a time so that the number
	 * of them is a property of this array — the disclosure summary below
	 * states a row count, and a count written down separately from the
	 * rows it counts is a count that drifts (HLR-190).
	 */
	const struct {
		const char *name;
		uint64_t    value;
	} rows[] = {
		{ "Files",          (uint64_t)sum->file_count },
		{ "Physical lines", sum->physical_lines },
		{ "ELOC",           sum->eloc },
		{ "Functions",      sum->function_count },
		{ "Skipped",        (uint64_t)report->skipped_files.count },
		/* Beside the totals it qualifies, not buried below them. Every
		 * figure above covers the file *minus* these lines, and a
		 * reader comparing ELOC against a line count of their own needs
		 * to know that before they start looking for the discrepancy
		 * (HLR-035). */
		{ "Unparsed lines", unparsed_total(report) },
		/* Counted in the summary so the shape of the run is visible
		 * before the tables. A severity is a label and moves no exit
		 * status, so these are figures to read rather than gates to
		 * pass (HLR-100). */
		{ "Critical findings", severity_total(report, "critical") },
		{ "Warnings",          severity_total(report, "warning") },
		/* Not a failure and not a defect — a measure of how complete
		 * the graph is. A project calling into libc has unresolved
		 * calls by definition, and a reader comparing fan-out against
		 * the source needs to know how many calls the graph could not
		 * represent (HLR-077). */
		{ "Unresolved calls", (uint64_t)report->unresolved_calls },
		/* The completeness of the pruning, stated for the reason the
		 * unresolved-call count is: a region elc could not decide is
		 * left whole and counted here, so a reader can tell a
		 * configuration that was cut cleanly from one that mostly was
		 * not (HLR-133). */
		{ "Undecided regions", report->undecided_regions },
		/* How this run's files reached the parser. Two files in one
		 * report may have been measured two different ways and nothing
		 * in the figures above says which: an expanded file's macros
		 * are resolved, a fallen-back file's are not, and its unparsed
		 * count may be non-zero for a reason that has nothing to do
		 * with the code (HLR-206). */
		{ "Files expanded",    expanded_total(report) },
		{ "Measured as written", (uint64_t)report->file_count -
		                         expanded_total(report) },
	};
	const size_t row_count = sizeof rows / sizeof *rows;

	if (width_of(sum->eloc) > value)
		value = width_of(sum->eloc);
	if (width_of(sum->function_count) > value)
		value = width_of(sum->function_count);
	if (width_of((uint64_t)sum->file_count) > value)
		value = width_of((uint64_t)sum->file_count);

	if (style == STYLE_MARKDOWN) {
		fputs("\n## Project summary\n\n", out);
		fprintf(out, "<details>\n<summary>%zu rows (click to expand)"
		             "</summary>\n\n", row_count);
		fprintf(out, "| %-*s | %*s |\n", label, "Metric", value, "Value");
		fputc('|', out);
		grid_rule(out, label + 2, '-');
		fputc('|', out);
		grid_rule(out, value + 1, '-');
		fputs(": |\n", out);
	} else {
		fputs("Project summary\n", out);
	}

	for (size_t i = 0; i < row_count; i++)
		summary_pair(out, style, label, value, rows[i].name,
		             rows[i].value);

	if (style == STYLE_MARKDOWN)
		fputs("\n</details>\n", out);
}

/* -------------------------------------------------------- the traversal --
 *
 * One walk of the model, emitting every tier the uniform-composition rule
 * requires, in a fixed order (HLR-031, LLR-SUM-01, LLR-SUM-02).
 */
/* One function per section of the report, and a traversal that calls them in
 * order.
 *
 * This was one function of 668 lines and a cyclomatic complexity of 83 — a
 * sequence of brace-delimited blocks, each declaring its own column names,
 * filling a grid and rendering it. The blocks were already the sections; only
 * the braces around them said so, and nothing prevented one reaching into
 * another's variables.
 *
 * Splitting them changes neither the order they run in nor what they emit,
 * which is what LLR-SUM-02 and LLR-SUM-03 require: both human-facing formats
 * still come from this one traversal, so a section cannot appear in the table
 * and be forgotten in the Markdown. What changes is that a section is now a
 * thing with a name, and the traversal is a list of them.
 */
static int callouts_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

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
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int discovery_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	static const char *const names[]   = { "Target", "Route" };

	grid_begin(&grid, "Discovery", 2, names, NULL);
	for (size_t i = 0; i < report->routes.count; i++)
		grid_row(&grid, report->routes.items[i].target,
		         report->routes.items[i].route == ROUTE_REPOSITORY
		                 ? "repository" : "filesystem");
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int languages_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];
	char b[32];
	char c[32];

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
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int files_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];
	char b[32];
	char c[32];

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
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

/* Every function, with everything measured about it on the one line.
 *
 * **One table where there were three.** `Functions` carried the lengths and
 * the complexity, `Fan-out (distinct callees)` the out-degree, and
 * `Information flow` the pair of degrees with a Henry-Kafura value formed
 * from them — three tables enumerating the same functions in the same order,
 * which is three chances to disagree about which functions exist and three
 * places a reader had to look to answer one question about one function
 * (HLR-183).
 *
 * Driven by the file metrics rather than by the flow rows, and that is the
 * load-bearing choice. The flow rows exist only where a graph was built; the
 * functions exist because they were parsed. A table driven by the rows would
 * report no functions at all on a run whose graph was not built, which is
 * exactly the run whose per-function figures a reader still wants
 * (HLR-014, HLR-015, HLR-017).
 *
 * No severity column. Which functions a band names is the threshold
 * listing's subject and the findings' — this table is the measurements, and
 * a severity repeated in three places is a severity that can differ between
 * them.
 */
static int functions_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];
	char b[32];
	char c[32];
	char d[32];
	char e[32];
	char f2[32];

	static const char *const names[]   = { "File", "Function",
	                                       "Lines", "ELOC",
	                                       "Complexity", "Fan-in",
	                                       "Fan-out", "MI" };
	static const bool        numeric[] = { false, false, true,
	                                       true, true, true, true,
	                                       true };

	grid_begin(&grid, "Functions", 8, names, numeric);
	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		for (size_t j = 0; j < f->function_count; j++) {
			const FunctionMetric *fn = &f->functions[j];

			snprintf(a, sizeof a, "%" PRIu32 "-%" PRIu32,
			         fn->start_line, fn->end_line);
			snprintf(b, sizeof b, "%" PRIu32, fn->eloc);
			snprintf(c, sizeof c, "%" PRIu32, fn->complexity);
			snprintf(d, sizeof d, "%" PRIu32, fn->fan_in);
			snprintf(e, sizeof e, "%" PRIu32, fn->fan_out);
			snprintf(f2, sizeof f2, "%" PRIu32, fn->mi);
			grid_row(&grid, f->path, fn->name, a, b, c, d, e, f2);
		}
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

/* Every function a threshold named, and why.
 *
 * Two rules, one list (HLR-187). A function whose complexity met the value
 * `--complexity-threshold` sets is here because the user asked to see it,
 * and carries no severity — that threshold governs a listing and has never
 * moved anything else (HLR-021, HLR-023). A function whose complexity,
 * fan-in or fan-out fell in a warning or critical band is here because a
 * threshold put it here, and carries the higher of the bands that named it.
 *
 * The three measurements are repeated from the function table so the row can
 * be read without going back to it: a reader working down this list is
 * deciding which function to open next, and a severity with no figures beside
 * it does not help them choose.
 */
static int threshold_listing_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];
	char b[32];
	char c[32];
	char d[32];

	static const char *const names[]   = { "File", "Function",
	                                       "Complexity", "Fan-in",
	                                       "Fan-out", "MI", "Severity" };
	static const bool        numeric[] = { false, false, true, true,
	                                       true, true, false };
	char                     heading[160];

	snprintf(heading, sizeof heading,
	         "At or over a threshold (complexity listed at %" PRIu32
	         "; complexity, fan-in, fan-out and maintainability banded)",
	         report->complexity_threshold);

	grid_begin(&grid, heading, 7, names, numeric);
	for (size_t i = 0; i < report->over_threshold.count; i++) {
		const ThresholdEntry *e = &report->over_threshold.items[i];

		snprintf(a, sizeof a, "%" PRIu32, e->function->complexity);
		snprintf(b, sizeof b, "%" PRIu32, e->function->fan_in);
		snprintf(c, sizeof c, "%" PRIu32, e->function->fan_out);
		snprintf(d, sizeof d, "%" PRIu32, e->function->mi);
		/* Blank rather than "info" for a function present only
		 * because it met the listing threshold. `info` is a severity
		 * and this row has none: printing one would turn a listing
		 * the user configured into a finding elc reported
		 * (HLR-023, HLR-100). */
		grid_row(&grid, e->file, e->function->name, a, b, c, d,
		         e->severity == SEVERITY_INFO
		                 ? ""
		                 : severity_name(e->severity));
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int recursion_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	/* Recursion is presented as the cycle itself, not as a count:
	 * "two cycles" tells the reader nothing to act on, and the
	 * members are what MISRA C Rule 17.2 is about (HLR-089). */
	static const char *const names[] = { "Kind", "Functions" };

	grid_begin(&grid, "Recursion", 2, names, NULL);
	for (size_t i = 0; i < report->cycle_count; i++) {
		const CycleRow *c  = &report->cycles[i];
		char            buf[512];
		size_t          at = 0;

		/* Members, comma-separated — not joined with arrows.
		 * A strongly connected component is a *set*: every
		 * member can reach every other, but the decomposition
		 * does not yield an order, and "a -> b -> c" would
		 * assert a path that may not exist. The set is the
		 * true statement, and it is the one a reader needs:
		 * breaking any edge among these functions breaks the
		 * recursion. */
		buf[0] = '\0';
		for (size_t m = 0; m < c->count; m++) {
			int n = snprintf(buf + at, sizeof buf - at,
			                 "%s%s", m ? ", " : "",
			                 c->members[m]);

			if (n < 0 || (size_t)n >= sizeof buf - at)
				break;
			at += (size_t)n;
		}
		grid_row(&grid, c->count == 1 ? "direct" : "mutual",
		         buf);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int deepest_chain_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

	/* The depth and the chain that achieves it. Four outcomes, and
	 * the heading says which one happened: a reader who sees no
	 * number must not have to guess whether the analysis was
	 * omitted, unbounded, or simply zero (HLR-087, HLR-090,
	 * HLR-115). */
	static const char *const names[] = { "Step", "File", "Function" };
	char                     heading[192];

	switch (report->depth_state) {
	case DEPTH_MEASURED:
		snprintf(heading, sizeof heading,
		         "Deepest call chain (%" PRIu32 " layers; a lower "
		         "bound, %zu calls unresolved)",
		         report->depth, report->unresolved_calls);
		break;
	case DEPTH_UNBOUNDED_RECURSION:
		snprintf(heading, sizeof heading,
		         "Deepest call chain (unbounded: the call graph "
		         "is recursive)");
		break;
	case DEPTH_OMITTED_ENTRY_UNRESOLVED:
		snprintf(heading, sizeof heading,
		         "Deepest call chain (omitted: no declared entry "
		         "point matches an analysed function)");
		break;
	case DEPTH_OMITTED_NO_ENTRY_POINTS:
	default:
		snprintf(heading, sizeof heading,
		         "Deepest call chain (omitted: no entry points "
		         "declared, see --entry)");
		break;
	}

	grid_begin(&grid, heading, 3, names, NULL);
	for (size_t i = 0; i < report->deepest_count; i++) {
		const ChainRow *r = &report->deepest[i];

		snprintf(a, sizeof a, "%zu", i + 1);
		grid_row(&grid, a, r->file, r->function);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int coupling_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];
	char b[32];

	/* Coupling per component, with Instability beside it. The
	 * attribution sits in the heading rather than in a column of
	 * identical citations: it belongs to the metric, not to any one
	 * row (HLR-082, LLR-INS-03).
	 *
	 * Reported for every component whether or not anything crossed
	 * a line — a value inside its accepted band is still a
	 * measurement the reader asked for. */
	static const char *const names[]   = { "Component", "Ca", "Ce",
	                                       "Instability", "Finding" };
	static const bool        numeric[] = { false, true, true, true,
	                                       false };
	char                     heading[192];

	snprintf(heading, sizeof heading,
	         "Component coupling (I = Ce/(Ce+Ca), %s; bottleneck "
	         "at Ca and Ce >= %" PRIu32 ")",
	         threshold_attribution(MEASURE_INSTABILITY),
	         report->bottleneck_threshold);

	grid_begin(&grid, heading, 5, names, numeric);
	for (size_t i = 0; i < report->coupling_count; i++) {
		const CouplingRow *r = &report->coupling[i];

		snprintf(a, sizeof a, "%" PRIu32, r->ca);
		snprintf(b, sizeof b, "%" PRIu32, r->ce);
		grid_row(&grid, r->component, a, b, r->instability,
		         r->bottleneck
		                 ? threshold_attribution(MEASURE_BOTTLENECK)
		                 : "");
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int dependency_cycles_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	/* Circular dependencies between *components*, which is a
	 * different fact from recursion between functions: two
	 * mutually recursive functions in one file appear in the
	 * Recursion section above and not here, because a file does
	 * not depend on itself (HLR-083, HLR-114).
	 *
	 * Two columns because one alone misleads. The group is what
	 * has to be broken up; the loop is which edge to cut. */
	static const char *const names[] = { "Components",
	                                     "Example loop" };

	grid_begin(&grid, "Component dependency cycles", 2, names, NULL);
	for (size_t i = 0; i < report->dep_cycle_count; i++)
		grid_row(&grid, report->dep_cycles[i].components,
		         report->dep_cycles[i].path);
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int layering_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

	/* Skip-level and direction-inverted, in one table and as
	 * distinct kinds. A call ascending two layers appears twice,
	 * because both statements are true of it and each has its own
	 * remedy (HLR-079, HLR-118, LLR-LAY-03). */
	static const char *const names[] = { "Kind", "From", "Function",
	                                     "To", "Function",
	                                     "Layers" };
	static const bool        numeric[] = { false, false, false,
	                                       false, false, true };
	char                     heading[160];

	if (report->strata_state == STRATA_MEASURED)
		snprintf(heading, sizeof heading, "Layering (%zu)",
		         report->layering_count);
	else
		snprintf(heading, sizeof heading,
		         "Layering (omitted: no architectural strata "
		         "declared, see --stratum)");

	grid_begin(&grid, heading, 6, names, numeric);
	for (size_t i = 0; i < report->layering_count; i++) {
		const LayeringRow *r = &report->layering[i];

		snprintf(a, sizeof a, "%" PRIu32, r->layers_crossed);
		grid_row(&grid,
		         r->kind == LAYER_SKIP_LEVEL ? "skip-level"
		                                     : "inverted",
		         r->from_stratum, r->from_function,
		         r->to_stratum, r->to_function, a);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

/* The two conformance indices, and the matrix beneath them.
 *
 * A project-level aggregate rather than one row per anything, so it sits in
 * the summary tier beside the other figures a reader is given before the
 * tables (LLR-SUM-11). The matrix that follows it enumerates one row per
 * subject and is a detail tier by the same rule.
 *
 * **The two indices are never summed.** A call ascending two layers is a
 * back-call and a skip-level call at once and is counted once in each, so a
 * combined score would count twice exactly the call most worth acting on —
 * and would name no remedy, where each index separately names one (HLR-163,
 * LLR-LAY-04). They are presented as two rows for that reason, not as a
 * decomposition of a total.
 */
static int conformance_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

	static const char *const names[]   = { "Index", "Violating",
	                                       "Conforming", "Of" };
	static const bool        numeric[] = { false, true, true, true };
	char                     heading[224];

	/* Measured *and* rendered. The state alone is not enough: STRATA_MEASURED
	 * is the zero of its enum, so a model that carries no indices at all —
	 * a record written before they existed, or a report a test built by
	 * hand — reads as measured while holding nothing to print. A renderer
	 * is a pure consumer, so it prints what the model has rather than what
	 * the model's state implies it should have. */
	bool measured = report->strata_state == STRATA_MEASURED &&
	                report->back_call.index && report->back_call.conforming &&
	                report->skip_call.index && report->skip_call.conforming;

	if (measured)
		snprintf(heading, sizeof heading,
		         "Architecture conformance (over %" PRIu64
		         " inter-layer call edges; undefined where there "
		         "are none)", report->back_call.edges);
	else
		snprintf(heading, sizeof heading,
		         "Architecture conformance (omitted: no "
		         "architectural strata declared, see --stratum)");

	grid_begin(&grid, heading, 4, names, numeric);
	if (measured) {
		snprintf(a, sizeof a, "%" PRIu64, report->back_call.edges);
		grid_row(&grid, "Back-call", report->back_call.index,
		         report->back_call.conforming, a);
		snprintf(a, sizeof a, "%" PRIu64, report->skip_call.edges);
		grid_row(&grid, "Skip-call", report->skip_call.index,
		         report->skip_call.conforming, a);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

/* The matrix, in whichever decoration the traversal is wearing.
 *
 * One entry in the section table and two calls, so the tier cannot be present
 * in one human-facing format and absent from the other. It is rendered by
 * `format_dsm.c` rather than built into a Grid because its column count is the
 * number of subjects rather than a fixed few, and because its cells must
 * escape the Markdown separator (HLR-064, HLR-166).
 */
static int dsm_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	/* A matrix over no subjects has no cells, and the convention note and
	 * corner label that would be all that survived say nothing on their
	 * own. Named in the closing statement instead, with the heading the
	 * grid itself would have carried (HLR-188, HLR-189). */
	if (report->dsm.count == 0) {
		empty_tables_add(empty, format_dsm_heading(&report->dsm));
		return 0;
	}

	return style == STYLE_MARKDOWN
	               ? format_dsm_markdown(&report->dsm, out)
	               : format_dsm_table(&report->dsm, out);
}

/* What purification concluded, and what it did about it (HLR-174).
 *
 * **A report section, written to the results destination like every other
 * result.** Never to standard output directly: HLR-038 reserves that stream,
 * and a run redirecting its report to a file must not have a second report
 * appear on the terminal. There is no `printf` in this module for that reason —
 * every section writes to the stream it is handed.
 *
 * A detail tier by the partition rule of HLR-150: it enumerates one row per
 * classified function. The heading carries what a reader needs before the rows
 * mean anything — that the thresholds are `elc`'s own rather than published
 * (HLR-171), the five values in force, and what the masking left behind.
 *
 * **No severity column, and nothing to put in one.** A classification is an
 * observation about the shape of a graph, not a measurement banded against an
 * accepted range, and a column here would present `elc`'s own view as a finding
 * (HLR-171, HLR-101).
 */
static int purification_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	static const char *const names[] = { "File", "Function", "Class",
	                                     "Metric", "Value", "Action",
	                                     "Source" };
	char                     heading[512];

	snprintf(heading, sizeof heading,
	         "Graph purification (recovery view only, no measurement above "
	         "is taken over it; %s: sink at authority >= %" PRIu32
	         "%% and hub <= %" PRIu32 "%%, god object at betweenness >= %"
	         PRIu32 "%% and hub >= %" PRIu32 "%%, peripheral below core "
	         "depth %" PRIu32 "; %zu functions retained, %zu call edges "
	         "masked)",
	         ELC_OWN_HEURISTIC,
	         report->purify_thresholds.sink_authority,
	         report->purify_thresholds.sink_hub,
	         report->purify_thresholds.god_betweenness,
	         report->purify_thresholds.god_hub,
	         report->purify_thresholds.core_depth,
	         report->purified_nodes, report->purified_edges);

	grid_begin(&grid, heading, 7, names, NULL);
	for (size_t i = 0; i < report->purification_count; i++) {
		const PurificationRow *r = &report->purification[i];

		grid_row(&grid, r->file, r->function, r->class_name,
		         r->metric, r->value, r->action, r->source);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

/* The layering read off the purified view, and how to adopt it (HLR-172,
 * HLR-173).
 *
 * **A proposal, and never a baseline.** Nothing above this section was
 * measured against these rows: the conformance analyses take their layer index
 * from the declared strata of HLR-078 and from nothing else, and where none
 * were declared they stay omitted with their reason stated whatever was
 * recovered here (HLR-115). The heading says so, because a table of layers
 * printed under an architecture report is otherwise easy to read as a verdict.
 *
 * **Nothing here is ranked or scored.** The section states the order the graph
 * already has; it does not say the design is wrong, name a directory as
 * belonging elsewhere, or compare what was found against what was declared —
 * each of which would cross from describing a structure into prescribing one
 * (HLR-101).
 *
 * The `Layer` column holds an ordinal where a layering was proposed and the
 * word `cycle` where one could not be, since a cycle is what is reported in
 * place of an ordering rather than beside it (HLR-172).
 */
static int recovery_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid                     grid;
	Grid                     adopt;
	static const char *const names[] = { "Layer", "Directory or cycle",
	                                     "Functions" };
	static const bool        numeric[] = { false, false, true };
	static const char *const adopt_names[] = { "Adopt with" };
	char                     heading[384];
	char                     count[24];

	switch (report->recovery_state) {
	case RECOVERY_PROPOSED:
		snprintf(heading, sizeof heading,
		         "Architecture recovery (a proposal, never the baseline "
		         "conformance is measured against; %zu layers over %zu "
		         "directories, %zu functions masked and %zu excluded)",
		         report->recovery_strata, report->recovery_count,
		         report->recovery_masked, report->recovery_excluded);
		break;
	case RECOVERY_CYCLIC:
		snprintf(heading, sizeof heading,
		         "Architecture recovery (omitted: the recovery view is "
		         "cyclic, so no ordering exists; the mutually reachable "
		         "groups below are reported in its place)");
		break;
	case RECOVERY_OMITTED_EMPTY:
	default:
		snprintf(heading, sizeof heading,
		         "Architecture recovery (omitted: no function survived "
		         "purification, so there is nothing to order)");
		break;
	}

	grid_begin(&grid, heading, 3, names, numeric);
	for (size_t i = 0; i < report->recovery_count; i++) {
		char layer[24];

		snprintf(layer, sizeof layer, "%zu",
		         report->recovery[i].layer);
		snprintf(count, sizeof count, "%zu",
		         report->recovery[i].functions);
		grid_row(&grid, layer, report->recovery[i].directory, count);
	}
	for (size_t i = 0; i < report->recovery_cycles.count; i++)
		grid_row(&grid, "cycle", report->recovery_cycles.paths[i], "");
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	/* **The proposal as arguments, not as prose** (HLR-173). Rendering it
	 * in the form `--stratum` and `--stratum-order` accept is what makes
	 * adoption a copy rather than a transcription — and it is the boundary
	 * the requirement draws made visible: `elc` produces an argument list,
	 * and it takes effect only when the user passes it back. */
	grid_begin(&adopt,
	           "Architecture recovery — the proposal as arguments (elc "
	           "never applies it; passing it back is what declares it)",
	           1, adopt_names, NULL);
	if (report->recovery_proposal)
		grid_row(&adopt, report->recovery_proposal);
	if (grid_render(&adopt, style, out, empty) != 0)
		return -1;

	return 0;
}

static int global_state_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	/* Every global object, with the functions that write it and
	 * the functions that read it, and the verdict on the pair
	 * (HLR-091 – HLR-093). Reported whether or not anything
	 * crossed a line: a value inside its accepted band is still a
	 * measurement the reader asked for.
	 *
	 * The finding travels with its attribution, so a reader can
	 * see what published rule the judgement rests on rather than
	 * taking elc's word for it (HLR-099, LLR-GLB-04). */
	static const char *const names[] = { "Object", "Writers",
	                                     "Readers", "Finding" };

	grid_begin(&grid, "Global state", 4, names, NULL);
	for (size_t i = 0; i < report->global_state_count; i++) {
		const GlobalStateRow *r     = &report->global_state[i];
		const char           *where =
			global_verdict_attribution(r->verdict);
		char                  finding[1024];

		switch (r->verdict) {
		case GLOBAL_SCOPE_REDUCTION:
			snprintf(finding, sizeof finding,
			         "scope reduction — one function names "
			         "it (%s)", where);
			break;
		case GLOBAL_HIDDEN_CHANNEL:
			snprintf(finding, sizeof finding,
			         "hidden channel — %s never call each "
			         "other (%s)", r->participants, where);
			break;
		case GLOBAL_ORDINARY:
		default:
			finding[0] = '\0';
			break;
		}
		grid_row(&grid, r->object, r->writers, r->readers,
		         finding);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int unreachable_functions_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

	/* The headline claim, and the heading says on what basis it
	 * was or was not made. With no entry points declared nothing
	 * is listed here — and the heading says *that*, rather than
	 * leaving an empty table that reads as a clean bill of health
	 * (HLR-096, HLR-115). */
	static const char *const names[]   = { "File", "Function",
	                                       "Line" };
	static const bool        numeric[] = { false, false, true };
	char                     heading[192];

	switch (report->reach_state) {
	case REACH_MEASURED:
		snprintf(heading, sizeof heading,
		         "Unreachable functions (%zu; from the declared "
		         "entry points and every address-taken "
		         "function)", report->unreachable_count);
		break;
	case REACH_OMITTED_ENTRY_UNRESOLVED:
		snprintf(heading, sizeof heading,
		         "Unreachable functions (omitted: no declared "
		         "entry point matches an analysed function)");
		break;
	case REACH_OMITTED_NO_ENTRY_POINTS:
	default:
		snprintf(heading, sizeof heading,
		         "Unreachable functions (omitted: no entry "
		         "points declared, see --entry)");
		break;
	}

	grid_begin(&grid, heading, 3, names, numeric);
	for (size_t i = 0; i < report->unreachable_count; i++) {
		const UnreachableRow *r = &report->unreachable[i];

		snprintf(a, sizeof a, "%" PRIu32, r->line);
		grid_row(&grid, r->file, r->function, a);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int unreachable_globals_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	/* Data the same traversal condemned: an object every one of
	 * whose accessing functions is itself unreachable (HLR-096). */
	static const char *const names[] = { "Object" };

	grid_begin(&grid,
	           "Unreachable globals (touched only by unreachable "
	           "functions)", 1, names, NULL);
	for (size_t i = 0; i < report->unreachable_global_count; i++)
		grid_row(&grid, report->unreachable_globals[i]);
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int dead_code_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

	/* The other dead-code question, answered by a different means
	 * against a different scope. A function may be perfectly
	 * reachable and still contain statements that are not, so
	 * neither analysis subsumes the other and both are reported
	 * (HLR-137, LLR-DED-06).
	 *
	 * The heading names the languages the analysis was *not*
	 * performed for. An empty table under a language with no
	 * dead-code query would otherwise read as a clean file, which
	 * is a claim elc has not made (HLR-139). */
	static const char *const names[] = { "File", "Function",
	                                     "Lines", "Cause" };
	char                     heading[512];
	char                     langs[256];
	size_t                   at = 0;

	langs[0] = '\0';
	for (size_t i = 0; i < report->dead_unanalysed.count; i++) {
		int n = snprintf(langs + at, sizeof langs - at, "%s%s",
		                 i ? ", " : "",
		                 report->dead_unanalysed.paths[i]);

		if (n < 0 || (size_t)n >= sizeof langs - at)
			break;
		at += (size_t)n;
	}

	if (report->dead_unanalysed.count == 0)
		snprintf(heading, sizeof heading,
		         "Dead code within functions (every language "
		         "analysed)");
	else
		snprintf(heading, sizeof heading,
		         "Dead code within functions (not analysed "
		         "for: %s)", langs);

	grid_begin(&grid, heading, 4, names, NULL);
	for (size_t i = 0; i < report->dead_count; i++) {
		const DeadRow *r = &report->dead[i];

		snprintf(a, sizeof a, "%" PRIu32 "-%" PRIu32,
		         r->start_line, r->end_line);
		grid_row(&grid, r->file, r->function, a,
		         r->cause == DEAD_LITERAL_CONDITION
		                 ? "literal condition"
		                 : "after a terminator");
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int cross_scope_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	/* Every call and every shared global by which one declared
	 * execution scope reaches another. Both kinds, because a scope
	 * that never calls into another but writes a variable the
	 * other reads has not been isolated (HLR-094). */
	static const char *const names[] = { "From", "Function", "To",
	                                     "Function", "Via" };
	char                     heading[160];

	if (report->scope_state == SCOPES_MEASURED)
		snprintf(heading, sizeof heading,
		         "Cross-scope access (%zu)",
		         report->cross_scope_count);
	else
		snprintf(heading, sizeof heading,
		         "Cross-scope access (omitted: no execution "
		         "scopes declared, see --scope)");

	grid_begin(&grid, heading, 5, names, NULL);
	for (size_t i = 0; i < report->cross_scope_count; i++) {
		const CrossScopeRow *r = &report->cross_scope[i];

		grid_row(&grid, r->from_scope, r->from_function,
		         r->to_scope, r->to_function,
		         r->object && *r->object ? r->object : "call");
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int findings_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	/* Every measurement that crossed a published line, ranked most
	 * severe first, each naming the source that draws the line.
	 *
	 * **Additional to the tables above, never a replacement for
	 * them.** A measurement inside its accepted band is still
	 * reported where it was measured (HLR-031); this section is
	 * the subset a reader acts on, and its emptiness is a result
	 * rather than an absence of information.
	 *
	 * No row advises. Each says what was measured, where, and
	 * which standard places it outside the range — and stops
	 * (HLR-101). */
	static const char *const names[]   = { "Severity", "Measurement",
	                                       "Subject", "Detail",
	                                       "Source" };

	grid_begin(&grid, "Findings", 5, names, NULL);
	for (size_t i = 0; i < report->finding_count; i++) {
		const FindingRow *r = &report->findings[i];

		grid_row(&grid, r->severity, r->measurement, r->subject,
		         r->detail, r->source);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int definitions_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	/* The configuration these figures describe.
	 *
	 * A section rather than a summary line, because a definition is
	 * a string and there may be several. Emitted whether or not any
	 * was supplied: "measured with no definitions" and "measured
	 * with these" are different claims, and a reader of a report
	 * that showed nothing could not tell which they had
	 * (HLR-031, HLR-136). */
	static const char *const names[] = { "Definition" };
	char                     heading[96];

	snprintf(heading, sizeof heading,
	         "Conditional-compilation definitions (%zu)",
	         report->definition_count);
	grid_begin(&grid, heading, 1, names, NULL);
	for (size_t i = 0; i < report->definition_count; i++)
		grid_row(&grid, report->definitions[i]);
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int rule_matches_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

	/* What the user's own rules matched.
	 *
	 * **Beside the findings and deliberately not among them.** A
	 * finding is a measurement `elc` banded against a published
	 * threshold and can name the source that draws the line. A rule
	 * match is a query somebody else wrote, and `elc` has no view
	 * about whether it was worth writing — so there is no severity
	 * column here and no source column, because there is nothing
	 * honest to put in either (HLR-109, HLR-111).
	 *
	 * Emitted whether or not any rule was supplied, like every
	 * other section: an absent section and an empty one are
	 * different claims (HLR-031). */
	static const char *const names[] = { "Rule", "File", "Lines" };

	snprintf(a, sizeof a, "Custom rule matches (%zu)",
	         report->rule_match_count);
	grid_begin(&grid, a, 3, names, NULL);
	for (size_t i = 0; i < report->rule_match_count; i++) {
		const RuleMatchRow *r = &report->rule_matches[i];
		char                lines[32];

		snprintf(lines, sizeof lines, "%" PRIu32 "-%" PRIu32,
		         r->start_line, r->end_line);
		grid_row(&grid, r->rule, r->file, lines);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int partially_parsed_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

	/* The files whose measurements are partial, and by how much.
	 * A file here *is* measured — its functions appear in every
	 * table above — and this says how much of it the grammar could
	 * not follow, so a partial figure is never mistaken for a
	 * complete one (HLR-035).
	 *
	 * A section rather than a column on the Files table: the
	 * number is zero for almost every file in almost every
	 * project, and a column of zeros hides the rows that matter. */
	static const char *const names[]   = { "File", "Unparsed lines" };
	static const bool        numeric[] = { false, true };

	grid_begin(&grid,
	           "Partially parsed files (measured except for these "
	           "lines)", 2, names, numeric);
	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		if (!f->unparsed_lines)
			continue;
		snprintf(a, sizeof a, "%" PRIu32, f->unparsed_lines);
		grid_row(&grid, f->path, a);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int expansion_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	/* The files measured from their own text, and why the expansion did
	 * not happen (HLR-206).
	 *
	 * Only the fallbacks are listed. On a tree the host toolchain cannot
	 * preprocess at all every file falls back, and a row per file would
	 * be a table the length of the project saying one thing — but the
	 * summary counts both, so "all of them" is still legible.
	 *
	 * A figure to read, not a gate: no severity, and no reach into the
	 * exit status (HLR-100). */
	static const char *const names[] = { "File", "Why" };

	grid_begin(&grid, "Measured as written (macros not expanded)", 2,
	           names, NULL);
	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		if (f->preproc_status == PREPROC_EXPANDED)
			continue;
		grid_row(&grid, f->path,
		         preproc_status_text((PreprocStatus)f->preproc_status));
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int stdlib_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

	/* Which standard library each file draws on, and how much of it
	 * (HLR-207).
	 *
	 * A fact rather than a finding. Depending on the standard library is
	 * the ordinary case for most programs, and elc recommends nothing
	 * about it (HLR-101). What it answers is the question no other
	 * measurement here does — what it would take to build this somewhere
	 * else — which a freestanding or embedded target makes urgent and
	 * everyone else can skip.
	 *
	 * A file that fell back contributes no row, and that is not a claim
	 * that it uses none: the provenance table above is what says which
	 * files could be asked. */
	static const char *const names[]   = { "File", "Library", "Headers",
	                                       "Which" };
	static const bool        numeric[] = { false, false, true, false };
	char                     list[512];

	grid_begin(&grid, "Standard-library dependence", 4, names, numeric);
	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		for (int k = 0; k < STDLIB_KIND_COUNT; k++) {
			size_t n   = 0;
			size_t off = 0;

			list[0] = '\0';
			for (size_t j = 0; j < f->stdlib_count; j++) {
				if (f->stdlib_kinds[j] != (unsigned char)k)
					continue;
				n++;
				if (off < sizeof list - 1)
					off += (size_t)snprintf(
						list + off, sizeof list - off,
						"%s%s", off ? " " : "",
						f->stdlib_headers[j]);
			}
			if (!n)
				continue;
			snprintf(a, sizeof a, "%zu", n);
			grid_row(&grid, f->path,
			         stdlib_kind_name((StdlibKind)k), a, list);
		}
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int skipped_files_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;

	static const char *const names[] = { "File" };

	grid_begin(&grid, "Skipped files (no language module)", 1,
	           names, NULL);
	for (size_t i = 0; i < report->skipped_files.count; i++)
		grid_row(&grid, report->skipped_files.paths[i]);
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

static int image_filter_section(const Report *report, Style style,
                             FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];
	char b[32];
	char c[32];
	char d[32];

	/* The linked image these figures describe.
	 *
	 * **Emitted only for a filtered run**, which is the one place
	 * the uniform-composition rule gives way and does so by
	 * requirement rather than by preference: HLR-140 says a run
	 * without the option reports exactly what it reported before
	 * the option existed, and an empty section is not nothing.
	 *
	 * The two counts are different claims and are labelled as
	 * such. The unresolved count states how complete the filter
	 * is, as the unresolved-call count states how complete the
	 * graph is; the list beneath it states what the build did not
	 * keep (HLR-143, LLR-SUM-06). */
	static const char *const names[]   = { "Property", "Value" };
	static const bool        numeric[] = { false, false };

	if (!report->image)
		return 0;

	grid_begin(&grid, "Linked-image filter", 2, names, numeric);
	grid_row(&grid, "Image", report->image);
	snprintf(a, sizeof a, "%" PRIu64, report->image_unresolved);
	grid_row(&grid, "Unresolved linkage names", a);
	/* The one figure the filter did not narrow. Folding it into
	 * the totals would leave a reader unable to tell a file of
	 * retained functions from a file of retained data (HLR-145). */
	snprintf(b, sizeof b, "%" PRIu64, report->file_scope_eloc);
	grid_row(&grid, "ELOC outside any function", b);
	/* The finer granularity, and the pair that says how far it
	 * reached. Both rows appear whether or not the image carried
	 * line information: two zeroes state that nothing was pruned
	 * and nothing was uncoverable, which is a different claim
	 * from a section that omits the question (HLR-155). */
	snprintf(c, sizeof c, "%" PRIu64, report->pruned_lines);
	grid_row(&grid, "Lines not compiled by this build", c);
	snprintf(d, sizeof d, "%" PRIu64, report->uncovered_files);
	grid_row(&grid, "Files with no debug coverage", d);
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

/* The functions the image does not define.
 *
 * Split from the filter provenance above rather than sharing its section, and
 * the split is the tier boundary: the three properties above are the image a
 * run was filtered by, which HLR-150 keeps in the summary, and this is one row
 * per function the build dropped, which is a detail tier by the same rule that
 * makes every other per-function table one. The two are no longer adjacent:
 * this one closes the report (HLR-184), because it is the longest table a
 * filtered run produces and a reader consults it after the report rather than
 * reading the report through it (HLR-147, HLR-150, LLR-SUM-06).
 */
static int absent_functions_section(const Report *report, Style style,
                                    FILE *out, EmptyTables *empty)
{
	Grid grid;
	char a[32];

	if (!report->image)
		return 0;

	static const char *const names[] = { "Function", "File", "Line" };
	char                     heading[96];

	snprintf(heading, sizeof heading,
	         "Functions the image does not define (%zu)",
	         report->absent_count);
	grid_begin(&grid, heading, 3, names, NULL);
	for (size_t i = 0; i < report->absent_count; i++) {
		const AbsentRow *r = &report->absent[i];

		snprintf(a, sizeof a, "%" PRIu32, r->line);
		grid_row(&grid, r->function, r->file, a);
	}
	if (grid_render(&grid, style, out, empty) != 0)
		return -1;

	return 0;
}

/* The closing statement: the tables this run had nothing to put in.
 *
 * **The report ends by saying what it did not print** (HLR-189). Without it,
 * suppressing an empty table would make a report's shape vary with its
 * content in a way a reader cannot audit — "there is no Recursion section"
 * would mean either "no recursion was found" or "this build does not look for
 * recursion", and nothing on the page would separate the two.
 *
 * Named by their full headings, verbatim. Several of those headings carry the
 * reason an analysis was omitted rather than measured, and that reason has to
 * be stated wherever the analysis is not (HLR-115); repeating the heading is
 * what keeps that satisfied by the same words in both cases.
 *
 * Emitted whether or not anything was empty, because a section that appears
 * only sometimes is the problem this section exists to solve.
 */
static void empty_tables_section(const EmptyTables *empty, Style style,
                                 FILE *out)
{
	if (style == STYLE_MARKDOWN)
		fputs("\n## Nothing to report\n\n", out);
	else
		fputs("\nNothing to report\n", out);

	if (empty->count == 0) {
		fputs(style == STYLE_MARKDOWN
		              ? "Every table above carried rows.\n"
		              : "  Every table above carried rows.\n", out);
		return;
	}

	fprintf(out, "%s%zu table%s above %s empty and omitted:\n%s",
	        style == STYLE_MARKDOWN ? "" : "  ",
	        empty->count, empty->count == 1 ? "" : "s",
	        empty->count == 1 ? "was" : "were",
	        style == STYLE_MARKDOWN ? "\n" : "");

	for (size_t i = 0; i < empty->count; i++)
		fprintf(out, "%s%s\n",
		        style == STYLE_MARKDOWN ? "- " : "    - ",
		        empty->headings[i]);
}

/* Which of the two tiers a section belongs to (HLR-150).
 *
 * The partition rule: a tier presenting a project-level aggregate, a file's
 * own totals, or a finding a reader is expected to act on is a summary tier;
 * a tier enumerating one row per analysed entity — per function, per global
 * object, per unreachable statement, per graph edge, per custom-rule match —
 * is a detail tier.
 *
 * Coupling, the cycles, the layering violations, and the recursive chains sit
 * on the detail side of that line despite each row naming a component rather
 * than a function. They enumerate the graph one entity at a time, which is the
 * property the rule turns on; a *file-level aggregate* in the rule's sense is
 * a file's own totals, which is what the Files tier presents. Nothing is lost
 * from the summary by it: every one of those measurements that crossed a
 * published line is a finding, and the findings tier is a summary tier.
 */
typedef enum {
	TIER_SUMMARY = 0,
	TIER_DETAIL
} Tier;

/* Whether an analysis was omitted for want of a declaration (HLR-115).
 *
 * A detail section carrying such a notice is emitted in the summary too,
 * because HLR-150 lists the omission notices among the summary tiers. An
 * omitted analysis produces no rows, so since HLR-188 the section prints
 * nothing at all and the notice reaches the reader through the closing
 * statement, which names it by the heading carrying the reason (HLR-189).
 * The classification below still matters: a section filtered out of a summary
 * run never renders, and would be named nowhere.
 */
static bool depth_omitted(const Report *report)
{
	return report->depth_state == DEPTH_OMITTED_NO_ENTRY_POINTS
	    || report->depth_state == DEPTH_OMITTED_ENTRY_UNRESOLVED;
}

static bool strata_omitted(const Report *report)
{
	return report->strata_state != STRATA_MEASURED;
}

static bool reach_omitted(const Report *report)
{
	return report->reach_state != REACH_MEASURED;
}

static bool scopes_omitted(const Report *report)
{
	return report->scope_state != SCOPES_MEASURED;
}

int render_report(const Report *report, Style style, Verbosity verbosity,
                  FILE *out)
{
	/* One traversal, in one order, for both formats and both verbosities.
	 *
	 * The verbosity is a *filter over this table*, never a second table and
	 * never a second walk. That is what keeps a tier from being present at
	 * one verbosity and forgotten at the other, by the same construction
	 * that keeps it from being present in one format and forgotten in the
	 * other — there is nowhere to forget it, because a section is written
	 * down once and classified once (LLR-SUM-02, LLR-SUM-09). */
	static const struct {
		int  (*render)(const Report *, Style, FILE *, EmptyTables *);
		Tier   tier;
		bool (*omitted)(const Report *);
	} SECTIONS[] = {
		/* **The findings come first**, ahead of every table that
		 * supplies their evidence (HLR-182). They were twenty-second
		 * for the reason everything else is in the order it is in —
		 * each analysis appended its section as it was built — and
		 * that put the one tier a reader is expected to act on below
		 * six hundred rows they are not. Nothing else needs to move
		 * for this: a finding names its subject and its file, so it
		 * is read without the tables and the tables are found from
		 * it. */
		{ findings_section,              TIER_SUMMARY, NULL            },
		{ callouts_section,              TIER_SUMMARY, NULL            },
		{ discovery_section,             TIER_SUMMARY, NULL            },
		{ languages_section,             TIER_SUMMARY, NULL            },
		{ files_section,                 TIER_SUMMARY, NULL            },
		/* From here to the recursion table the order is the reader's
		 * descent, not the pipeline's: the component, then what is
		 * wrong inside it, then the functions themselves, then the
		 * two whole-graph shapes that only make sense once the
		 * functions are in view (HLR-184). Coupling before functions
		 * because a reader deciding where to look starts at the file
		 * that everything depends on, and the threshold listing
		 * before the function table because it is the short list the
		 * long one is read through. */
		{ coupling_section,              TIER_DETAIL,  NULL            },
		{ dependency_cycles_section,     TIER_DETAIL,  NULL            },
		{ threshold_listing_section,     TIER_SUMMARY, NULL            },
		{ functions_section,             TIER_DETAIL,  NULL            },
		{ deepest_chain_section,         TIER_DETAIL,  depth_omitted   },
		{ recursion_section,             TIER_DETAIL,  NULL            },
		{ layering_section,              TIER_DETAIL,  strata_omitted  },
		{ conformance_section,           TIER_SUMMARY, NULL            },
		{ dsm_section,                   TIER_DETAIL,  NULL            },
		{ purification_section,          TIER_DETAIL,  NULL            },
		{ recovery_section,              TIER_DETAIL,  NULL            },
		{ global_state_section,          TIER_DETAIL,  NULL            },
		{ unreachable_functions_section, TIER_DETAIL,  reach_omitted   },
		{ unreachable_globals_section,   TIER_DETAIL,  NULL            },
		{ dead_code_section,             TIER_DETAIL,  NULL            },
		{ cross_scope_section,           TIER_DETAIL,  scopes_omitted  },
		{ definitions_section,           TIER_SUMMARY, NULL            },
		{ image_filter_section,          TIER_SUMMARY, NULL            },
		{ rule_matches_section,          TIER_DETAIL,  NULL            },
		{ partially_parsed_section,      TIER_SUMMARY, NULL            },
		{ expansion_section,             TIER_SUMMARY, NULL            },
		{ stdlib_section,                TIER_SUMMARY, NULL            },
		{ skipped_files_section,         TIER_SUMMARY, NULL            },
		/* Last, and the only section after the files the run could not
		 * measure. It is the longest table a filtered run produces —
		 * one row per function the build dropped — and it answers a
		 * question a reader asks after reading the report rather than
		 * one they read the report to answer (HLR-184). */
		{ absent_functions_section,      TIER_DETAIL,  NULL            },
	};
	EmptyTables empty;
	int         status = -1;

	memset(&empty, 0, sizeof empty);

	/* The project summary heads every report at either verbosity: it is the
	 * one tier a reader of a summary is certain to want. */
	summary_section(report, style, out);

	for (size_t i = 0; i < sizeof SECTIONS / sizeof *SECTIONS; i++) {
		bool wanted = verbosity == VERBOSITY_VERBOSE
		           || SECTIONS[i].tier == TIER_SUMMARY
		           || (SECTIONS[i].omitted
		               && SECTIONS[i].omitted(report));

		if (!wanted)
			continue;
		if (SECTIONS[i].render(report, style, out, &empty) != 0)
			goto done;
	}

	if (empty.failed)
		goto done;
	empty_tables_section(&empty, style, out);

	/* The stream is checked once, after the last write rather than at every
	 * one (SDD §14.3.1). A section returns non-zero only where its own
	 * grid_render reported a failure, and a stream that filled partway
	 * through the final section reports it here — so a truncated report is
	 * never returned as success. */
	if (fflush(out) == 0 && !ferror(out))
		status = 0;

done:
	empty_tables_free(&empty);
	return status;
}

int format_table(const Report *report, Verbosity verbosity, FILE *out)
{
	return render_report(report, STYLE_TABLE, verbosity, out);
}

int format_markdown(const Report *report, Verbosity verbosity, FILE *out)
{
	return render_report(report, STYLE_MARKDOWN, verbosity, out);
}
