/* analyze.h — the single parse.
 *
 * analyze.c is the only module that opens a source file for its contents,
 * and it opens every one of them read-only, which is how HLR-043 is
 * satisfied structurally rather than by convention (doc/SDD.md §7).
 *
 * Phase 2 extracts function identity. ELOC (Phase 3), complexity (Phase 4),
 * and the graph facts (Phase 8) are added to the same traversal, at which
 * point analyze_file() also produces a FileFacts.
 */
#ifndef ELC_ANALYZE_H
#define ELC_ANALYZE_H

#include "elc.h"
#include "registry.h"

/* analyze_file() outcomes. A skip and a failure are distinct: a file whose
 * language is unavailable is reported skipped and leaves the exit status at
 * 0, where a file that could not be read or parsed makes it 1 (HLR-012,
 * HLR-035, HLR-037). */
enum {
	ANALYZE_OK      = 0,
	ANALYZE_FAILED  = 1,
	ANALYZE_SKIPPED = 2
};

/* Analyse one file.
 *
 * On ANALYZE_OK, `*out` receives a newly allocated FileMetrics that owns its
 * path and its functions; the caller releases it with filemetrics_free() or
 * hands it to the metrics accumulator. On any other outcome `*out` is NULL
 * and a diagnostic has been written to stderr.
 */
int analyze_file(Registry *reg, const char *path, FileMetrics **out);

/* Release a file's metrics and every function name it owns. Safe on NULL. */
void filemetrics_free(FileMetrics *metrics);

#endif /* ELC_ANALYZE_H */
