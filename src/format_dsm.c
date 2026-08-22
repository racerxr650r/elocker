/* format_dsm.c — the Dependency Structure Matrix and its renderings.
 *
 * The square grid whose cells carry the call counts between layers, or
 * between directories where no layer was declared, and whose diagonal
 * separates conforming dependencies from back-calls (doc/SDD.md §22).
 *
 * **Rows are callers and columns are callees**, both in the same ascending
 * order, so the diagonal has a meaning a reader can rely on: above it are
 * dependencies running the declared way, on it are dependencies inside one
 * subject, and below it are the back-calls of HLR-162. A matrix whose
 * orientation the reader must infer is worse than no matrix, so the
 * convention is printed with every rendering (HLR-166).
 *
 * **The matrix is dense and its subjects are few.** Rows and columns are
 * layers or directories rather than files or functions, so the grid stays
 * readable at the size an architecture actually has; a per-function DSM of a
 * real project is a matrix nobody can look at.
 *
 * **Escaping is not this module's own.** The CSV rendering emits every cell
 * through the same `write_field` the per-function renderer uses, and the
 * Markdown rendering escapes the cell separator, so a directory containing a
 * comma or a pipe cannot corrupt the grid (HLR-064).
 *
 * **One walk, three decorations.** The three renderings share `render`, for
 * the reason format_text.c's tiers share one traversal: three walks of one
 * grid is how two of them come to disagree about what is in it.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch.h"
#include "elc.h"
#include "format_csv.h"
#include "format_dsm.h"
#include "graph.h"
#include "report.h"

const char DSM_CONVENTION[] =
	"Rows are callers, columns callees, in ascending order. "
	"Above the diagonal: the declared direction. "
	"On it: within one subject. Below it: back-calls.";

const char DSM_CORNER[] = "caller \\ callee";

/* ----------------------------------------------------------- the subjects -- */

/* The distinct directories of the analysed components, ascending by path.
 *
 * Sorted here rather than taken in component order, and the difference is not
 * theoretical: the components arrive in ascending *path* order, which is not
 * ascending *directory* order. `/a-b/x.c` sorts before `/a/y.c` because `-`
 * precedes `/`, yet `/a` precedes `/a-b`. Reading the directories off in
 * component order would put them in an order no rule describes, and HLR-166
 * asks for ascending path.
 *
 * This is one of the two places outside report.c that impose an order, and it
 * is the same exception the graph writers take (LLR-DOT-04): the sequence is a
 * property of this artefact rather than of a collection the model already
 * holds, so there is nothing in report.c to sort.
 */
static int by_path(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int subjects_from_directories(const Report *r, size_t components,
                                     Dsm *out)
{
	char **labels = calloc(components ? components : 1, sizeof *labels);
	size_t count  = 0;

	if (!labels)
		return -1;

	for (size_t c = 0; c < components; c++) {
		const char *dir = r->files[c]->directory;
		bool        seen = false;

		if (!dir)
			continue;
		for (size_t i = 0; i < count && !seen; i++)
			seen = strcmp(labels[i], dir) == 0;
		if (seen)
			continue;

		labels[count] = strdup(dir);
		if (!labels[count]) {
			for (size_t i = 0; i < count; i++)
				free(labels[i]);
			free(labels);
			return -1;
		}
		count++;
	}

	if (count > 1)
		qsort(labels, count, sizeof *labels, by_path);

	out->subjects = labels;
	out->count    = count;
	return 0;
}

/* The declared layers, ordered by ascending layer index (HLR-161, HLR-166).
 *
 * A stratum's ordinal *is* its layer index, so the ordinal is the position in
 * this sequence rather than a key to sort by. `--stratum-order` may permute
 * the ordinals away from declaration order, which is exactly why the label is
 * placed at its ordinal instead of appended in the order the options were
 * parsed.
 */
static int subjects_from_strata(const ElcOptions *opts, Dsm *out)
{
	char **labels = calloc(opts->strata.count ? opts->strata.count : 1,
	                       sizeof *labels);

	if (!labels)
		return -1;

	for (size_t i = 0; i < opts->strata.count; i++) {
		size_t at = opts->strata.items[i].ordinal;

		if (at >= opts->strata.count)
			continue;   /* cli.c guarantees a permutation */
		labels[at] = strdup(opts->strata.items[i].name);
		if (!labels[at]) {
			for (size_t j = 0; j < opts->strata.count; j++)
				free(labels[j]);
			free(labels);
			return -1;
		}
	}

	/* A slot no ordinal claimed cannot arise from `cli.c`, which validates
	 * the order into a permutation — but a caller constructing options by
	 * hand can leave one, and a NULL label would be dereferenced by every
	 * rendering. An empty label is a visible gap; a null pointer is a
	 * fault. */
	for (size_t i = 0; i < opts->strata.count; i++)
		if (!labels[i]) {
			labels[i] = strdup("");
			if (!labels[i]) {
				for (size_t j = 0; j < opts->strata.count; j++)
					free(labels[j]);
				free(labels);
				return -1;
			}
		}

	out->subjects    = labels;
	out->count       = opts->strata.count;
	out->from_strata = true;
	return 0;
}

/* Which subject each component belongs to, or SIZE_MAX for one no subject
 * holds — which happens only under a declaration, where a component matching
 * no stratum lies outside the partition (HLR-161, LLR-LAY-05).
 *
 * The layer assignment comes from `arch.c` rather than from a matcher of this
 * module's own, so that the grid and the layering findings cannot disagree
 * about which file is in which layer.
 */
static size_t *subject_of_components(const Sdg *g, const Report *r,
                                     const ElcOptions *opts, const Dsm *m,
                                     size_t components)
{
	size_t *map = calloc(components ? components : 1, sizeof *map);
	size_t *stratum;

	if (!map)
		return NULL;

	if (!m->from_strata) {
		for (size_t c = 0; c < components; c++) {
			map[c] = SIZE_MAX;
			for (size_t s = 0; s < m->count; s++)
				if (r->files[c]->directory &&
				    strcmp(m->subjects[s],
				           r->files[c]->directory) == 0) {
					map[c] = s;
					break;
				}
		}
		return map;
	}

	stratum = stratum_of_components(g, opts);
	if (!stratum) {
		free(map);
		return NULL;
	}

	for (size_t c = 0; c < components; c++) {
		size_t s = c < g->component_count ? stratum[c] : SIZE_MAX;

		/* The stratum's ordinal is its position in the sequence, which
		 * is what puts a back-call below the diagonal rather than
		 * wherever the option happened to be typed. An ordinal outside
		 * the sequence indexes no cell, and is treated as a component
		 * outside the partition rather than written past the grid. */
		map[c] = s == SIZE_MAX ? SIZE_MAX
		                       : opts->strata.items[s].ordinal;
		if (map[c] != SIZE_MAX && map[c] >= m->count)
			map[c] = SIZE_MAX;
	}

	free(stratum);
	return map;
}

int dsm_build(const Sdg *g, const Report *r, const ElcOptions *opts, Dsm *out)
{
	size_t *map;
	size_t  components;

	memset(out, 0, sizeof *out);

	if (!g || !r || !opts)
		return 0;

	/* The component table and the report's file list are the same list
	 * under two names — graph.c builds one from the other — but the grid
	 * indexes both, so it walks the shorter rather than trusting the
	 * identity. */
	components = g->component_count < r->file_count ? g->component_count
	                                                : r->file_count;

	if (opts->strata.count > 0) {
		if (subjects_from_strata(opts, out) != 0)
			return -1;
	} else if (subjects_from_directories(r, components, out) != 0) {
		return -1;
	}

	out->cells = calloc(out->count ? out->count * out->count : 1,
	                    sizeof *out->cells);
	if (!out->cells) {
		dsm_free(out);
		return -1;
	}

	map = subject_of_components(g, r, opts, out, components);
	if (!map) {
		dsm_free(out);
		return -1;
	}

	for (size_t e = 0; e < g->edge_count; e++) {
		const SdgEdge *edge = &g->edges[e];
		size_t         from;
		size_t         to;

		/* **Calls only** (HLR-165, LLR-LAY-05). A global object two
		 * subjects share is coupling and not invocation, and counting
		 * one here would put a cell below the diagonal that no call
		 * put there. */
		if (edge->kind != EDGE_CALL)
			continue;
		if (edge->from >= g->node_count || edge->to >= g->node_count)
			continue;

		from = g->nodes[edge->from].component;
		to   = g->nodes[edge->to].component;
		if (from >= components || to >= components)
			continue;
		if (map[from] == SIZE_MAX || map[to] == SIZE_MAX)
			continue;

		out->cells[map[from] * out->count + map[to]]++;
	}

	free(map);
	return 0;
}

bool dsm_warranted(const ElcOptions *opts)
{
	return opts && opts->dsm && opts->output_path != NULL;
}

void dsm_free(Dsm *m)
{
	if (!m)
		return;

	for (size_t i = 0; i < m->count; i++)
		free(m->subjects[i]);
	free(m->subjects);
	free(m->cells);
	memset(m, 0, sizeof *m);
}

/* --------------------------------------------------------- the renderings -- */

/* Which decoration the shared walk is wearing. */
typedef enum {
	DSM_TABLE = 0,
	DSM_MARKDOWN,
	DSM_CSV
} DsmStyle;

static const char *dsm_heading(const Dsm *m)
{
	/* Which kind of subject the grid is over is said in the heading rather
	 * than left to the reader, because only a *declared* order makes a
	 * below-diagonal cell a violation. Over directories the same cell is a
	 * dependency and nothing more (HLR-165). */
	return m->from_strata
	               ? "Dependency structure matrix (declared layers)"
	               : "Dependency structure matrix (directories: no "
	                 "strata declared, see --stratum)";
}

/* The Markdown cell separator, escaped.
 *
 * GFM ends a cell at an unescaped `|`, so a directory containing one would
 * shift every cell to its right by a column — a grid that still parses and
 * says something else (HLR-064). Written into a scratch buffer the caller
 * sized to twice the widest subject, which is the worst case: every character
 * a pipe. Nothing is truncated, because a truncated label is a wrong answer
 * rather than an untidy one, and a path is not bounded by anything this
 * module could pick.
 */
static const char *markdown_cell(const char *value, char *scratch)
{
	size_t at = 0;

	if (!scratch || !strchr(value, '|'))
		return value;

	for (const char *p = value; *p; p++) {
		if (*p == '|')
			scratch[at++] = '\\';
		scratch[at++] = *p;
	}
	scratch[at] = '\0';
	return scratch;
}

/* The rendered width of one cell in the Markdown decoration, which is the
 * escaped width rather than the raw one — a column measured before escaping
 * comes out a character short for every pipe in it. */
static int cell_width(const char *value, DsmStyle style)
{
	int width = (int)strlen(value);

	if (style != DSM_MARKDOWN)
		return width;

	for (const char *p = value; *p; p++)
		if (*p == '|')
			width++;
	return width;
}

static int digits_of(size_t value)
{
	int width = 1;

	while (value >= 10) {
		value /= 10;
		width++;
	}
	return width;
}

static void rule(FILE *out, int width, char fill)
{
	for (int i = 0; i < width; i++)
		fputc(fill, out);
}

/* Emit one text cell in the requested decoration. */
static void emit_text(FILE *out, DsmStyle style, const char *value, int width,
                      bool last, char *scratch)
{
	switch (style) {
	case DSM_CSV:
		write_field(value, out);
		break;
	case DSM_MARKDOWN:
		fprintf(out, " %-*s |", width, markdown_cell(value, scratch));
		break;
	case DSM_TABLE:
	default:
		/* A left-aligned final column is not padded, by the rule the
		 * report's other tables follow: padding it puts trailing
		 * whitespace on every line (LLR-SUM-04). */
		if (last)
			fputs(value, out);
		else
			fprintf(out, "%-*s", width, value);
		break;
	}
}

static void emit_number(FILE *out, DsmStyle style, size_t value, int width)
{
	switch (style) {
	case DSM_CSV: {
		char rendered[32];

		snprintf(rendered, sizeof rendered, "%zu", value);
		write_field(rendered, out);
		break;
	}
	case DSM_MARKDOWN:
		fprintf(out, " %*zu |", width, value);
		break;
	case DSM_TABLE:
	default:
		fprintf(out, "%*zu", width, value);
		break;
	}
}

/* The one walk of the grid the three renderings share.
 *
 * Widths are measured before anything is written, because a column's width is
 * not known until its last cell is in — and measuring a value one way and
 * printing it another is how a column comes out a character short.
 */
static int render(const Dsm *m, DsmStyle style, FILE *out)
{
	int    *width   = calloc(m->count + 1, sizeof *width);
	size_t  columns = m->count + 1;
	size_t  longest = strlen(DSM_CORNER);
	char   *scratch = NULL;

	if (!width)
		return -1;

	width[0] = cell_width(DSM_CORNER, style);
	for (size_t s = 0; s < m->count; s++) {
		int label = cell_width(m->subjects[s], style);

		if (label > width[0])
			width[0] = label;
		width[s + 1] = cell_width(m->subjects[s], style);
		if (strlen(m->subjects[s]) > longest)
			longest = strlen(m->subjects[s]);
	}

	/* Twice the widest label plus a terminator: the worst case is a label
	 * that is every character a pipe. Allocated once for the whole grid
	 * rather than per cell, and only where an escaping style needs it. */
	if (style == DSM_MARKDOWN) {
		scratch = malloc(2 * longest + 1);
		if (!scratch) {
			free(width);
			return -1;
		}
	}
	for (size_t row = 0; row < m->count; row++)
		for (size_t col = 0; col < m->count; col++) {
			int figure = digits_of(m->cells[row * m->count + col]);

			if (figure > width[col + 1])
				width[col + 1] = figure;
		}

	/* The convention, with the grid and never apart from it (HLR-166). */
	switch (style) {
	case DSM_CSV:
		/* A record of its own ahead of the grid: CSV has no comment
		 * syntax, and a convention folded into the corner cell would
		 * be read as a column name by every consumer that keys on
		 * one. It goes through `write_field` like everything else, so
		 * the commas in it cannot split it into several fields. */
		write_field(DSM_CONVENTION, out);
		fputs("\r\n", out);
		break;
	case DSM_MARKDOWN:
		fprintf(out, "\n## %s\n\n%s\n\n", dsm_heading(m),
		        DSM_CONVENTION);
		break;
	case DSM_TABLE:
	default:
		fprintf(out, "\n%s\n  %s\n", dsm_heading(m), DSM_CONVENTION);
		break;
	}

	/* --- the column names ---------------------------------------- */

	if (style == DSM_MARKDOWN)
		fputc('|', out);
	else if (style == DSM_TABLE)
		fputs("  ", out);

	emit_text(out, style, DSM_CORNER, width[0], columns == 1, scratch);
	for (size_t s = 0; s < m->count; s++) {
		if (style == DSM_CSV)
			fputc(',', out);
		else if (style == DSM_TABLE)
			fputs("  ", out);
		emit_text(out, style, m->subjects[s], width[s + 1], false,
		          scratch);
	}
	fputs(style == DSM_CSV ? "\r\n" : "\n", out);

	/* --- the rule under them -------------------------------------- */

	if (style == DSM_MARKDOWN) {
		fputc('|', out);
		fputc(' ', out);
		rule(out, width[0], '-');
		fputs(" |", out);
		for (size_t s = 0; s < m->count; s++) {
			/* GFM marks a right-aligned column with a trailing
			 * colon, so a renderer downstream aligns the counts the
			 * way this one does. */
			fputc(' ', out);
			rule(out, width[s + 1] - 1, '-');
			fputs(": |", out);
		}
		fputc('\n', out);
	} else if (style == DSM_TABLE) {
		fputs("  ", out);
		for (size_t c = 0; c < columns; c++) {
			if (c)
				fputs("  ", out);
			rule(out, width[c], '-');
		}
		fputc('\n', out);
	}

	/* --- the rows -------------------------------------------------- */

	for (size_t row = 0; row < m->count; row++) {
		if (style == DSM_MARKDOWN)
			fputc('|', out);
		else if (style == DSM_TABLE)
			fputs("  ", out);

		emit_text(out, style, m->subjects[row], width[0], false,
		          scratch);
		for (size_t col = 0; col < m->count; col++) {
			if (style == DSM_CSV)
				fputc(',', out);
			else if (style == DSM_TABLE)
				fputs("  ", out);
			emit_number(out, style, m->cells[row * m->count + col],
			            width[col + 1]);
		}
		fputs(style == DSM_CSV ? "\r\n" : "\n", out);
	}

	free(scratch);
	free(width);

	if (fflush(out) != 0 || ferror(out))
		return -1;

	return 0;
}

int format_dsm_csv(const Dsm *m, FILE *out)
{
	return render(m, DSM_CSV, out);
}

int format_dsm_markdown(const Dsm *m, FILE *out)
{
	return render(m, DSM_MARKDOWN, out);
}

int format_dsm_table(const Dsm *m, FILE *out)
{
	return render(m, DSM_TABLE, out);
}
