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
#include "thresholds.h"
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

/* Lines the grammar could not follow, across every analysed file. */
static uint64_t unparsed_total(const Report *report)
{
	uint64_t total = 0;

	for (size_t i = 0; i < report->file_count; i++)
		total += report->files[i]->unparsed_lines;
	return total;
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
	/* Beside the totals it qualifies, not buried below them. Every figure
	 * above covers the file *minus* these lines, and a reader comparing
	 * ELOC against a line count of their own needs to know that before
	 * they start looking for the discrepancy (HLR-035). */
	summary_pair(out, style, label, value, "Unparsed lines",
	             (uint64_t)unparsed_total(report));
	/* Counted in the summary so the shape of the run is visible before the
	 * tables. A severity is a label and moves no exit status, so these are
	 * figures to read rather than gates to pass (HLR-100). */
	summary_pair(out, style, label, value, "Critical findings",
	             severity_total(report, "critical"));
	summary_pair(out, style, label, value, "Warnings",
	             severity_total(report, "warning"));
	/* Not a failure and not a defect — a measure of how complete the graph
	 * is. A project calling into libc has unresolved calls by definition,
	 * and a reader comparing fan-out against the source needs to know how
	 * many calls the graph could not represent (HLR-077). */
	summary_pair(out, style, label, value, "Unresolved calls",
	             (uint64_t)report->unresolved_calls);
	/* The completeness of the pruning, stated for the reason the
	 * unresolved-call count is: a region elc could not decide is left whole
	 * and counted here, so a reader can tell a configuration that was cut
	 * cleanly from one that mostly was not (HLR-133). */
	summary_pair(out, style, label, value, "Undecided regions",
	             report->undecided_regions);
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
		static const char *const names[]   = { "File", "Function",
		                                       "Fan-out" };
		static const bool        numeric[] = { false, false, true };

		grid_begin(&grid, "Fan-out (distinct callees)", 3, names,
		           numeric);
		for (size_t i = 0; i < report->fan_out_count; i++) {
			const FanOutRow *r = &report->fan_out[i];

			snprintf(a, sizeof a, "%" PRIu32, r->fan_out);
			grid_row(&grid, r->file, r->function, a);
		}
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
		/* Data the same traversal condemned: an object every one of
		 * whose accessing functions is itself unreachable (HLR-096). */
		static const char *const names[] = { "Object" };

		grid_begin(&grid,
		           "Unreachable globals (touched only by unreachable "
		           "functions)", 1, names, NULL);
		for (size_t i = 0; i < report->unreachable_global_count; i++)
			grid_row(&grid, report->unreachable_globals[i]);
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
		if (grid_render(&grid, style, out) != 0)
			return -1;
	}

	{
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
