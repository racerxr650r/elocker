/* report_html.c — the interactive HTML report format.
 *
 * The System Dependence Graph serialised as a *containment hierarchy* —
 * layers holding files holding functions — and one page that renders it at
 * whichever of those three levels the reader asks for (doc/SDD.md §27).
 *
 * **Selected by the output filename's extension**, exactly as `.md`, `.csv`
 * and `.xml` are (HLR-148). There is no option asking for it: an option would
 * be a second way of saying what `report.html` has already said, and two
 * spellings of one fact is the disagreement HLR-149 exists to prevent.
 *
 * **Nothing here measures anything.** Every figure the payload carries was
 * computed upstream and is copied; every edge it carries is an edge of the
 * graph; every layer assignment is `arch.c`'s. A drawing that recomputed any
 * of the three could disagree with the tables printed beside it, which is the
 * failure HLR-164 forbids for the conformance indices and this module avoids
 * by having no opinions of its own (HLR-213).
 *
 * Two decisions are worth reading before changing anything:
 *
 *   * **No meta-edges** (HLR-214). The connection between two collapsed
 *     containers is synthesised by the viewer from the edges crossing between
 *     them. Emitting one would state the same fact twice by two rules, and the
 *     one emitted here would be the statement with no threshold behind it —
 *     unreconcilable with the Ca/Ce figures the report prints (HLR-081).
 *   * **The escaping is not JSON escaping** (LLR-HTM-03). The payload is
 *     embedded in an HTML script element, where a `</script>` inside a C++
 *     template signature is *well-formed JSON* that ends the element and turns
 *     the rest of the graph into body text. `write_payload` is where that is
 *     dealt with, and it is the correctness core of this file.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "arch.h"
#include "diag.h"
#include "elc.h"
#include "graph.h"
#include "report_html.h"

/* The rendering library and its expand-collapse extension, fetched by the
 * *browser* when the page is opened.
 *
 * Not by `elc`, which is why this is not an HLR-040 matter: the run writes
 * text and exits, requiring no interpreter, no virtual machine and no network
 * of its own. What the artefact promises is that it needs no server and no
 * build step (HLR-215); the manual states that it does need the network the
 * first time it is opened, so a reader on a disconnected machine is told
 * rather than left to infer it from a blank page.
 */
#define CYTOSCAPE_CDN \
	"https://unpkg.com/cytoscape/dist/cytoscape.min.js"
#define EXPAND_COLLAPSE_CDN \
	"https://unpkg.com/cytoscape-expand-collapse/cytoscape-expand-collapse.js"

/* ------------------------------------------------------------------ model */

/* Set `key` on `obj` to a newly created value, stealing the reference.
 *
 * `json_object_set_new` steals on success and on failure alike, so a caller
 * that checks only the return value cannot leak — but every call site would
 * otherwise repeat the same four lines around a constructor that may itself
 * have returned NULL. Returns 0, or -1 with `value` released.
 */
static int set_new(json_t *obj, const char *key, json_t *value)
{
	if (!value)
		return -1;
	return json_object_set_new(obj, key, value) == 0 ? 0 : -1;
}

/* One element of the Cytoscape array: `{ "data": { ... } }`.
 *
 * The wrapper is the library's shape rather than elc's, and adopting it is the
 * design choice HLR-112 leaves open. Returns the inner data object, borrowed,
 * with the wrapper already appended to `elements`; NULL on failure, with
 * nothing appended.
 */
static json_t *append_element(json_t *elements)
{
	json_t *wrapper = json_object();
	json_t *data    = json_object();

	if (!wrapper || !data) {
		json_decref(wrapper);
		json_decref(data);
		return NULL;
	}
	if (json_object_set_new(wrapper, "data", data) != 0) {
		json_decref(wrapper);
		return NULL;
	}
	if (json_array_append_new(elements, wrapper) != 0)
		return NULL;

	return data;
}

/* Tier 1 — one node per declared stratum, identified by its ordinal
 * (LLR-CYT-01).
 *
 * The ordinal and not the name, because the name is user text: two strata
 * whose names differ only in characters an identifier cannot carry would
 * collide, and every file of one layer would silently reparent into the other.
 * The ordinal is already the stratum's identity everywhere else in the program
 * — it is what makes a direction out of a set of names (HLR-078).
 *
 * Emitted first, so a consumer reading the sequence forward never meets a
 * `parent` naming a node it has not yet seen. With no strata declared this
 * appends nothing and the document has two tiers rather than three.
 */
static int append_layers(json_t *elements, const ElcOptions *opts)
{
	for (size_t i = 0; i < opts->strata.count; i++) {
		const StratumDecl *d    = &opts->strata.items[i];
		json_t            *data = append_element(elements);
		char               id[64];

		if (!data)
			return -1;
		if (snprintf(id, sizeof id, "layer_%zu", d->ordinal) >=
		    (int)sizeof id)
			return -1;

		if (set_new(data, "id", json_string(id)) != 0 ||
		    set_new(data, "label", json_string(d->name)) != 0 ||
		    set_new(data, "tier", json_string("layer")) != 0 ||
		    set_new(data, "ordinal",
		            json_integer((json_int_t)d->ordinal)) != 0)
			return -1;
	}
	return 0;
}

/* Tier 2 — one node per component, parented on its declared layer
 * (LLR-CYT-02).
 *
 * `stratum` is `arch.c`'s assignment and is not re-derived here. That is
 * `format_dsm.c`'s rule and it is this module's for the same reason: two
 * matchers over one set of stratum patterns eventually disagree about which
 * layer a file is in, and this drawing would then place a file in one layer
 * while the matrix beside it placed the file in another (HLR-164).
 *
 * **`SIZE_MAX` omits the key entirely.** A file matching no stratum lies
 * outside the declared architecture — the judgement is argued above
 * `stratum_of_components` and followed here rather than reversed. A layer
 * named "other" would be a structure nobody declared, and would arrive on
 * every run declaring no strata at all, wrapping the whole project in a
 * fiction.
 */
static int append_files(json_t *elements, const Sdg *g, const size_t *stratum)
{
	for (size_t c = 0; c < g->component_count; c++) {
		json_t *data = append_element(elements);
		char    id[64], parent[64];

		if (!data)
			return -1;
		if (snprintf(id, sizeof id, "file_%zu", c) >= (int)sizeof id)
			return -1;

		if (set_new(data, "id", json_string(id)) != 0 ||
		    set_new(data, "label",
		            json_string(g->component_paths[c])) != 0 ||
		    set_new(data, "tier", json_string("file")) != 0)
			return -1;

		if (stratum && stratum[c] != SIZE_MAX) {
			if (snprintf(parent, sizeof parent, "layer_%zu",
			             stratum[c]) >= (int)sizeof parent)
				return -1;
			if (set_new(data, "parent", json_string(parent)) != 0)
				return -1;
		}
	}
	return 0;
}

/* Tier 3 — one node per graph node, parented on its component (LLR-CYT-03).
 *
 * The index is the SDG's own, which is the report's sorted file order
 * (LLR-SDG-09). Taking it rather than assigning one here is what makes the
 * document byte-identical across runs without this module sorting anything
 * (HLR-032), and it is what lets a reader match a node here to a node in the
 * GraphML export, whose identifiers are the same indices.
 *
 * The figures are copied, never recomputed: the program keeps exactly one
 * opinion about a function's complexity and it is the one with a threshold
 * behind it (HLR-099).
 */
/* One function node's fields.
 *
 * Split out of the loop below, and the failures accumulated rather than
 * branched on, because a chain of `set_new(...) != 0 || set_new(...) != 0 ||`
 * is one decision point per field: the two together put this function over the
 * complexity threshold `elc` holds its own source to (LLR-BLD-23). `set_new`
 * returns 0 or -1 and never leaks on either, so OR-ing the results is
 * equivalent to short-circuiting on the one path where they differ — an
 * allocation failure, after which the whole document is discarded anyway.
 */
static int function_fields(json_t *data, const SdgNode *n, size_t index,
                           size_t component_count)
{
	char id[64], parent[64];
	int  rc = 0;

	if (snprintf(id, sizeof id, "func_%zu", index) >= (int)sizeof id)
		return -1;

	rc |= set_new(data, "id", json_string(id));
	rc |= set_new(data, "label", json_string(n->name));
	rc |= set_new(data, "tier", json_string("function"));
	rc |= set_new(data, "file", json_string(n->file ? n->file : ""));
	rc |= set_new(data, "line", json_integer((json_int_t)n->line_start));
	rc |= set_new(data, "eloc", json_integer((json_int_t)n->eloc));
	rc |= set_new(data, "complexity",
	              json_integer((json_int_t)n->complexity));

	/* Unreachable by construction, and handled rather than asserted: the
	 * failure it would otherwise produce is a `parent` naming a node that
	 * does not exist, which a viewer reports as a corrupt document rather
	 * than as the bug it is. */
	if (n->component < component_count) {
		if (snprintf(parent, sizeof parent, "file_%zu",
		             n->component) >= (int)sizeof parent)
			return -1;
		rc |= set_new(data, "parent", json_string(parent));
	}

	return rc;
}

static int append_functions(json_t *elements, const Sdg *g)
{
	for (size_t i = 0; i < g->node_count; i++) {
		json_t *data = append_element(elements);

		if (!data)
			return -1;
		if (function_fields(data, &g->nodes[i], i,
		                    g->component_count) != 0)
			return -1;
	}
	return 0;
}

/* Ascending source, then target — `format_graph.c`'s comparison, for the same
 * reason it has one (LLR-DOT-04).
 *
 * **Named apart from that one deliberately.** `elc` resolves calls by matching
 * names across the files it was given, without a compiler's type information,
 * so two file-local functions sharing a name are a call it cannot resolve —
 * in its own source as in anyone's (HLR-075, LLR-BLD-25). Copying the name
 * along with the comparison would put an ambiguity into the graph this very
 * artefact draws.
 */
static int html_edges_by_source_then_target(const void *a, const void *b)
{
	const SdgEdge *x = a, *y = b;

	if (x->from != y->from)
		return x->from < y->from ? -1 : 1;
	if (x->to != y->to)
		return x->to < y->to ? -1 : 1;
	return 0;
}

/* The call edges, function to function, and nothing else (LLR-CYT-04).
 *
 * **Global edges are excluded** (HLR-074). A global edge joins a writer to a
 * reader and is not a call; drawing the call structure with them folded in
 * would report one kind of coupling as the other, which is the distinction the
 * graph carries two views to preserve.
 *
 * The order is imposed here rather than inherited, which is the second of the
 * two exceptions to `report.c` owning every sort: it is a property of this
 * artefact rather than of a collection the model holds (LLR-RPT-10).
 */
static int append_edges(json_t *elements, const Sdg *g)
{
	SdgEdge *sorted = NULL;
	int      status = -1;

	if (g->edge_count > 0) {
		sorted = malloc(g->edge_count * sizeof *sorted);
		if (!sorted)
			return -1;
		memcpy(sorted, g->edges, g->edge_count * sizeof *sorted);
		qsort(sorted, g->edge_count, sizeof *sorted,
		      html_edges_by_source_then_target);
	}

	for (size_t i = 0; i < g->edge_count; i++) {
		const SdgEdge *e = &sorted[i];
		json_t        *data;
		char           id[80], source[64], target[64];

		if (e->kind != EDGE_CALL)
			continue;

		if (snprintf(id, sizeof id, "call_%u_%u", e->from, e->to) >=
		            (int)sizeof id ||
		    snprintf(source, sizeof source, "func_%u", e->from) >=
		            (int)sizeof source ||
		    snprintf(target, sizeof target, "func_%u", e->to) >=
		            (int)sizeof target)
			goto done;

		data = append_element(elements);
		if (!data)
			goto done;

		if (set_new(data, "id", json_string(id)) != 0 ||
		    set_new(data, "source", json_string(source)) != 0 ||
		    set_new(data, "target", json_string(target)) != 0 ||
		    set_new(data, "weight",
		            json_integer((json_int_t)e->call_sites)) != 0)
			goto done;
	}

	status = 0;
done:
	free(sorted);
	return status;
}

/* The complete elements array: the three node tiers in that order, then the
 * edges (HLR-213, HLR-214). Returns a new reference, or NULL. */
static json_t *html_elements(const Sdg *g, const ElcOptions *opts)
{
	json_t *elements = json_array();
	size_t *stratum  = NULL;

	if (!elements)
		return NULL;

	/* Only where a layering was declared. `stratum_of_components`
	 * allocates for a project with no components too, and asking it for a
	 * map nothing will read is an allocation that can fail for no reason. */
	if (opts->strata.count > 0) {
		stratum = stratum_of_components(g, opts);
		if (!stratum) {
			json_decref(elements);
			return NULL;
		}
	}

	if (append_layers(elements, opts) != 0 ||
	    append_files(elements, g, stratum) != 0 ||
	    append_functions(elements, g) != 0 ||
	    append_edges(elements, g) != 0) {
		free(stratum);
		json_decref(elements);
		return NULL;
	}

	free(stratum);
	return elements;
}

/* ------------------------------------------------------------------- page */

/* Write the serialised payload into the script element (LLR-HTM-03).
 *
 * **This is not the serialisation's escaping and cannot be delegated to it.**
 * A C++ template signature containing `</script>` is *well-formed* JSON;
 * Jansson emits it verbatim and is right to, and the HTML parser then ends the
 * script element at it — the remainder of the graph becomes body text and the
 * page renders empty. Escaping `<` closes that, and escaping `&` closes the
 * other half: an entity reference in the text would be decoded before the
 * script ever ran.
 *
 * U+2028 and U+2029 are the case that is invisible in review. They are line
 * terminators to a JavaScript parser and ordinary characters to a JSON one, so
 * a name containing either is valid in the document and a syntax error the
 * moment it is embedded. Both arrive as the UTF-8 sequences E2 80 A8 and
 * E2 80 A9.
 *
 * A `\u`-escape is used for all four because it is legal inside a JSON string
 * literal and inside a JavaScript one, so the escaped text is still parseable
 * by either — and it is applied to the *serialised* text rather than to each
 * name beforehand, since escaping earlier would put the sequence in the data
 * and a viewer would render the literal characters in a function's label.
 */
static void write_payload(FILE *out, const char *json)
{
	const unsigned char *p = (const unsigned char *)json;

	for (; *p; p++) {
		if (*p == '<') {
			fputs("\\u003c", out);
		} else if (*p == '&') {
			fputs("\\u0026", out);
		} else if (p[0] == 0xE2 && p[1] == 0x80 &&
		           (p[2] == 0xA8 || p[2] == 0xA9)) {
			fprintf(out, "\\u202%c", p[2] == 0xA8 ? '8' : '9');
			p += 2;
		} else {
			fputc(*p, out);
		}
	}
}

/* The document shell: the two library references, and the container the
 * drawing is mounted in (LLR-HTM-02).
 *
 * Written even where the graph holds no nodes. A page that is absent when the
 * project has no call structure makes the artefact's existence vary with its
 * content — the rule `format_dsm.c` follows for an empty matrix and the report
 * follows for an empty section.
 */
static void write_head(FILE *out)
{
	fputs("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n", out);
	fputs("<meta charset=\"utf-8\">\n", out);
	fputs("<meta name=\"viewport\" "
	      "content=\"width=device-width, initial-scale=1\">\n", out);
	fputs("<title>elc — System Dependence Graph</title>\n", out);
	fprintf(out, "<script src=\"%s\"></script>\n", CYTOSCAPE_CDN);
	fprintf(out, "<script src=\"%s\"></script>\n", EXPAND_COLLAPSE_CDN);
	fputs("<style>\n"
	      "  html, body { margin: 0; height: 100%; "
	      "font-family: system-ui, sans-serif; }\n"
	      "  #cy { width: 100%; height: 100%; display: block; }\n"
	      "  #legend { position: absolute; top: 0; left: 0; padding: 8px "
	      "12px; background: rgba(255,255,255,0.9); font-size: 13px; }\n"
	      "</style>\n", out);
	fputs("</head>\n<body>\n", out);
	fputs("<div id=\"legend\">Click a box to open it, and again to close "
	      "it. Layers &rarr; files &rarr; functions.</div>\n", out);
	fputs("<div id=\"cy\"></div>\n", out);
}

/* The initialisation script (LLR-HTM-04).
 *
 * `collapseAll()` is not a default this script chooses; it is HLR-216. The
 * page opens showing the declared layers and the reader descends. Initialising
 * without it would reproduce, with an extra step, the density failure the
 * `.dot` and GraphML companions have.
 *
 * `fisheye` and `animate` are what make the descent navigable, which HLR-216
 * also requires: expanding a container in place, and moving to the new
 * arrangement rather than cutting to it, is what keeps the reader's bearings
 * across an expansion. Without them each expansion presents a freshly
 * laid-out drawing and the reader navigates a new picture every time.
 *
 * `cose` is chosen because it is the family that keeps a cluster's members
 * near one another, so a collapsed container occupies the space its contents
 * did.
 */
static void write_glue(FILE *out)
{
	fputs(
"<script>\n"
"const cy = cytoscape({\n"
"  container: document.getElementById('cy'),\n"
"  elements: graphData,\n"
"  layout: { name: 'cose', animate: false, nestingFactor: 1.2,\n"
"            idealEdgeLength: 80, padding: 20 },\n"
"  style: [\n"
"    { selector: 'node', style: {\n"
"        'label': 'data(label)', 'font-size': 10,\n"
"        'text-valign': 'center', 'background-color': '#cfd8dc',\n"
"        'border-width': 1, 'border-color': '#78909c' } },\n"
"    { selector: 'node[tier = \"layer\"]', style: {\n"
"        'background-opacity': 0.12, 'background-color': '#1565c0',\n"
"        'border-color': '#1565c0', 'font-size': 16,\n"
"        'text-valign': 'top' } },\n"
"    { selector: 'node[tier = \"file\"]', style: {\n"
"        'background-opacity': 0.18, 'background-color': '#00897b',\n"
"        'border-color': '#00897b', 'font-size': 12,\n"
"        'text-valign': 'top' } },\n"
"    { selector: 'edge', style: {\n"
"        'width': 1, 'line-color': '#90a4ae',\n"
"        'target-arrow-color': '#90a4ae',\n"
"        'target-arrow-shape': 'triangle',\n"
"        'curve-style': 'bezier' } }\n"
"  ]\n"
"});\n"
"\n"
"const api = cy.expandCollapse({\n"
"  layoutBy: { name: 'cose', animate: false, randomize: false,\n"
"              fit: false },\n"
"  fisheye: true,\n"
"  animate: true,\n"
"  undoable: false\n"
"});\n"
"\n"
"/* HLR-216: the view opens at the highest architectural level the run\n"
"   produced. A view that opened at function level and offered collapsing\n"
"   would reproduce the density failure this companion exists to fix. */\n"
"api.collapseAll();\n"
"cy.fit(undefined, 30);\n"
"\n"
"/* The descent, bound explicitly rather than left to the extension's cue\n"
"   icons. HLR-216 requires that the reader can descend and return, and a\n"
"   requirement met only by whatever gesture a CDN's current version happens\n"
"   to bind is a requirement that can stop being met without this file\n"
"   changing. `tap` is cytoscape's own event, so this works whatever the\n"
"   extension's defaults are. */\n"
"cy.on('tap', 'node:parent', function (event) {\n"
"  const node = event.target;\n"
"  if (node.hasClass('cy-expand-collapse-collapsed-node')) {\n"
"    api.expand(node);\n"
"  } else {\n"
"    api.collapse(node);\n"
"  }\n"
"});\n"
"</script>\n", out);
	fputs("</body>\n</html>\n", out);
}

/* ------------------------------------------------------------------ public */

int format_html(const Report *report, const Sdg *g, const ElcOptions *opts,
                FILE *out)
{
	json_t *elements = NULL;
	char   *payload  = NULL;
	int     status   = -1;

	/* The report is not read here yet: the page presents the graph, and
	 * every figure it draws is already on the graph's nodes. It is taken
	 * so that the tiers this format will grow — presented in the context
	 * of the drawing rather than as tables beside it (HLR-031) — arrive
	 * without changing every caller. */
	(void)report;

	/* Serialised before anything is written, so a failure leaves a stream
	 * the caller can still report on rather than a half-page. A partially
	 * written page is worse than none: it opens, renders a truncated
	 * graph, and states a structure that is wrong while looking exactly
	 * like one that is right (LLR-HTM-05). */
	elements = html_elements(g, opts);
	if (!elements) {
		diag_printf("elc: out of memory building the HTML graph\n");
		goto done;
	}

	payload = json_dumps(elements, JSON_COMPACT | JSON_SORT_KEYS);
	if (!payload) {
		diag_printf("elc: the HTML graph could not be serialised\n");
		goto done;
	}

	write_head(out);
	fputs("<script>\nconst graphData = ", out);
	write_payload(out, payload);
	fputs(";\n</script>\n", out);
	write_glue(out);

	/* Checked after the writing rather than per call: a full disk shows up
	 * on the flush, and a report claimed as written when it was truncated
	 * is the failure worth catching. The stream is the caller's, so it is
	 * neither flushed nor closed here — `emit` owns both, as it does for
	 * every other renderer. */
	status = ferror(out) ? -1 : 0;

done:
	free(payload);
	json_decref(elements);
	return status;
}
