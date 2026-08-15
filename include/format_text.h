/* format_text.h — the human-facing renderers.
 *
 * format_table() is the default, used when the user selects none (HLR-027,
 * LLR-TBL-02); format_markdown() is what a saved record regenerates into
 * (HLR-029, HLR-055).
 *
 * Both are the same traversal under two decorations. A renderer is a pure
 * consumer of the report model: it recomputes nothing, mutates nothing, and
 * — the part that is easy to get wrong — sorts nothing. Ordering is
 * report.c's responsibility (doc/SDD.md §14).
 */
#ifndef ELC_FORMAT_TEXT_H
#define ELC_FORMAT_TEXT_H

#include <stdio.h>

#include "report.h"

/* How a tier is decorated. Not *which* tiers are emitted — that is fixed by
 * the traversal, which is what makes HLR-031's uniform composition a
 * structural property rather than a maintained one. */
typedef enum {
	STYLE_TABLE = 0,
	STYLE_MARKDOWN
} Style;

/* Walk the report model once, emitting every tier in the fixed order, in the
 * requested style (LLR-SUM-01, LLR-SUM-02).
 *
 * Returns 0 on success, non-zero if the stream reported a write failure — a
 * truncated report is never reported as success.
 */
int render_report(const Report *report, Style style, FILE *out);

/* Render the aligned table (LLR-TBL-01). */
int format_table(const Report *report, FILE *out);

/* Render GitHub-Flavored Markdown, with functions grouped under the file
 * that defines them (LLR-MKD-01). */
int format_markdown(const Report *report, FILE *out);

#endif /* ELC_FORMAT_TEXT_H */
