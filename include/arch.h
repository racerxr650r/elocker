/* arch.h — the component-level analyses.
 *
 * Afferent and efferent coupling, Instability, bottlenecks, dependency cycles,
 * and layering validation (doc/SDD.md §9). Everything here is a *measurement*:
 * the bands that turn a number into a finding are `thresholds.c`'s work in
 * Phase 12, and nothing in this module carries a severity.
 *
 * **A component is a source file** (HLR-114), and that is the whole difference
 * between this module and `calltree.c`. Every analysis here reads the
 * *component projection* of the SDG. Two functions in one file calling each
 * other are a recursion finding and no cycle at all here, because a component
 * does not depend on itself; the same pair split across two files is
 * legitimately both, because the two facts are different.
 *
 * The bottleneck threshold is the one exception to `elc`'s rule that thresholds
 * come from published sources. It is `elc`'s own heuristic and is marked as
 * such wherever it is reported (HLR-081, HLR-099).
 */
#ifndef ELC_ARCH_H
#define ELC_ARCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elc.h"
#include "graph.h"
#include "report.h"

/* One component's coupling and what follows from it (HLR-080 – HLR-082). */
typedef struct {
	uint32_t ca;                  /* components depending on this one   */
	uint32_t ce;                  /* components this one depends upon   */
	double   instability;         /* Ce / (Ce + Ca)                     */
	bool     instability_defined; /* false when both are zero           */
	bool     bottleneck;          /* both couplings at the threshold    */
} ComponentCoupling;

/* One cyclic dependency between components (HLR-083).
 *
 * Two facts, because one alone misleads. `members` is the whole strongly
 * connected group — the set that has to be broken up, and the unit the reader
 * acts on. `path` is a concrete loop through it, because "these five files are
 * entangled" does not say which edge to cut and "A → B → C → A" does.
 *
 * The path may be shorter than the membership: a group can hold several
 * overlapping loops, and enumerating them all is exponential in the group's
 * size. One witness is what the requirement asks for and what a reader can use.
 */
typedef struct {
	size_t *members;   /* component indices, ascending; owned */
	size_t  member_count;
	size_t *path;      /* a loop through them, in order; owned. The first
	                    * component is not repeated at the end — the
	                    * renderer closes the loop.                     */
	size_t  path_count;
} ComponentCycle;

/* One call offending against the declared layering (HLR-079, HLR-118). */
typedef struct {
	uint32_t           from;         /* node ids into Sdg.nodes */
	uint32_t           to;
	size_t             from_stratum; /* into ElcOptions.strata   */
	size_t             to_stratum;
	LayerViolationKind kind;
	size_t             layers_crossed; /* the ordinal distance    */
} LayerViolation;

typedef struct {
	ComponentCoupling *coupling;      /* one per component; owned      */
	size_t             component_count;

	ComponentCycle    *cycles;        /* owned (HLR-083)               */
	size_t             cycle_count;
	size_t             cycle_capacity;

	StrataState        strata_state;
	LayerViolation    *violations;    /* owned (HLR-079, HLR-118)      */
	size_t             violation_count;
	size_t             violation_capacity;
	/* The denominator of both conformance indices: the run's call edges
	 * joining two components that lie in *different* declared layers
	 * (HLR-162, HLR-163).
	 *
	 * Counted by `check_strata` in the same pass that produces the
	 * violations above, and that is the point rather than an economy. The
	 * three exclusions the denominator needs — a call is not a global
	 * edge, both ends lie inside the partition, and the two ends lie in
	 * different layers — are the three tests that loop already makes
	 * before it decides whether to report anything. A second traversal
	 * applying them again would be a second opinion about which edges are
	 * candidates, and the percentage would eventually contradict the table
	 * printed beside it (HLR-164).
	 *
	 * Zero when no strata were declared, since the loop does not run. */
	size_t             inter_layer_edges;
} ArchResults;

/* The two conformance indices over one run (HLR-162, HLR-163).
 *
 * Both are proportions of the same denominator and neither is a summary of
 * the other. They are **never** added together: a call ascending two layers
 * is a back-call and a skip-level call at once, counted once in each, and a
 * combined score would count it twice — exactly the call most worth acting on
 * (LLR-LAY-04).
 *
 * `defined` is false where the denominator is zero, and the caller must then
 * report the index as undefined rather than as 0 or 1. A project with no
 * inter-layer call has not achieved perfect conformance; it has demonstrated
 * nothing either way, which is the rule HLR-082 already applies to
 * Instability.
 */
typedef struct {
	size_t inter_layer_edges;   /* the shared denominator          */
	size_t back_calls;          /* inverted findings counted       */
	size_t skip_calls;          /* skip-level findings counted     */
	double back_call_index;     /* back_calls / inter_layer_edges  */
	double skip_call_index;
	bool   defined;             /* false where the denominator is 0 */
} ConformanceIndices;

/* Run every component-level analysis.
 *
 * Returns 0 on success; non-zero only on allocation failure. A cycle is not a
 * failure, and an absent stratum declaration is not a failure — each is a
 * stated outcome (HLR-083, HLR-115).
 */
int arch_analyse(const Sdg *g, const ElcOptions *opts, ArchResults *out);

/* Populate Ca and Ce for every component from the component projection
 * (LLR-CPL-01 – LLR-CPL-03). Returns 0 on success. */
int compute_coupling(const Sdg *g, ArchResults *out);

/* Ce / (Ce + Ca), with `*defined` set false and no division performed when
 * both couplings are zero (LLR-INS-01, LLR-INS-02).
 *
 * Exposed because the undefined case is the one worth pinning directly: a
 * component nothing depends on and which depends on nothing is ordinary — a
 * lone file in a single-file target — and dividing there is the defect the
 * requirement names.
 */
double instability(uint32_t ca, uint32_t ce, bool *defined);

/* The non-trivial strongly connected components of the component projection,
 * each with a concrete loop through it (LLR-CYC-01 – LLR-CYC-03). */
int find_cycles(const Sdg *g, ArchResults *out);

/* Report every call bypassing a declared layer and every call inverting the
 * declared direction, as distinct findings (LLR-LAY-01 – LLR-LAY-03). */
int check_strata(const Sdg *g, const ElcOptions *opts, ArchResults *out);

/* The declared stratum each component lies in, as a `g->component_count`
 * array the caller frees, or NULL on allocation failure. SIZE_MAX marks a
 * component no declaration names (HLR-161, LLR-LAY-05).
 *
 * Exposed so that the dependency matrix assigns components to layers by the
 * same rule the layering findings do. Two matchers over one set of patterns
 * would eventually disagree about which layer a file is in, and the matrix's
 * below-diagonal cells would then stop accounting for the back-calls listed
 * beside them — the failure HLR-164 forbids for the indices, arriving instead
 * through the grid.
 */
size_t *stratum_of_components(const Sdg *g, const ElcOptions *opts);

/* Count the recorded layering violations into the two indices, over the
 * inter-layer call edges as denominator (HLR-162 – HLR-164).
 *
 * **Counts, never re-derives.** The numerators come from `a->violations` and
 * the denominator from the count `check_strata` took while producing them, so
 * there is one decision in the program about what a back-call is and the
 * indices cannot disagree with the table beside them.
 *
 * Returns 0. Exposed because the undefined case and the both-kinds case are
 * the two worth pinning directly.
 */
int conformance_indices(const ArchResults *a, ConformanceIndices *out);

/* Attribution for these measurements lives in the threshold catalogue, not
 * here: `threshold_attribution(MEASURE_INSTABILITY)` and
 * `MEASURE_BOTTLENECK`. Phase 12 absorbed the two functions that used to sit
 * at this point, so that one table names every source and a citation cannot
 * drift between the modules that quote it (HLR-099, LLR-INS-03, LLR-ARC-02).
 */

/* Copy the component-level measurements onto an assembled report. */
int report_set_arch(Report *report, const ArchResults *arch, const Sdg *g,
                    const ElcOptions *opts);

/* Release the coupling table, the cycle list, and the violation list. */
void arch_results_free(ArchResults *r);

#endif /* ELC_ARCH_H */
