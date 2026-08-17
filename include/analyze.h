/* analyze.h — the single parse.
 *
 * analyze.c is the only module that opens a source file for its contents,
 * and it opens every one of them read-only, which is how HLR-043 is
 * satisfied structurally rather than by convention (doc/SDD.md §7).
 *
 * Phase 2 extracts function identity. ELOC (Phase 3), complexity (Phase 4),
 * and the graph facts (Phase 8) are added to the same traversal: analyze_file()
 * produces a FileMetrics and a FileFacts from one parse of one file.
 */
#ifndef ELC_ANALYZE_H
#define ELC_ANALYZE_H

#include "elc.h"
#include "elfsyms.h"
#include "registry.h"

/* analyze_file() outcomes. A skip and a failure are distinct: a file whose
 * language is unavailable is reported skipped and leaves the exit status at
 * 0, where a file that could not be read or parsed makes it 1 (HLR-012,
 * HLR-035, HLR-037). */
enum {
	ANALYZE_OK      = 0,
	ANALYZE_FAILED  = 1,
	ANALYZE_SKIPPED = 2,
	/* Metrics were produced, and part of the file could not be parsed.
	 * The caller uses the metrics *and* counts the outcome against the
	 * exit status: something in the file went unanalysed, which is a
	 * degraded run rather than a clean one (HLR-035, HLR-037). */
	ANALYZE_DAMAGED = 3
};

/* Analyse one file.
 *
 * On ANALYZE_OK, `*out` receives a newly allocated FileMetrics that owns its
 * path and its functions; the caller releases it with filemetrics_free() or
 * hands it to the metrics accumulator. On any other outcome `*out` is NULL
 * and a diagnostic has been written to stderr.
 *
 * `image` is the function set of the linked image the run was given, or NULL
 * where none was. It is a parameter for the reason the options are: measuring
 * a file depends on which program is being measured, so the program is passed
 * in rather than reached for. A function the image does not define is recorded
 * in `(*out)->absent` and excluded from everything else (HLR-140, HLR-144).
 */
int analyze_file(Registry *reg, const ElcOptions *opts, const SymbolSet *image,
                 const char *path, FileMetrics **out, FileFacts **facts_out);

/* Release a file's metrics and every function name it owns. Safe on NULL. */
void filemetrics_free(FileMetrics *metrics);

/* Release one file's graph facts and every string they own. Safe on NULL. */
void filefacts_free(FileFacts *facts);

/* Append one file's facts to the run's list, which takes ownership. Returns
 * 0 on success. The list is released with factlist_free() once graph_build()
 * has copied what it needs; it is not kept alive for the analyses (SDD §18). */
int factlist_add(FactList *list, FileFacts *facts);

/* Release the fact list and every file's facts within it. Safe on NULL. */
void factlist_free(FactList *list);

/* --------------------------------------------------------------------------
 * The two pieces of arithmetic ELOC rests on. Both are exposed because both
 * are where this class of tool goes wrong, and both are worth testing against
 * their contract directly rather than only through a parsed file.
 */

/* One comment's extent, in bytes and in lines. */
typedef struct {
	uint32_t start_byte;
	uint32_t end_byte;
	uint32_t start_line; /* 1-based */
	uint32_t end_line;   /* 1-based */
} CommentSpan;

typedef struct {
	CommentSpan *items;
	size_t       count;
	size_t       capacity;
} SpanList;

/* Sort the spans by start byte and coalesce every overlapping or nested run
 * into one, in place; returns the number of lines the merged set covers.
 *
 * Captured comment spans overlap and nest — a block comment can contain what
 * looks like an inline comment, and a language may nest block comments
 * outright. Excluding them one capture at a time removes a shared line more
 * than once, which is how a file's ELOC goes negative. Merging first is what
 * makes the exclusion idempotent (HLR-016, LLR-MRG-01 – LLR-MRG-03).
 */
uint32_t merge_comment_spans(SpanList *spans);

/* One reported function's byte extent, and where its metrics live. */
typedef struct {
	uint32_t start_byte;
	uint32_t end_byte;
	size_t   index;      /* into FileMetrics.functions */
} FnRange;

typedef struct {
	FnRange *items;
	size_t   count;
	size_t   capacity;
} FnRangeIndex;

/* The narrowest reported function containing `byte`, or NULL when the offset
 * lies outside every one of them — file-scope code, which contributes to the
 * file's ELOC and to no function's.
 *
 * Narrowest, not first: a nested named function is reported in its own right
 * (HLR-067), and its statements must contribute to it and not also to the
 * function enclosing it (HLR-068, LLR-INN-01).
 */
const FnRange *innermost_enclosing(const FnRangeIndex *index, uint32_t byte);

/* Record every statement within a function that cannot execute (HLR-137).
 *
 * `ranges` holds the file's reported functions, so each finding is attributed
 * to the one containing it by the rule ELOC and complexity already use
 * (LLR-DED-04). On return `facts` holds one span per unreachable statement,
 * sorted and de-duplicated, and `facts->dead_analysed` records whether the
 * language supplied a `deadcode.scm` at all — the absence being "not looked
 * for" rather than "none found" (HLR-139, LLR-DED-05).
 *
 * Returns 0 on success; non-zero only on allocation failure. A language with
 * no dead-code query is not a failure.
 *
 * Exposed rather than static so the unit level can drive it against a tree it
 * built, which is the level at which the false-claim cases — a label after a
 * return, a branch guarded by a variable — are cheapest to pin.
 */
int collect_dead_code(const LanguageModule *module, Registry *reg,
                      const char *data, TSNode root,
                      const FnRangeIndex *ranges, const SpanList *comments,
                      FileFacts *facts);

#endif /* ELC_ANALYZE_H */
