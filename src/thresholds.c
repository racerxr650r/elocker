/* thresholds.c — the published threshold catalogue, and the only judgement.
 *
 * Every measurement `elc` makes arrives here as a number. This module decides
 * which of them are worth a reader's attention, how urgently, and on whose
 * authority (doc/SDD.md §12).
 *
 * **It is the only module that judges, and that is the design.** The project's
 * central claim is that it carries no opinion of its own — that every line it
 * draws comes from MISRA, Martin, or Henry–Kafura, and that the one exception
 * says so. A reviewer can check that claim by reading the table below. If
 * banding were spread across the analyses, checking it would mean auditing
 * every one of them for a constant, and the claim would rest on nobody having
 * hidden one.
 *
 * Two rules the catalogue keeps:
 *
 *   * **Severity is a label.** It never reaches the exit status, which is
 *     reserved for the failure conditions of HLR-120. A critical finding on a
 *     clean run still exits 0, because deciding what a finding warrants is the
 *     caller's business (HLR-100).
 *   * **Nothing here advises.** A finding says where a measurement fell and
 *     which standard says so. It does not say what to do about it, rank one
 *     design above another, or express a preference no cited source holds
 *     (HLR-101, HLR-111).
 */

#include <fnmatch.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch.h"
#include "calltree.h"
#include "elc.h"
#include "graph.h"
#include "state.h"
#include "thresholds.h"

/* ------------------------------------------------------- the catalogue --
 *
 * PVD Appendix A, as data. Every row names its source; the one row `elc`
 * invented says so in the text a reader sees.
 *
 * The bands are **exclusive upper bounds**: a value strictly greater than
 * `warning_above` warns, and strictly greater than `critical_above` is
 * critical. Written that way because the published tables are written that
 * way — "> 15 is a god function" — and a catalogue that had to be mentally
 * translated from its source would be a catalogue nobody could check against
 * it.
 */
static const Threshold CATALOGUE[] = {
	/* Fan-out: 0–2 below healthy, 3–7 healthy, 8–10 acceptable, all
	 * silent; 11–15 warns; above 15 is critical. Exhaustive over every
	 * value a fan-out can take (HLR-086). */
	{ MEASURE_FAN_OUT, "fan-out",
	  ELC_FANOUT_WARNING, ELC_FANOUT_CRITICAL,
	  SEVERITY_INFO, false, "Henry-Kafura", false },

	/* Depth: an embedded constraint rather than a numbered rule. Beyond 8
	 * to 12 layers the stack risks colliding with the heap on a target
	 * with a couple of kilobytes of SRAM. */
	{ MEASURE_CALL_DEPTH, "call depth",
	  ELC_DEPTH_WARNING, ELC_DEPTH_CRITICAL,
	  SEVERITY_INFO, false, "embedded practice (PVD Appendix A.2)", false },

	/* Occurrence is the finding for the next four: there is no acceptable
	 * count of recursive cycles or dependency cycles, and a global touched
	 * by one function is a finding whatever the number of touches. */
	{ MEASURE_RECURSION, "recursion", 0, 0,
	  SEVERITY_CRITICAL, true, "MISRA C Rule 17.2", false },

	{ MEASURE_COMPONENT_CYCLE, "component dependency cycle", 0, 0,
	  SEVERITY_CRITICAL, true, "Martin, acyclic dependencies", false },

	{ MEASURE_SCOPE_REDUCTION, "single-function global", 0, 0,
	  SEVERITY_WARNING, true, "MISRA C Rule 8.9", false },

	{ MEASURE_HIDDEN_CHANNEL, "hidden channel", 0, 0,
	  SEVERITY_WARNING, true, "MISRA C Rule 8.9", false },

	{ MEASURE_INSTABILITY, "instability", 0, 0,
	  SEVERITY_WARNING, true, "Martin", false },

	/* **The one row that is not a published standard**, and the label is
	 * load-bearing. Presenting it beside MISRA and Martin without saying
	 * so would lend it an authority it has not got, and would make the
	 * "no built-in opinion" claim false (HLR-099, LLR-ARC-02). */
	{ MEASURE_BOTTLENECK, "bottleneck", 0, 0,
	  SEVERITY_WARNING, true,
	  ELC_OWN_HEURISTIC, true }
};

/* ------------------------------------------------- sources without bands --
 *
 * A citation and a threshold are separate claims, and Henry-Kafura is the
 * measurement that separates them. The formula is Henry and Kafura's and must
 * be attributed wherever it is reported (HLR-157, HLR-099); no published
 * source divides it into accepted and unaccepted ranges, so it gets no band
 * (HLR-159).
 *
 * A row in CATALOGUE with empty bounds would not express that. `occurrence`
 * is false and both bounds are zero for such a row, which is a *silent*
 * band — every value passes — and `thresholds_lookup` would answer "there is
 * a threshold for this" to a caller asking precisely because there is not.
 * Keeping the citation in its own table is what leaves LLR-THR-08's path the
 * one that runs, and what leaves the catalogue holding only rows a reader can
 * check against a published table.
 */
static const struct {
	MeasurementKind kind;
	const char     *attribution;
} UNBANDED[] = {
	{ MEASURE_HENRY_KAFURA, "Henry-Kafura" }
};

const Threshold *thresholds_lookup(MeasurementKind kind)
{
	for (size_t i = 0; i < sizeof CATALOGUE / sizeof *CATALOGUE; i++)
		if (CATALOGUE[i].kind == kind)
			return &CATALOGUE[i];
	return NULL;
}

const char *threshold_attribution(MeasurementKind kind)
{
	const Threshold *t = thresholds_lookup(kind);

	if (t)
		return t->attribution;

	for (size_t i = 0; i < sizeof UNBANDED / sizeof *UNBANDED; i++)
		if (UNBANDED[i].kind == kind)
			return UNBANDED[i].attribution;

	return NULL;
}

bool threshold_is_elc_own(MeasurementKind kind)
{
	const Threshold *t = thresholds_lookup(kind);

	return t ? t->elc_own : false;
}

const char *severity_name(Severity severity)
{
	switch (severity) {
	case SEVERITY_CRITICAL: return "critical";
	case SEVERITY_WARNING:  return "warning";
	case SEVERITY_INFO:
	default:                return "info";
	}
}

int severity_rank(const char *name)
{
	if (!name)
		return 0;
	if (strcmp(name, "critical") == 0)
		return 2;
	if (strcmp(name, "warning") == 0)
		return 1;
	return 0;
}

/* Band a counted measurement.
 *
 * The critical test comes first, so that where both bands apply the higher
 * severity is the one reported — which is what HLR-123 asks for, and is why
 * the two tests are ordered rather than written as an if/else chain from the
 * bottom up.
 */
static bool band_of(const Threshold *t, uint32_t value, Severity *out)
{
	if (value > t->critical_above) {
		*out = SEVERITY_CRITICAL;
		return true;
	}
	if (value > t->warning_above) {
		*out = SEVERITY_WARNING;
		return true;
	}
	return false;   /* inside the accepted range; no finding */
}

/* ------------------------------------------------------------- findings -- */

static int finding_add(FindingList *out, MeasurementKind kind,
                       Severity severity, const char *subject,
                       const char *where, uint32_t line, const char *detail)
{
	if (out->count == out->capacity) {
		size_t   next   = out->capacity ? out->capacity * 2 : 16;
		Finding *bigger = realloc(out->items, next * sizeof *bigger);

		if (!bigger)
			return -1;
		out->items    = bigger;
		out->capacity = next;
	}

	Finding *f = &out->items[out->count];

	memset(f, 0, sizeof *f);
	f->kind     = kind;
	f->severity = severity;
	f->subject  = strdup(subject ? subject : "");
	f->where    = strdup(where ? where : "");
	f->detail   = strdup(detail ? detail : "");
	if (!f->subject || !f->where || !f->detail) {
		free(f->subject);
		free(f->where);
		free(f->detail);
		return -1;
	}
	f->line = line;
	out->count++;
	return 0;
}

/* Join node names for a finding's detail, bounded: a cycle of two hundred
 * functions is a finding about the cycle, not a place to print two hundred
 * names. */
static void join_nodes(char *buf, size_t len, const Sdg *g,
                       const uint32_t *nodes, size_t count)
{
	size_t at = 0;

	buf[0] = '\0';
	for (size_t i = 0; i < count; i++) {
		if (nodes[i] >= g->node_count)
			continue;

		int n = snprintf(buf + at, len - at, "%s%s", at ? ", " : "",
		                 g->nodes[nodes[i]].name);

		if (n < 0 || (size_t)n >= len - at)
			break;
		at += (size_t)n;
	}
}

/* --------------------------------------------------------- the analyses -- */

static int apply_fan_out(const TreeResults *tree, const Sdg *g,
                         FindingList *out)
{
	const Threshold *t = thresholds_lookup(MEASURE_FAN_OUT);

	if (!t || !tree->fan_out)
		return 0;

	for (size_t i = 0; i < tree->node_count && i < g->node_count; i++) {
		Severity severity;
		char     detail[64];

		if (!band_of(t, tree->fan_out[i], &severity))
			continue;

		snprintf(detail, sizeof detail, "calls %" PRIu32
		         " distinct subroutines", tree->fan_out[i]);
		if (finding_add(out, MEASURE_FAN_OUT, severity,
		                g->nodes[i].name, g->nodes[i].file,
		                g->nodes[i].line_start, detail) != 0)
			return -1;
	}

	return 0;
}

static int apply_depth(const TreeResults *tree, FindingList *out)
{
	const Threshold *t = thresholds_lookup(MEASURE_CALL_DEPTH);
	Severity         severity;
	char             detail[96];

	/* Only a measured depth is banded. An omitted one is not a depth of
	 * zero, and an unbounded one is reported as recursion below — banding
	 * either would be judging a number that does not exist (HLR-115). */
	if (!t || tree->depth_state != DEPTH_MEASURED)
		return 0;

	if (!band_of(t, tree->depth, &severity))
		return 0;

	snprintf(detail, sizeof detail,
	         "%" PRIu32 " layers deep; a lower bound, %zu calls unresolved",
	         tree->depth, tree->unresolved_calls);
	return finding_add(out, MEASURE_CALL_DEPTH, severity, "call graph", "",
	                   0, detail);
}

static int apply_recursion(const TreeResults *tree, const Sdg *g,
                           FindingList *out)
{
	const Threshold *t = thresholds_lookup(MEASURE_RECURSION);

	if (!t)
		return 0;

	for (size_t i = 0; i < tree->cycle_count; i++) {
		char members[512];
		char detail[600];

		join_nodes(members, sizeof members, g, tree->cycles[i].members,
		           tree->cycles[i].count);
		snprintf(detail, sizeof detail, "%s recursion among %s",
		         tree->cycles[i].count == 1 ? "direct" : "mutual",
		         members);

		/* The cycle's lowest-numbered member locates it: a set has no
		 * single line, and sorted-file order makes the choice a
		 * property of the tree rather than of the decomposition. */
		const char *file = "";
		uint32_t    line = 0;

		if (tree->cycles[i].count > 0 &&
		    tree->cycles[i].members[0] < g->node_count) {
			file = g->nodes[tree->cycles[i].members[0]].file;
			line = g->nodes[tree->cycles[i].members[0]].line_start;
		}

		if (finding_add(out, MEASURE_RECURSION, t->fixed, members, file,
		                line, detail) != 0)
			return -1;
	}

	return 0;
}

static int apply_cycles(const ArchResults *arch, const Sdg *g,
                        FindingList *out)
{
	const Threshold *t = thresholds_lookup(MEASURE_COMPONENT_CYCLE);

	if (!t)
		return 0;

	/* The acceptable count is strictly zero, so every cycle is a finding
	 * and every one is critical (HLR-084). */
	for (size_t i = 0; i < arch->cycle_count; i++) {
		const ComponentCycle *cycle = &arch->cycles[i];
		char                  detail[600];
		size_t                at = 0;

		detail[0] = '\0';
		for (size_t m = 0; m < cycle->path_count; m++) {
			if (cycle->path[m] >= g->component_count)
				continue;

			int n = snprintf(detail + at, sizeof detail - at,
			                 "%s%s", at ? " -> " : "",
			                 g->component_paths[cycle->path[m]]);

			if (n < 0 || (size_t)n >= sizeof detail - at)
				break;
			at += (size_t)n;
		}

		/* Closed by naming the first component again, as the cycles
		 * section does: the returning edge is the one a reader is most
		 * likely to cut, and leaving it implied makes the two
		 * renderings of one cycle disagree. */
		if (cycle->path_count > 0 && cycle->path[0] < g->component_count)
			snprintf(detail + at, sizeof detail - at, " -> %s",
			         g->component_paths[cycle->path[0]]);

		const char *first = cycle->member_count > 0 &&
		                    cycle->members[0] < g->component_count
		                            ? g->component_paths[cycle->members[0]]
		                            : "";

		if (finding_add(out, MEASURE_COMPONENT_CYCLE, t->fixed, first,
		                first, 0, detail) != 0)
			return -1;
	}

	return 0;
}

static int apply_globals(const StateResults *state, FindingList *out)
{
	for (size_t i = 0; i < state->global_count; i++) {
		const GlobalRow *row = &state->globals[i];
		MeasurementKind  kind;
		char             detail[256];

		switch (row->verdict) {
		case GLOBAL_SCOPE_REDUCTION:
			kind = MEASURE_SCOPE_REDUCTION;
			snprintf(detail, sizeof detail,
			         "named by one function; belongs at block scope");
			break;
		case GLOBAL_HIDDEN_CHANNEL:
			kind = MEASURE_HIDDEN_CHANNEL;
			snprintf(detail, sizeof detail,
			         "shared across %zu regions of the call graph "
			         "that never call each other", row->region_count);
			break;
		case GLOBAL_ORDINARY:
		default:
			continue;   /* inside its accepted range */
		}

		const Threshold *t = thresholds_lookup(kind);

		if (!t)
			continue;
		if (finding_add(out, kind, t->fixed, row->object, "", 0,
		                detail) != 0)
			return -1;
	}

	return 0;
}

/* Instability against the layer a component was declared in.
 *
 * Martin's point is not that any value is wrong, but that a component's
 * instability should match its intended level of abstraction: a layer
 * everything rests on should be stable, and top-level logic should be free to
 * change. The declared strata are what supply that intent — `elc` will not
 * guess it from a directory name — so with none declared there is nothing to
 * compare against and no finding is possible (HLR-115).
 *
 * The rule is deliberately coarse. The topmost declared layer is expected to
 * be unstable and the bottommost stable; a component sitting on the wrong side
 * of the midpoint from that expectation is the mismatch. A finer rule would be
 * inventing precision the source does not have.
 */
static int apply_instability(const ArchResults *arch, const Sdg *g,
                             const ElcOptions *opts, FindingList *out)
{
	const Threshold *t = thresholds_lookup(MEASURE_INSTABILITY);

	if (!t || opts->strata.count < 2 ||
	    arch->strata_state != STRATA_MEASURED)
		return 0;

	for (size_t c = 0; c < arch->component_count && c < g->component_count;
	     c++) {
		const ComponentCoupling *k = &arch->coupling[c];

		if (!k->instability_defined)
			continue;

		/* The component's declared layer, or none. */
		size_t ordinal = SIZE_MAX;
		size_t layer   = 0;

		for (size_t sidx = 0; sidx < opts->strata.count &&
		     ordinal == SIZE_MAX; sidx++)
			for (size_t p = 0;
			     p < opts->strata.items[sidx].pattern_count; p++)
				if (fnmatch(opts->strata.items[sidx].patterns[p],
				            g->component_paths[c], 0) == 0) {
					ordinal = opts->strata.items[sidx].ordinal;
					layer   = sidx;
					break;
				}

		if (ordinal == SIZE_MAX)
			continue;   /* outside the declared architecture */

		double expected = 1.0 - (double)ordinal /
		                        (double)(opts->strata.count - 1);

		/* Both on the same side of the midpoint is agreement. Only a
		 * component whose measurement contradicts its declared role is
		 * reported, so an ordinary middle layer is silent. */
		if ((expected >= 0.5) == (k->instability >= 0.5))
			continue;

		char detail[192];

		snprintf(detail, sizeof detail,
		         "instability %.2f, but declared in layer %s, which the "
		         "declaration places at %.2f",
		         k->instability, opts->strata.items[layer].name, expected);
		if (finding_add(out, MEASURE_INSTABILITY, t->fixed,
		                g->component_paths[c], g->component_paths[c], 0,
		                detail) != 0)
			return -1;
	}

	return 0;
}

static int apply_bottlenecks(const ArchResults *arch, const Sdg *g,
                             const ElcOptions *opts, FindingList *out)
{
	const Threshold *t = thresholds_lookup(MEASURE_BOTTLENECK);

	if (!t)
		return 0;

	for (size_t c = 0; c < arch->component_count && c < g->component_count;
	     c++) {
		char detail[192];

		if (!arch->coupling[c].bottleneck)
			continue;

		snprintf(detail, sizeof detail,
		         "Ca %" PRIu32 " and Ce %" PRIu32 ", each at or above "
		         "the threshold of %" PRIu32,
		         arch->coupling[c].ca, arch->coupling[c].ce,
		         opts->bottleneck_threshold);
		if (finding_add(out, MEASURE_BOTTLENECK, t->fixed,
		                g->component_paths[c], g->component_paths[c], 0,
		                detail) != 0)
			return -1;
	}

	return 0;
}

int thresholds_apply(const ArchResults *arch, const TreeResults *tree,
                     const StateResults *state, const Sdg *g,
                     const ElcOptions *opts, FindingList *out)
{
	memset(out, 0, sizeof *out);

	if (!g)
		return 0;

	/* Henry-Kafura is measured and never banded, so nothing appears here
	 * for it. That is LLR-THR-08's path taken by omission rather than by a
	 * branch: a kind the catalogue holds no row for produces no finding,
	 * and the value reaches the reader through the report model as a bare
	 * measurement (HLR-159). */
	if ((tree && apply_fan_out(tree, g, out) != 0) ||
	    (tree && apply_depth(tree, out) != 0) ||
	    (tree && apply_recursion(tree, g, out) != 0) ||
	    (arch && apply_cycles(arch, g, out) != 0) ||
	    (state && apply_globals(state, out) != 0) ||
	    (arch && apply_instability(arch, g, opts, out) != 0) ||
	    (arch && apply_bottlenecks(arch, g, opts, out) != 0)) {
		findinglist_free(out);
		return -1;
	}

	return 0;
}

void findinglist_free(FindingList *f)
{
	if (!f)
		return;

	for (size_t i = 0; i < f->count; i++) {
		free(f->items[i].subject);
		free(f->items[i].where);
		free(f->items[i].detail);
	}
	free(f->items);
	memset(f, 0, sizeof *f);
}
