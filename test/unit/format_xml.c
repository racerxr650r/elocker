/* test/unit/format_xml.c — unit tests for src/format_xml.c.
 *
 * `write_escaped` is where a document stays parseable or does not, and the
 * characters that break it are ones `elc` cannot yet produce in an
 * identifier — C has no template signatures. Constructing them here is the
 * only way to test the requirement before Phase 6 makes them reachable.
 *
 * The reader's rejections are tested at the fixture level, against whole
 * documents; what is tested here is the escaping, and the one property that
 * makes regeneration byte-identical — that both paths assemble the model
 * with the same code.
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elc.h"
#include "format_xml.h"
#include "report.h"

/* `value` as write_escaped emits it; the caller frees the result. */
static char *escaped(const char *value)
{
	FILE *fp = tmpfile();
	char *buffer;
	long  length;

	cr_assert_not_null(fp, "could not open a temporary stream");
	write_escaped(value, fp);

	length = ftell(fp);
	cr_assert_geq(length, 0);
	rewind(fp);

	buffer = calloc(1, (size_t)length + 1);
	cr_assert_not_null(buffer);
	if (length > 0)
		cr_assert_eq(fread(buffer, 1, (size_t)length, fp),
		             (size_t)length);
	fclose(fp);
	return buffer;
}

/* Verifies LLR-ESC-01: the ampersand is escaped first in principle and must
 * be escaped at all — an unescaped one begins an entity reference. */
Test(format_xml, an_ampersand_is_escaped)
{
	char *out = escaped("a & b");

	cr_assert_str_eq(out, "a &amp; b");
	free(out);
}

/* Verifies LLR-ESC-01: an angle bracket would open or close a tag. This is
 * the template-signature case: `foo<int, long>` carries both. */
Test(format_xml, angle_brackets_are_escaped)
{
	char *out = escaped("foo<int, long>");

	cr_assert_str_eq(out, "foo&lt;int, long&gt;");
	free(out);
}

/* Verifies LLR-ESC-01: a quotation mark inside an attribute value ends the
 * attribute early, leaving the document unparseable from that point. */
Test(format_xml, quotation_marks_are_escaped)
{
	char *out = escaped("say \"hi\" and 'bye'");

	cr_assert_str_eq(out, "say &quot;hi&quot; and &apos;bye&apos;");
	free(out);
}

/* Verifies LLR-ESC-01: escaping is not recursive — an already-escaped value
 * must survive one pass, not be doubled. */
Test(format_xml, an_ampersand_in_an_entity_is_escaped_once)
{
	char *out = escaped("&amp;");

	cr_assert_str_eq(out, "&amp;amp;",
	                 "the text is `&amp;`, which encodes as `&amp;amp;`");
	free(out);
}

Test(format_xml, ordinary_text_is_unchanged)
{
	char *out = escaped("plain_identifier_42");

	cr_assert_str_eq(out, "plain_identifier_42");
	free(out);
}

Test(format_xml, escaping_null_emits_nothing)
{
	char *out = escaped(NULL);

	cr_assert_str_eq(out, "");
	free(out);
}

/* Verifies LLR-XWR-03, LLR-XWR-04: the record carries a format-version
 * identifier in its root, which is what lets a consumer decide whether it
 * understands the structure before interpreting it. */
Test(format_xml, the_record_carries_its_format_version)
{
	Report report = { 0 };
	FILE  *fp     = tmpfile();
	/* Comfortably larger than an empty record. The skeleton grows an
	 * element with each analysis that lands, and a buffer sized to today's
	 * output makes the next phase's test failure look like a defect in the
	 * writer. */
	char   buffer[4096];

	cr_assert_not_null(fp);
	cr_assert_eq(xml_write_report(&report, fp), 0);
	rewind(fp);

	size_t got = fread(buffer, 1, sizeof buffer - 1, fp);
	buffer[got] = '\0';

	cr_assert_not_null(strstr(buffer, "<elc-report format-version=\""));
	cr_assert_not_null(strstr(buffer, "</elc-report>"),
	                   "the root element is closed");
	fclose(fp);
}

/* Verifies LLR-XWR-01: an empty run still writes a complete, well-formed
 * record rather than nothing. */
Test(format_xml, an_empty_report_is_still_a_complete_record)
{
	Report report = { 0 };
	FILE  *fp     = tmpfile();
	/* Large enough for the whole empty record, which is what the assertions
	 * below are about: every section is emitted whether or not it has rows,
	 * so the document grows by a pair of tags with each phase and a window
	 * sized to today's would fail on the next one for the wrong reason. */
	char   buffer[8192];

	cr_assert_not_null(fp);
	cr_assert_eq(xml_write_report(&report, fp), 0);
	rewind(fp);

	size_t got = fread(buffer, 1, sizeof buffer - 1, fp);
	buffer[got] = '\0';

	cr_assert_not_null(strstr(buffer, "<?xml"));
	cr_assert_not_null(strstr(buffer, "<files>"));
	cr_assert_not_null(strstr(buffer, "<skipped>"));
	fclose(fp);
}

/* Verifies LLR-XWR-04: a write failure is reported rather than silently
 * truncating the document — a truncated record is not well-formed, and a
 * consumer would reject it with no idea why. */
Test(format_xml, a_write_failure_is_reported)
{
	Report report = { 0 };
	FILE  *fp     = fopen("/dev/full", "w");

	cr_assert_not_null(fp, "/dev/full is needed to provoke a write failure");
	cr_assert_neq(xml_write_report(&report, fp), 0);
	fclose(fp);
}

/* Verifies LLR-XRD-03: a file that is not XML at all is rejected. */
Test(format_xml, input_that_is_not_xml_is_rejected)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, "this is not xml <<<\n", 20), 0);
	close(fd);

	cr_assert_neq(xml_read_report(path, &opts, &report), 0);
	unlink(path);
	report_free(&report);
}

/* Verifies LLR-XRD-04, LLR-XRD-06: a well-formed document of some other
 * shape is rejected outright, with nothing reconstructed from it. */
Test(format_xml, well_formed_but_foreign_input_is_rejected)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] = "<?xml version=\"1.0\"?><other><file path=\"a\"/>"
	                    "</other>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	cr_assert_neq(xml_read_report(path, &opts, &report), 0);
	cr_assert_eq(report.file_count, 0,
	             "no partial conversion survives a rejection");
	unlink(path);
	report_free(&report);
}

/* Verifies LLR-XRD-05: a record of a version this build does not read is
 * rejected rather than interpreted optimistically. */
Test(format_xml, an_unsupported_format_version_is_rejected)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] = "<?xml version=\"1.0\"?>"
	                    "<elc-report format-version=\"99\"></elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	cr_assert_neq(xml_read_report(path, &opts, &report), 0);
	unlink(path);
	report_free(&report);
}

/* Verifies LLR-XRD-04: a record with no version identifier at all is
 * rejected — the identifier is what makes the structure knowable. */
Test(format_xml, a_record_without_a_version_is_rejected)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] = "<?xml version=\"1.0\"?><elc-report></elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	cr_assert_neq(xml_read_report(path, &opts, &report), 0);
	unlink(path);
	report_free(&report);
}

/* Verifies LLR-XRD-07: the threshold applied is the one supplied now, not
 * one recorded in the file. The record carries no threshold at all, which is
 * the point — it stores what was measured, not what was decided about it. */
Test(format_xml, the_threshold_supplied_now_is_the_one_applied)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <files>\n"
		"    <file path=\"/a.c\" language=\"c\" physical-lines=\"9\" eloc=\"5\">\n"
		"      <function name=\"busy\" start-line=\"1\" end-line=\"9\""
		" eloc=\"5\" complexity=\"7\"/>\n"
		"    </file>\n"
		"  </files>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	opts.complexity_threshold = 7;
	cr_assert_eq(xml_read_report(path, &opts, &report), 0);
	cr_assert_eq(report.over_threshold.count, 1,
	             "complexity 7 meets a threshold of 7");
	report_free(&report);

	opts.complexity_threshold = 8;
	cr_assert_eq(xml_read_report(path, &opts, &report), 0);
	cr_assert_eq(report.over_threshold.count, 0,
	             "the same record, a different threshold, a different list");

	unlink(path);
	report_free(&report);
}

/* Verifies LLR-XRD-01: the model is reconstructed from the record alone. */
Test(format_xml, the_model_is_reconstructed_from_the_record)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <files>\n"
		"    <file path=\"/z.c\" language=\"c\" physical-lines=\"4\" eloc=\"2\">\n"
		"      <function name=\"g\" start-line=\"1\" end-line=\"4\""
		" eloc=\"2\" complexity=\"1\"/>\n"
		"    </file>\n"
		"    <file path=\"/a.c\" language=\"c\" physical-lines=\"6\" eloc=\"3\"/>\n"
		"  </files>\n"
		"  <skipped>\n"
		"    <file path=\"/notes.md\"/>\n"
		"  </skipped>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	opts.complexity_threshold = 15;
	cr_assert_eq(xml_read_report(path, &opts, &report), 0);

	cr_assert_eq(report.file_count, 2);
	cr_assert_str_eq(report.files[0]->path, "/a.c",
	                 "the reconstructed model is ordered by the same code "
	                 "that orders a live one");
	cr_assert_eq(report.summary.eloc, 5, "totals are recomputed, not read");
	cr_assert_eq(report.skipped_files.count, 1);
	cr_assert_eq(report.languages.count, 1);

	unlink(path);
	report_free(&report);
}

/* Verifies LLR-XWR-08 for the information-flow figures: fan-in and the
 * Henry-Kafura value are carried in the record and restored from it, and the
 * project total is re-summed from the restored rows.
 *
 * Neither figure can be recomputed here. Regeneration has no graph and no
 * source from which to build one, so a record that carried fan-out alone
 * would regenerate every Henry-Kafura value as zero — which renders as an
 * ordinary number and reads as a project where nothing is connected.
 */
Test(format_xml, the_flow_figures_are_carried_by_the_record)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <calltree depth-state=\"0\" depth=\"0\">\n"
		"    <fanout function=\"hub\" file=\"/a.c\" line=\"1\""
		" value=\"2\" fan-in=\"3\" eloc=\"4\" hk=\"144\"/>\n"
		"    <fanout function=\"leaf\" file=\"/a.c\" line=\"9\""
		" value=\"0\" fan-in=\"1\" eloc=\"6\" hk=\"0\"/>\n"
		"  </calltree>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	cr_assert_eq(xml_read_report(path, &opts, &report), 0);

	cr_assert_eq(report.fan_out_count, 2);
	cr_assert_eq(report.fan_out[0].fan_in, 3);
	cr_assert_eq(report.fan_out[0].eloc, 4);
	cr_assert_eq(report.fan_out[0].henry_kafura, 144);
	cr_assert_eq(report.fan_out[1].henry_kafura, 0,
	             "a leaf's zero is carried as a zero, not as an absence");
	cr_assert_eq(report.summary.henry_kafura, 144,
	             "the total is re-summed from the rows, since "
	             "report_assemble has no fan-in to derive it from");

	unlink(path);
	report_free(&report);
}

/* A record written before the information-flow figures existed carries the
 * same format version and must still read: additions to the format are what
 * the version number does *not* mark, so an older record's missing attributes
 * mean zero rather than a rejection. */
Test(format_xml, a_record_without_the_flow_attributes_still_reads)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <calltree depth-state=\"0\" depth=\"0\">\n"
		"    <fanout function=\"hub\" file=\"/a.c\" line=\"1\""
		" value=\"2\"/>\n"
		"  </calltree>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	cr_assert_eq(xml_read_report(path, &opts, &report), 0);
	cr_assert_eq(report.fan_out_count, 1);
	cr_assert_eq(report.fan_out[0].fan_out, 2);
	cr_assert_eq(report.fan_out[0].fan_in, 0);
	cr_assert_eq(report.summary.henry_kafura, 0);

	unlink(path);
	report_free(&report);
}

/* Verifies LLR-XRD-03: a truncated record — one whose root never closes — is
 * not well-formed, and is rejected rather than read as far as it goes. */
Test(format_xml, a_truncated_record_is_rejected)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <files>\n"
		"    <file path=\"/a.c\" physical-lines=\"4\" eloc=\"2\">\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	cr_assert_neq(xml_read_report(path, &opts, &report), 0);
	cr_assert_eq(report.file_count, 0);
	unlink(path);
	report_free(&report);
}

/* Verifies LLR-XRD-03: a record that cannot be opened is a rejection, not a
 * crash. */
Test(format_xml, an_absent_record_is_rejected)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };

	cr_assert_neq(xml_read_report("/nonexistent/record.xml", &opts, &report),
	              0);
	report_free(&report);
}

/* The two conformance indices and the matrix are carried in the record and
 * restored from it (LLR-XWR-17, LLR-XRD-17).
 *
 * Neither can be recomputed on the way back. Regeneration has no call graph,
 * so a record that carried the layering rows alone would have no denominator
 * to divide by and no edges to arrange — and a regenerated report would show
 * an undefined index and an empty grid for a run whose report showed neither.
 *
 * The indices come back as *rendered* text rather than as the counts beside
 * them, so a regenerated report cannot round a figure differently from the
 * report it came from.
 */
Test(format_xml, the_conformance_indices_and_the_matrix_survive_the_record)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <files>\n"
		"    <file path=\"/a.c\" language=\"c\" physical-lines=\"6\" eloc=\"3\"/>\n"
		"  </files>\n"
		"  <architecture strata-state=\"0\" bottleneck-threshold=\"5\">\n"
		"    <conformance kind=\"back-call\" index=\"16.67%\""
		" conforming=\"83.33%\" violations=\"1\" edges=\"6\"/>\n"
		"    <conformance kind=\"skip-call\" index=\"33.33%\""
		" conforming=\"66.67%\" violations=\"2\" edges=\"6\"/>\n"
		"  </architecture>\n"
		"  <dsm from-strata=\"1\">\n"
		"    <dsm-subject name=\"app\"/>\n"
		"    <dsm-subject name=\"hal\"/>\n"
		"    <dsm-cell row=\"0\" col=\"1\" calls=\"2\"/>\n"
		"    <dsm-cell row=\"1\" col=\"0\" calls=\"1\"/>\n"
		"  </dsm>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	opts.complexity_threshold = 15;
	cr_assert_eq(xml_read_report(path, &opts, &report), 0);

	cr_assert_str_eq(report.back_call.index, "16.67%");
	cr_assert_str_eq(report.back_call.conforming, "83.33%");
	cr_assert_eq(report.back_call.violations, 1);
	cr_assert_eq(report.back_call.edges, 6);
	cr_assert_str_eq(report.skip_call.index, "33.33%");
	cr_assert_eq(report.skip_call.violations, 2);

	cr_assert(report.dsm.from_strata);
	cr_assert_eq(report.dsm.count, 2);
	cr_assert_str_eq(report.dsm.subjects[0], "app");
	cr_assert_str_eq(report.dsm.subjects[1], "hal");
	/* Above the diagonal, then below it. The cells the document omits are
	 * the zeroes, which is what keeps a mostly-empty grid small. */
	cr_assert_eq(report.dsm.cells[0 * 2 + 1], 2);
	cr_assert_eq(report.dsm.cells[1 * 2 + 0], 1);
	cr_assert_eq(report.dsm.cells[0 * 2 + 0], 0);
	cr_assert_eq(report.dsm.cells[1 * 2 + 1], 0);

	unlink(path);
	report_free(&report);
}

Test(format_xml, the_classifications_survive_the_record)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <files>\n"
		"    <file path=\"/a.c\" language=\"c\" physical-lines=\"6\" eloc=\"3\"/>\n"
		"  </files>\n"
		"  <purification sink-authority=\"90\" sink-hub=\"10\""
		" god-betweenness=\"90\" god-hub=\"90\" core-depth=\"2\""
		" retained=\"9\" masked-edges=\"12\">\n"
		"    <classification function=\"dispatch\" file=\"/a.c\""
		" class=\"god object\" metric=\"betweenness\""
		" value=\"14.00, above 100% of functions\""
		" action=\"all edges masked\" source=\"computed\" line=\"17\"/>\n"
		"    <classification function=\"util_log\" file=\"/a.c\""
		" class=\"utility sink\" metric=\"authority\""
		" value=\"1.0000, above 100% of functions\""
		" action=\"incoming edges masked\" source=\"manifest\" line=\"31\"/>\n"
		"  </purification>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	/* A record carries no graph, so a classification absent from it is one
	 * a regenerated report cannot present. The thresholds travel beside the
	 * rows because they are what the rows were decided against, and a
	 * record read a year later has no command line to consult (HLR-054,
	 * HLR-174). */
	cr_assert_eq(xml_read_report(path, &opts, &report), 0);
	cr_assert_eq(report.purification_count, 2);
	cr_assert_str_eq(report.purification[0].function, "dispatch");
	cr_assert_str_eq(report.purification[0].class_name, "god object");
	cr_assert_str_eq(report.purification[0].metric, "betweenness");
	cr_assert_str_eq(report.purification[0].action, "all edges masked");
	cr_assert_eq(report.purification[0].line, 17);
	cr_assert_str_eq(report.purification[1].class_name, "utility sink");
	/* The provenance travels with the row. A record that dropped it would
	 * leave a reader of a regenerated report unable to tell the tool's
	 * assumptions from their own team's (HLR-177). */
	cr_assert_str_eq(report.purification[0].source, "computed");
	cr_assert_str_eq(report.purification[1].source, "manifest");
	cr_assert_eq(report.purify_thresholds.sink_authority, 90);
	cr_assert_eq(report.purify_thresholds.core_depth, 2);
	cr_assert_eq(report.purified_nodes, 9);
	cr_assert_eq(report.purified_edges, 12);

	unlink(path);
	report_free(&report);
}

Test(format_xml, the_proposal_survives_the_record)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <recovery state=\"proposed\" layers=\"2\" masked=\"1\""
		" excluded=\"1\">\n"
		"    <recovered directory=\"/p/app\" layer=\"0\""
		" functions=\"2\"/>\n"
		"    <recovered directory=\"/p/hal\" layer=\"1\""
		" functions=\"3\"/>\n"
		"    <proposal arguments=\"--stratum app:'/p/app/*'"
		" --stratum-order 'app'\"/>\n"
		"  </recovery>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	/* A record carries no graph to re-order, so a proposal absent from it
	 * is one a regenerated report cannot present (HLR-054, HLR-172).
	 *
	 * **It is a proposal in the record too.** Nothing reads these elements
	 * back as a declaration: the conformance analyses of a regenerated
	 * report are exactly as omitted as they were in the run it describes
	 * (HLR-115, HLR-173). */
	cr_assert_eq(xml_read_report(path, &opts, &report), 0);
	cr_assert_eq(report.recovery_state, RECOVERY_PROPOSED);
	cr_assert_eq(report.recovery_count, 2);
	cr_assert_str_eq(report.recovery[1].directory, "/p/hal");
	cr_assert_eq(report.recovery[1].layer, 1);
	cr_assert_eq(report.recovery_strata, 2);
	cr_assert_eq(report.recovery_masked, 1);
	cr_assert_eq(report.recovery_excluded, 1);
	cr_assert_not_null(report.recovery_proposal);
	cr_assert_eq(report.strata_state, STRATA_MEASURED,
	             "the record's own strata state was left alone");
	cr_assert_eq(report.layering_count, 0);

	unlink(path);
	report_free(&report);
}

Test(format_xml, a_cyclic_recovery_survives_the_record)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <recovery state=\"cyclic\" layers=\"0\" masked=\"0\""
		" excluded=\"0\">\n"
		"    <recovery-cycle members=\"hal_init, svc_open\"/>\n"
		"  </recovery>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	/* The cycles are what is reported in place of an ordering, so a record
	 * that dropped them would leave a regenerated report claiming a
	 * layering was omitted and unable to say what stopped it (HLR-172). */
	cr_assert_eq(xml_read_report(path, &opts, &report), 0);
	cr_assert_eq(report.recovery_state, RECOVERY_CYCLIC);
	cr_assert_eq(report.recovery_cycles.count, 1);
	cr_assert_str_eq(report.recovery_cycles.paths[0],
	                 "hal_init, svc_open");
	cr_assert_null(report.recovery_proposal);

	unlink(path);
	report_free(&report);
}

Test(format_xml, an_incomplete_classification_is_a_malformed_record)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <purification core-depth=\"2\">\n"
		"    <classification function=\"dispatch\" file=\"/a.c\""
		" class=\"god object\"/>\n"
		"  </purification>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	/* A classification without the metric and value that produced it is
	 * exactly what HLR-174 forbids reporting, so a record carrying one is
	 * rejected rather than half-read (HLR-058). */
	cr_assert_neq(xml_read_report(path, &opts, &report), 0);

	unlink(path);
	report_free(&report);
}

/* A record written before these elements existed reads back without them, and
 * renders as an omitted conformance section and an empty grid rather than as
 * a failure. Adding an element is an addition an older reader ignores, which
 * is why the format version marks removals and meaning changes alone. */
Test(format_xml, a_record_without_the_matrix_still_reads)
{
	ElcOptions opts   = { 0 };
	Report     report = { 0 };
	char       path[] = "/tmp/elc-xml-XXXXXX";
	int        fd     = mkstemp(path);
	const char body[] =
		"<?xml version=\"1.0\"?>\n"
		"<elc-report format-version=\"1\">\n"
		"  <files>\n"
		"    <file path=\"/a.c\" language=\"c\" physical-lines=\"6\" eloc=\"3\"/>\n"
		"  </files>\n"
		"</elc-report>\n";

	cr_assert_neq(fd, -1);
	cr_assert_gt(write(fd, body, sizeof body - 1), 0);
	close(fd);

	cr_assert_eq(xml_read_report(path, &opts, &report), 0);
	cr_assert_null(report.back_call.index);
	cr_assert_eq(report.dsm.count, 0);
	cr_assert_eq(report.purification_count, 0);

	unlink(path);
	report_free(&report);
}
