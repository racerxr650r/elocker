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
#include "discover.h"
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

/* Accumulate one file into its language's totals, adding the language on
 * first sight. The list is short — one entry per language present — so a
 * linear search costs less than the structure that would avoid it. */
static int language_add(LanguageList *list, const FileMetrics *file)
{
	const char *name = file->language ? file->language : "";

	for (size_t i = 0; i < list->count; i++) {
		if (strcmp(list->items[i].language, name) != 0)
			continue;
		list->items[i].file_count++;
		list->items[i].physical_lines += file->physical_lines;
		list->items[i].eloc           += file->eloc;
		return 0;
	}

	if (list->count == list->capacity) {
		size_t          next   = list->capacity ? list->capacity * 2 : 8;
		LanguageTotals *bigger = realloc(list->items, next * sizeof *bigger);

		if (!bigger) {
			fputs("elc: out of memory summarising by language\n",
			      stderr);
			return -1;
		}
		list->items    = bigger;
		list->capacity = next;
	}

	list->items[list->count].language       = name;
	list->items[list->count].file_count     = 1;
	list->items[list->count].physical_lines = file->physical_lines;
	list->items[list->count].eloc           = file->eloc;
	list->count++;
	return 0;
}

static int by_language(const void *a, const void *b)
{
	const LanguageTotals *x = a;
	const LanguageTotals *y = b;

	return strcmp(x->language, y->language);
}

/* Routes are ordered by target, not by the order they were named. A section
 * listing targets in command-line order makes `elc a b` and `elc b a`
 * different reports, which is exactly what HLR-033 forbids — and it is the
 * kind of thing that only shows up as a determinism failure, never as a
 * wrong number. */
static int by_route_target(const void *a, const void *b)
{
	const RouteRecord *x = a;
	const RouteRecord *y = b;

	return strcmp(x->target, y->target);
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

/* Add one function to the per-file threshold listing. */
static int threshold_add(ThresholdList *list, const char *file,
                         const FunctionMetric *function)
{
	if (list->count == list->capacity) {
		size_t          next   = list->capacity ? list->capacity * 2 : 8;
		ThresholdEntry *bigger = realloc(list->items, next * sizeof *bigger);

		if (!bigger) {
			fputs("elc: out of memory listing threshold breaches\n",
			      stderr);
			return -1;
		}
		list->items    = bigger;
		list->capacity = next;
	}

	list->items[list->count].file     = file;
	list->items[list->count].function = function;
	list->count++;
	return 0;
}

/* Select the highest-ELOC file and the highest-complexity function.
 *
 * Called after the model is ordered, and taking a new candidate only on a
 * *strictly* greater value. Both together are the tie-break: scanning in
 * presentation order and refusing to displace an equal value means the
 * winner is whichever sorts first, which is what makes the callout the same
 * on every run (HLR-026, HLR-033).
 */
static void select_callouts(Report *out)
{
	for (size_t i = 0; i < out->file_count; i++) {
		const FileMetrics *file = out->files[i];

		if (!out->summary.largest_file ||
		    file->eloc > out->summary.largest_file_eloc) {
			out->summary.largest_file      = file->path;
			out->summary.largest_file_eloc = file->eloc;
		}

		for (size_t j = 0; j < file->function_count; j++) {
			const FunctionMetric *fn = &file->functions[j];

			if (!out->summary.most_complex ||
			    fn->complexity > out->summary.most_complex_value) {
				out->summary.most_complex       = fn->name;
				out->summary.most_complex_file  = file->path;
				out->summary.most_complex_value = fn->complexity;
			}
		}
	}
}

int report_assemble(MetricsAccumulator *acc, const RouteList *routes,
                    const ElcOptions *opts, Report *out)
{
	memset(out, 0, sizeof *out);

	/* Copied rather than moved: discovery owns its list until the run
	 * ends, and a regenerated report has none to move (LLR-RPT-17). */
	for (size_t i = 0; routes && i < routes->count; i++)
		if (routelist_add(&out->routes, routes->items[i].target,
		                  routes->items[i].route) != 0) {
			fputs("elc: out of memory recording a discovery route\n",
			      stderr);
			return -1;
		}
	out->complexity_threshold = opts->complexity_threshold;

	out->files      = acc->files;
	out->file_count = acc->count;
	acc->files      = NULL;
	acc->count      = 0;
	acc->capacity   = 0;

	out->skipped_files = acc->skipped;
	memset(&acc->skipped, 0, sizeof acc->skipped);

	for (size_t i = 0; i < out->file_count; i++) {
		out->summary.physical_lines += out->files[i]->physical_lines;
		out->summary.eloc           += out->files[i]->eloc;
		out->summary.function_count += out->files[i]->function_count;

		if (language_add(&out->languages, out->files[i]) != 0)
			return -1;
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

	if (out->languages.count > 1)
		qsort(out->languages.items, out->languages.count,
		      sizeof *out->languages.items, by_language);

	if (out->routes.count > 1)
		qsort(out->routes.items, out->routes.count,
		      sizeof *out->routes.items, by_route_target);

	/* Both of these read the model *after* it is ordered, so the listing
	 * comes out in presentation order and the callouts break their ties
	 * by it (HLR-021, HLR-026). */
	for (size_t i = 0; i < out->file_count; i++) {
		const FileMetrics *file = out->files[i];

		for (size_t j = 0; j < file->function_count; j++) {
			if (file->functions[j].complexity <
			    out->complexity_threshold)
				continue;
			if (threshold_add(&out->over_threshold, file->path,
			                  &file->functions[j]) != 0)
				return -1;
		}
	}

	select_callouts(out);

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
	routelist_free(&report->routes);
	free(report->languages.items);
	report->languages.items    = NULL;
	report->languages.count    = 0;
	report->languages.capacity = 0;
	free(report->over_threshold.items);
	report->over_threshold.items    = NULL;
	report->over_threshold.count    = 0;
	report->over_threshold.capacity = 0;
	pathlist_free(&report->skipped_files);
	memset(&report->summary, 0, sizeof report->summary);
}
