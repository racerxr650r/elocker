/* format_graph.h — the graph-shaped outputs.
 *
 * Phase 8 delivered the GraphML export and Phase 13 the annotated Graphviz
 * `.dot` call tree. Both are plain text emission, which keeps Graphviz a tool
 * the user may run on the output rather than a library elc links (HLR-102),
 * and keeps GraphML independent of any XML library (doc/SDD.md §17).
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
#include "report.h"

/* True when the `.dot` call tree should be written.
 *
 * The default runs the opposite way to GraphML's: the companion is written
 * unless refused (HLR-103). The other two halves are the same — a report on
 * standard output has no path to derive a name from (HLR-104), and a saved
 * record carries findings rather than topology, so regeneration produces no
 * companion either (HLR-122, LLR-WAR-01 – LLR-WAR-03).
 */
bool graph_dot_warranted(const ElcOptions *opts);

/* Write the call tree as an annotated Graphviz `.dot` file at `path`.
 *
 * The graph supplies the topology and the report supplies the findings drawn
 * onto it; neither is re-derived here, and nothing is banded — a renderer that
 * decided for itself what counted as exceeding a threshold would be a second
 * opinion in a codebase that keeps exactly one (LLR-DOT-01, LLR-STY-01).
 *
 * Returns 0 on success. On failure a diagnostic naming the file is on stderr;
 * the caller records a failure and still writes the primary report, which is
 * never sacrificed to a companion (LLR-DOT-05).
 */
int graph_write_dot(const Sdg *g, const Report *r, const char *path);

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
