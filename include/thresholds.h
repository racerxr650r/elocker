/* thresholds.h — the published threshold catalogue, and the only judgement.
 *
 * Every other analysis module measures and refuses to judge. This one applies
 * the catalogue of PVD Appendix A to what they measured, attaches a severity,
 * and names the source the threshold came from (doc/SDD.md §12).
 *
 * **All banding lives here, and that is what makes the project's central claim
 * checkable.** `elc` says it carries no opinion of its own; a reviewer can
 * verify that by reading one table in one file rather than auditing every
 * analysis for a hidden constant. Phases 9 and 11 deferred HLR-086 and
 * HLR-084 here for exactly that reason, rather than each building a fragment
 * of this module and reworking it.
 *
 * Two properties the catalogue must keep:
 *
 *   * **Every threshold names its source.** Where the source is `elc` itself
 *     — the bottleneck heuristic is the only one — the entry says so in the
 *     text a reader sees. That label is the whole of what separates shipping
 *     MISRA and Martin values from having invented them (HLR-099).
 *   * **A severity is a label.** It never reaches the exit status (HLR-100),
 *     and no finding carries remediation: `elc` reports where a measurement
 *     falls and what standard says so, and stops there (HLR-101).
 */
#ifndef ELC_THRESHOLDS_H
#define ELC_THRESHOLDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch.h"
#include "calltree.h"
#include "elc.h"
#include "graph.h"
#include "report.h"
#include "state.h"

/* The label every threshold `elc` invented carries wherever it is reported
 * (HLR-099, HLR-171).
 *
 * Written down once and quoted from both places that need it: the one
 * catalogue row that is not a published standard, and the purification
 * thresholds, which carry no catalogue row at all because they band nothing
 * and produce no finding. Two spellings of this label would let a reader
 * conclude that one of the two is a citation.
 */
#define ELC_OWN_HEURISTIC "elc heuristic — not a published standard"

/* One row of the catalogue.
 *
 * `warning_above` and `critical_above` are exclusive bounds on a counted
 * measurement: a value strictly greater than the bound falls in that band. A
 * kind that is a finding by its mere occurrence — recursion, a cycle — leaves
 * both zero and carries its severity in `fixed`.
 */
typedef struct {
	MeasurementKind kind;
	const char     *name;           /* what the measurement is called   */
	uint32_t        warning_above;
	uint32_t        critical_above;
	Severity        fixed;          /* for occurrence-is-the-finding    */
	bool            occurrence;     /* true when `fixed` governs        */
	const char     *attribution;    /* the published source             */
	bool            elc_own;        /* not a published standard         */
} Threshold;

/* One reportable observation: a measurement that fell outside its accepted
 * range, with the severity and the citation that say so. */
typedef struct {
	MeasurementKind kind;
	Severity        severity;
	char           *subject;   /* the function, component or object; owned */
	char           *where;     /* file, or empty; owned                    */
	uint32_t        line;      /* 0 where the finding has no single line   */
	char           *detail;    /* the measurement, rendered; owned         */
} Finding;

typedef struct {
	Finding *items;
	size_t   count;
	size_t   capacity;
} FindingList;

/* Evaluate every measurement against the catalogue and emit the findings.
 *
 * Returns 0 on success; non-zero only on allocation failure. Finding nothing
 * is a perfectly ordinary result and is not a failure (HLR-100).
 */
int thresholds_apply(const ArchResults *arch, const TreeResults *tree,
                     const StateResults *state, const Sdg *g,
                     const ElcOptions *opts, FindingList *out);

/* The catalogue entry for a measurement kind, or NULL where the catalogue
 * holds none — in which case the caller reports the measurement as a bare
 * value with no severity, rather than discarding it or inventing a band
 * (LLR-THR-08). */
const Threshold *thresholds_lookup(MeasurementKind kind);

/* The published source a measurement is attributed to, and whether that
 * source is `elc` itself.
 *
 * Here rather than in the analysis modules so that one definition serves every
 * reader: the coupling table's heading, the findings list, and the record all
 * name the same source, and a citation cannot drift between them (HLR-099).
 */
const char *threshold_attribution(MeasurementKind kind);
bool        threshold_is_elc_own(MeasurementKind kind);

/* The severity's name, from the closed set (HLR-123), and its rank.
 *
 * The rank exists so the report can order findings without re-deriving the
 * ordering from the names — alphabetically `critical` precedes `info`
 * precedes `warning`, which is not the order the set is defined in.
 */
const char *severity_name(Severity severity);
int         severity_rank(const char *name);

/* Copy the findings onto an assembled report, ordered for presentation. */
int report_set_findings(Report *report, const FindingList *findings);

/* Release every finding and the strings each owns. Safe on NULL. */
void findinglist_free(FindingList *f);

#endif /* ELC_THRESHOLDS_H */
