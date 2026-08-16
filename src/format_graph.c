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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* --------------------------------------------------------------- GraphML -- */

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
			fputs("elc: out of memory writing GraphML\n", stderr);
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
		fprintf(stderr, "elc: %s: write failed\n", path);
		goto done;
	}
	status = 0;

done:
	free(sorted);
	if (fclose(out) != 0 && status == 0) {
		fprintf(stderr, "elc: %s: write failed\n", path);
		status = -1;
	}
	return status;
}
