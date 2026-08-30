/* test/unit/annotate.c — unit tests for src/annotate.c.
 *
 * Where a finding *lands* is this module's whole job, and it is the question
 * two drawings must never answer differently (HLR-217). So the tests here are
 * about placement — a definition site, a component, the users of a global —
 * and about the severities and marks that come to rest on a node. What either
 * drawing then does with an annotation belongs to that drawing's own tests.
 *
 * The findings are hand-built rather than produced by a run: a finding placed
 * on the wrong node is invisible in an end-to-end test that only counts them,
 * and the cases worth pinning here — a name shared by two functions, an object
 * touched by one — are awkward to provoke through a whole analysis.
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "annotate.h"
#include "elc.h"
#include "graph.h"
#include "report.h"

/* ---------------------------------------------------------- scaffolding -- */

/* Two files, three functions, with `dup` defined in both: `/p/a.c` holds
 * `top` and `dup`, `/p/b.c` holds `dup`. The repeated name is the case that
 * separates matching on a definition site from matching on a name. */
static FileMetrics *file_with(const char *path, const char *const *names,
                              size_t count)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path      = strdup(path);
	m->directory = strdup("/p");
	m->language  = strdup("c");
	cr_assert_not_null(m->path);
	cr_assert_not_null(m->directory);
	cr_assert_not_null(m->language);

	m->functions = calloc(count, sizeof *m->functions);
	cr_assert_not_null(m->functions);
	for (size_t i = 0; i < count; i++) {
		m->functions[i].name       = strdup(names[i]);
		cr_assert_not_null(m->functions[i].name);
		m->functions[i].start_line = (uint32_t)(i * 10 + 1);
		m->functions[i].end_line   = (uint32_t)(i * 10 + 5);
	}
	m->function_count = count;
	return m;
}

typedef struct {
	Report report;
	Sdg    graph;
} Scene;

static void scene_build(Scene *s)
{
	static const char *const A[] = { "top", "dup" };
	static const char *const B[] = { "dup" };
	MetricsAccumulator       acc   = { 0 };
	ElcOptions               opts  = { 0 };
	FactList                 facts = { 0 };
	FileFacts               *fa    = calloc(1, sizeof *fa);
	FileFacts               *fb    = calloc(1, sizeof *fb);

	memset(s, 0, sizeof *s);
	cr_assert_not_null(fa);
	cr_assert_not_null(fb);
	fa->path = strdup("/p/a.c");
	fb->path = strdup("/p/b.c");
	cr_assert_not_null(fa->path);
	cr_assert_not_null(fb->path);

	cr_assert_eq(metrics_add(&acc, file_with("/p/a.c", A, 2)), 0);
	cr_assert_eq(metrics_add(&acc, file_with("/p/b.c", B, 1)), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &s->report), 0);
	metrics_free(&acc);

	cr_assert_eq(factlist_add(&facts, fa), 0);
	cr_assert_eq(factlist_add(&facts, fb), 0);
	cr_assert_eq(graph_build(&facts, &s->report, &s->graph), 0);
	factlist_free(&facts);
}

static void scene_free(Scene *s)
{
	graph_free(&s->graph);
	report_free(&s->report);
}

/* One hand-built finding on the report. `where` and `line` together are the
 * definition site; either alone means something else entirely. */
static void add_finding(Report *r, const char *severity, const char *subject,
                        const char *where, uint32_t line)
{
	FindingRow *grown = realloc(r->findings,
	                            (r->finding_count + 1) * sizeof *grown);

	cr_assert_not_null(grown);
	r->findings = grown;

	FindingRow *f = &r->findings[r->finding_count];

	memset(f, 0, sizeof *f);
	f->severity    = strdup(severity);
	f->measurement = strdup("complexity");
	f->subject     = strdup(subject);
	f->where       = strdup(where);
	f->detail      = strdup("cyclomatic complexity 20");
	f->source      = strdup("McCabe");
	f->line        = line;
	cr_assert_not_null(f->severity);
	cr_assert_not_null(f->subject);
	cr_assert_not_null(f->where);
	r->finding_count++;
}

/* The index of the node defined at `file`:`line`. */
static size_t node_index(const Sdg *g, const char *file, uint32_t line)
{
	for (size_t i = 0; i < g->node_count; i++)
		if (g->nodes[i].line_start == line &&
		    strcmp(g->nodes[i].file, file) == 0)
			return i;
	cr_assert_fail("no node defined at %s:%u", file, line);
	return 0;
}

typedef struct {
	Annotation *nodes;
	Annotation *comps;
	char       *notes;
	uint32_t   *chain;
} Built;

static Built build(Scene *s)
{
	Built b = { 0 };

	cr_assert_eq(annotations_build(&s->graph, &s->report, &b.nodes,
	                               &b.comps, &b.notes, &b.chain), 0);
	return b;
}

static void built_free(Built *b, const Scene *s)
{
	annotations_free(b->nodes, s->graph.node_count);
	annotations_free(b->comps, s->graph.component_count);
	free(b->notes);
	free(b->chain);
}

/* ------------------------------------------------------------- placement -- */

/* A finding names a *definition site*, and two functions sharing a name are
 * two nodes. Matching on the name would mark both, which is a false claim
 * about the one that is clean (LLR-ANN-02). */
Test(annotate, a_finding_lands_on_the_definition_site_not_the_name)
{
	Scene s;
	Built b;
	size_t marked, other;

	scene_build(&s);
	/* `dup` in a.c is at line 11; `dup` in b.c is at line 1. */
	add_finding(&s.report, "critical", "dup", "/p/a.c", 11);
	b = build(&s);

	marked = node_index(&s.graph, "/p/a.c", 11);
	other  = node_index(&s.graph, "/p/b.c", 1);

	cr_assert_not_null(b.nodes[marked].note,
	                   "the finding did not reach the node it names");
	cr_assert_eq(b.nodes[other].severity, 0,
	             "a function was marked because it shares a name");
	cr_assert_null(b.nodes[other].note);

	built_free(&b, &s);
	scene_free(&s);
}

/* Severities rank rather than accumulate: a node carrying a critical and a
 * warning is a critical one (LLR-ANN-01, HLR-123). */
Test(annotate, the_highest_severity_is_the_one_kept)
{
	Scene s;
	Built b;
	size_t n;

	scene_build(&s);
	add_finding(&s.report, "warning",  "top", "/p/a.c", 1);
	add_finding(&s.report, "critical", "top", "/p/a.c", 1);
	add_finding(&s.report, "warning",  "top", "/p/a.c", 1);
	b = build(&s);

	n = node_index(&s.graph, "/p/a.c", 1);
	cr_assert_eq(b.nodes[n].severity, 2,
	             "the worst finding did not decide the severity");
	/* All three are still readable: the drawing shows the worst and the
	 * tooltip says what was found. */
	cr_assert_not_null(strstr(b.nodes[n].note, "critical"));
	cr_assert_not_null(strstr(b.nodes[n].note, "warning"));

	built_free(&b, &s);
	scene_free(&s);
}

/* A finding naming a component path lands on the component, not on some
 * function of it (LLR-ANN-02). */
Test(annotate, a_component_finding_lands_on_the_component)
{
	Scene s;
	Built b;

	scene_build(&s);
	add_finding(&s.report, "warning", "/p/a.c", "", 0);
	b = build(&s);

	{
		bool any_node = false;

		for (size_t i = 0; i < s.graph.node_count; i++)
			any_node |= b.nodes[i].note != NULL;
		cr_assert_not(any_node,
		              "a component finding was placed on a function");
	}
	{
		bool any_comp = false;

		for (size_t c = 0; c < s.graph.component_count; c++)
			any_comp |= b.comps[c].note != NULL;
		cr_assert(any_comp, "the component finding reached nothing");
	}

	built_free(&b, &s);
	scene_free(&s);
}

/* A finding that describes neither a node, nor a component, nor a global
 * object belongs to the graph as a whole, and reaches the caller rather than
 * being dropped (LLR-ANN-02). */
Test(annotate, a_finding_about_the_graph_reaches_the_notes)
{
	Scene s;
	Built b;

	scene_build(&s);
	add_finding(&s.report, "warning", "the call tree", "", 0);
	b = build(&s);

	cr_assert_not_null(b.notes, "a graph-wide finding was dropped");
	/* The note is the finding as a reader meets it — its severity, what
	 * was measured and the measurement — rather than its subject, which
	 * is precisely the thing such a finding does not usefully have. */
	cr_assert_not_null(strstr(b.notes, "complexity"));
	cr_assert_not_null(strstr(b.notes, "cyclomatic complexity 20"));

	built_free(&b, &s);
	scene_free(&s);
}

/* Nothing found, nothing marked: the common case must add no severity and no
 * note, so a drawing can test for their presence (LLR-ANN-01). */
Test(annotate, a_clean_graph_carries_no_annotation)
{
	Scene s;
	Built b;

	scene_build(&s);
	b = build(&s);

	for (size_t i = 0; i < s.graph.node_count; i++) {
		cr_assert_eq(b.nodes[i].severity, 0);
		cr_assert_eq(b.nodes[i].marks, 0u);
		cr_assert_null(b.nodes[i].note);
	}
	cr_assert_null(b.notes);

	built_free(&b, &s);
	scene_free(&s);
}

/* ----------------------------------------------------------- the marks --- */

/* Recursion is marked on every member from the report's cycle rows, because
 * the catalogue locates a cycle at one subject and HLR-105 asks for the
 * members (LLR-ANN-03). */
Test(annotate, recursion_is_marked_on_every_member_of_the_cycle)
{
	Scene s;
	Built b;
	size_t n;

	scene_build(&s);
	s.report.cycles = calloc(1, sizeof *s.report.cycles);
	cr_assert_not_null(s.report.cycles);
	s.report.cycles[0].members = calloc(1, sizeof *s.report.cycles[0].members);
	cr_assert_not_null(s.report.cycles[0].members);
	s.report.cycles[0].members[0] = strdup("top");
	cr_assert_not_null(s.report.cycles[0].members[0]);
	s.report.cycles[0].count = 1;
	s.report.cycle_count     = 1;

	b = build(&s);
	n = node_index(&s.graph, "/p/a.c", 1);

	cr_assert(b.nodes[n].marks & MARK_RECURSIVE,
	          "a member of a recursive cycle was not marked");
	cr_assert_not_null(b.nodes[n].note,
	                   "the cycle was marked but not explained");

	built_free(&b, &s);
	scene_free(&s);
}

/* The unreachable set marks the node at its definition site (LLR-ANN-01). */
Test(annotate, an_unreachable_function_is_marked_at_its_definition_site)
{
	Scene s;
	Built b;
	size_t marked, other;

	scene_build(&s);
	s.report.unreachable = calloc(1, sizeof *s.report.unreachable);
	cr_assert_not_null(s.report.unreachable);
	s.report.unreachable[0].file = strdup("/p/b.c");
	cr_assert_not_null(s.report.unreachable[0].file);
	s.report.unreachable[0].line = 1;
	s.report.unreachable_count   = 1;

	b = build(&s);
	marked = node_index(&s.graph, "/p/b.c", 1);
	other  = node_index(&s.graph, "/p/a.c", 11);

	cr_assert(b.nodes[marked].marks & MARK_UNREACHABLE);
	cr_assert_not(b.nodes[other].marks & MARK_UNREACHABLE,
	              "the function sharing its name was marked too");

	built_free(&b, &s);
	scene_free(&s);
}

/* An edge is a step of the chain only where the two sites are consecutive in
 * it: the pair, not mere membership (LLR-ANN-01, HLR-088). */
Test(annotate, only_consecutive_pairs_are_steps_of_the_chain)
{
	const uint32_t chain[] = { 3, 7, 9 };

	cr_assert(annotation_on_chain(chain, 3, 3, 7));
	cr_assert(annotation_on_chain(chain, 3, 7, 9));
	cr_assert_not(annotation_on_chain(chain, 3, 3, 9),
	              "a pair that skips a step was called one");
	cr_assert_not(annotation_on_chain(chain, 3, 7, 3),
	              "the chain is directed and was read backwards");
	cr_assert_not(annotation_on_chain(NULL, 0, 3, 7));
}
