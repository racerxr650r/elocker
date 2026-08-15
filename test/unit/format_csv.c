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

/* Verifies LLR-CSV-01: the header names the columns a record carries, and
 * goes through the same emission path as every record. */
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
	                 "file,language,function,start_line,end_line,eloc,"
	                 "complexity\r\n");
	fclose(fp);
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
