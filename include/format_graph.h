/* format_graph.h — the graph-shaped outputs.
 *
 * Phase 8 delivers the GraphML export. The annotated Graphviz `.dot` call
 * tree shares this module and arrives with Phase 13; both are plain text
 * emission, which keeps Graphviz a tool the user may run on the output rather
 * than a library elc links (HLR-102), and keeps GraphML independent of any
 * XML library (doc/SDD.md §17).
 *
 * GraphML ships here rather than with the visualisation phase because it is
 * the only channel that exposes the graph's *topology*. The rendered findings
 * report conclusions; without the topology, this phase and the three after it
 * would have nothing a fixture could assert against (STP §5).
 */
#ifndef ELC_FORMAT_GRAPH_H
#define ELC_FORMAT_GRAPH_H

#include <stdbool.h>

#include "elc.h"
#include "graph.h"

/* True when a GraphML export was requested *and* there is an output path to
 * derive its name from. Export is off by default, unlike the `.dot` companion
 * (HLR-106), and a report written to standard output produces no companion
 * file at all — there is no name to derive (HLR-104, HLR-106, LLR-GML-02,
 * LLR-GML-03). */
bool graph_graphml_warranted(const ElcOptions *opts);

/* The companion filename for an output path, by extension substitution:
 * `report.md` yields `report.graphml`. Returns a newly allocated string the
 * caller frees, or NULL on allocation failure (LLR-GML-03). */
char *graph_companion_path(const char *output_path, const char *extension);

/* Write the SDG as GraphML to `path`.
 *
 * Returns 0 on success. On failure a diagnostic naming the file has been
 * written to stderr; the caller records a failure and still writes the
 * primary report, which is never sacrificed to a companion (LLR-DOT-05).
 */
int graph_write_graphml(const Sdg *g, const char *path);

#endif /* ELC_FORMAT_GRAPH_H */
