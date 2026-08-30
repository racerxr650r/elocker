/* test/unit/report_html.c — unit tests for src/report_html.c.
 *
 * Built from hand-made facts and reports, as the DSM, arch and graph tests
 * are: what is under test is how a graph is *arranged* into a containment
 * hierarchy, and a fixture that had to be parsed first would put a query
 * file's behaviour into every assertion about the arrangement.
 *
 * The cases worth reading first are the three a wrong document fails silently
 * on. **The containment** — a `parent` naming the wrong tier still produces a
 * document that renders, just one drawn against a structure nobody declared.
 * **The absent parent** — a file in no stratum must have no `parent` key at
 * all, because an empty one and a synthesised layer are both structures the
 * user did not state (HLR-213). And **the embedding escape**, which is not
 * the serialiser's: a name containing `</script>` is well-formed JSON that
 * ends the element holding it, and the page then renders empty rather than
 * wrong — a failure no assertion about the JSON alone would catch
 * (LLR-HTM-03).
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "analyze.h"
#include "arch.h"
#include "cli.h"
#include "elc.h"
#include "graph.h"
#include "report.h"
#include "report_html.h"

/* ------------------------------------------------------------ scaffolding */

static FileMetrics *file_with(const char *path, const char *function)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path      = strdup(path);
	m->language  = strdup("c");
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

/* Write the page to a temporary file and read it back. Assertions are about
 * the bytes a reader's browser would receive, which is the only level at which
 * the embedding escape of LLR-HTM-03 is observable at all. The caller frees. */
static char *page_of(const Report *report, const Sdg *g,
                     const ElcOptions *opts)
{
	FILE *fp = tmpfile();
	char *buffer;
	long  length;

	cr_assert_not_null(fp, "could not open a temporary stream");

	/* Rendered to a stream the test owns, because that is what the format
	 * does: `emit` opens the destination and this renderer never does
	 * (LLR-HTM-05). */
	cr_assert_eq(format_html(report, g, opts, fp), 0);

	length = ftell(fp);
	cr_assert_geq(length, 0);
	rewind(fp);

	buffer = calloc(1, (size_t)length + 1);
	cr_assert_not_null(buffer);
	cr_assert_eq(fread(buffer, 1, (size_t)length, fp), (size_t)length);
	fclose(fp);
	return buffer;
}

/* The embedded payload alone — everything between the assignment and the
 * statement that ends it. Returned borrowed into `page`, NUL-terminated in
 * place, so the caller must not free it separately. */
static char *payload_of(char *page)
{
	static const char *const OPEN = "const graphData = ";
	char *start = strstr(page, OPEN);
	char *end;

	cr_assert_not_null(start, "the page carries no payload assignment");
	start += strlen(OPEN);
	end = strstr(start, ";\n</script>");
	cr_assert_not_null(end, "the payload assignment is unterminated");
	*end = '\0';
	return start;
}

static ElcOptions two_layers(void)
{
	ElcOptions opts = { 0 };

	cr_assert_eq(parse_stratum("app:/p/app/*", &opts), 0);
	cr_assert_eq(parse_stratum("hal:/p/hal/*", &opts), 0);
	return opts;
}

/* Two components in declared layers and one in none, so that every case of
 * LLR-CYT-02 is present in one scene. */
static const char *const PATHS[]     = { "/p/app/a.c", "/p/hal/b.c",
                                         "/p/vendor/c.c" };
static const char *const FUNCTIONS[] = { "app_fn", "hal_fn", "vendor_fn" };

/* One hand-built finding on the report, so that a node with an annotation and
 * a node without are both present in one document. */
static void add_finding(Report *r, const char *severity, const char *subject,
                        const char *where, uint32_t line)
{
	FindingRow *grown = realloc(r->findings,
	                            (r->finding_count + 1) * sizeof *grown);
	FindingRow *f;

	cr_assert_not_null(grown);
	r->findings = grown;
	f = &r->findings[r->finding_count];

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

/* The one element carrying `label`, copied out whole.
 *
 * Scanned in both directions from the label rather than forward from it: the
 * payload is written with its keys sorted, so `finding` and `eloc` sit *before*
 * `label` and a forward scan to the next `}` would miss them. The caller frees.
 */
static char *element_with(const char *payload, const char *label)
{
	char  needle[128];
	const char *at, *open, *close;
	char *copy;

	snprintf(needle, sizeof needle, "\"label\":\"%s\"", label);
	at = strstr(payload, needle);
	cr_assert_not_null(at, "no element is labelled %s", label);

	for (open = at; open > payload && *open != '{'; open--)
		;
	close = strchr(at, '}');
	cr_assert_not_null(close);

	copy = calloc(1, (size_t)(close - open) + 2);
	cr_assert_not_null(copy);
	memcpy(copy, open, (size_t)(close - open) + 1);
	return copy;
}

/* ------------------------------------------------------- the three tiers -- */

/* One node per declared stratum, identified by ordinal rather than by name:
 * the name is user text, and two strata whose names differ only in characters
 * an identifier cannot carry would collide and silently reparent a layer
 * (LLR-CYT-01). */
Test(report_html, a_node_is_emitted_for_each_declared_layer)
{
	Scene       s;
	ElcOptions  opts = two_layers();
	char       *page;

	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);
	page = page_of(&s.report, &s.graph, &opts);

	cr_assert_not_null(strstr(page, "\"id\":\"layer_0\""));
	cr_assert_not_null(strstr(page, "\"id\":\"layer_1\""));
	cr_assert_null(strstr(page, "\"id\":\"layer_2\""),
	               "a layer nobody declared was emitted");
	cr_assert_not_null(strstr(page, "\"label\":\"app\""));
	cr_assert_not_null(strstr(page, "\"label\":\"hal\""));

	free(page);
	cli_options_free(&opts);
	scene_free(&s);
}

/* A file's container is the layer `arch.c` assigned it, and is not matched
 * again here. Two matchers over one set of patterns eventually disagree about
 * which layer a file is in, and this drawing would then contradict the matrix
 * printed beside it (LLR-CYT-02, HLR-164). */
Test(report_html, a_file_names_the_layer_arch_assigned_it)
{
	Scene       s;
	ElcOptions  opts = two_layers();
	char       *page, *payload;

	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);
	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	/* `/p/app/a.c` is component 0 by the report's sorted file order, and
	 * matches the first declared stratum. The label has shed the `/p/`
	 * every component shares, and `path` keeps it (LLR-CYT-02). */
	cr_assert_not_null(strstr(payload,
	        "\"id\":\"file_0\",\"label\":\"app/a.c\","
	        "\"parent\":\"layer_0\",\"path\":\"/p/app/a.c\""));
	cr_assert_not_null(strstr(payload,
	        "\"id\":\"file_1\",\"label\":\"hal/b.c\","
	        "\"parent\":\"layer_1\",\"path\":\"/p/hal/b.c\""));

	free(page);
	cli_options_free(&opts);
	scene_free(&s);
}

/* **A file matching no stratum carries no `parent` key at all** — not an
 * empty one and not a synthesised layer. `stratum_of_components` places such
 * a file outside the declared architecture because the user said nothing
 * about it, and a renderer that invented a container would report a structure
 * nobody drew (LLR-CYT-02, HLR-078). */
Test(report_html, a_file_in_no_declared_layer_has_no_parent)
{
	Scene       s;
	ElcOptions  opts = two_layers();
	char       *page, *payload, *node;

	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);
	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	node = strstr(payload, "\"label\":\"vendor/c.c\"");
	cr_assert_not_null(node, "the unmatched component was not emitted");

	/* The key is absent from this element rather than merely unset: the
	 * element ends at the next `}` and no `parent` appears before it. */
	{
		char *end = strchr(node, '}');
		cr_assert_not_null(end);
		*end = '\0';
		cr_assert_null(strstr(node, "\"parent\""),
		               "a container was invented for a file in no "
		               "declared layer");
	}

	free(page);
	cli_options_free(&opts);
	scene_free(&s);
}

/* The prefix a label sheds ends at a path separator, never inside a name:
 * `/p/app/` and `/p/apple/` share `/p/app` byte-wise, and a label `le/b.c`
 * would name a directory that does not exist. What was shed is recoverable
 * from `path` on the same node rather than lost (LLR-CYT-02). */
Test(report_html, the_shed_prefix_ends_at_a_separator)
{
	static const char *const NEAR[] = { "/p/app/a.c", "/p/apple/b.c" };
	static const char *const FNS[]  = { "app_fn", "apple_fn" };
	Scene       s;
	ElcOptions  opts = { 0 };
	char       *page, *payload;

	scene_build(&s, NEAR, FNS, 2, NULL, NULL, 0);
	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	cr_assert_not_null(strstr(payload,
	        "\"label\":\"app/a.c\",\"path\":\"/p/app/a.c\""));
	cr_assert_not_null(strstr(payload,
	        "\"label\":\"apple/b.c\",\"path\":\"/p/apple/b.c\""));

	free(page);
	scene_free(&s);
}

/* A lone component shares its whole directory with nothing, so it is labelled
 * by its file name — the one case where the shared prefix is not a claim
 * about other components (LLR-CYT-02). */
Test(report_html, a_lone_component_is_labelled_by_its_file_name)
{
	static const char *const ONE[]    = { "/p/deep/dir/only.c" };
	static const char *const ONE_FN[] = { "only_fn" };
	Scene       s;
	ElcOptions  opts = { 0 };
	char       *page, *payload;

	scene_build(&s, ONE, ONE_FN, 1, NULL, NULL, 0);
	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	cr_assert_not_null(strstr(payload,
	        "\"label\":\"only.c\",\"path\":\"/p/deep/dir/only.c\""));

	free(page);
	scene_free(&s);
}

/* With nothing declared the document has two tiers, not three rooted in a
 * fiction. A run declaring no strata is the common case, and it must not
 * acquire an invented top (LLR-CYT-01, LLR-CYT-02). */
Test(report_html, with_no_strata_the_document_has_two_tiers)
{
	Scene       s;
	ElcOptions  opts = { 0 };
	char       *page;

	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);
	page = page_of(&s.report, &s.graph, &opts);

	cr_assert_null(strstr(page, "\"tier\":\"layer\""),
	               "a layer was emitted for a run that declared none");
	cr_assert_null(strstr(page, "\"parent\":\"layer_"));
	cr_assert_not_null(strstr(page, "\"tier\":\"file\""));
	cr_assert_not_null(strstr(page, "\"tier\":\"function\""));

	free(page);
	scene_free(&s);
}

/* A function's container is the file that defines it, and the figures beside
 * it are the ones the graph already holds rather than any this renderer
 * derived (LLR-CYT-03, HLR-099). */
Test(report_html, a_function_names_the_file_that_defines_it)
{
	Scene       s;
	ElcOptions  opts = two_layers();
	char       *page, *payload, *node, *end;

	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);
	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	node = strstr(payload, "\"label\":\"app_fn\"");
	cr_assert_not_null(node);
	end = strchr(node, '}');
	cr_assert_not_null(end);
	*end = '\0';

	cr_assert_not_null(strstr(node, "\"parent\":\"file_0\""));
	cr_assert_not_null(strstr(node, "\"tier\":\"function\""));
	/* Copied from the node, which took it from the report. */
	cr_assert_not_null(strstr(node, "\"line\":1"));

	free(page);
	cli_options_free(&opts);
	scene_free(&s);
}

/* -------------------------------------------------------------- the edges */

/* Every edge joins two function nodes. **No meta-edge is emitted**: the
 * connection between two collapsed containers is synthesised by the viewer,
 * and an emitted one would state coupling a second time by a rule with no
 * threshold behind it (LLR-CYT-04, HLR-214). */
Test(report_html, edges_join_functions_and_never_containers)
{
	Scene       s;
	ElcOptions  opts   = two_layers();
	size_t      from[] = { 0, 1 };
	size_t      to[]   = { 1, 2 };
	char       *page, *payload, *scan;

	scene_build(&s, PATHS, FUNCTIONS, 3, from, to, 2);
	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	cr_assert_null(strstr(payload, "\"source\":\"file_"),
	               "a file-to-file meta-edge was emitted");
	cr_assert_null(strstr(payload, "\"target\":\"file_"));
	cr_assert_null(strstr(payload, "\"source\":\"layer_"));
	cr_assert_null(strstr(payload, "\"target\":\"layer_"));

	/* And every edge that *is* emitted names two function nodes. */
	for (scan = strstr(payload, "\"source\":"); scan;
	     scan = strstr(scan + 1, "\"source\":"))
		cr_assert_eq(strncmp(scan, "\"source\":\"func_", 15), 0);

	free(page);
	cli_options_free(&opts);
	scene_free(&s);
}

/* ---------------------------------------------------------- the embedding */

/* **The correctness core, and it is not the serialiser's job.** A name
 * containing `</script>` is well-formed JSON; the serialiser emits it
 * verbatim and is right to, and the HTML parser then ends the element at it —
 * the rest of the graph becomes body text and the page renders empty rather
 * than wrong. `<` and `&` are escaped so neither the element-termination case
 * nor the entity case can occur (LLR-HTM-03). */
Test(report_html, no_raw_angle_bracket_or_ampersand_reaches_the_payload)
{
	Scene       s;
	ElcOptions  opts = two_layers();
	char       *page, *payload;
	static const char *const HOSTILE[] = { "/p/app/a.c", "/p/hal/b.c",
	                                       "/p/vendor/c.c" };
	static const char *const NAMES[]   = { "</script><b>x", "a&b",
	                                       "plain" };

	scene_build(&s, HOSTILE, NAMES, 3, NULL, NULL, 0);
	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	cr_assert_null(strchr(payload, '<'),
	               "a raw '<' reached the script element and can end it");
	cr_assert_null(strchr(payload, '&'),
	               "a raw '&' reached the script element and will be "
	               "decoded as an entity");

	/* Escaped rather than dropped: the name is still in the document, and
	 * still readable by a JSON or a JavaScript parser.
	 *
	 * `>` is left alone deliberately. An HTML parser ends a script element
	 * on `</script` and never on a `>` by itself, so escaping the `<` closes
	 * the case completely; escaping the `>` as well would be a second rule
	 * doing nothing, and a reader of this file would have to work out which
	 * of the two was load-bearing. */
	cr_assert_not_null(strstr(payload, "\\u003c/script>"));
	cr_assert_not_null(strstr(payload, "a\\u0026b"));

	free(page);
	cli_options_free(&opts);
	scene_free(&s);
}

/* U+2028 and U+2029 are line terminators to a JavaScript parser and ordinary
 * characters to a JSON one, so a name carrying either is valid in the
 * document and a syntax error the moment it is embedded. This is the case
 * that is invisible in review (LLR-HTM-03). */
Test(report_html, the_javascript_line_terminators_are_escaped)
{
	Scene       s;
	ElcOptions  opts = two_layers();
	char       *page, *payload;
	static const char *const PS[]   = { "/p/app/a.c", "/p/hal/b.c",
	                                    "/p/vendor/c.c" };
	/* E2 80 A8 and E2 80 A9 as UTF-8, inside otherwise ordinary names. */
	static const char *const NAMES[] = { "a\xE2\x80\xA8z",
	                                     "b\xE2\x80\xA9z", "plain" };

	scene_build(&s, PS, FUNCTIONS, 3, NULL, NULL, 0);
	scene_free(&s);
	scene_build(&s, PS, NAMES, 3, NULL, NULL, 0);

	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	cr_assert_null(strstr(payload, "\xE2\x80\xA8"),
	               "a raw U+2028 reached the script element");
	cr_assert_null(strstr(payload, "\xE2\x80\xA9"),
	               "a raw U+2029 reached the script element");
	cr_assert_not_null(strstr(payload, "\\u2028"));
	cr_assert_not_null(strstr(payload, "\\u2029"));

	free(page);
	cli_options_free(&opts);
	scene_free(&s);
}

/* ----------------------------------------------------------- the page ----- */

/* The shell references the rendering library and its extension, and the glue
 * opens the view collapsed with the two behaviours that keep a descent
 * navigable (LLR-HTM-02, LLR-HTM-04, HLR-216). */
Test(report_html, the_page_loads_the_viewer_and_opens_collapsed)
{
	Scene       s;
	ElcOptions  opts = two_layers();
	char       *page;

	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);
	page = page_of(&s.report, &s.graph, &opts);

	cr_assert_not_null(strstr(page,
	        "https://unpkg.com/cytoscape/dist/cytoscape.min.js"));
	cr_assert_not_null(strstr(page,
	        "https://unpkg.com/cytoscape-expand-collapse/"
	        "cytoscape-expand-collapse.js"));
	cr_assert_not_null(strstr(page, "cytoscape({"));
	/* Ranked by call direction and respecting containment, which is what
	 * makes this drawing and the .dot companion two renderings of one
	 * picture (LLR-HTM-04). */
	cr_assert_not_null(strstr(page, "algorithm: 'layered'"));
	cr_assert_not_null(strstr(page,
	        "'elk.hierarchyHandling': 'INCLUDE_CHILDREN'"));
	/* The extension adds and removes children and does nothing else: a
	 * layout of its own re-ranks the drawing and its fisheye repositions
	 * the box, and either moves the file the reader just clicked
	 * (LLR-HTM-04, HLR-216). */
	cr_assert_not_null(strstr(page, "layoutBy: null"));
	cr_assert_not_null(strstr(page, "fisheye: false"));
	cr_assert_not_null(strstr(page, "animate: false"));
	/* The placement is the page's own, and it keeps the box where it is
	 * while the drawing moves aside. */
	cr_assert_not_null(strstr(page, "const inPlace = function (node, act)"));
	cr_assert_not_null(strstr(page, "const reflow = function"));
	cr_assert_not_null(strstr(page, "node.children().layout(CHILD_LAYOUT)"));
	cr_assert_not_null(strstr(page, "expandCollapse({"));
	/* The files, and only the files (HLR-216). */
	cr_assert_not_null(strstr(page,
	        "api.collapse(cy.nodes('[tier = \"file\"]'));"));
	cr_assert_null(strstr(page, "api.collapseAll();"),
	               "the layers were collapsed along with the files");
	/* The fit follows each layout as it settles rather than racing the
	 * two asynchronous ones, and the reader's first gesture ends it
	 * (LLR-HTM-04). */
	cr_assert_not_null(strstr(page, "cy.on('layoutstop', refit)"));
	cr_assert_not_null(strstr(page,
	        "cy.one('tap', function () { cy.off('layoutstop', refit); })"));
	/* And the descent is bound here rather than inherited from whatever
	 * gesture the extension's current release happens to bind: the
	 * extension is fetched at view time and is not pinned, so a
	 * requirement resting on its defaults can stop being met without this
	 * file changing (LLR-HTM-04, HLR-216). */
	/* Bound on the file tier alone, so a tap on a layer or a function
	 * reaches no handler and the only thing that opens is the thing the
	 * key names (LLR-HTM-04, HLR-216). */
	cr_assert_not_null(strstr(page,
	        "cy.on('tap', 'node[tier = \"file\"]', function"));
	cr_assert_not_null(strstr(page,
	        "if (node.hasClass('cy-expand-collapse-collapsed-node'))"));
	cr_assert_not_null(strstr(page, "api.expand(node)"));
	cr_assert_not_null(strstr(page, "api.collapse(node)"));

	free(page);
	cli_options_free(&opts);
	scene_free(&s);
}

/* A page is written for a graph with no nodes rather than withheld. An
 * artefact whose existence varies with its content makes the run's shape
 * depend on the project's, which is the rule `format_dsm.c` follows for an
 * empty matrix (LLR-HTM-02). */
Test(report_html, an_empty_graph_still_produces_a_page)
{
	Sdg        empty  = { 0 };
	Report     report = { 0 };
	ElcOptions opts   = { 0 };
	char      *page;

	page = page_of(&report, &empty, &opts);
	cr_assert_not_null(strstr(page, "const graphData = []"));
	/* The files, and only the files (HLR-216). */
	cr_assert_not_null(strstr(page,
	        "api.collapse(cy.nodes('[tier = \"file\"]'));"));
	cr_assert_null(strstr(page, "api.collapseAll();"),
	               "the layers were collapsed along with the files");
	free(page);
}

/* ------------------------------------------------------------ the stream -- */

/* The renderer writes to the stream it is given and does not close it: `emit`
 * owns the destination here as it does for every other format, which is what
 * makes this a format rather than a companion (LLR-HTM-05). */
Test(report_html, the_stream_is_left_open_for_the_caller)
{
	Scene       s;
	ElcOptions  opts = two_layers();
	FILE       *fp   = tmpfile();

	cr_assert_not_null(fp);
	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);

	cr_assert_eq(format_html(&s.report, &s.graph, &opts, fp), 0);
	/* Still writable, and still positioned where the renderer left it. */
	cr_assert_gt(ftell(fp), 0);
	cr_assert_eq(fputc('x', fp), 'x');

	fclose(fp);
	cli_options_free(&opts);
	scene_free(&s);
}

/* ------------------------------------------------------- the annotations -- */

/* The severity the catalogue decided reaches the node, spelled as the word the
 * stylesheet selects on — and a node nothing was found about carries none, so
 * a selector matches only what actually carries a finding (LLR-CYT-05). */
Test(report_html, a_finding_reaches_the_node_it_describes)
{
	Scene       s;
	ElcOptions  opts = { 0 };
	char       *page, *payload, *node;

	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);
	add_finding(&s.report, "critical", "app_fn", "/p/app/a.c", 1);
	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	node = element_with(payload, "app_fn");
	cr_assert_not_null(strstr(node, "\"severity\":\"critical\""));
	cr_assert_not_null(strstr(node, "cyclomatic complexity 20"),
	                   "the finding was not carried in full");
	free(node);

	/* And the clean function beside it says nothing at all. */
	node = element_with(payload, "hal_fn");
	cr_assert_null(strstr(node, "\"severity\""),
	               "a function with no finding was given a severity");
	free(node);

	free(page);
	scene_free(&s);
}

/* A mark is present or absent, never `false`: the stylesheet tests it with a
 * truthy selector, and stating all five on every node would say the same thing
 * in several times the bytes (LLR-CYT-05). */
Test(report_html, an_absent_mark_is_an_absent_key)
{
	Scene       s;
	ElcOptions  opts = { 0 };
	char       *page, *payload;

	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);
	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	cr_assert_null(strstr(payload, "false"),
	               "a mark that does not hold was stated as false");
	cr_assert_null(strstr(payload, "\"recursive\""));
	cr_assert_null(strstr(payload, "\"unreachable\""));

	free(page);
	scene_free(&s);
}

/* Every mark takes a different visual attribute, so that several holding at
 * once compose rather than overwrite — and the key names each of them, in the
 * page rather than in the manual (LLR-HTM-06, HLR-217). */
Test(report_html, the_page_carries_a_key_for_every_mark)
{
	Scene       s;
	ElcOptions  opts = { 0 };
	char       *page;

	scene_build(&s, PATHS, FUNCTIONS, 3, NULL, NULL, 0);
	page = page_of(&s.report, &s.graph, &opts);

	/* The pigments are the .dot writer's, so one reader's understanding
	 * serves both drawings. */
	cr_assert_not_null(strstr(page, "'node[severity = \"warning\"]'"));
	cr_assert_not_null(strstr(page, "'node[severity = \"critical\"]'"));
	cr_assert_not_null(strstr(page, "#f7e0b0"));
	cr_assert_not_null(strstr(page, "#f6c7c7"));

	/* Shape, border and fill are separate attributes. */
	cr_assert_not_null(strstr(page, "'node[?hidden]'"));
	cr_assert_not_null(strstr(page, "'node[?soleUser]'"));
	cr_assert_not_null(strstr(page, "'node[?unreachable]'"));
	cr_assert_not_null(strstr(page, "'node[?deepest]'"));
	cr_assert_not_null(strstr(page, "'node[?recursive]'"));
	cr_assert_not_null(strstr(page, "'edge[?chain]'"));
	/* Edges pass behind a box, opened or not: a node that becomes a
	 * container is drawn at a lower compound depth than an edge between
	 * two nodes outside it (LLR-HTM-06). */
	cr_assert_not_null(strstr(page, "'z-compound-depth': 'bottom'"));

	/* And the legend names them for the reader. */
	cr_assert_not_null(strstr(page, "id=\"legend\""));
	for (const char *const *w = (const char *const[]){
	             "warning", "critical", "recursive", "unreachable",
	             "deepest call chain", "hidden channel",
	             "sole namer of a global", NULL }; *w; w++)
		cr_assert_not_null(strstr(page, *w),
		                   "the key does not name %s", *w);

	free(page);
	scene_free(&s);
}

/* ----------------------------------------------- the components drawn --- */

/* A component defining no function is not drawn. It can hold no node and
 * join no edge, so its box would state nothing — and the `.dot` companion has
 * never drawn one, because it opens a cluster while walking the functions and
 * never reaches a component with none (LLR-CYT-02, HLR-217). */
Test(report_html, a_component_with_no_function_is_not_drawn)
{
	static const char *const PATHS2[] = { "/p/a.c", "/p/b.h" };
	static const char *const FNS2[]   = { "a_fn", "b_fn" };
	Scene       s;
	ElcOptions  opts = { 0 };
	char       *page, *payload;

	/* Two components, then the second is emptied by removing its only
	 * function from the graph's node set — the shape a header has, and
	 * the shape `--elf` leaves behind when an image defines none of a
	 * file's functions. */
	scene_build(&s, PATHS2, FNS2, 2, NULL, NULL, 0);
	cr_assert_geq(s.graph.node_count, 2);
	s.graph.node_count = 1;   /* only /p/a.c's function remains */

	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	cr_assert_not_null(strstr(payload, "\"tier\":\"file\""),
	                   "the component that does define a function is gone");
	cr_assert_null(strstr(payload, "b.h"),
	               "a component defining no function was drawn");

	free(page);
	scene_free(&s);
}

/* The shed prefix is measured over the components actually drawn: a prefix
 * shared by a file nobody will see is not shared by anything on the page
 * (LLR-CYT-02). */
Test(report_html, the_shed_prefix_ignores_a_component_not_drawn)
{
	static const char *const PATHS2[] = { "/p/src/a.c", "/p/inc/b.h" };
	static const char *const FNS2[]   = { "a_fn", "b_fn" };
	Scene       s;
	ElcOptions  opts = { 0 };
	char       *page, *payload;

	scene_build(&s, PATHS2, FNS2, 2, NULL, NULL, 0);
	cr_assert_geq(s.graph.node_count, 2);
	s.graph.node_count = 1;

	page    = page_of(&s.report, &s.graph, &opts);
	payload = payload_of(page);

	/* Components sort by path, so the node kept is `/p/inc/b.h`'s and
	 * `/p/src/a.c` is the one left undrawn. The prefix is then `/p/inc/`
	 * and the label is bare. Were it measured over both components it
	 * would be `/p/` and the label `inc/b.h`, so this assertion tells the
	 * two apart. */
	cr_assert_not_null(strstr(payload,
	        "\"label\":\"b.h\",\"path\":\"/p/inc/b.h\""));

	free(page);
	scene_free(&s);
}
