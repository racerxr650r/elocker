/* format_csv.c — the RFC 4180 renderer.
 *
 * One record per function over the complete dataset: no threshold is applied
 * here, because CSV is the view a consumer filters for itself (HLR-028,
 * LLR-CSV-01). The architectural findings are absent by design rather than
 * by omission — they are not expressible as a single flat record set, and
 * XML is the format that carries a complete run (LLR-CSV-02).
 *
 * **The columns are the Functions table's columns, in its order.** CSV is that
 * table for a consumer that loads it rather than reads it, and the two had
 * drifted: the table gained a visibility, a navigable location and the flow
 * degrees, and this still wrote a start and end line nothing else reported.
 * One view of one set of rows, spelled two ways, is two views nobody
 * reconciles (HLR-014).
 *
 * That reverses what HLR-014 said of the complete-record formats — that they
 * keep a start and an end line because a consumer cannot subtract a column it
 * was not given. The reasoning holds for XML, which must rebuild a report from
 * its record and needs both numbers as numbers (HLR-056). It did not hold
 * here, where the record is the table.
 *
 * The `language` field is the same rule read the other way. It was dropped when
 * the two were first matched, because the table did not carry one; the table
 * carries one now, so this does too. They move together or the drift starts
 * again.
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

/* What the record calls a visibility.
 *
 * The three states `format_text.c` renders, spelled for a consumer rather than
 * for a reader: the unknown state is the empty field, which is what a loader
 * reads as "no value" and what an em dash would not be. It is never written as
 * `public` — a language whose module supplies no visibility query has not been
 * asked, and that is a different claim from having answered (HLR-209).
 */
static const char *csv_visibility(Visibility v)
{
	switch (v) {
	case VISIBILITY_PUBLIC:  return "global";
	case VISIBILITY_PRIVATE: return "local";
	case VISIBILITY_UNKNOWN:
	default:                 return "";
	}
}

int format_csv(const Report *report, FILE *out)
{
	static const char *const header[] = {
		"file", "language", "function", "visibility", "lines", "eloc",
		"complexity", "fan_in", "fan_out",
		"wtbi", "wtbi_status"
	};
	const size_t columns = sizeof header / sizeof *header;

	/* The header goes through the same path as every record, so a column
	 * name containing a comma could not corrupt the document either. */
	write_record(out, columns, header);

	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		for (size_t j = 0; j < f->function_count; j++) {
			const FunctionMetric *fn = &f->functions[j];
			char where[4096];
			char lines[16], eloc[16], complexity[16];
			char fan_in[16], fan_out[16];
			char wtbi[32];
			const char *fields[] = {
				where, f->language ? f->language : "",
				fn->name, csv_visibility(fn->visibility),
				lines, eloc, complexity, fan_in, fan_out,
				wtbi, elc_wtbi_status(fn->wtbi)
			};

			/* `path:line`, the same field the table carries. The
			 * start line is in it rather than in a column of its
			 * own, which is what makes the extent a count here as
			 * it is there (HLR-014, HLR-210). */
			snprintf(where, sizeof where, "%s:%" PRIu32,
			         f->path, fn->start_line);
			snprintf(lines, sizeof lines, "%" PRIu32,
			         fn->end_line - fn->start_line + 1);
			snprintf(eloc, sizeof eloc, "%" PRIu32, fn->eloc);
			snprintf(complexity, sizeof complexity, "%" PRIu32,
			         fn->complexity);
			snprintf(fan_in, sizeof fan_in, "%" PRIu32, fn->fan_in);
			snprintf(fan_out, sizeof fan_out, "%" PRIu32,
			         fn->fan_out);
			/* Two decimals: the weights the index is built from
			 * are quarters and tenths, so two places carry every
			 * value it can take exactly and no more (HLR-032). */
			snprintf(wtbi, sizeof wtbi, "%.2f", fn->wtbi);

			write_record(out, columns, fields);
		}
	}

	if (fflush(out) != 0 || ferror(out))
		return -1;

	return 0;
}
