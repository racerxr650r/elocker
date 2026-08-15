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

/* Per-file metrics as they accumulate during the run, before assembly.
 * Owns every FileMetrics handed to it. */
typedef struct {
	FileMetrics **files;
	size_t        count;
	size_t        capacity;
} MetricsAccumulator;

/* Project-level totals across every analysed file (HLR-024). */
typedef struct {
	size_t   file_count;
	uint64_t physical_lines;
} ProjectSummary;

/* The model every renderer consumes. Every collection is sorted before a
 * renderer sees it. */
typedef struct {
	ProjectSummary summary;
	FileMetrics  **files;      /* sorted by path; owned */
	size_t         file_count;
} Report;

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
