/* state.h — global-state coupling, execution-scope isolation, reachability.
 *
 * The analyses that answer "what shares state with what" and "what does not
 * run" (doc/SDD.md §11). Everything here is a *measurement*: which numbers
 * cross a threshold is `thresholds.c`'s judgement in Phase 12, and nothing in
 * this module carries a band.
 *
 * **Reachability reads the call view, not the whole SDG, and that is a
 * decision rather than an inheritance.** A global-state edge joins a function
 * that writes an object to one that reads it — but writing a variable another
 * function later reads is not calling it. Control never travels along that
 * edge, so a function reachable only through one has not been reached at all.
 * Following it would quietly rescue genuinely dead code from the report, which
 * is the one error this analysis exists to avoid making in the other
 * direction. Phase 9 met the same question for recursion and answered it the
 * same way, for a different reason (see graph.h).
 */
#ifndef ELC_STATE_H
#define ELC_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elc.h"
#include "graph.h"
#include "report.h"

/* One function's relationship to one global object, and the region of the
 * call graph it sits in. The region is what separates ordinary shared state
 * from a hidden channel: functions that call one another sharing a variable is
 * a design, while functions that never meet sharing one is a coupling nobody
 * declared (HLR-093). */
typedef struct {
	uint32_t node;
	size_t   region;  /* weakly connected component of the call graph */
	bool     writes;
	bool     reads;
} GlobalToucher;

/* One global object, with the functions that touch it and the verdict on
 * them (HLR-091 – HLR-093). */
typedef struct {
	const char    *object;        /* borrowed from the graph          */
	GlobalToucher *touchers;      /* ascending by node id; owned      */
	size_t         toucher_count;
	size_t         region_count;  /* distinct regions among touchers  */
	GlobalVerdict  verdict;
} GlobalRow;

/* One edge by which a declared execution scope reaches another (HLR-094). */
typedef struct {
	uint32_t    from;
	uint32_t    to;
	size_t      from_scope;   /* into ElcOptions.scopes */
	size_t      to_scope;
	SdgEdgeKind kind;
	const char *object;       /* the shared object, or NULL for a call */
} ScopeViolation;

typedef struct {
	GlobalRow *globals;             /* one per object touched; owned   */
	size_t     global_count;

	ReachState  reach_state;
	uint32_t   *unreachable;        /* node ids, ascending; owned      */
	size_t      unreachable_count;
	const char **dead_globals;      /* object names, ascending;
	                                 * borrowed from the graph         */
	size_t       dead_global_count;

	ScopeState      scope_state;
	ScopeViolation *violations;     /* owned (HLR-094)                 */
	size_t          violation_count;
	size_t          violation_capacity;
} StateResults;

/* Run the global-state, reachability, and scope-isolation analyses.
 *
 * Returns 0 on success; non-zero only on allocation failure. An absent
 * declaration is not a failure — it is an omission with a stated reason, and
 * with no entry points declared nothing whatsoever is reported unreachable
 * (HLR-115, LLR-STA-01, LLR-STA-02).
 */
int state_analyse(const Sdg *g, const ElcOptions *opts, StateResults *out);

/* Apply the scope-reduction and hidden-channel rules to each global
 * (LLR-GLB-01 – LLR-GLB-03). */
int classify_globals(const Sdg *g, StateResults *out);

/* Forward traversal of the call graph from `roots`; `*out` receives the node
 * identifiers never visited, ascending (LLR-RCH-01 – LLR-RCH-03).
 *
 * Exposed so the unit level can drive the traversal against a graph it built,
 * which is where the clique case — unused functions calling one another — is
 * cheapest to pin.
 */
int reachability(const Sdg *g, const uint32_t *roots, size_t root_count,
                 uint32_t **out, size_t *out_count);

/* The reachability root set: the declared entry points, together with every
 * function whose address is taken (LLR-RTS-01, LLR-RTS-02).
 *
 * `*resolved` reports how many declared symbols named an analysed function, so
 * the caller can tell "you declared nothing" from "what you declared is not
 * here" — two absences that call for different actions.
 */
int collect_roots(const Sdg *g, const ElcOptions *opts, uint32_t **out,
                  size_t *out_count, size_t *resolved);

/* Copy the state measurements onto an assembled report, resolving node
 * identifiers to names and locations a reader can act on. */
int report_set_state(Report *report, const StateResults *state, const Sdg *g,
                     const ElcOptions *opts);

/* Release every list the results own. Safe on NULL. */
void state_results_free(StateResults *r);

#endif /* ELC_STATE_H */
