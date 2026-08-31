#ifndef ANNOTATE_H
#define ANNOTATE_H

/* What the analyses found about each function and each component, gathered
 * once for every drawing that shows it (SDD §26).
 *
 * **This module exists so that two drawings cannot disagree.** The `.dot`
 * companion and the interactive HTML report draw the same graph with the same
 * findings on it; deciding twice which finding describes which node is how
 * the two come to differ about a node's severity, in the way `format_dsm.c`
 * and `report_html.c` would differ about a file's layer if each matched the
 * stratum patterns itself (HLR-164).
 *
 * **Nothing here bands anything.** Every severity was decided by
 * `thresholds.c` and arrives in `report->findings`; this module places what
 * the catalogue already judged, so the codebase holds exactly one opinion
 * about what exceeds a threshold (HLR-098, HLR-099).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graph.h"
#include "report.h"

/* What was found about one node or one component. A bitset rather than a
 * winner, because a function can be several things at once and a drawing
 * shows each on a different attribute: shape says what it takes part in,
 * fill says how severely, and the border says whether it is reached. */
enum {
	MARK_UNREACHABLE = 1u << 0,  /* HLR-096 */
	MARK_RECURSIVE   = 1u << 1,  /* HLR-089 */
	MARK_DEEPEST     = 1u << 2,  /* HLR-088 */
	MARK_HIDDEN      = 1u << 3,  /* HLR-093 */
	MARK_SOLE_USER   = 1u << 4   /* HLR-092 */
	/* A counted measurement leaving its band needs no mark of its own: it
	 * arrives as a finding, and the severity it carries is already the
	 * fill (HLR-081, HLR-086). */
};

typedef struct {
	unsigned  marks;
	int       severity; /* the highest rank of any finding on it */
	char     *note;     /* those findings, joined; owned         */
} Annotation;

/* Everything a drawing needs that is derived rather than written: the node
 * and component annotations, the note naming what belongs to no single node,
 * and the deepest chain as node identifiers.
 *
 * Returns 0 with all four published, or -1. **It does not diagnose**: the
 * caller names the artefact it was writing, which this module does not know.
 */
int annotations_build(const Sdg *g, const Report *r, Annotation **nodes,
                      Annotation **comps, char **notes, uint32_t **chain);

void annotations_free(Annotation *a, size_t count);

/* Is this edge a step of the deepest chain? The chain is a list of definition
 * sites, so the step is a consecutive pair of them (HLR-088). */
bool annotation_on_chain(const uint32_t *chain, size_t count, uint32_t from,
                         uint32_t to);

#endif /* ANNOTATE_H */
