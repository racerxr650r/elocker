/* format_text.h — the human-facing renderers.
 *
 * format_table() is the default format, used when the user selects none
 * (HLR-027, LLR-TBL-02). Markdown (HLR-029) joins it in Phase 5.
 *
 * A renderer is a pure consumer of the report model: it recomputes nothing,
 * mutates nothing, and — the part that is easy to get wrong — sorts nothing.
 * Ordering is report.c's responsibility (doc/SDD.md §14).
 */
#ifndef ELC_FORMAT_TEXT_H
#define ELC_FORMAT_TEXT_H

#include <stdio.h>

#include "report.h"

/* Render the report as an aligned table on `out`, computing column widths
 * from the longest path (LLR-TBL-01). Writes results only; every diagnostic
 * goes to stderr (HLR-038, LLR-TBL-03).
 *
 * Returns 0 on success, non-zero if the stream reported a write failure — a
 * truncated report is never reported as success.
 */
int format_table(const Report *report, FILE *out);

#endif /* ELC_FORMAT_TEXT_H */
