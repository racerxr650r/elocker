/* test/unit/format_csv.c — unit tests for src/format_csv.c.
 *
 * `write_field` is where a record's structure is preserved or lost, and the
 * values that break it are values `elc` cannot yet produce — C has no
 * template signatures. Constructing them here is the only way to test the
 * requirement before Phase 6 makes them reachable, and by then a defect
 * would be discovered by a user rather than by a test.
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "format_csv.h"
#include "report.h"

/* Field `value` as write_field emits it; the caller frees the result. */
static char *emitted(const char *value)
{
	FILE *fp = tmpfile();
	char *buffer;
	long  length;

	cr_assert_not_null(fp, "could not open a temporary stream");
	write_field(value, fp);

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

/* Verifies LLR-FLD-01: an ordinary field is emitted bare, so the common case
 * stays readable rather than uniformly quoted. */
Test(format_csv, an_ordinary_field_is_not_quoted)
{
	char *out = emitted("main");

	cr_assert_str_eq(out, "main");
	free(out);
}

/* Verifies LLR-FLD-01: a comma is what splits a record, so a field carrying
 * one must be quoted. This is the C++ template signature case — `foo<int,
 * long>` is one identifier that looks like two fields. */
Test(format_csv, a_field_containing_a_comma_is_quoted)
{
	char *out = emitted("foo<int, long>");

	cr_assert_str_eq(out, "\"foo<int, long>\"");
	free(out);
}

/* Verifies LLR-FLD-01: RFC 4180 escapes a quote by doubling it. A backslash
 * escape would produce a field that parses without error and carries the
 * wrong text, which is worse than one that fails. */
Test(format_csv, a_quote_is_doubled_not_backslashed)
{
	char *out = emitted("say \"hi\"");

	cr_assert_str_eq(out, "\"say \"\"hi\"\"\"");
	cr_assert_null(strchr(out, '\\'), "CSV has no backslash escape");
	free(out);
}

/* Verifies LLR-FLD-01: a line break inside a field must be quoted, or it
 * ends the record. */
Test(format_csv, a_field_containing_a_newline_is_quoted)
{
	char *out = emitted("two\nlines");

	cr_assert_str_eq(out, "\"two\nlines\"");
	free(out);
}

/* Verifies LLR-FLD-01: a carriage return ends a record as surely as a
 * newline does, since records are CRLF-terminated. */
Test(format_csv, a_field_containing_a_carriage_return_is_quoted)
{
	char *out = emitted("two\rlines");

	cr_assert_str_eq(out, "\"two\rlines\"");
	free(out);
}

/* Verifies LLR-FLD-01: all three triggers at once, in one field. */
Test(format_csv, a_field_needing_every_escape_survives)
{
	char *out = emitted("a,\"b\"\nc");

	cr_assert_str_eq(out, "\"a,\"\"b\"\"\nc\"");
	free(out);
}

Test(format_csv, an_empty_field_is_emitted_empty)
{
	char *out = emitted("");

	cr_assert_str_eq(out, "");
	free(out);
}

Test(format_csv, a_null_field_is_emitted_empty)
{
	char *out = emitted(NULL);

	cr_assert_str_eq(out, "", "a missing value is an empty field, not a "
	                          "fault");
	free(out);
}

/* Verifies LLR-CSV-03: the header names the columns a record carries, in the
 * order and with the meaning the Functions table gives them, and goes through
 * the same emission path as every record.
 *
 * Written out in full rather than compared field by field, because the
 * requirement is the whole list: this is the Functions table for a consumer
 * that loads it, and a column here that the table does not have — or one it has
 * and this does not — is the drift the requirement exists to end (HLR-014). */
Test(format_csv, the_header_row_is_written)
{
	Report report = { 0 };
	FILE  *fp     = tmpfile();
	char   line[256];

	cr_assert_not_null(fp);
	cr_assert_eq(format_csv(&report, fp), 0);
	rewind(fp);
	cr_assert_not_null(fgets(line, sizeof line, fp));

	cr_assert_str_eq(line,
	                 "file,language,function,visibility,lines,eloc,"
	                 "complexity,fan_in,fan_out,"
	                 "mock_burden,wf_out,tbi,tbi_status\r\n");
	fclose(fp);
}

/* Render a whole report and return the bytes written; the caller frees. */
static char *rendered(Report *report)
{
	FILE *fp = tmpfile();
	char *buffer;
	long  length;

	cr_assert_not_null(fp, "could not open a temporary stream");
	cr_assert_eq(format_csv(report, fp), 0);

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

/* One file holding one function, built by hand rather than measured: the
 * record's shape is what is under test, not the analysis that fills it. */
static Report one_function(Visibility visibility)
{
	static FileMetrics  *file;
	static FileMetrics  *files[1];
	Report               report = { 0 };

	file = calloc(1, sizeof *file);
	cr_assert_not_null(file);
	file->path     = strdup("/tree/a.c");
	file->language = strdup("c");
	file->functions = calloc(1, sizeof *file->functions);
	cr_assert_not_null(file->path);
	cr_assert_not_null(file->language);
	cr_assert_not_null(file->functions);

	file->functions[0].name       = strdup("f");
	cr_assert_not_null(file->functions[0].name);
	file->functions[0].start_line = 10;
	file->functions[0].end_line   = 14;
	file->functions[0].visibility = visibility;
	file->functions[0].eloc       = 3;
	file->functions[0].complexity = 2;
	file->functions[0].fan_in     = 4;
	file->functions[0].fan_out    = 5;
	/* Chosen so the rendered row shows the two decimals the format uses
	 * and a band that is neither the floor nor the default: 21.50 is over
	 * the warning bound and under the critical one (HLR-224). */
	file->functions[0].mock_burden = 0.85;
	file->functions[0].wf_out      = 3.60;
	file->functions[0].tbi         = 21.5;
	file->function_count          = 1;

	files[0]          = file;
	report.files      = files;
	report.file_count = 1;
	return report;
}

static void release(Report *report)
{
	filemetrics_free(report->files[0]);
}

/* Verifies LLR-CSV-03: a record carries the Functions table's fields, in its
 * order and with its meanings — the location as `path:line`, and the extent as
 * a count rather than a range.
 *
 * The whole record is asserted rather than a field of it, because the
 * requirement is the whole list: this is that table for a consumer that loads
 * it, and one column here the table does not have is the drift the requirement
 * exists to end (HLR-014).
 */
Test(format_csv, a_record_carries_the_function_tables_fields)
{
	Report report = one_function(VISIBILITY_PUBLIC);
	char  *out    = rendered(&report);

	cr_assert_not_null(strstr(out,
	        "/tree/a.c:10,c,f,public,5,3,2,4,5,"
	        "0.85,3.60,21.50,warning\r\n"),
	        "the record was: %s", out);

	free(out);
	release(&report);
}

/* Verifies LLR-CSV-03: the unknown visibility is the empty field.
 *
 * Never `public`. A language whose module supplies no visibility query has not
 * been asked, and that is a different claim from having answered — the
 * asymmetry HLR-209 draws, kept in the format a consumer loads, where an empty
 * field is what reads as "no value" (HLR-138).
 */
Test(format_csv, an_unknown_visibility_is_an_empty_field)
{
	Report report = one_function(VISIBILITY_UNKNOWN);
	char  *out    = rendered(&report);

	cr_assert_not_null(strstr(out, "/tree/a.c:10,c,f,,5,3,2,4,5,"
	                               "0.85,3.60,21.50,warning\r\n"),
	        "the record was: %s", out);

	free(out);
	release(&report);
}

/* Verifies LLR-CSV-01: a report with no functions is a header and nothing
 * else, rather than no output at all. */
Test(format_csv, an_empty_report_is_a_header_alone)
{
	Report report = { 0 };
	FILE  *fp     = tmpfile();

	cr_assert_not_null(fp);
	cr_assert_eq(format_csv(&report, fp), 0);

	long length = ftell(fp);
	rewind(fp);

	char *buffer = calloc(1, (size_t)length + 1);
	cr_assert_not_null(buffer);
	cr_assert_eq(fread(buffer, 1, (size_t)length, fp), (size_t)length);

	cr_assert_eq(strchr(buffer, '\n'), strrchr(buffer, '\n'),
	             "exactly one line: the header");

	free(buffer);
	fclose(fp);
}

/* Verifies LLR-CSV-01: a write failure is reported rather than silently
 * truncating the document. */
Test(format_csv, a_write_failure_is_reported)
{
	Report report = { 0 };
	FILE  *fp     = fopen("/dev/full", "w");

	cr_assert_not_null(fp, "/dev/full is needed to provoke a write failure");
	cr_assert_neq(format_csv(&report, fp), 0);
	fclose(fp);
}
