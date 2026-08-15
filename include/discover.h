/* discover.h — target validation, classification, and file discovery.
 *
 * discover.c turns the target arguments into the de-duplicated, stably
 * ordered list of files every later stage consumes (doc/SDD.md §5). It reads
 * directory structure and file metadata only; opening a source file for its
 * contents is analyze.c's responsibility.
 */
#ifndef ELC_DISCOVER_H
#define ELC_DISCOVER_H

#include <stdbool.h>
#include <stddef.h>

#include "elc.h"

/* The discovered files: canonical absolute paths, each appearing once, in
 * ascending byte order. Owned by the list. */
typedef struct {
	char  **paths;
	size_t  count;
	size_t  capacity;
} FileList;

/* The binary-extension exclusion list, loaded from the runtime location
 * (HLR-005). It is passed explicitly rather than held in a global so that
 * every function that needs it receives it through its arguments. */
typedef struct {
	char  **exts;   /* each including its leading dot, e.g. ".png"       */
	size_t  count;
	size_t  capacity;
} ExtensionList;

/* Produce the complete, ordered, de-duplicated analysis file list.
 *
 * Every target is validated with stat(2) before any of them is walked, so an
 * invalid target aborts the run before a report could cover fewer targets
 * than the user named (HLR-062, LLR-DSC-01).
 *
 * Returns 0 when every target was valid, in which case `*failures` holds the
 * number of per-file failures encountered during traversal (an unreadable
 * subdirectory, a path that could not be canonicalised), which the caller
 * folds into its exit status. Returns non-zero when a target was invalid, in
 * which case `*out` is empty.
 */
int discover_targets(const ElcOptions *opts, FileList *out, size_t *failures);

/* Release the list and every path it owns. Safe on NULL and on a zeroed
 * list, so teardown is unconditional on every exit path. */
void filelist_free(FileList *list);

/* fts(3) traversal of one directory, with hidden-entry, binary-extension and
 * symbolic-link filtering (LLR-FTS-01 – LLR-FTS-06).
 *
 * Returns 0 when the walk ran, incrementing `*failures` for each entry that
 * could not be read; non-zero only when the traversal could not be started.
 */
int walk_filesystem(const char *root, const ExtensionList *exts,
                    FileList *out, size_t *failures);

/* True when the path's extension appears in the exclusion list. The list is
 * runtime data; no extension is compiled into the executable (LLR-EXT-01). */
bool is_excluded_extension(const char *path, const ExtensionList *exts);

/* Load the binary-extension list from `binary.exts` in the runtime location:
 * $ELC_RUNTIME_DIR when set, otherwise the runtime directory adjacent to the
 * executable (HLR-059).
 *
 * An absent or unreadable file is a diagnostic and an empty list, not a
 * fatal error: discovery still runs, and nothing is excluded (LLR-EXT-02).
 * Returns 0 unless the list itself could not be built.
 */
int binary_exts_load(ExtensionList *out);

/* Release the extension list and every extension it owns. */
void binary_exts_free(ExtensionList *list);

#endif /* ELC_DISCOVER_H */
