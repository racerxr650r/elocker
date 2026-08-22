/* format_dsm.h — the Dependency Structure Matrix and its renderings.
 *
 * format_dsm.c builds the square grid whose cells carry the call counts
 * between layers — or between directories, where no layer was declared — and
 * renders it in the three decorations `elc` writes matrices in (doc/SDD.md
 * §22, HLR-165, HLR-166).
 *
 * **The matrix arranges edges; it decides nothing.** The layer a component
 * lies in comes from `arch.c`, so the grid and the layering findings cannot
 * disagree about which file is in which layer, and the cells below the
 * diagonal account for exactly the back-calls the layering section lists.
 *
 * The `Dsm` itself is a report-model type and lives in report.h beside the
 * other collections a renderer consumes, because a saved record must be able
 * to regenerate it: a record carries no call graph to rebuild it from.
 */
#ifndef ELC_FORMAT_DSM_H
#define ELC_FORMAT_DSM_H

#include <stdbool.h>
#include <stdio.h>

#include "elc.h"
#include "graph.h"
#include "report.h"

/* The orientation, stated wherever the matrix is rendered (HLR-166).
 *
 * One string, printed by all three renderings, because a matrix whose
 * orientation the reader has to infer conveys the opposite of what it is for —
 * and three renderings each phrasing the convention for themselves is how one
 * of them comes to phrase it backwards.
 */
extern const char DSM_CONVENTION[];

/* The corner cell: the label above the row labels and left of the column
 * labels, which says which way round the grid runs in the space a reader
 * looks first. */
extern const char DSM_CORNER[];

/* Populate the square matrix of call counts between subjects, in their
 * defined order (HLR-165, HLR-166).
 *
 * The subjects are the declared layers ordered by ascending layer index where
 * strata were declared, and the analysed directories ordered by path where
 * they were not. A component matching no declared stratum lies outside the
 * partition and contributes to no cell (HLR-161).
 *
 * Call edges alone, for the reason the layering analysis considers them alone
 * (LLR-LAY-05): a global object two subjects happen to share is a different
 * fact with its own analyses.
 *
 * Returns 0 on success, non-zero only on allocation failure. A run with
 * nothing to put in the grid yields an empty matrix, which renders normally.
 */
int dsm_build(const Sdg *g, const Report *r, const ElcOptions *opts, Dsm *out);

/* Render the matrix as RFC 4180 CSV, every cell through the field writer the
 * per-function renderer uses, so that a directory containing a comma cannot
 * corrupt the grid (HLR-064).
 *
 * Returns 0 on success, non-zero if the stream reported a write failure.
 */
int format_dsm_csv(const Dsm *m, FILE *out);

/* Render the matrix as a GitHub-Flavored Markdown table, escaping the cell
 * separator so that a directory containing a pipe cannot corrupt the grid
 * (HLR-064). Returns 0 on success. */
int format_dsm_markdown(const Dsm *m, FILE *out);

/* Render the matrix as the report's aligned table. Returns 0 on success. */
int format_dsm_table(const Dsm *m, FILE *out);

/* Whether the CSV companion of HLR-180 is to be written for this run.
 *
 * Both halves matter, and they are the two `graph_graphml_warranted` tests for
 * the same reason: the companion is off unless asked for, and asking for it
 * while the report goes to standard output produces nothing, because the
 * companion's name is derived from the report's path and there is no path
 * (HLR-104).
 *
 * Regeneration is *not* excluded, and that is the one difference from the two
 * graph companions. A saved record carries the matrix where it carries no
 * topology, so there is something to write from (HLR-054, HLR-122).
 */
bool dsm_warranted(const ElcOptions *opts);

/* Release the matrix and the subject labels it owns. Safe on NULL and on a
 * zeroed matrix, so teardown is unconditional. */
void dsm_free(Dsm *m);

#endif /* ELC_FORMAT_DSM_H */
