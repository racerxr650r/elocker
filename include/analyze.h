/* analyze.h — per-file measurement.
 *
 * analyze.c is the only module that opens a source file for its contents,
 * and it opens every one of them read-only, which is how HLR-043 is
 * satisfied structurally rather than by convention (doc/SDD.md §7).
 *
 * Phase 1 measures the physical line count and nothing else. The single
 * parse — function discovery, ELOC, complexity, and the graph facts — is
 * added here by Phases 2 onwards, at which point analyze_file() also takes
 * the registry and produces a FileFacts.
 */
#ifndef ELC_ANALYZE_H
#define ELC_ANALYZE_H

#include "elc.h"

/* Measure one file.
 *
 * On success `*out` receives a newly allocated FileMetrics that owns its
 * path; the caller releases it with filemetrics_free() or hands it to the
 * metrics accumulator.
 *
 * Returns 0 on success; non-zero on a read failure, which the caller records
 * as a per-file failure without aborting the run (HLR-035).
 */
int analyze_file(const char *path, FileMetrics **out);

/* Release a file's metrics and everything it owns. Safe on NULL. */
void filemetrics_free(FileMetrics *metrics);

#endif /* ELC_ANALYZE_H */
