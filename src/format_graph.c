/* format_graph.c — the graph-shaped outputs.
 *
 * Plain text emission, deliberately. GraphML without an XML library and DOT
 * without Graphviz keeps both formats a description of the graph rather than
 * a dependency on someone else's model of one (doc/SDD.md §17).
 *
 * The content model in §17.2.1 of the SDD is fixed there rather than decided
 * here, because a fixture's `expected.graphml` is compared byte for byte: a
 * writer free to rename a key would silently invalidate every graph fixture
 * in the suite.
 *
 * The two writers differ in what they are *for*, and it shows in what each
 * carries. GraphML exports the graph's topology for another program to
 * process, so it holds every edge and every measurement. DOT draws the call
 * tree for someone to look at, so it holds the call edges and the findings —
 * and neither is a lesser version of the other.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "annotate.h"
#include "diag.h"
#include "elc.h"
#include "format_graph.h"
#include "format_xml.h"   /* write_escaped: one escaper, not two */
#include "graph.h"

/* The GraphML structure version. Incremented on a removal or a change of
 * meaning, never on an addition — a consumer ignores keys it does not know,
 * so adding one breaks nothing (SDD §17.2.1). */
#define GRAPHML_FORMAT_VERSION 1

bool graph_graphml_warranted(const ElcOptions *opts)
{
	/* Both halves matter. The export is off unless asked for (HLR-106),
	 * and asking for it while writing the report to standard output
	 * produces nothing: the companion's name is derived from the report's
	 * path, and there is no path (HLR-104). */
	return opts->graphml && opts->output_path != NULL;
}

char *graph_companion_path(const char *output_path, const char *extension)
{
	const char *slash = strrchr(output_path, '/');
	const char *base  = slash ? slash + 1 : output_path;
	const char *dot   = strrchr(base, '.');
	size_t      keep  = dot ? (size_t)(dot - output_path)
	                        : strlen(output_path);
	size_t      len   = keep + strlen(extension) + 1;
	char       *path  = malloc(len + 1);

	if (!path)
		return NULL;

	/* The extension is substituted, not appended: `report.md` yields
	 * `report.graphml` rather than `report.md.graphml`. A dot in a
	 * *directory* name is not an extension, which is why the search
	 * starts at the last component (LLR-GML-03). */
	memcpy(path, output_path, keep);
	path[keep] = '.';
	memcpy(path + keep + 1, extension, strlen(extension) + 1);
	return path;
}

bool graph_dot_warranted(const ElcOptions *opts)
{
	/* Three halves, and the first is the one that differs from GraphML's:
	 * the companion is written unless refused (HLR-103, LLR-WAR-01).
	 *
	 * The other two are the same rule twice. A report on standard output
	 * has no path to derive a name from, so there is nothing to write —
	 * whether or not generation was disabled, which is why the destination
	 * is tested rather than the switch alone (HLR-104, LLR-WAR-02). And a
	 * saved record carries the findings of a run rather than the topology
	 * they were drawn from, so regeneration has no graph to draw
	 * (HLR-122, LLR-WAR-03). */
	return !opts->no_dot && opts->output_path != NULL &&
	       opts->mode != MODE_REGENERATE;
}

/* Edges are emitted grouped by source and ordered by target within a source,
 * so the file records the graph and not the order this module happened to
 * build it in (LLR-DOT-04, HLR-032). */
static int by_source_then_target(const void *a, const void *b)
{
	const SdgEdge *x = a;
	const SdgEdge *y = b;

	if (x->from != y->from)
		return x->from < y->from ? -1 : 1;
	if (x->to != y->to)
		return x->to < y->to ? -1 : 1;
	if (x->kind != y->kind)
		return x->kind < y->kind ? -1 : 1;
	/* Two global edges between one pair of functions are two distinct
	 * objects, so the object name is what separates them. */
	if (x->kind == EDGE_GLOBAL)
		return strcmp(x->global ? x->global : "",
		              y->global ? y->global : "");
	return 0;
}

/* ------------------------------------------------------------------- DOT --
 *
 * The call tree, with the findings of Phases 9 through 12 drawn onto it.
 *
 * Only the *call* edges are drawn, because HLR-102 asks for the call tree and
 * a global edge is not a call. Coupling through shared state still reaches the
 * drawing, but as a property of the functions that take part in it — which is
 * what HLR-105 asks for: the participants of a hidden channel, not the channel.
 *
 * **Nothing here bands anything.** Every severity on the page was decided by
 * `thresholds.c` and arrives in `report->findings`; a renderer forming its own
 * view of what counts as exceeding a threshold would put a second opinion in a
 * codebase whose central claim is that it holds exactly one (HLR-098, HLR-099).
 */



/* A DOT quoted string escapes exactly two characters. Kept separate from
 * `write_escaped` rather than folded into it: XML's five entities and DOT's
 * two backslashes are different languages, and one escaper serving both would
 * have to be told which, at which point it is two functions anyway. */
static void dot_escape(const char *s, FILE *out)
{
	for (const char *p = s; p && *p; p++) {
		if (*p == '"' || *p == '\\')
			fputc('\\', out);
		fputc(*p, out);
	}
}

/* The fill for a severity rank, or NULL where there is no finding to colour.
 * The rank comes from `thresholds.c`; only the pigment is decided here. */
static const char *severity_fill(int rank)
{
	switch (rank) {
	case 2:  return "#f6c7c7";   /* critical */
	case 1:  return "#f7e0b0";   /* warning  */
	default: return NULL;
	}
}






/* The Graphviz attributes for one node, given the findings that apply to it.
 *
 * Every one is an attribute a renderer may ignore: drop them all and a valid
 * call tree remains, with the same nodes and the same edges (HLR-105,
 * LLR-STY-02). Each mark takes a different attribute so that several can
 * apply at once without one overwriting another.
 */
static void node_style(FILE *out, const Annotation *a)
{
	const char *fill   = severity_fill(a->severity);
	bool        dashed = (a->marks & MARK_UNREACHABLE) != 0;

	/* Shape says what the function takes part in. A hidden channel outranks
	 * a sole-user global where a function has both, being the more serious
	 * of the two and the one HLR-105 names. */
	if (a->marks & MARK_HIDDEN)
		fputs(", shape=octagon", out);
	else if (a->marks & MARK_SOLE_USER)
		fputs(", shape=note", out);

	/* A second border, which reads as a loop back on itself. */
	if (a->marks & MARK_RECURSIVE)
		fputs(", peripheries=2", out);

	if (fill || dashed) {
		fputs(", style=\"", out);
		fputs(fill ? "filled" : "", out);
		fputs(fill && dashed ? "," : "", out);
		fputs(dashed ? "dashed" : "", out);
		fputc('"', out);
	}
	if (fill)
		fprintf(out, ", fillcolor=\"%s\"", fill);

	/* The border carries one fact, so the two that want it are ordered
	 * rather than both emitted: a function on the deepest chain was
	 * reached by definition, so the two never really compete. */
	if (a->marks & MARK_DEEPEST)
		fputs(", color=\"#1f6fb4\", penwidth=3", out);
	else if (dashed)
		fputs(", color=\"#9a9a9a\", fontcolor=\"#6a6a6a\"", out);
}

/* The key to the drawing, as DOT comments.
 *
 * A comment rather than a legend subgraph: a legend would add nodes that are
 * not functions to a graph whose every node is one, and a reader opening the
 * file gets the key either way. */
static void write_preamble(FILE *out, const char *notes)
{
	fputs("/* Generated by elc. The call tree of the analysed project,\n"
	      " * annotated with the architectural findings that apply to it.\n"
	      " *\n"
	      " * Render with, for example:  dot -Tsvg graph.dot -o graph.svg\n"
	      " *\n"
	      " * The key:\n"
	      " *\n"
	      " *   cluster            one source file; the label is its path\n"
	      " *   filled amber       a warning-severity finding on the node\n"
	      " *   filled red         a critical-severity finding\n"
	      " *   double border      a member of a recursive cycle\n"
	      " *   octagon            takes part in a hidden channel\n"
	      " *   note shape         the only function naming some global\n"
	      " *   dashed grey        no path reaches it from a declared entry\n"
	      " *   thick blue         a step of the deepest call chain\n"
	      " *\n"
	      " * Every one of those is a Graphviz attribute a renderer may\n"
	      " * ignore; ignoring them all leaves the same tree, undecorated.\n"
	      " * The tooltip of each node carries its definition site, its\n"
	      " * ELOC and complexity, and the findings in full, one to a\n"
	      " * line. A renderer that shows tooltips shows them; `dot -Tsvg`\n"
	      " * writes them into each node's xlink:title.\n", out);
	if (notes)
		fprintf(out, " *\n * Findings that belong to no single node:\n"
		             " *\n *   %s\n", notes);
	fputs(" */\n", out);
}

static void write_cluster_open(FILE *out, const Sdg *g, size_t c,
                               const Annotation *a)
{
	const char *path = c < g->component_count ? g->component_paths[c] : "";
	const char *fill = severity_fill(a->severity);

	fprintf(out, "\tsubgraph cluster_%zu {\n", c);
	fputs("\t\tlabel=\"", out);
	dot_escape(path, out);
	fputs("\";\n", out);
	fprintf(out, "\t\tstyle=\"rounded%s\";\n", fill ? ",filled" : "");
	if (fill)
		fprintf(out, "\t\tbgcolor=\"%s\";\n\t\tpenwidth=2;\n", fill);
	if (a->note) {
		fputs("\t\ttooltip=\"", out);
		dot_escape(a->note, out);
		fputs("\";\n", out);
	}
}

/* The call edges, copied out and sorted, so that the file records the graph
 * rather than the order the resolver happened to build it in (LLR-DOT-04).
 *
 * Returns 0 with `*sorted` owned by the caller — NULL when the graph has no
 * edges at all — or -1 after a diagnostic.
 */
static int sort_call_edges(const Sdg *g, SdgEdge **sorted, size_t *calls)
{
	*sorted = NULL;
	*calls  = 0;

	if (g->edge_count == 0)
		return 0;

	*sorted = malloc(g->edge_count * sizeof **sorted);
	if (!*sorted) {
		diag_printf("elc: out of memory writing the call tree\n");
		return -1;
	}
	for (size_t i = 0; i < g->edge_count; i++)
		if (g->edges[i].kind == EDGE_CALL)
			(*sorted)[(*calls)++] = g->edges[i];
	qsort(*sorted, *calls, sizeof **sorted, by_source_then_target);
	return 0;
}

/* Every node, in ascending identifier order throughout, with a cluster opened
 * where the component changes. The identifiers run in the report's sorted file
 * order (LLR-SDG-09), so a component's nodes are contiguous and the clustering
 * costs the ordering nothing.
 */
static void write_nodes(FILE *out, const Sdg *g, const Annotation *nodes,
                        const Annotation *comps)
{
	size_t open = SIZE_MAX;

	for (uint32_t i = 0; i < g->node_count; i++) {
		const SdgNode *n = &g->nodes[i];

		if (n->component != open) {
			static const Annotation none = { 0 };

			if (open != SIZE_MAX)
				fputs("\t}\n", out);
			open = n->component;
			write_cluster_open(out, g, open,
			                   open < g->component_count
			                           ? &comps[open] : &none);
		}

		fprintf(out, "\t\tn%u [label=\"", i);
		dot_escape(n->name, out);
		fputc('"', out);
		node_style(out, &nodes[i]);
		/* The definition site, the figures the report states for the
		 * function, and the findings upon it — the same three things
		 * the interactive report shows when a reader points at a box,
		 * because they are two drawings of one graph and a reader who
		 * has asked one of them a question should not have to learn
		 * what the other will answer (HLR-217, LLR-HTM-09). */
		fputs(", tooltip=\"", out);
		dot_escape(n->file, out);
		fprintf(out, ":%" PRIu32, n->line_start);
		/* A real newline, not DOT's `\n` escape.
		 *
		 * Graphviz resolves the escape for SVG and *not* for the xdot
		 * format, so a viewer reading xdot — which is what `xdot`
		 * does — shows the reader the two characters of the escape
		 * instead of a line break. A newline in the quoted string is
		 * legal DOT and reaches both: SVG still writes `&#10;` and
		 * xdot now carries the break.
		 *
		 * The cost is that a node's attribute list spans several
		 * physical lines, so the file is no longer one line per node.
		 * That is a real loss for anything reading it line-wise, and
		 * it is accepted because this artefact is a drawing for
		 * someone to look at — the machine-readable export of the
		 * same graph is GraphML, which is unaffected (SDD §17,
		 * LLR-STY-03). */
		fprintf(out, "\nELOC %" PRIu32 ", complexity %" PRIu32,
		        n->eloc, n->complexity);
		if (nodes[i].note) {
			fputc('\n', out);
			dot_escape(nodes[i].note, out);
		}
		fputs("\"];\n", out);
	}
	if (open != SIZE_MAX)
		fputs("\t}\n", out);
}

/* Every call edge, with the steps of the deepest chain drawn apart from the
 * rest (HLR-088).
 */
static void write_edges(FILE *out, const SdgEdge *sorted, size_t calls,
                        const uint32_t *chain, size_t chain_count)
{
	for (size_t i = 0; i < calls; i++) {
		const SdgEdge *e = &sorted[i];

		fprintf(out, "\tn%" PRIu32 " -> n%" PRIu32, e->from, e->to);
		if (annotation_on_chain(chain, chain_count, e->from, e->to))
			fputs(" [color=\"#1f6fb4\", penwidth=2]", out);
		fputs(";\n", out);
	}
}


/* The graph, its defaults, its nodes and its edges. */
static void write_dot_document(FILE *out, const Sdg *g,
                               const Annotation *nodes,
                               const Annotation *comps, const char *notes,
                               const SdgEdge *sorted, size_t calls,
                               const uint32_t *chain, size_t chain_count)
{
	write_preamble(out, notes);
	fputs("digraph elc {\n", out);
	fputs("\tgraph [rankdir=LR, compound=true];\n", out);
	fputs("\tnode [shape=box, style=filled, fillcolor=\"#ffffff\", "
	      "fontname=\"Helvetica\", fontsize=10];\n", out);
	fputs("\tedge [color=\"#666666\"];\n", out);

	write_nodes(out, g, nodes, comps);
	write_edges(out, sorted, calls, chain, chain_count);

	fputs("}\n", out);
}

int graph_write_dot(const Sdg *g, const Report *r, const char *path)
{
	FILE       *out    = NULL;
	Annotation *nodes  = NULL;
	Annotation *comps  = NULL;
	char       *notes  = NULL;
	uint32_t   *chain  = NULL;
	SdgEdge    *sorted = NULL;
	size_t      calls  = 0;
	int         status = -1;

	/* The shared builder names no artefact, so the message is this
	 * writer's — the reader is told which file was being written. */
	if (annotations_build(g, r, &nodes, &comps, &notes, &chain) != 0) {
		diag_printf("elc: out of memory writing the call tree\n");
		goto done;
	}

	if (sort_call_edges(g, &sorted, &calls) != 0)
		goto done;

	out = fopen(path, "w");
	if (!out) {
		perror(path);
		goto done;
	}

	write_dot_document(out, g, nodes, comps, notes, sorted, calls, chain,
	                   r->deepest_count);

	/* Checked after the writing rather than per call, for the reason the
	 * GraphML writer gives: a full disk shows up on the flush. */
	if (ferror(out)) {
		diag_printf("elc: %s: write failed\n", path);
		goto done;
	}
	status = 0;

done:
	if (out && fclose(out) != 0 && status == 0) {
		diag_printf("elc: %s: write failed\n", path);
		status = -1;
	}
	free(sorted);
	free(chain);
	free(notes);
	annotations_free(nodes, g->node_count);
	annotations_free(comps, g->component_count);
	return status;
}

/* ------------------------------------------------- raw and purified draw --
 *
 * Two drawings of one graph, so that a reader can see what purification acted
 * on before deciding whether to trust what it produced (HLR-178). A single
 * drawing of the result cannot show that: it looks like a clean layering
 * whether the masking was right or wrong.
 *
 * They share a writer for the same reason they exist: the pair is meant to be
 * compared, so the node set, the labels, and the layout must differ between
 * them only where the masking differs. Two writers is how two drawings come to
 * differ in a way that has nothing to do with what was masked.
 */

bool graph_purify_dot_warranted(const ElcOptions *opts)
{
	/* The GraphML rule exactly: off unless asked for, and nothing at all
	 * when the report goes to standard output, since the names are derived
	 * from a path that does not exist (HLR-104, HLR-119, HLR-178). A saved
	 * record carries no graph to draw, and the parser rejects the
	 * combination outright rather than leaving it to be discovered here. */
	return opts->purify_dot && opts->output_path != NULL &&
	       opts->mode != MODE_REGENERATE;
}

/* The fill a node is drawn with.
 *
 * Grey for anything the masking touched, white for the rest, so the classified
 * nodes read as set aside rather than as absent — which is what "distinguished
 * rather than removed" asks for.
 */
static const char *purify_fill(const Classification *c, bool purified)
{
	if (!purified || c->klass == PURIFY_ORDINARY || !c->masked)
		return "#ffffff";
	return c->klass == PURIFY_PERIPHERAL ? "#e8e8e8" : "#d0d0d0";
}

static void write_purify_nodes(FILE *out, const Sdg *g,
                               const PurifyResults *p, bool purified)
{
	for (size_t i = 0; i < g->node_count; i++) {
		const Classification *c = &p->classes[i];
		bool                  set_aside = purified &&
		                                  c->klass != PURIFY_ORDINARY &&
		                                  c->masked;

		fprintf(out, "\tn%zu [label=\"", i);
		dot_escape(g->nodes[i].name, out);
		/* The class is drawn on the node rather than left to a legend,
		 * because the question this drawing answers is "what was set
		 * aside, and as what" — and a legend answers it one lookup
		 * away from where it is asked. */
		if (set_aside) {
			fputs("\\n(", out);
			dot_escape(purify_class_name(c->klass), out);
			fputs(")", out);
		}
		fprintf(out, "\", fillcolor=\"%s\"", purify_fill(c, purified));
		/* Dashed for an excluded node, since it is not in the view at
		 * all; solid and grey for a masked one, which is. */
		if (purified && c->klass == PURIFY_PERIPHERAL && c->masked)
			fputs(", style=\"filled,dashed\"", out);
		fputs("];\n", out);
	}
}

int graph_write_purify_dot(const Sdg *g, const PurifyResults *p, bool purified,
                           const char *path)
{
	FILE    *out    = NULL;
	SdgEdge *sorted = NULL;
	size_t   calls  = 0;
	int      status = -1;

	if (sort_call_edges(g, &sorted, &calls) != 0)
		return -1;

	out = fopen(path, "w");
	if (!out) {
		diag_printf("elc: %s: %s\n", path, strerror(errno));
		goto done;
	}

	fputs("/* Generated by elc. ", out);
	fputs(purified
	              ? "The purified recovery view: the graph a layering was "
	                "read off.\n * Greyed nodes were masked and dashed ones "
	                "excluded; both are drawn holding\n * no edge rather "
	                "than deleted, so this and the raw drawing beside it "
	                "can be\n * compared. Nothing measured elsewhere in the "
	                "report is taken over this graph. */\n"
	              : "The call graph as built, before any masking.\n * The "
	                "purified drawing beside it is the same graph with the "
	                "fusing nodes\n * set aside; the pair is what shows what "
	                "purification did. */\n",
	      out);

	fprintf(out, "digraph %s {\n", purified ? "elc_purified" : "elc_raw");
	fputs("\tgraph [rankdir=LR];\n", out);
	fputs("\tnode [shape=box, style=filled, fillcolor=\"#ffffff\", "
	      "fontname=\"Helvetica\", fontsize=10];\n", out);
	fputs("\tedge [color=\"#666666\"];\n", out);

	write_purify_nodes(out, g, p, purified);

	/* **The masked edges are absent from the purified drawing and present
	 * in the raw one**, which is the whole of the difference between them.
	 * The question of which edges survive is asked of `purify.c` rather
	 * than answered again here: two answers to one question is how a
	 * drawing comes to show a graph the analysis never read. */
	for (size_t i = 0; i < calls; i++) {
		if (purified &&
		    !purify_edge_retained(p->classes, sorted[i].from,
		                          sorted[i].to))
			continue;
		fprintf(out, "\tn%" PRIu32 " -> n%" PRIu32 ";\n",
		        sorted[i].from, sorted[i].to);
	}

	fputs("}\n", out);

	if (ferror(out)) {
		diag_printf("elc: %s: write failed\n", path);
		goto done;
	}
	status = 0;

done:
	if (out && fclose(out) != 0 && status == 0) {
		diag_printf("elc: %s: write failed\n", path);
		status = -1;
	}
	free(sorted);
	return status;
}

/* --------------------------------------------------------------- GraphML -- */

/* Distinct callees, which is what fan-out means: the count of subroutines
 * invoked, not the count of call sites (HLR-085). The graph is simple, so
 * that is exactly the out-degree over call edges. */
static uint32_t fan_out(const Sdg *g, uint32_t node)
{
	uint32_t n = 0;

	for (size_t i = 0; i < g->edge_count; i++)
		if (g->edges[i].from == node && g->edges[i].kind == EDGE_CALL)
			n++;
	return n;
}

/* Distinct callers, and the same rule read the other way: the in-degree over
 * call edges alone (HLR-156). `kind == EDGE_CALL` is doing the work — a
 * global edge runs from a function that writes an object to one that reads
 * it, and being read by someone is not being called by them.
 *
 * The export carries both degrees beside each node's ELOC and complexity, so
 * a consumer reading the GraphML has the same per-function figures the report
 * presents in its function table (HLR-183). */
static uint32_t fan_in(const Sdg *g, uint32_t node)
{
	uint32_t n = 0;

	for (size_t i = 0; i < g->edge_count; i++)
		if (g->edges[i].to == node && g->edges[i].kind == EDGE_CALL)
			n++;
	return n;
}

static void write_key(FILE *out, const char *id, const char *domain,
                      const char *name, const char *type)
{
	fprintf(out, "  <key id=\"%s\" for=\"%s\" attr.name=\"%s\" "
	             "attr.type=\"%s\"/>\n", id, domain, name, type);
}

static void write_data(FILE *out, const char *indent, const char *key,
                       const char *value)
{
	fprintf(out, "%s<data key=\"%s\">", indent, key);
	write_escaped(value, out);
	fputs("</data>\n", out);
}

static void write_data_num(FILE *out, const char *indent, const char *key,
                           unsigned long value)
{
	fprintf(out, "%s<data key=\"%s\">%lu</data>\n", indent, key, value);
}

int graph_write_graphml(const Sdg *g, const char *path)
{
	FILE    *out    = fopen(path, "w");
	SdgEdge *sorted = NULL;
	int      status = -1;

	if (!out) {
		perror(path);
		return -1;
	}

	if (g->edge_count > 0) {
		sorted = malloc(g->edge_count * sizeof *sorted);
		if (!sorted) {
			diag_printf("elc: out of memory writing GraphML\n");
			goto done;
		}
		memcpy(sorted, g->edges, g->edge_count * sizeof *sorted);
		qsort(sorted, g->edge_count, sizeof *sorted,
		      by_source_then_target);
	}

	fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", out);
	fputs("<graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\">\n", out);

	write_key(out, "g_version",   "graph", "format-version",   "int");
	write_key(out, "g_unresolved","graph", "unresolved-calls", "int");
	write_key(out, "n_name",      "node",  "name",             "string");
	write_key(out, "n_file",      "node",  "file",             "string");
	write_key(out, "n_start",     "node",  "line-start",       "int");
	write_key(out, "n_end",       "node",  "line-end",         "int");
	write_key(out, "n_component", "node",  "component",        "string");
	write_key(out, "n_eloc",      "node",  "eloc",             "int");
	write_key(out, "n_complexity","node",  "complexity",       "int");
	write_key(out, "n_fanout",    "node",  "fan-out",          "int");
	write_key(out, "n_fanin",     "node",  "fan-in",           "int");
	write_key(out, "n_address",   "node",  "address-taken",    "boolean");
	write_key(out, "e_kind",      "edge",  "kind",             "string");
	write_key(out, "e_global",    "edge",  "global",           "string");
	write_key(out, "e_sites",     "edge",  "call-sites",       "int");

	fputs("  <graph id=\"sdg\" edgedefault=\"directed\">\n", out);
	write_data_num(out, "    ", "g_version", GRAPHML_FORMAT_VERSION);
	write_data_num(out, "    ", "g_unresolved",
	               (unsigned long)graph_unresolved_count(g));

	for (size_t i = 0; i < g->node_count; i++) {
		const SdgNode *n = &g->nodes[i];

		fprintf(out, "    <node id=\"n%zu\">\n", i);
		write_data(out, "      ", "n_name", n->name);
		write_data(out, "      ", "n_file", n->file);
		write_data_num(out, "      ", "n_start", n->line_start);
		write_data_num(out, "      ", "n_end", n->line_end);
		write_data(out, "      ", "n_component",
		           n->component < g->component_count
		                   ? g->component_paths[n->component] : "");
		write_data_num(out, "      ", "n_eloc", n->eloc);
		write_data_num(out, "      ", "n_complexity", n->complexity);
		write_data_num(out, "      ", "n_fanout",
		               fan_out(g, (uint32_t)i));
		write_data_num(out, "      ", "n_fanin",
		               fan_in(g, (uint32_t)i));
		write_data(out, "      ", "n_address",
		           n->address_taken ? "true" : "false");
		fputs("    </node>\n", out);
	}

	for (size_t i = 0; i < g->edge_count; i++) {
		const SdgEdge *e = &sorted[i];

		fprintf(out, "    <edge source=\"n%u\" target=\"n%u\">\n",
		        e->from, e->to);
		write_data(out, "      ", "e_kind",
		           e->kind == EDGE_CALL ? "call" : "global");
		if (e->kind == EDGE_GLOBAL)
			write_data(out, "      ", "e_global",
			           e->global ? e->global : "");
		else
			write_data_num(out, "      ", "e_sites", e->call_sites);
		fputs("    </edge>\n", out);
	}

	fputs("  </graph>\n", out);
	fputs("</graphml>\n", out);

	/* Checked after the writing rather than per call: a full disk shows up
	 * on the flush, and a report claimed as written when it was truncated
	 * is the failure worth catching. */
	if (ferror(out)) {
		diag_printf("elc: %s: write failed\n", path);
		goto done;
	}
	status = 0;

done:
	free(sorted);
	if (fclose(out) != 0 && status == 0) {
		diag_printf("elc: %s: write failed\n", path);
		status = -1;
	}
	return status;
}
