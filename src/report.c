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

static void pathlist_free(PathList *list)
{
	for (size_t i = 0; i < list->count; i++)
		free(list->paths[i]);
	free(list->paths);
	list->paths    = NULL;
	list->count    = 0;
	list->capacity = 0;
}

int metrics_add_skipped(MetricsAccumulator *acc, const char *path)
{
	PathList *list = &acc->skipped;

	if (list->count == list->capacity) {
		size_t next   = list->capacity ? list->capacity * 2 : 8;
		char **bigger = realloc(list->paths, next * sizeof *bigger);

		if (!bigger) {
			fputs("elc: out of memory recording a skipped file\n",
			      stderr);
			return -1;
		}
		list->paths    = bigger;
		list->capacity = next;
	}

	list->paths[list->count] = strdup(path);
	if (!list->paths[list->count]) {
		fputs("elc: out of memory recording a skipped file\n", stderr);
		return -1;
	}
	list->count++;
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
	pathlist_free(&acc->skipped);
}

static int by_path(const void *a, const void *b)
{
	const FileMetrics *x = *(FileMetrics *const *)a;
	const FileMetrics *y = *(FileMetrics *const *)b;

	return strcmp(x->path, y->path);
}

static int by_string(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Functions within a file are ordered by start line (LLR-RPT-11), with the
 * name as the tie-break. Two functions can share a start line — a nested one
 * declared on the same line as its enclosing body opens — and a comparator
 * that returned 0 there would leave their order to qsort, which is not
 * stable. The tie-break is what keeps HLR-032 true in that case. */
static int by_start_line(const void *a, const void *b)
{
	const FunctionMetric *x = a;
	const FunctionMetric *y = b;

	if (x->start_line != y->start_line)
		return x->start_line < y->start_line ? -1 : 1;
	return strcmp(x->name, y->name);
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

	out->skipped_files = acc->skipped;
	memset(&acc->skipped, 0, sizeof acc->skipped);

	for (size_t i = 0; i < out->file_count; i++) {
		out->summary.physical_lines += out->files[i]->physical_lines;
		out->summary.function_count += out->files[i]->function_count;
	}
	out->summary.file_count = out->file_count;

	/* Every collection in the model is ordered here, by an explicit key,
	 * so that no renderer sorts and no enumeration order reaches the
	 * output (LLR-RPT-10, LLR-RPT-11). */
	if (out->file_count > 1)
		qsort(out->files, out->file_count, sizeof *out->files, by_path);

	for (size_t i = 0; i < out->file_count; i++)
		if (out->files[i]->function_count > 1)
			qsort(out->files[i]->functions,
			      out->files[i]->function_count,
			      sizeof *out->files[i]->functions, by_start_line);

	if (out->skipped_files.count > 1)
		qsort(out->skipped_files.paths, out->skipped_files.count,
		      sizeof *out->skipped_files.paths, by_string);

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
	pathlist_free(&report->skipped_files);
	memset(&report->summary, 0, sizeof report->summary);
}
