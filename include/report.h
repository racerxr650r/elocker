/* report.h — the format-independent report model.
 *
 * report.c assembles every measurement into one structure and imposes the
 * ordering that makes the output deterministic. **Every sort lives here.**
 * No renderer sorts, and no library's enumeration order reaches the output;
 * this file is the single place a reviewer must check to be satisfied that
 * HLR-032's byte-identical guarantee holds (doc/SDD.md §13, LLR-RPT-10/11).
 *
 * Phase 1 carries per-file physical line counts and the project totals over
 * them. Findings, measurements, omissions, and the rest of the model arrive
 * with the analyses that produce them.
 */
#ifndef ELC_REPORT_H
#define ELC_REPORT_H

#include <stddef.h>
#include <stdint.h>

#include "elc.h"

/* Files discovered but not analysed, for want of a language module. The
 * report accounts for every discovered file, so a skip is visible rather
 * than a silent absence (HLR-012). */
typedef struct {
	char  **paths;
	size_t  count;
	size_t  capacity;
} PathList;

/* One function listed for its file because its complexity met or exceeded
 * the threshold (HLR-021). Both fields are borrowed from the report's own
 * files, which outlive the list.
 *
 * The list is built here rather than filtered by a renderer: a renderer is a
 * pure consumer, and a threshold applied at render time would be applied
 * once per format and could differ between them.
 */
typedef struct {
	const char           *file;
	const FunctionMetric *function;
} ThresholdEntry;

typedef struct {
	ThresholdEntry *items; /* ordered by file, then by function start line */
	size_t          count;
	size_t          capacity;
} ThresholdList;

/* Per-file metrics as they accumulate during the run, before assembly.
 * Owns every FileMetrics handed to it. */
typedef struct {
	FileMetrics **files;
	size_t        count;
	size_t        capacity;
	PathList      skipped;
} MetricsAccumulator;

/* Project-level totals across every analysed file (HLR-024), and the
 * most-complex callouts of HLR-026.
 *
 * A callout's `where` fields are NULL when the run analysed nothing. Ties are
 * broken by the stable presentation order, so the callout is a property of
 * the report rather than of the order files were discovered — without that,
 * two runs over the same tree could name different functions and HLR-032
 * would fail.
 */
typedef struct {
	size_t   file_count;
	uint64_t physical_lines;
	uint64_t eloc;
	uint64_t function_count;

	const char *largest_file;      /* highest file-level ELOC; borrowed */
	uint32_t    largest_file_eloc;

	const char *most_complex;      /* function name; borrowed           */
	const char *most_complex_file; /* the file defining it; borrowed    */
	uint32_t    most_complex_value;
} ProjectSummary;

/* One language's share of the project totals, so that the contribution of
 * each language present in the target is separately visible (HLR-025). */
typedef struct {
	const char *language; /* borrowed from a language module */
	size_t      file_count;
	uint64_t    physical_lines;
	uint64_t    eloc;
} LanguageTotals;

typedef struct {
	LanguageTotals *items; /* sorted by language name */
	size_t          count;
	size_t          capacity;
} LanguageList;


/* The model every renderer consumes. Every collection is sorted before a
 * renderer sees it. */
typedef struct {
	ProjectSummary summary;
	FileMetrics  **files;         /* sorted by path; owned            */
	size_t         file_count;
	LanguageList   languages;     /* sorted by name; owned (HLR-025)  */
	ThresholdList  over_threshold; /* the per-file listing (HLR-021)  */
	uint32_t       complexity_threshold; /* the value it was built at */
	PathList       skipped_files; /* sorted by path; owned (HLR-012)  */
} Report;

/* Record a file skipped for want of a language module, copying its path.
 * Returns 0 on success (LLR-RPT-07). */
int metrics_add_skipped(MetricsAccumulator *acc, const char *path);

/* Append one file's metrics, taking ownership of them. Grows by doubling
 * through a checked reallocation; on failure the accumulator is left intact
 * and the caller still owns `metrics` (LLR-RPT-16). Returns 0 on success. */
int metrics_add(MetricsAccumulator *acc, FileMetrics *metrics);

/* Release the accumulator and every FileMetrics it still owns. */
void metrics_free(MetricsAccumulator *acc);

/* Produce the ordered, format-independent model.
 *
 * On success the accumulator's contents move into `*out`, which owns them
 * thereafter, and the accumulator is left empty. A run in which no file was
 * analysed yields a complete model with zero totals, which renders normally
 * (HLR-066, LLR-RPT-12). Returns 0 on success.
 */
int report_assemble(MetricsAccumulator *acc, const ElcOptions *opts,
                    Report *out);

/* Release the report model and everything it owns. Safe on NULL. */
void report_free(Report *report);

#endif /* ELC_REPORT_H */
