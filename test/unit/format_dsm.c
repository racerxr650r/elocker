/* test/unit/format_dsm.c — unit tests for src/format_dsm.c.
 *
 * Built from hand-made facts and reports, as the arch, graph and call-tree
 * tests are: what is under test is how call edges are arranged into a grid,
 * and a fixture that had to be parsed first would put a query file's
 * behaviour into every assertion about the arrangement.
 *
 * The three cases worth reading first are the ones a wrong matrix fails
 * silently on: the orientation (a back-call must land *below* the diagonal,
 * and a matrix built the other way round still looks like a matrix), the
 * ordering (a subject sequence that is not ascending puts every cell
 * somewhere a reader cannot interpret), and the escaping (a directory
 * carrying a comma or a pipe must not split a row).
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "arch.h"
#include "cli.h"
#include "elc.h"
#include "format_dsm.h"
#include "graph.h"
#include "report.h"

/* ------------------------------------------------------------ scaffolding */

static FileMetrics *file_with(const char *path, const char *function)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path      = strdup(path);
	m->language  = strdup("c");
	/* Recorded here for the reason analyze.c records it on a measured
	 * file: every consumer reads the field rather than slicing the path
	 * for itself (HLR-160). */
	m->directory = component_directory(path);
	cr_assert_not_null(m->path);
	cr_assert_not_null(m->language);
	cr_assert_not_null(m->directory);

	m->functions = calloc(1, sizeof *m->functions);
	cr_assert_not_null(m->functions);
	m->functions[0].name       = strdup(function);
	cr_assert_not_null(m->functions[0].name);
	m->functions[0].start_line = 1;
	m->functions[0].end_line   = 5;
	m->function_count          = 1;
	return m;
}

static FileFacts *facts_for(const char *path)
{
	FileFacts *f = calloc(1, sizeof *f);

	cr_assert_not_null(f);
	f->path = strdup(path);
	cr_assert_not_null(f->path);
	return f;
}

static void add_call(FileFacts *f, const char *callee)
{
	f->calls = realloc(f->calls, (f->call_count + 1) * sizeof *f->calls);
	cr_assert_not_null(f->calls);
	f->calls[f->call_count].callee = strdup(callee);
	cr_assert_not_null(f->calls[f->call_count].callee);
	f->calls[f->call_count].caller = 0;
	f->calls[f->call_count].line   = 1;
	f->call_count++;
	f->call_capacity = f->call_count;
}

/* One file per named path, each defining one function, with the calls the
 * caller lists. Everything the tests below need is here so that each of them
 * reads as its claim rather than as a construction. */
typedef struct {
	Report   report;
	FactList facts;
	Sdg      graph;
} Scene;

static void scene_build(Scene *s, const char *const *paths,
                        const char *const *functions, size_t count,
                        const size_t *call_from, const size_t *call_to,
                        size_t call_count)
{
	MetricsAccumulator acc  = { 0 };
	ElcOptions         none = { 0 };
	FileFacts        **ff   = calloc(count, sizeof *ff);

	memset(s, 0, sizeof *s);
	cr_assert_not_null(ff);

	for (size_t i = 0; i < count; i++)
		cr_assert_eq(metrics_add(&acc, file_with(paths[i],
		                                         functions[i])), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &none, &s->report), 0);
	metrics_free(&acc);

	/* One fact record per path, in the caller's order rather than the
	 * report's: graph_build matches facts to files by path, so a call
	 * names the function the caller meant whichever order the paths
	 * happened to sort into. */
	for (size_t i = 0; i < count; i++)
		ff[i] = facts_for(paths[i]);

	for (size_t c = 0; c < call_count; c++)
		add_call(ff[call_from[c]], functions[call_to[c]]);

	for (size_t i = 0; i < count; i++)
		cr_assert_eq(factlist_add(&s->facts, ff[i]), 0);
	free(ff);

	cr_assert_eq(graph_build(&s->facts, &s->report, &s->graph), 0);
}

static void scene_free(Scene *s)
{
	graph_free(&s->graph);
	factlist_free(&s->facts);
	report_free(&s->report);
}

static size_t subject_of(const Dsm *m, const char *name)
{
	for (size_t i = 0; i < m->count; i++)
		if (strcmp(m->subjects[i], name) == 0)
			return i;
	return SIZE_MAX;
}

static size_t cell(const Dsm *m, const char *from, const char *to)
{
	size_t row = subject_of(m, from);
	size_t col = subject_of(m, to);

	cr_assert_neq(row, SIZE_MAX, "no subject named %s", from);
	cr_assert_neq(col, SIZE_MAX, "no subject named %s", to);
	return m->cells[row * m->count + col];
}

/* Render into memory, so the assertions are about bytes rather than about a
 * terminal. The caller frees. */
static char *rendered(const Dsm *m, int (*render)(const Dsm *, FILE *))
{
	FILE *fp = tmpfile();
	char *buffer;
	long  length;

	cr_assert_not_null(fp, "could not open a temporary stream");
	cr_assert_eq(render(m, fp), 0);

	length = ftell(fp);
	cr_assert_geq(length, 0);
	rewind(fp);

	buffer = calloc(1, (size_t)length + 1);
	cr_assert_not_null(buffer);
	cr_assert_eq(fread(buffer, 1, (size_t)length, fp), (size_t)length);
	fclose(fp);
	return buffer;
}

/* The three-layer tree the arch fixture uses, in miniature: app calls hal,
 * app reaches past hal into drv, hal calls drv, and hal calls back up into
 * app. Six call edges over three components, of which one is a back-call and
 * one a skip. */
static const char *const TREE_PATHS[]     = { "/p/app/a.c", "/p/hal/b.c",
                                              "/p/drv/c.c" };
static const char *const TREE_FUNCTIONS[] = { "app_fn", "hal_fn", "drv_fn" };

static ElcOptions three_layers(void)
{
	ElcOptions opts = { 0 };

	cr_assert_eq(parse_stratum("app:/p/app/*", &opts), 0);
	cr_assert_eq(parse_stratum("hal:/p/hal/*", &opts), 0);
	cr_assert_eq(parse_stratum("drv:/p/drv/*", &opts), 0);
	return opts;
}

/* ------------------------------------------------------------ orientation -- */

/* **Rows are callers and columns are callees.** A matrix built the other way
 * round is still a square grid of plausible numbers, and every reading taken
 * from it is exactly backwards — which is why the orientation is asserted
 * directly rather than inferred from a total. */
Test(format_dsm, rows_are_callers_and_columns_are_callees)
{
	Scene       s;
	ElcOptions  opts = three_layers();
	Dsm         m    = { 0 };
	size_t      from[] = { 0, 1 };
	size_t      to[]   = { 1, 0 };

	/* app calls hal, and hal calls back up into app. */
	scene_build(&s, TREE_PATHS, TREE_FUNCTIONS, 3, from, to, 2);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);

	cr_assert_eq(cell(&m, "app", "hal"), 1);
	cr_assert_eq(cell(&m, "hal", "app"), 1);
	cr_assert_eq(cell(&m, "drv", "app"), 0);

	dsm_free(&m);
	cli_options_free(&opts);
	scene_free(&s);
}

/* The subjects run in ascending layer index, so a back-call is *below* the
 * diagonal by construction rather than by coincidence, and the sum of the
 * cells below it is exactly the count of inverted layering findings. */
Test(format_dsm, back_calls_gather_below_the_diagonal)
{
	Scene              s;
	ElcOptions         opts = three_layers();
	Dsm                m    = { 0 };
	ArchResults        arch = { 0 };
	ConformanceIndices idx  = { 0 };
	size_t             below = 0;
	size_t             from[] = { 0, 0, 1, 1 };
	size_t             to[]   = { 1, 2, 2, 0 };

	/* app→hal, app→drv (a skip), hal→drv, hal→app (a back-call). */
	scene_build(&s, TREE_PATHS, TREE_FUNCTIONS, 3, from, to, 4);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);
	cr_assert_eq(arch_analyse(&s.graph, &opts, &arch), 0);
	cr_assert_eq(conformance_indices(&arch, &idx), 0);

	cr_assert_eq(m.count, 3);
	cr_assert_str_eq(m.subjects[0], "app");
	cr_assert_str_eq(m.subjects[1], "hal");
	cr_assert_str_eq(m.subjects[2], "drv");

	for (size_t row = 0; row < m.count; row++)
		for (size_t col = 0; col < row; col++)
			below += m.cells[row * m.count + col];

	cr_assert_eq(below, 1);
	cr_assert_eq(below, idx.back_calls,
	             "the cells below the diagonal must account for exactly "
	             "the back-calls the layering section lists");

	arch_results_free(&arch);
	dsm_free(&m);
	cli_options_free(&opts);
	scene_free(&s);
}

/* A stratum's ordinal *is* its layer index, so `--stratum-order` moves a
 * subject in the sequence. Ordering by declaration order instead would put
 * the back-calls above the diagonal for every project that used the option. */
Test(format_dsm, subjects_follow_the_layer_index_not_the_declaration_order)
{
	Scene      s;
	ElcOptions opts   = three_layers();
	Dsm        m      = { 0 };
	size_t     from[] = { 2 };
	size_t     to[]   = { 0 };

	/* Declared app, hal, drv; ordered drv, hal, app — the permutation
	 * `--stratum-order drv>hal>app` leaves behind. The call from drv into
	 * app now descends two layers rather than ascending them. */
	opts.strata.items[0].ordinal = 2;   /* app */
	opts.strata.items[1].ordinal = 1;   /* hal */
	opts.strata.items[2].ordinal = 0;   /* drv */

	scene_build(&s, TREE_PATHS, TREE_FUNCTIONS, 3, from, to, 1);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);

	cr_assert_eq(m.count, 3);
	cr_assert_str_eq(m.subjects[0], "drv");
	cr_assert_str_eq(m.subjects[1], "hal");
	cr_assert_str_eq(m.subjects[2], "app");
	/* Above the diagonal, because the declaration now permits it. */
	cr_assert_eq(m.cells[0 * 3 + 2], 1);

	dsm_free(&m);
	cli_options_free(&opts);
	scene_free(&s);
}

/* ---------------------------------------------------------- the subjects -- */

/* With nothing declared the matrix is still produced, over the analysed
 * directories — which is what makes it useful to the reader on a first run,
 * who is most readers (HLR-165). */
Test(format_dsm, with_no_strata_the_subjects_are_the_directories)
{
	Scene      s;
	ElcOptions opts   = { 0 };
	Dsm        m      = { 0 };
	size_t     from[] = { 0, 1 };
	size_t     to[]   = { 1, 2 };

	scene_build(&s, TREE_PATHS, TREE_FUNCTIONS, 3, from, to, 2);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);

	cr_assert_not(m.from_strata);
	cr_assert_eq(m.count, 3);
	cr_assert_eq(cell(&m, "/p/app", "/p/hal"), 1);
	cr_assert_eq(cell(&m, "/p/hal", "/p/drv"), 1);

	dsm_free(&m);
	scene_free(&s);
}

/* Ascending by *path*, which is not the order the components arrive in.
 * `/p/a-b/x.c` sorts before `/p/a/y.c` because `-` precedes `/`, yet the
 * directory `/p/a` precedes `/p/a-b`. Reading the directories off in
 * component order would produce a sequence no rule describes — and one that
 * two runs over the same tree would agree on, so no determinism test would
 * catch it either (HLR-166). */
Test(format_dsm, directories_are_ordered_by_path_not_by_component_order)
{
	Scene              s;
	ElcOptions         opts    = { 0 };
	Dsm                m       = { 0 };
	static const char *paths[] = { "/p/a-b/x.c", "/p/a/y.c" };
	static const char *fns[]   = { "x_fn", "y_fn" };
	size_t             from[]  = { 0 };
	size_t             to[]    = { 1 };

	scene_build(&s, paths, fns, 2, from, to, 1);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);

	cr_assert_eq(m.count, 2);
	cr_assert_str_eq(m.subjects[0], "/p/a");
	cr_assert_str_eq(m.subjects[1], "/p/a-b");

	dsm_free(&m);
	scene_free(&s);
}

/* A component no declaration names lies outside the partition rather than in
 * a layer of its own, and contributes to no cell — the rule the layering
 * findings already follow (HLR-161, LLR-LAY-05). */
Test(format_dsm, a_component_outside_every_stratum_reaches_no_cell)
{
	Scene      s;
	ElcOptions opts   = { 0 };
	Dsm        m      = { 0 };
	size_t     from[] = { 0, 1 };
	size_t     to[]   = { 1, 2 };
	size_t     total  = 0;

	/* Only the middle layer is declared. Both calls touch a file it does
	 * not name, so the grid is one subject wide and entirely empty. */
	cr_assert_eq(parse_stratum("hal:/p/hal/*", &opts), 0);

	scene_build(&s, TREE_PATHS, TREE_FUNCTIONS, 3, from, to, 2);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);

	cr_assert(m.from_strata);
	cr_assert_eq(m.count, 1);
	for (size_t i = 0; i < m.count * m.count; i++)
		total += m.cells[i];
	cr_assert_eq(total, 0);

	dsm_free(&m);
	cli_options_free(&opts);
	scene_free(&s);
}

/* The matrix reports call edges alone. A global object two subjects happen to
 * share is coupling and not invocation, and counting one would put a number
 * below the diagonal that no call put there (LLR-LAY-05, LLR-CTR-07). */
Test(format_dsm, a_shared_global_is_not_a_matrix_cell)
{
	MetricsAccumulator acc    = { 0 };
	ElcOptions         none   = { 0 };
	Report             report = { 0 };
	FactList           facts  = { 0 };
	Sdg                g      = { 0 };
	Dsm                m      = { 0 };
	FileFacts         *fa     = facts_for("/p/app/a.c");
	FileFacts         *fc     = facts_for("/p/drv/c.c");
	size_t             total  = 0;

	cr_assert_eq(metrics_add(&acc, file_with("/p/app/a.c", "app_fn")), 0);
	cr_assert_eq(metrics_add(&acc, file_with("/p/drv/c.c", "drv_fn")), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &none, &report), 0);
	metrics_free(&acc);

	fa->globals = calloc(2, sizeof *fa->globals);
	cr_assert_not_null(fa->globals);
	fa->globals[0].name     = strdup("shared");
	fa->globals[0].function = ELC_NO_FUNCTION;
	fa->globals[0].kind     = GLOBAL_DECLARATION;
	fa->globals[1].name     = strdup("shared");
	fa->globals[1].function = 0;
	fa->globals[1].kind     = GLOBAL_WRITE;
	fa->global_count        = 2;
	fa->global_capacity     = 2;

	fc->globals = calloc(1, sizeof *fc->globals);
	cr_assert_not_null(fc->globals);
	fc->globals[0].name     = strdup("shared");
	fc->globals[0].function = 0;
	fc->globals[0].kind     = GLOBAL_READ;
	fc->global_count        = 1;
	fc->global_capacity     = 1;

	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, fc), 0);
	cr_assert_eq(graph_build(&facts, &report, &g), 0);

	cr_assert_eq(dsm_build(&g, &report, &none, &m), 0);
	cr_assert_eq(m.count, 2);
	for (size_t i = 0; i < m.count * m.count; i++)
		total += m.cells[i];
	cr_assert_eq(total, 0, "a global edge is coupling, not a call");

	dsm_free(&m);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* Repeated calls from one function to the same callee collapse to one graph
 * edge, so a cell counts distinct call edges — the figure the indices are
 * over. A cell counting call *sites* would disagree with the denominator the
 * percentages are computed against. */
Test(format_dsm, repeated_calls_to_one_callee_are_one_edge)
{
	Scene      s;
	ElcOptions opts   = three_layers();
	Dsm        m      = { 0 };
	size_t     from[] = { 0, 0, 0 };
	size_t     to[]   = { 1, 1, 1 };

	scene_build(&s, TREE_PATHS, TREE_FUNCTIONS, 3, from, to, 3);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);

	cr_assert_eq(cell(&m, "app", "hal"), 1);

	dsm_free(&m);
	cli_options_free(&opts);
	scene_free(&s);
}

/* -------------------------------------------------------- the renderings -- */

/* A matrix whose orientation the reader has to infer conveys the opposite of
 * what it is for, half the time. Every rendering says which way round it runs
 * (HLR-166). */
Test(format_dsm, every_rendering_states_the_convention)
{
	Dsm   m = { 0 };
	char *csv;
	char *markdown;
	char *table;

	csv      = rendered(&m, format_dsm_csv);
	markdown = rendered(&m, format_dsm_markdown);
	table    = rendered(&m, format_dsm_table);

	cr_assert_not_null(strstr(csv, DSM_CONVENTION));
	cr_assert_not_null(strstr(markdown, DSM_CONVENTION));
	cr_assert_not_null(strstr(table, DSM_CONVENTION));

	free(csv);
	free(markdown);
	free(table);
}

/* A directory containing a comma must not split a record. The cell goes
 * through the same `write_field` the per-function renderer uses, so the
 * quoting rule is written down once (HLR-064, LLR-FLD-02). */
Test(format_dsm, a_comma_in_a_subject_is_quoted_in_the_csv)
{
	Scene              s;
	ElcOptions         opts    = { 0 };
	Dsm                m       = { 0 };
	static const char *paths[] = { "/p/one,two/x.c", "/p/z/y.c" };
	static const char *fns[]   = { "x_fn", "y_fn" };
	size_t             from[]  = { 0 };
	size_t             to[]    = { 1 };
	char              *csv;

	scene_build(&s, paths, fns, 2, from, to, 1);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);

	csv = rendered(&m, format_dsm_csv);
	cr_assert_not_null(strstr(csv, "\"/p/one,two\""),
	                   "a subject carrying a comma must be quoted");
	cr_assert_null(strstr(csv, "\r\n/p/one,two,"),
	               "an unquoted comma splits the row in two");

	free(csv);
	dsm_free(&m);
	scene_free(&s);
}

/* GitHub-Flavored Markdown ends a cell at an unescaped pipe, so a directory
 * carrying one would shift every cell to its right by a column — producing a
 * grid that still parses and says something else (HLR-064). */
Test(format_dsm, a_pipe_in_a_subject_is_escaped_in_the_markdown)
{
	Scene              s;
	ElcOptions         opts    = { 0 };
	Dsm                m       = { 0 };
	static const char *paths[] = { "/p/a|b/x.c", "/p/z/y.c" };
	static const char *fns[]   = { "x_fn", "y_fn" };
	size_t             from[]  = { 0 };
	size_t             to[]    = { 1 };
	char              *markdown;

	scene_build(&s, paths, fns, 2, from, to, 1);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);

	markdown = rendered(&m, format_dsm_markdown);
	cr_assert_not_null(strstr(markdown, "/p/a\\|b"));
	cr_assert_null(strstr(markdown, " /p/a|b "),
	               "an unescaped pipe opens a column that is not there");

	free(markdown);
	dsm_free(&m);
	scene_free(&s);
}

/* Every line of the Markdown table is the same width, escaping included. A
 * column measured before escaping comes out a character short for every pipe
 * in it, and the raw document goes ragged. */
Test(format_dsm, the_markdown_columns_are_measured_after_escaping)
{
	Scene              s;
	ElcOptions         opts    = { 0 };
	Dsm                m       = { 0 };
	static const char *paths[] = { "/p/a|b/x.c", "/p/z/y.c" };
	static const char *fns[]   = { "x_fn", "y_fn" };
	size_t             from[]  = { 0 };
	size_t             to[]    = { 1 };
	char              *markdown;
	size_t             width   = 0;

	scene_build(&s, paths, fns, 2, from, to, 1);
	cr_assert_eq(dsm_build(&s.graph, &s.report, &opts, &m), 0);

	markdown = rendered(&m, format_dsm_markdown);
	for (char *line = strtok(markdown, "\n"); line;
	     line = strtok(NULL, "\n")) {
		if (line[0] != '|')
			continue;
		if (width == 0)
			width = strlen(line);
		cr_assert_eq(strlen(line), width,
		             "every row of the grid is the same width");
	}
	cr_assert_gt(width, 0);

	free(markdown);
	dsm_free(&m);
	scene_free(&s);
}

/* An empty matrix renders as its heading, its convention, and its column
 * names — not as nothing. A section that vanishes when it has no content
 * makes the report's shape vary with its content. */
Test(format_dsm, an_empty_matrix_still_renders)
{
	Dsm   m = { 0 };
	char *table = rendered(&m, format_dsm_table);

	cr_assert_not_null(strstr(table, DSM_CORNER));
	free(table);
}

/* The companion is off unless asked for, and produces nothing where the report
 * has no path to derive a name from — the two tests the GraphML export makes.
 * Regeneration is not a third: a record carries the matrix (HLR-104,
 * HLR-180). */
Test(format_dsm, the_companion_is_written_only_when_asked_and_named)
{
	ElcOptions opts = { 0 };

	cr_assert_not(dsm_warranted(&opts), "off unless asked for");

	opts.dsm = true;
	cr_assert_not(dsm_warranted(&opts),
	              "a report on standard output has no name to derive");

	opts.output_path = "report.md";
	cr_assert(dsm_warranted(&opts));

	opts.mode = MODE_REGENERATE;
	cr_assert(dsm_warranted(&opts),
	          "a record carries the matrix, so there is something to write");
}

Test(format_dsm, free_is_safe_on_null_and_on_a_zeroed_matrix)
{
	Dsm m = { 0 };

	dsm_free(NULL);
	dsm_free(&m);
	dsm_free(&m);
	cr_assert_eq(m.count, 0);
}
