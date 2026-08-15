/* report.c — the format-independent report model.
 *
 * Assembles every measurement into one structure and imposes the ordering
 * the renderers rely on. Centralising every sort here is deliberate: it is
 * the one file a reviewer must read to be satisfied that HLR-032 holds,
 * rather than auditing six renderers and three analysis modules
 * (doc/SDD.md §13, LLR-RPT-10, LLR-RPT-11).
 *
 * discover.c also sorts, and the two are not redundant. Its sort exists so
 * that de-duplication can collapse equal paths and so that analysis order is
 * not the filesystem's; this one exists so that *presentation* order is a
 * property of the model. A later phase that changes how files are discovered
 * cannot silently change how they are presented.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "elc.h"
#include "report.h"

int metrics_add(MetricsAccumulator *acc, FileMetrics *metrics)
{
	if (acc->count == acc->capacity) {
		size_t        next   = acc->capacity ? acc->capacity * 2 : 16;
		FileMetrics **bigger = realloc(acc->files, next * sizeof *bigger);

		/* The result goes into a temporary that is checked before the
		 * original is overwritten: `x = realloc(x, n)` loses the whole
		 * accumulator on failure (HLR-125). */
		if (!bigger) {
			fputs("elc: out of memory assembling the report\n",
			      stderr);
			return -1;
		}
		acc->files    = bigger;
		acc->capacity = next;
	}

	acc->files[acc->count++] = metrics;
	return 0;
}

void metrics_free(MetricsAccumulator *acc)
{
	if (!acc)
		return;

	for (size_t i = 0; i < acc->count; i++)
		filemetrics_free(acc->files[i]);
	free(acc->files);
	acc->files    = NULL;
	acc->count    = 0;
	acc->capacity = 0;
}

static int by_path(const void *a, const void *b)
{
	const FileMetrics *x = *(FileMetrics *const *)a;
	const FileMetrics *y = *(FileMetrics *const *)b;

	return strcmp(x->path, y->path);
}

int report_assemble(MetricsAccumulator *acc, const ElcOptions *opts,
                    Report *out)
{
	(void)opts;   /* Thresholds and format selection reach here later. */

	memset(out, 0, sizeof *out);

	out->files      = acc->files;
	out->file_count = acc->count;
	acc->files      = NULL;
	acc->count      = 0;
	acc->capacity   = 0;

	for (size_t i = 0; i < out->file_count; i++)
		out->summary.physical_lines += out->files[i]->physical_lines;
	out->summary.file_count = out->file_count;

	/* Files are presented in ascending path order (LLR-RPT-11). */
	if (out->file_count > 1)
		qsort(out->files, out->file_count, sizeof *out->files, by_path);

	return 0;
}

void report_free(Report *report)
{
	if (!report)
		return;

	for (size_t i = 0; i < report->file_count; i++)
		filemetrics_free(report->files[i]);
	free(report->files);
	report->files      = NULL;
	report->file_count = 0;
	memset(&report->summary, 0, sizeof report->summary);
}
