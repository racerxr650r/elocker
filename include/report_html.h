/* report_html.h — the interactive HTML companion.
 *
 * The graph `elc` already builds, drawn at the level the reader chooses
 * (doc/SDD.md §27). The `.dot` and GraphML companions draw every function at
 * one altitude, so they scale by getting denser and a real project renders as
 * a drawing nobody looks at twice. The containment that would fix that is
 * already computed on every run — a function belongs to a file (HLR-114), a
 * file belongs to a declared layer (HLR-078) — and was discarded at the point
 * of emission. This module emits it.
 *
 * Three tiers of nodes joined by a `parent` reference, and edges between
 * functions only: the connection between two collapsed containers is derived
 * by the viewer, and an emitted one would be a second opinion about coupling
 * in an artefact that sits beside the Ca/Ce figures the report states
 * (HLR-213, HLR-214).
 */
#ifndef ELC_REPORT_HTML_H
#define ELC_REPORT_HTML_H

#include <stdbool.h>

#include "elc.h"
#include "graph.h"

/* True when the interactive companion was asked for *and* there is an output
 * path to derive its name from.
 *
 * Off by default, like the GraphML export and unlike the annotated call tree,
 * and absent when the report goes to standard output for the reason every
 * companion is: the name is derived from the report's path by extension
 * substitution, and there is then no path to derive it from (HLR-104,
 * HLR-119, HLR-215, LLR-HTM-01).
 */
bool report_html_warranted(const ElcOptions *opts);

/* Write the graph as one self-contained page at `path`.
 *
 * The page opens from the filesystem — no build step, no bundler, and no local
 * web server (HLR-215) — and opens *collapsed*, showing the declared layers
 * (HLR-216). The rendering library is fetched by the browser at view time,
 * which is the bound on the word "standalone" and is not an HLR-040 matter:
 * this function writes text and returns.
 *
 * Returns 0 on success. On failure a diagnostic naming the file is on stderr
 * and nothing is left behind that would open; the caller records a failure and
 * still writes the primary report, which is never sacrificed to a companion
 * (LLR-HTM-05, LLR-DOT-05).
 */
int report_html_write(const Sdg *g, const ElcOptions *opts, const char *path);

#endif /* ELC_REPORT_HTML_H */
