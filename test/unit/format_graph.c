/* test/unit/format_graph.c — unit tests for src/format_graph.c.
 *
 * The naming and warranted rules are pure decisions over an ElcOptions and a
 * path, and are tested here directly. Whether the emitted GraphML says the
 * right thing about a real project is the `graph/` fixture group's job — that
 * is a question about content, and content is compared against a hand-written
 * expected file rather than against assertions.
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "analyze.h"
#include "elc.h"
#include "format_graph.h"
#include "graph.h"
#include "purify.h"
#include "report.h"

/* ---------------------------------------------------------- scaffolding --
 *
 * A three-function graph, built from hand-made facts. Only the raw and
 * purified drawings need one: every other test in this file is a decision over
 * an `ElcOptions` and a path, with no graph in it at all.
 */

static FileMetrics *one_file(void)
{
	static const char *const names[] = { "top", "middle", "leaf" };
	FileMetrics             *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path      = strdup("/p/a.c");
	m->directory = strdup("/p");
	m->language  = strdup("c");
	cr_assert_not_null(m->path);
	cr_assert_not_null(m->directory);
	cr_assert_not_null(m->language);
	m->functions = calloc(3, sizeof *m->functions);
	cr_assert_not_null(m->functions);
	for (size_t i = 0; i < 3; i++) {
		m->functions[i].name       = strdup(names[i]);
		cr_assert_not_null(m->functions[i].name);
		m->functions[i].start_line = (uint32_t)(i * 10 + 1);
		m->functions[i].end_line   = (uint32_t)(i * 10 + 5);
	}
	m->function_count = 3;
	return m;
}

/* `top -> middle -> leaf`, so the middle node is the one whose masking is
 * visible in both directions. */
static void build_graph(Sdg *g, Report *r)
{
	MetricsAccumulator acc   = { 0 };
	ElcOptions         opts  = { 0 };
	FactList           facts = { 0 };
	FileFacts         *f     = calloc(1, sizeof *f);

	cr_assert_not_null(f);
	f->path = strdup("/p/a.c");
	cr_assert_not_null(f->path);
	f->calls = calloc(2, sizeof *f->calls);
	cr_assert_not_null(f->calls);
	f->calls[0].callee = strdup("middle");
	f->calls[0].caller = 0;
	f->calls[0].line   = 1;
	f->calls[1].callee = strdup("leaf");
	f->calls[1].caller = 1;
	f->calls[1].line   = 2;
	cr_assert_not_null(f->calls[0].callee);
	cr_assert_not_null(f->calls[1].callee);
	f->call_count    = 2;
	f->call_capacity = 2;

	cr_assert_eq(metrics_add(&acc, one_file()), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, r), 0);
	metrics_free(&acc);
	cr_assert_eq(factlist_add(&facts, f), 0);
	cr_assert_eq(graph_build(&facts, r, g), 0);
	factlist_free(&facts);
}

static size_t call_edges(const Sdg *g)
{
	size_t n = 0;

	for (size_t i = 0; i < g->edge_count; i++)
		if (g->edges[i].kind == EDGE_CALL)
			n++;
	return n;
}

static char *slurp(const char *path)
{
	FILE  *file = fopen(path, "r");
	char  *text;
	long   size;
	size_t got;

	cr_assert_not_null(file);
	cr_assert_eq(fseek(file, 0, SEEK_END), 0);
	size = ftell(file);
	cr_assert_gt(size, 0);
	rewind(file);
	text = malloc((size_t)size + 1);
	cr_assert_not_null(text);
	got = fread(text, 1, (size_t)size, file);
	text[got] = '\0';
	fclose(file);
	return text;
}

/* ------------------------------------------------------------- warranted */

Test(format_graph, graphml_is_off_unless_asked_for)
{
	ElcOptions opts = { 0 };

	opts.output_path = "report.md";
	cr_assert_not(graph_graphml_warranted(&opts),
	              "the export is opt-in, unlike the .dot companion "
	              "(HLR-106, LLR-GML-02)");
}

Test(format_graph, graphml_needs_an_output_path)
{
	ElcOptions opts = { 0 };

	opts.graphml     = true;
	opts.output_path = NULL;

	cr_assert_not(graph_graphml_warranted(&opts),
	              "with the report on standard output there is no path to "
	              "derive the companion's name from, so no file is written "
	              "(HLR-104, HLR-106, LLR-GML-03)");
}

Test(format_graph, graphml_is_written_when_asked_for_and_named)
{
	ElcOptions opts = { 0 };

	opts.graphml     = true;
	opts.output_path = "report.md";

	cr_assert(graph_graphml_warranted(&opts));
}

/* The `.dot` companion runs the default the other way round: written unless
 * refused. A zeroed ElcOptions is therefore the *enabled* state, which is
 * exactly why `no_dot` records the refusal rather than the request. */

Test(format_graph, dot_is_written_without_being_asked_for)
{
	ElcOptions opts = { 0 };

	opts.output_path = "report.md";
	cr_assert(graph_dot_warranted(&opts),
	          "generation is enabled by default, unlike the GraphML export "
	          "(HLR-103, LLR-WAR-01)");
}

Test(format_graph, dot_is_suppressed_by_the_disable_switch)
{
	ElcOptions opts = { 0 };

	opts.output_path = "report.md";
	opts.no_dot      = true;

	cr_assert_not(graph_dot_warranted(&opts));
}

Test(format_graph, dot_needs_an_output_path)
{
	ElcOptions opts = { 0 };

	opts.output_path = NULL;

	cr_assert_not(graph_dot_warranted(&opts),
	              "with the report on standard output there is no path to "
	              "derive the companion's name from (HLR-104, LLR-WAR-02)");
}

Test(format_graph, dot_needs_an_output_path_even_when_disabled)
{
	ElcOptions opts = { 0 };

	opts.output_path = NULL;
	opts.no_dot      = true;

	cr_assert_not(graph_dot_warranted(&opts),
	              "\"whether or not generation was disabled\" is the "
	              "requirement's wording: the destination decides it, not "
	              "the switch (HLR-104)");
}

Test(format_graph, no_dot_is_written_from_a_saved_record)
{
	ElcOptions opts = { 0 };

	opts.output_path = "report.md";
	opts.mode        = MODE_REGENERATE;

	cr_assert_not(graph_dot_warranted(&opts),
	              "a record carries the findings of a run, not the topology "
	              "they were drawn from, so there is no graph to draw "
	              "(HLR-122, LLR-WAR-03)");
}

/* ------------------------------------------------------------ the naming */

Test(format_graph, the_companion_replaces_the_extension)
{
	char *p = graph_companion_path("report.md", "graphml");

	cr_assert_not_null(p);
	cr_assert_str_eq(p, "report.graphml",
	                 "substituted, not appended: report.md.graphml would "
	                 "be a second extension rather than a sibling file");
	free(p);
}

Test(format_graph, both_companions_derive_from_one_output_path)
{
	char *dot     = graph_companion_path("/a/report.md", "dot");
	char *graphml = graph_companion_path("/a/report.md", "graphml");

	cr_assert_not_null(dot);
	cr_assert_not_null(graphml);
	cr_assert_str_eq(dot, "/a/report.dot");
	cr_assert_str_eq(graphml, "/a/report.graphml",
	                 "one derivation serves both, so neither can take an "
	                 "output path of its own (HLR-119)");
	free(dot);
	free(graphml);
}

Test(format_graph, a_path_without_an_extension_gains_one)
{
	char *p = graph_companion_path("report", "graphml");

	cr_assert_not_null(p);
	cr_assert_str_eq(p, "report.graphml");
	free(p);
}

Test(format_graph, a_dot_in_a_directory_is_not_an_extension)
{
	char *p = graph_companion_path("/a/b.d/report", "graphml");

	cr_assert_not_null(p);
	cr_assert_str_eq(p, "/a/b.d/report.graphml",
	                 "the extension search is scoped to the last path "
	                 "component, or a versioned directory name eats the "
	                 "file name");
	free(p);
}

Test(format_graph, a_directory_dot_with_an_extension_still_substitutes)
{
	char *p = graph_companion_path("/a/b.d/report.md", "graphml");

	cr_assert_not_null(p);
	cr_assert_str_eq(p, "/a/b.d/report.graphml");
	free(p);
}

/* ---------------------------------------------- the raw and purified draw --
 *
 * Two drawings of one graph, because a single drawing of the recovery view
 * cannot show what purification acted on: it looks like a clean layering
 * whether the masking was right or wrong (HLR-178).
 */

Test(format_graph, the_drawings_are_opt_in_and_need_an_output_path)
{
	ElcOptions opts = { 0 };

	opts.output_path = "report.md";
	cr_assert_not(graph_purify_dot_warranted(&opts),
	              "the pair is opt-in, like the GraphML export and unlike "
	              "the annotated call tree (HLR-178)");

	opts.purify_dot  = true;
	opts.output_path = NULL;
	cr_assert_not(graph_purify_dot_warranted(&opts),
	              "with the report on standard output there is no path to "
	              "derive either name from (HLR-104, HLR-119)");

	opts.output_path = "report.md";
	cr_assert(graph_purify_dot_warranted(&opts));

	opts.mode = MODE_REGENERATE;
	cr_assert_not(graph_purify_dot_warranted(&opts),
	              "a record carries findings, not the graph to draw");
}

Test(format_graph, the_companion_names_are_derived_from_the_report_path)
{
	char *raw      = graph_companion_path("out/report.md", "raw.dot");
	char *purified = graph_companion_path("out/report.md", "purified.dot");

	/* The one companion rule, applied to two more artefacts: the extension
	 * is substituted on the report's own path, and neither drawing accepts
	 * a path of its own (HLR-119). */
	cr_assert_str_eq(raw, "out/report.raw.dot");
	cr_assert_str_eq(purified, "out/report.purified.dot");
	free(raw);
	free(purified);
}

Test(format_graph, a_masked_node_is_greyed_rather_than_deleted)
{
	Sdg           g = { 0 };
	Report        r = { 0 };
	PurifyResults p = { 0 };
	char          path[] = "/tmp/elc-purified-XXXXXX";
	int           fd     = mkstemp(path);
	char         *text;

	cr_assert_neq(fd, -1);
	close(fd);
	build_graph(&g, &r);

	p.node_count = g.node_count;
	p.classes    = calloc(g.node_count, sizeof *p.classes);
	cr_assert_not_null(p.classes);
	p.classes[1].klass  = PURIFY_GOD_OBJECT;
	p.classes[1].masked = true;

	cr_assert_eq(graph_write_purify_dot(&g, &p, true, path), 0);
	text = slurp(path);

	/* **Distinguished, never removed** (HLR-178). A drawing that deleted
	 * the masked nodes could not show what purification did, which is the
	 * entire reason there are two of them. The node is present, greyed,
	 * labelled with its class, and holds no edge. */
	cr_assert_not_null(strstr(text, "n1 [label=\""));
	cr_assert_not_null(strstr(text, "(god object)"));
	cr_assert_not_null(strstr(text, "fillcolor=\"#d0d0d0\""));
	cr_assert_null(strstr(text, "n1 -> "));
	cr_assert_null(strstr(text, " -> n1;"));

	free(text);
	free(p.classes);
	unlink(path);
	graph_free(&g);
	report_free(&r);
}

Test(format_graph, the_raw_drawing_holds_every_call_edge)
{
	Sdg           g = { 0 };
	Report        r = { 0 };
	PurifyResults p = { 0 };
	char          path[] = "/tmp/elc-raw-XXXXXX";
	int           fd     = mkstemp(path);
	char         *text;
	size_t        arrows = 0;

	cr_assert_neq(fd, -1);
	close(fd);
	build_graph(&g, &r);

	p.node_count = g.node_count;
	p.classes    = calloc(g.node_count, sizeof *p.classes);
	cr_assert_not_null(p.classes);
	p.classes[1].klass  = PURIFY_GOD_OBJECT;
	p.classes[1].masked = true;

	/* The raw drawing is the graph *as built*: the masking is what the
	 * other one shows, and a raw drawing that anticipated it would leave
	 * the pair with nothing to compare (HLR-178). */
	cr_assert_eq(graph_write_purify_dot(&g, &p, false, path), 0);
	text = slurp(path);
	for (const char *at = text; (at = strstr(at, " -> ")); at += 4)
		arrows++;
	cr_assert_eq(arrows, call_edges(&g));
	cr_assert_null(strstr(text, "(god object)"));

	free(text);
	free(p.classes);
	unlink(path);
	graph_free(&g);
	report_free(&r);
}
