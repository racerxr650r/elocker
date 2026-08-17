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
	char   buffer[512];

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
