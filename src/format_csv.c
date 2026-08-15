/* format_csv.c — the RFC 4180 renderer.
 *
 * One record per function over the complete dataset: no threshold is applied
 * here, because CSV is the view a consumer filters for itself (HLR-028,
 * LLR-CSV-01). The architectural findings are absent by design rather than
 * by omission — they are not expressible as a single flat record set, and
 * XML is the format that carries a complete run (LLR-CSV-02).
 *
 * Every field goes through `write_field`. Not most fields: every one. A C++
 * template signature such as `foo<int, long>` contains a comma, and one
 * field emitted without quoting splits a record in two — which no consumer
 * detects, because the result is still valid CSV with the wrong shape
 * (HLR-064).
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "format_csv.h"
#include "report.h"

void write_field(const char *value, FILE *out)
{
	if (!value)
		value = "";

	/* Quoting is required for exactly three characters. A field is left
	 * bare otherwise, so the common case stays readable. */
	if (!strpbrk(value, ",\"\n\r")) {
		fputs(value, out);
		return;
	}

	fputc('"', out);
	for (const char *p = value; *p; p++) {
		/* RFC 4180 escapes a quote by doubling it — there is no
		 * backslash escape in CSV, and using one produces a field that
		 * parses without error and carries the wrong text. */
		if (*p == '"')
			fputc('"', out);
		fputc(*p, out);
	}
	fputc('"', out);
}

/* One record: the fields in header order, comma-separated, CRLF-terminated
 * as RFC 4180 specifies. */
static void write_record(FILE *out, size_t count, const char *const *fields)
{
	for (size_t i = 0; i < count; i++) {
		if (i)
			fputc(',', out);
		write_field(fields[i], out);
	}
	fputs("\r\n", out);
}

int format_csv(const Report *report, FILE *out)
{
	static const char *const header[] = {
		"file", "language", "function", "start_line", "end_line",
		"eloc", "complexity"
	};
	const size_t columns = sizeof header / sizeof *header;

	/* The header goes through the same path as every record, so a column
	 * name containing a comma could not corrupt the document either. */
	write_record(out, columns, header);

	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		for (size_t j = 0; j < f->function_count; j++) {
			const FunctionMetric *fn = &f->functions[j];
			char start[16], end[16], eloc[16], complexity[16];
			const char *fields[] = {
				f->path,
				f->language ? f->language : "",
				fn->name,
				start, end, eloc, complexity
			};

			snprintf(start, sizeof start, "%" PRIu32, fn->start_line);
			snprintf(end, sizeof end, "%" PRIu32, fn->end_line);
			snprintf(eloc, sizeof eloc, "%" PRIu32, fn->eloc);
			snprintf(complexity, sizeof complexity, "%" PRIu32,
			         fn->complexity);

			write_record(out, columns, fields);
		}
	}

	if (fflush(out) != 0 || ferror(out))
		return -1;

	return 0;
}
