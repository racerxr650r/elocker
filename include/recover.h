/* recover.h — the layering read off the purified recovery view.
 *
 * A user who has declared no architecture is not thereby a user with no
 * architecture. `purify.c` sets aside the utility sinks, god objects, and
 * peripheral nodes that fuse unrelated domains; this module orders what
 * remains and folds the order into layers, so that such a user is given a
 * description of the structure their code already has (doc/SDD.md §21).
 *
 * **What it produces is a proposal, and a proposal is never the baseline it
 * would be measured against** (HLR-173). That is the whole of this module's
 * contract, and it is kept by the dependency direction rather than by care:
 * `arch.c` cannot include this header, holds no `RecoveryResults`, and is
 * given no path to one. `elc` measuring conformance against its own proposal
 * would be a tool marking its own homework — every code base would conform,
 * because the standard would have been read off the thing it was judging. So
 * where no strata are declared the conformance analyses stay omitted with
 * their reason stated (HLR-115), however confidently a layering was recovered.
 *
 * The proposal is emitted as an **argument list** rather than as prose, in the
 * form `--stratum` and `--stratum-order` accept. That is the same boundary
 * made visible: what `elc` produces is a set of arguments, and it takes effect
 * only when the user reads it, agrees with it, and passes it back.
 */
#ifndef ELC_RECOVER_H
#define ELC_RECOVER_H

#include <stddef.h>

#include "elc.h"
#include "graph.h"
#include "purify.h"
#include "report.h"

/* One directory the proposal places, and where it placed it. */
typedef struct {
	char   *directory;   /* owned                                     */
	size_t  layer;       /* 0-based, topmost first                    */
	size_t  functions;   /* the ones the recovery view retained there */
} RecoveredLayer;

/* What recovery concluded, before the report is told about it.
 *
 * The three outcomes are exclusive and each is a complete answer: a layering,
 * the cycles that make one impossible, or the statement that nothing survived
 * purification to order. None of them is an error — a cyclic recovery view is
 * a fact about the program, reported in place of an ordering exactly as
 * HLR-090 reports a cyclic call graph in place of a depth.
 */
typedef struct {
	RecoveryState    state;
	RecoveredLayer  *layers;      /* sorted by layer, then path; owned */
	size_t           layer_count; /* rows, one per directory           */
	size_t           strata;      /* distinct layers among them        */
	char           **cycles;      /* rendered, sorted; owned           */
	size_t           cycle_count;
	size_t           masked;      /* functions whose edges were cut    */
	size_t           excluded;    /* peripheral, left out of the view  */
	/* The proposal in the form the stratum options accept, or NULL where
	 * there is nothing to propose. Adoption is then a copy rather than a
	 * transcription, which is what HLR-173 asks for — and the argument list
	 * is the boundary the requirement draws, in the one form a reader
	 * cannot mistake for a measurement. */
	char            *proposal;    /* owned */
} RecoveryResults;

/* Propose a layering, or report why none could be.
 *
 * The `Sdg` supplies each function's component and the `Report` supplies that
 * component's directory — recorded once at discovery rather than re-derived
 * here, since two consumers each slicing a path for themselves is how two of
 * them come to disagree about which directory a file is in (HLR-160).
 *
 * Returns 0 on success, non-zero on allocation failure or a failure inside the
 * graph library. Nothing here is a fatal condition of its own: an empty view
 * and a cyclic one are outcomes, not errors.
 */
int recover_layers(const PurifyResults *p, const Sdg *g, const Report *r,
                   RecoveryResults *out);

/* Fold a topological order of the view into per-directory layers.
 *
 * `order[i]` is the position node `i` holds in the ordering. Exposed because
 * the fold — and not the ordering — is the whole of what this module decides,
 * and it is worth pinning without a graph library in the way.
 *
 * **A topological order is not a layering.** It orders functions; an
 * architecture orders directories. A directory's layer is fixed by where the
 * bulk of its edges point rather than by its earliest or latest member, so one
 * function reaching far down the order cannot drag its whole directory with it.
 */
int layer_by_directory(const PurifyResults *p, const Sdg *g, const Report *r,
                       const size_t *order, RecoveryResults *out);

/* Copy the proposal onto an assembled report.
 *
 * Declared here rather than in `report.h`, exactly as `report_set_purify` is
 * declared in `purify.h`: the report model must not have to know what a
 * `RecoveryResults` is in order to be included.
 */
int report_set_recovery(Report *report, const RecoveryResults *rec);

/* Release the proposal, its cycles, and its exclusion counts. Safe on NULL. */
void recovery_results_free(RecoveryResults *r);

#endif /* ELC_RECOVER_H */
