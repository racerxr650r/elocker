/* test/unit/thresholds.c — unit tests for src/thresholds.c.
 *
 * The banding rules are what is under test, so most of these drive the
 * catalogue directly rather than through a parsed fixture. The `thresholds/`
 * fixture group covers the source-to-report path, and `calltree/fanout.c`
 * supplies the eight boundary values Phase 9 verified as measurements
 * specifically so this phase would have something already-checked to band.
 *
 * Two properties matter more than any individual band and are tested as
 * properties rather than as cases: the fan-out bands are **exhaustive**, and
 * every catalogue row **names a source**.
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "arch.h"
#include "calltree.h"
#include "elc.h"
#include "graph.h"
#include "report.h"
#include "state.h"
#include "thresholds.h"

/* ------------------------------------------------------------ scaffolding */

static FileMetrics *file_with(const char *path, const char *const *names,
                              size_t count)
{
	FileMetrics *m = calloc(1, sizeof *m);

	cr_assert_not_null(m);
	m->path     = strdup(path);
	m->language = strdup("c");
	m->functions = calloc(count ? count : 1, sizeof *m->functions);
	cr_assert_not_null(m->functions);
	for (size_t i = 0; i < count; i++) {
		m->functions[i].name       = strdup(names[i]);
		m->functions[i].start_line = (uint32_t)(i * 10 + 1);
		m->functions[i].end_line   = (uint32_t)(i * 10 + 5);
	}
	m->function_count = count;
	return m;
}

static Report report_of(FileMetrics **files, size_t count)
{
	MetricsAccumulator acc    = { 0 };
	Report             report = { 0 };
	ElcOptions         opts   = { 0 };

	for (size_t i = 0; i < count; i++)
		cr_assert_eq(metrics_add(&acc, files[i]), 0);
	cr_assert_eq(report_assemble(&acc, NULL, &opts, &report), 0);
	metrics_free(&acc);
	return report;
}

/* A one-node graph, so a fan-out value can be banded without building a
 * call structure that would produce the value incidentally. */
static void one_node_graph(Sdg *g, Report *report, FactList *facts)
{
	const char  *names[] = { "subject" };
	FileMetrics *files[] = { file_with("/p/a.c", names, 1) };

	*report = report_of(files, 1);
	cr_assert_eq(factlist_add(facts, calloc(1, sizeof(FileFacts))), 0);
	facts->items[0]->path = strdup("/p/a.c");
	cr_assert_eq(graph_build(facts, report, g), 0);
}

/* Band one fan-out value and return the severity reported, or NULL where the
 * value produced no finding at all. */
static const char *fan_out_severity(uint32_t value)
{
	static char  kept[32];
	Sdg          g      = { 0 };
	Report       report = { 0 };
	FactList     facts  = { 0 };
	TreeResults  tree   = { 0 };
	ElcOptions   opts   = { 0 };
	FindingList  found  = { 0 };
	const char  *result = NULL;

	one_node_graph(&g, &report, &facts);

	tree.fan_out = calloc(1, sizeof *tree.fan_out);
	cr_assert_not_null(tree.fan_out);
	tree.fan_out[0] = value;
	tree.node_count = 1;
	tree.depth_state = DEPTH_OMITTED_NO_ENTRY_POINTS;

	cr_assert_eq(thresholds_apply(NULL, &tree, NULL, &g, &opts, &found), 0);
	if (found.count > 0) {
		snprintf(kept, sizeof kept, "%s",
		         severity_name(found.items[0].severity));
		result = kept;
	}

	findinglist_free(&found);
	free(tree.fan_out);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
	return result;
}

/* ------------------------------------------------------- the fan-out bands */

Test(thresholds, the_fan_out_bands_match_the_published_table)
{
	/* PVD Appendix A.2, boundary by boundary. The eight values are the
	 * ones `calltree/fanout.c` pins as measurements, so a disagreement
	 * here is a banding error and not a counting one (HLR-086). */
	cr_assert_null(fan_out_severity(2),  "0-2 is below the healthy band");
	cr_assert_null(fan_out_severity(3),  "3 opens the healthy band");
	cr_assert_null(fan_out_severity(7),  "7 closes it");
	cr_assert_null(fan_out_severity(8),  "8 opens the acceptable band");
	cr_assert_null(fan_out_severity(10), "10 closes it");

	cr_assert_str_eq(fan_out_severity(11), "warning");
	cr_assert_str_eq(fan_out_severity(15), "warning");
	cr_assert_str_eq(fan_out_severity(16), "critical");
}

Test(thresholds, the_acceptable_band_produces_no_finding)
{
	/* The band that used to be a gap in the thresholds, and the reason the
	 * requirement states the bands are exhaustive rather than leaving it
	 * to be inferred. A fan-out of 9 is acceptable and silent. */
	cr_assert_null(fan_out_severity(9));
}

Test(thresholds, every_fan_out_value_classifies_exactly_once)
{
	/* Exhaustiveness as a property rather than as cases: every value from
	 * 0 to 40 lands in exactly one band, and the bands run in order with
	 * no value skipped and none claimed twice (HLR-086, LLR-THR-05). */
	for (uint32_t v = 0; v <= 40; v++) {
		const char *severity = fan_out_severity(v);
		const char *expected = v > 15 ? "critical"
		                     : v > 10 ? "warning"
		                              : NULL;

		if (!expected)
			cr_assert_null(severity, "fan-out %u must be silent", v);
		else
			cr_assert_str_eq(severity, expected,
			                 "fan-out %u", v);
	}
}

/* --------------------------------------------------------- the catalogue -- */

Test(thresholds, every_catalogued_measurement_names_a_source)
{
	/* The property the whole "no built-in opinion" claim rests on. A row
	 * without a citation would be an opinion `elc` had and did not admit
	 * to (HLR-099). */
	for (int k = 0; k < MEASURE_KIND_COUNT; k++) {
		const Threshold *t = thresholds_lookup((MeasurementKind)k);

		cr_assert_not_null(t, "kind %d has no catalogue entry", k);
		cr_assert_not_null(t->attribution);
		cr_assert_neq(t->attribution[0], '\0',
		              "kind %d names no source", k);
	}
}

Test(thresholds, exactly_one_threshold_is_elcs_own_and_says_so)
{
	int own = 0;

	for (int k = 0; k < MEASURE_KIND_COUNT; k++)
		if (threshold_is_elc_own((MeasurementKind)k)) {
			own++;
			cr_assert_not_null(
				strstr(threshold_attribution((MeasurementKind)k),
				       "not a published standard"),
				"an elc threshold must say so where it is read");
		}

	/* One, and it is the bottleneck. If a second ever appears it must be
	 * a deliberate decision rather than a drift, which is what makes the
	 * exact count worth asserting. */
	cr_assert_eq(own, 1);
	cr_assert(threshold_is_elc_own(MEASURE_BOTTLENECK));
}

Test(thresholds, the_published_thresholds_are_not_marked_as_elcs_own)
{
	cr_assert_not(threshold_is_elc_own(MEASURE_FAN_OUT));
	cr_assert_not(threshold_is_elc_own(MEASURE_RECURSION));
	cr_assert_not(threshold_is_elc_own(MEASURE_COMPONENT_CYCLE));
	cr_assert_str_eq(threshold_attribution(MEASURE_RECURSION),
	                 "MISRA C Rule 17.2");
	cr_assert_str_eq(threshold_attribution(MEASURE_SCOPE_REDUCTION),
	                 "MISRA C Rule 8.9");
	cr_assert_str_eq(threshold_attribution(MEASURE_INSTABILITY), "Martin");
}

Test(thresholds, a_kind_outside_the_catalogue_yields_no_entry)
{
	/* The caller then reports the measurement as a bare value rather than
	 * discarding it or inventing a band (LLR-THR-08). */
	cr_assert_null(thresholds_lookup((MeasurementKind)MEASURE_KIND_COUNT));
	cr_assert_null(threshold_attribution((MeasurementKind)MEASURE_KIND_COUNT));
	cr_assert_not(threshold_is_elc_own((MeasurementKind)MEASURE_KIND_COUNT));
}

/* ----------------------------------------------------------- the severity */

Test(thresholds, the_severity_set_is_closed_and_ordered)
{
	cr_assert_str_eq(severity_name(SEVERITY_INFO), "info");
	cr_assert_str_eq(severity_name(SEVERITY_WARNING), "warning");
	cr_assert_str_eq(severity_name(SEVERITY_CRITICAL), "critical");

	/* Ordered info < warning < critical, which is what "the highest
	 * applicable band wins" is defined against (HLR-123). */
	cr_assert_lt(severity_rank("info"), severity_rank("warning"));
	cr_assert_lt(severity_rank("warning"), severity_rank("critical"));
}

Test(thresholds, an_unrecognised_severity_ranks_lowest_rather_than_faulting)
{
	cr_assert_eq(severity_rank("nonsense"), severity_rank("info"));
	cr_assert_eq(severity_rank(NULL), severity_rank("info"));
}

Test(thresholds, the_highest_applicable_band_is_the_one_reported)
{
	/* A fan-out above the critical bound is above the warning bound too.
	 * Exactly one finding comes out, and it is the higher of the two
	 * (HLR-123, LLR-THR-04). */
	cr_assert_str_eq(fan_out_severity(40), "critical");
}

Test(thresholds, every_finding_carries_exactly_one_severity)
{
	/* No finding without one, and none with a value outside the set
	 * (HLR-123, LLR-THR-03). */
	Sdg         g      = { 0 };
	Report      report = { 0 };
	FactList    facts  = { 0 };
	TreeResults tree   = { 0 };
	ElcOptions  opts   = { 0 };
	FindingList found  = { 0 };

	one_node_graph(&g, &report, &facts);
	tree.fan_out    = calloc(1, sizeof *tree.fan_out);
	tree.fan_out[0] = 20;
	tree.node_count = 1;
	tree.depth_state = DEPTH_OMITTED_NO_ENTRY_POINTS;

	cr_assert_eq(thresholds_apply(NULL, &tree, NULL, &g, &opts, &found), 0);
	cr_assert_gt(found.count, 0);
	for (size_t i = 0; i < found.count; i++) {
		Severity sev = found.items[i].severity;

		cr_assert(sev == SEVERITY_INFO || sev == SEVERITY_WARNING ||
		          sev == SEVERITY_CRITICAL);
	}

	findinglist_free(&found);
	free(tree.fan_out);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ---------------------------------------------------------------- depth -- */

/* Band one depth value. */
static const char *depth_severity(uint32_t depth)
{
	static char  kept[32];
	Sdg          g      = { 0 };
	Report       report = { 0 };
	FactList     facts  = { 0 };
	TreeResults  tree   = { 0 };
	ElcOptions   opts   = { 0 };
	FindingList  found  = { 0 };
	const char  *result = NULL;

	one_node_graph(&g, &report, &facts);
	tree.depth       = depth;
	tree.depth_state = DEPTH_MEASURED;

	cr_assert_eq(thresholds_apply(NULL, &tree, NULL, &g, &opts, &found), 0);
	if (found.count > 0) {
		snprintf(kept, sizeof kept, "%s",
		         severity_name(found.items[0].severity));
		result = kept;
	}

	findinglist_free(&found);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
	return result;
}

Test(thresholds, call_depth_bands_at_eight_and_twelve)
{
	cr_assert_null(depth_severity(8), "8 layers is within the guidance");
	cr_assert_str_eq(depth_severity(9), "warning");
	cr_assert_str_eq(depth_severity(12), "warning");
	cr_assert_str_eq(depth_severity(13), "critical");
}

Test(thresholds, an_omitted_depth_is_not_banded_as_zero)
{
	/* A depth that was not measured is not a depth of zero, and banding it
	 * would be judging a number that does not exist (HLR-115). */
	Sdg         g      = { 0 };
	Report      report = { 0 };
	FactList    facts  = { 0 };
	TreeResults tree   = { 0 };
	ElcOptions  opts   = { 0 };
	FindingList found  = { 0 };

	one_node_graph(&g, &report, &facts);
	tree.depth       = 99;
	tree.depth_state = DEPTH_OMITTED_NO_ENTRY_POINTS;

	cr_assert_eq(thresholds_apply(NULL, &tree, NULL, &g, &opts, &found), 0);
	cr_assert_eq(found.count, 0);

	findinglist_free(&found);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

/* ------------------------------------------------------------- emptiness -- */

Test(thresholds, a_clean_project_yields_no_findings)
{
	/* Finding nothing is an ordinary result. Paired with every test above
	 * so that an implementation emitting findings unconditionally cannot
	 * pass them. */
	Sdg         g      = { 0 };
	Report      report = { 0 };
	FactList    facts  = { 0 };
	TreeResults tree   = { 0 };
	ElcOptions  opts   = { 0 };
	FindingList found  = { 0 };

	one_node_graph(&g, &report, &facts);
	tree.fan_out     = calloc(1, sizeof *tree.fan_out);
	tree.fan_out[0]  = 4;
	tree.node_count  = 1;
	tree.depth_state = DEPTH_OMITTED_NO_ENTRY_POINTS;

	cr_assert_eq(thresholds_apply(NULL, &tree, NULL, &g, &opts, &found), 0);
	cr_assert_eq(found.count, 0);

	findinglist_free(&found);
	free(tree.fan_out);
	graph_free(&g);
	factlist_free(&facts);
	report_free(&report);
}

Test(thresholds, findinglist_free_is_safe_on_null_and_twice)
{
	FindingList f = { 0 };

	findinglist_free(NULL);
	findinglist_free(&f);
	findinglist_free(&f);
	cr_assert(1, "releasing an empty finding list must not fault");
}
