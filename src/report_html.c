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

#include "annotate.h"
#include "arch.h"
#include "diag.h"
#include "elc.h"
#include "graph.h"
#include "report_html.h"
#include "thresholds.h"   /* severity_name: the judgement is the
                           * catalogue's, and only its spelling is used */

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

/* The layout engine, and the reason it is a third and fourth script rather
 * than one of the viewer's own (LLR-HTM-04).
 *
 * A call graph is read as a hierarchy and drawn as one by the `.dot`
 * companion, which Graphviz ranks and clusters. The viewer ships two layouts
 * and neither does both: its force-directed one keeps a container's members
 * together but arranges them by simulated physics, and its breadth-first one
 * ranks by call direction but scatters a container's members across the
 * drawing, so a declared layer is drawn as a box overlapping the files of
 * another. ELK's layered algorithm is the one that ranks *and* respects
 * containment, which is what makes this drawing and the `.dot` companion
 * two renderings of one picture rather than two different pictures. */
#define ELK_CDN \
	"https://unpkg.com/elkjs/lib/elk.bundled.js"
#define CYTOSCAPE_ELK_CDN \
	"https://unpkg.com/cytoscape-elk/dist/cytoscape-elk.js"

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

/* A structural mark, emitted only where it holds.
 *
 * Present-or-absent rather than true-or-false because the stylesheet tests it
 * with `[?key]`: a node carrying every key with `false` in most of them would
 * say the same thing in five times the bytes, on every node in the drawing.
 */
static int mark_flag(json_t *data, const char *key, unsigned bit)
{
	return bit ? set_new(data, key, json_true()) : 0;
}

/* What the analyses found about one node or component (LLR-CYT-05).
 *
 * **The severity is spelled, never re-decided.** It arrives on the annotation
 * from `thresholds.c` by way of `annotate.c`, and this renderer converts a
 * rank to the word the stylesheet selects on and nothing else — the same
 * annotation the `.dot` companion turns into a fill, so the two drawings
 * cannot disagree about a node (HLR-098, HLR-099).
 *
 * Nothing is emitted for a node nothing was found about, which is most of
 * them: a key that is absent costs no bytes and matches no selector.
 */
static int annotation_fields(json_t *data, const Annotation *a)
{
	int rc = 0;

	if (!a)
		return 0;

	if (a->note) {
		rc |= set_new(data, "severity",
		              json_string(severity_name((Severity)a->severity)));
		rc |= set_new(data, "finding", json_string(a->note));
	}

	rc |= mark_flag(data, "unreachable", a->marks & MARK_UNREACHABLE);
	rc |= mark_flag(data, "recursive",   a->marks & MARK_RECURSIVE);
	rc |= mark_flag(data, "deepest",     a->marks & MARK_DEEPEST);
	rc |= mark_flag(data, "hidden",      a->marks & MARK_HIDDEN);
	rc |= mark_flag(data, "soleUser",    a->marks & MARK_SOLE_USER);
	return rc;
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
/* The longest directory prefix every component path shares, measured in
 * bytes and always ending just past a `/` (LLR-CYT-02).
 *
 * Within one document the shared prefix distinguishes nothing — every
 * component has it — while consuming most of each label's width, which on a
 * real project is what makes the file tier illegible. It is computed from the
 * document's own components, so it is as deterministic as they are (HLR-032),
 * and a lone component is labelled by its file name, its whole directory
 * being a prefix nothing else contests.
 */
static size_t shared_prefix_len(char *const *paths, size_t count)
{
	size_t keep = 0;

	if (count == 0)
		return 0;
	for (size_t i = 0; paths[0][i] != '\0'; i++) {
		for (size_t p = 1; p < count; p++)
			if (paths[p][i] != paths[0][i])
				return keep;
		if (paths[0][i] == '/')
			keep = i + 1;
	}
	return keep;
}

static int append_files(json_t *elements, const Sdg *g, const size_t *stratum,
                        const Annotation *comps)
{
	size_t prefix = shared_prefix_len(g->component_paths,
	                                  g->component_count);

	for (size_t c = 0; c < g->component_count; c++) {
		json_t *data = append_element(elements);
		char    id[64], parent[64];

		if (!data)
			return -1;
		if (snprintf(id, sizeof id, "file_%zu", c) >= (int)sizeof id)
			return -1;

		/* The label is for reading and `path` is the record: what the
		 * label sheds stays recoverable on the same node rather than
		 * lost (LLR-CYT-02). */
		if (set_new(data, "id", json_string(id)) != 0 ||
		    set_new(data, "label",
		            json_string(g->component_paths[c] + prefix)) != 0 ||
		    set_new(data, "path",
		            json_string(g->component_paths[c])) != 0 ||
		    set_new(data, "tier", json_string("file")) != 0)
			return -1;

		if (annotation_fields(data, comps ? &comps[c] : NULL) != 0)
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
                           size_t component_count, const Annotation *a)
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
	rc |= annotation_fields(data, a);

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

static int append_functions(json_t *elements, const Sdg *g,
                            const Annotation *nodes)
{
	for (size_t i = 0; i < g->node_count; i++) {
		json_t *data = append_element(elements);

		if (!data)
			return -1;
		if (function_fields(data, &g->nodes[i], i, g->component_count,
		                    nodes ? &nodes[i] : NULL) != 0)
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
static int append_edges(json_t *elements, const Sdg *g,
                        const uint32_t *chain, size_t chain_count)
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

		/* A step of the deepest chain, marked from the same test the
		 * `.dot` writer uses so the two drawings agree about which
		 * edges the chain runs through (HLR-088). */
		if (annotation_on_chain(chain, chain_count, e->from, e->to) &&
		    set_new(data, "chain", json_true()) != 0)
			goto done;
	}

	status = 0;
done:
	free(sorted);
	return status;
}

/* The complete elements array: the three node tiers in that order, then the
 * edges (HLR-213, HLR-214). Returns a new reference, or NULL. */
static json_t *html_elements(const Sdg *g, const Report *r,
                             const ElcOptions *opts)
{
	json_t     *elements = json_array();
	size_t     *stratum  = NULL;
	Annotation *nodes    = NULL;
	Annotation *comps    = NULL;
	char       *notes    = NULL;
	uint32_t   *chain    = NULL;
	int         built    = -1;

	if (!elements)
		return NULL;

	/* The findings, placed by the module the `.dot` writer places them
	 * with. Deciding here which finding describes which node would be the
	 * second opinion `annotate.c` exists to prevent (HLR-098). */
	built = annotations_build(g, r, &nodes, &comps, &notes, &chain);

	/* Only where a layering was declared. `stratum_of_components`
	 * allocates for a project with no components too, and asking it for a
	 * map nothing will read is an allocation that can fail for no reason. */
	if (opts->strata.count > 0 && built == 0) {
		stratum = stratum_of_components(g, opts);
		if (!stratum)
			built = -1;
	}

	if (built != 0 ||
	    append_layers(elements, opts) != 0 ||
	    append_files(elements, g, stratum, comps) != 0 ||
	    append_functions(elements, g, nodes) != 0 ||
	    append_edges(elements, g, chain, r->deepest_count) != 0) {
		json_decref(elements);
		elements = NULL;
	}

	annotations_free(nodes, g->node_count);
	annotations_free(comps, g->component_count);
	free(notes);
	free(chain);
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
	fprintf(out, "<script src=\"%s\"></script>\n", ELK_CDN);
	fprintf(out, "<script src=\"%s\"></script>\n", CYTOSCAPE_ELK_CDN);
	fputs("<style>\n"
	      "  html, body { margin: 0; height: 100%; "
	      "font-family: system-ui, sans-serif; }\n"
	      "  #cy { width: 100%; height: 100%; display: block; }\n"
	      "  #legend { position: absolute; top: 0; left: 0; right: 0; "
	      "padding: 6px 10px; background: rgba(255,255,255,0.92); "
	      "border-bottom: 1px solid #cfd8dc; font-size: 12px; "
	      "line-height: 1.7; }\n"
	      "  #legend b { font-weight: 600; }\n"
	      "  #legend .k { display: inline-block; margin-right: 14px; "
	      "white-space: nowrap; }\n"
	      "  #legend .s { display: inline-block; width: 22px; "
	      "height: 12px; vertical-align: -2px; margin-right: 5px; "
	      "border: 1px solid #78909c; }\n"
	      "</style>\n", out);
	fputs("</head>\n<body>\n", out);

	/* The key, and it says the same things the `.dot` header says in
	 * prose. A drawing whose colours are not explained is a drawing the
	 * reader has to guess at, and the companion this one replaces
	 * explained itself (LLR-HTM-06). */
	fputs("<div id=\"legend\">\n"
	      "<b>Click a file to open it, and again to close it.</b> "
	      "Layers hold files; a file holds its functions.\n"
	      "<div>\n"
	      "<span class=\"k\"><span class=\"s\" "
	      "style=\"background:#f7e0b0\"></span>warning</span>\n"
	      "<span class=\"k\"><span class=\"s\" "
	      "style=\"background:#f6c7c7\"></span>critical</span>\n"
	      "<span class=\"k\"><span class=\"s\" "
	      "style=\"background:#fff;border:3px double #37474f\"></span>"
	      "recursive</span>\n"
	      "<span class=\"k\"><span class=\"s\" "
	      "style=\"background:#fff;border:1px dashed #9e9e9e\"></span>"
	      "unreachable</span>\n"
	      "<span class=\"k\"><span class=\"s\" "
	      "style=\"background:#fff;border:2px solid #1f6fb4\"></span>"
	      "deepest call chain</span>\n"
	      "<span class=\"k\">octagon &mdash; hidden channel</span>\n"
	      "<span class=\"k\">tag &mdash; sole namer of a global</span>\n"
	      "</div>\n"
	      "</div>\n", out);
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
"  /* No layout yet: the first one worth computing is of the collapsed\n"
"     view, below. Laying out the expanded graph here would spend the most\n"
"     expensive layout on a drawing HLR-216 forbids opening with — and\n"
"     collapsing while it still runs is a race the collapse loses on any\n"
"     real project, leaving the view fitted to a drawing that no longer\n"
"     exists. */\n"
"  layout: { name: 'preset' },\n"
"  style: [\n"
"    /* A function is a box, as it is in the .dot companion; the marks\n"
"       below take shape, fill and border separately so that several can\n"
"       apply at once without one overwriting another (LLR-HTM-06). */\n"
"    { selector: 'node', style: {\n"
"        'label': 'data(label)', 'font-size': 10,\n"
"        'text-valign': 'center', 'text-halign': 'center',\n"
"        'shape': 'round-rectangle', 'width': 'label', 'height': 'label',\n"
"        'padding': '6px', 'background-color': '#ffffff',\n"
"        'border-width': 1, 'border-color': '#78909c' } },\n"
"    { selector: 'node[tier = \"layer\"]', style: {\n"
"        'background-opacity': 0.10, 'background-color': '#1565c0',\n"
"        'border-color': '#1565c0', 'font-size': 16,\n"
"        'text-valign': 'top', 'padding': '14px' } },\n"
"    { selector: 'node[tier = \"file\"]', style: {\n"
"        'background-opacity': 0.14, 'background-color': '#00897b',\n"
"        'border-color': '#00897b', 'font-size': 12,\n"
"        'text-valign': 'top', 'padding': '10px' } },\n"
"\n"
"    /* A collapsed file is one box carrying the file's name — the tier\n"
"       the reader navigates, so it is drawn to be read rather than as a\n"
"       shrunken container (HLR-216). */\n"
"    { selector: 'node[tier = \"file\"].cy-expand-collapse-collapsed-node',\n"
"      style: {\n"
"        'text-valign': 'center', 'background-opacity': 1,\n"
"        'background-color': '#e0f2f1', 'border-width': 2,\n"
"        'font-size': 13, 'padding': '10px' } },\n"
"\n"
"    /* The severity is the catalogue's, spelled by annotate.c and only\n"
"       coloured here — the same pigments the .dot writer uses. */\n"
"    { selector: 'node[severity = \"warning\"]', style: {\n"
"        'background-color': '#f7e0b0', 'background-opacity': 1 } },\n"
"    { selector: 'node[severity = \"critical\"]', style: {\n"
"        'background-color': '#f6c7c7', 'background-opacity': 1 } },\n"
"\n"
"    { selector: 'node[?hidden]',   style: { 'shape': 'octagon' } },\n"
"    { selector: 'node[?soleUser]', style: { 'shape': 'tag' } },\n"
"    { selector: 'node[?unreachable]', style: {\n"
"        'border-style': 'dashed', 'border-color': '#9e9e9e' } },\n"
"    { selector: 'node[?deepest]', style: {\n"
"        'border-color': '#1f6fb4', 'border-width': 3 } },\n"
"    { selector: 'node[?recursive]', style: {\n"
"        'border-style': 'double', 'border-width': 4,\n"
"        'border-color': '#37474f' } },\n"
"\n"
"    { selector: 'edge', style: {\n"
"        'width': 1, 'line-color': '#90a4ae',\n"
"        'target-arrow-color': '#90a4ae',\n"
"        'target-arrow-shape': 'triangle', 'arrow-scale': 0.8,\n"
"        'curve-style': 'bezier' } },\n"
"    { selector: 'edge[?chain]', style: {\n"
"        'line-color': '#1f6fb4', 'target-arrow-color': '#1f6fb4',\n"
"        'width': 2 } }\n"
"  ]\n"
"});\n"
"\n"
, out);
	fputs(
"const api = cy.expandCollapse({\n"
"  layoutBy: null,\n"
"  fisheye: true,\n"
"  animate: true,\n"
"  undoable: false\n"
"});\n"
"\n"
"/* Only files open and close (HLR-216). A layer is a container the reader\n"
"   reads rather than one they navigate: collapsing it would hide the tier\n"
"   the view is arranged by, and the file is where the descent to the\n"
"   functions actually happens. */\n"
"api.collapse(cy.nodes('[tier = \"file\"]'));\n"
"\n"
"/* Layouts settle asynchronously, so a fit() called at a moment of this\n"
"   script's choosing frames whatever drawing is mid-flight. Fit when a\n"
"   layout says it has settled, and stop at the reader's first gesture, so\n"
"   the viewport is never taken back from them. */\n"
"const refit = function () { cy.fit(undefined, 30); };\n"
"cy.on('layoutstop', refit);\n"
"cy.one('tap', function () { cy.off('layoutstop', refit); });\n"
"\n"
, out);
	fputs(
"/* Laid out in flow order rather than by simulated physics: a call graph\n"
"   is read as a hierarchy — who calls whom, in which direction — and a\n"
"   force-directed arrangement of one is a hairball however the edges run.\n"
"   `INCLUDE_CHILDREN` is what keeps a declared layer a box around its own\n"
"   files while the files are still ranked by the calls between them\n"
"   (LLR-HTM-04). Re-run after a descent, because the drawing that needs\n"
"   arranging is the one now on screen. */\n"
"const relayout = function () {\n"
"  cy.layout({ name: 'elk', nodeDimensionsIncludeLabels: true,\n"
"              animate: false,\n"
"              elk: { algorithm: 'layered',\n"
"                     'elk.direction': 'DOWN',\n"
"                     'elk.hierarchyHandling': 'INCLUDE_CHILDREN',\n"
"                     'elk.spacing.nodeNode': '18',\n"
"                     'elk.layered.mergeEdges': 'true',\n"
"                     'elk.layered.spacing.nodeNodeBetweenLayers': '40' }\n"
"            }).run();\n"
"};\n"
"relayout();\n"
"\n"
"/* The descent, bound explicitly rather than left to the extension's cue\n"
"   icons. HLR-216 requires that the reader can descend and return, and a\n"
"   requirement met only by whatever gesture a CDN's current version happens\n"
"   to bind is a requirement that can stop being met without this file\n"
"   changing. `tap` is cytoscape's own event, so this works whatever the\n"
"   extension's defaults are.\n"
"\n"
"   Bound on the file tier alone: a tap on a layer or on a function reaches\n"
"   no handler, so the only thing that opens and closes is the thing the\n"
"   legend says opens and closes. */\n"
"cy.on('tap', 'node[tier = \"file\"]', function (event) {\n"
"  const node = event.target;\n"
"  if (node.hasClass('cy-expand-collapse-collapsed-node')) {\n"
"    api.expand(node);\n"
"  } else {\n"
"    api.collapse(node);\n"
"  }\n"
"  relayout();\n"
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

	/* Serialised before anything is written, so a failure leaves a stream
	 * the caller can still report on rather than a half-page. A partially
	 * written page is worse than none: it opens, renders a truncated
	 * graph, and states a structure that is wrong while looking exactly
	 * like one that is right (LLR-HTM-05). */
	elements = html_elements(g, report, opts);
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
