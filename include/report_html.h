/* report_html.h — the interactive HTML report format.
 *
 * The graph `elc` already builds, drawn at the level the reader chooses
 * (doc/SDD.md §27). The `.dot` and GraphML companions draw every
 * function at one altitude, so they scale by getting denser and a real project
 * renders as a drawing nobody looks at twice. The containment that would fix that is
 * already computed on every run — a function belongs to a file (HLR-114), a
 * file belongs to a declared layer (HLR-078) — and was discarded at the point
 * of emission. This module emits it.
 *
 * Three tiers of nodes joined by a `parent` reference, and edges between
 * functions only: the connection between two collapsed containers is derived
 * by the viewer, and an emitted one would be a second opinion about coupling
 * in an artefact whose other figures are the report's (HLR-213, HLR-214).
 *
 * **This is an output format, not a companion.** It is selected by the
 * extension of the output filename exactly as `.md`, `.csv` and `.xml` are
 * (HLR-148); an option asking for it would be a second way of saying what the
 * filename has already said, which is the inconsistency HLR-149 exists to
 * prevent between the two spellings it already has.
 */
#ifndef ELC_REPORT_HTML_H
#define ELC_REPORT_HTML_H

#include <stdio.h>

#include "elc.h"
#include "graph.h"
#include "report.h"

/* Render the graph as one self-contained HTML page on `out`.
 *
 * A renderer like every other: it consumes the assembled model and the graph
 * and writes to a stream the caller opened, and it is selected the way every
 * other format is — by the extension of the output filename (HLR-148). There
 * is no option asking for it, because an option would be a second way of
 * saying what `report.html` has already said.
 *
 * The page opens from the filesystem — no build step, no bundler, and no local
 * web server (HLR-215) — and opens *collapsed*, showing the declared layers
 * (HLR-216). The rendering library is fetched by the browser at view time,
 * which is the bound on the word "standalone" and is not an HLR-040 matter:
 * this function writes text and returns.
 *
 * **It takes the graph as well as the report**, which no other renderer does.
 * The topology is the thing being presented rather than a figure derived from
 * it, and a saved record carries findings without it — which is why a
 * regenerated report cannot be written in this format at all (HLR-122).
 *
 * Returns 0 on success, or -1 with the diagnostic already on stderr
 * (LLR-HTM-05).
 */
int format_html(const Report *report, const Sdg *g, const ElcOptions *opts,
                FILE *out);

#endif /* ELC_REPORT_HTML_H */
