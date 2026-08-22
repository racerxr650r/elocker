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

/* How much of the report is presented (HLR-150, HLR-151).
 *
 * The other axis, and deliberately not the same one as Style. Style decides
 * how a tier is decorated; Verbosity decides whether a tier is reached at
 * all. Both are parameters of the *one* traversal rather than selectors
 * between traversals — a second walk for the summary is the thing that would
 * let a tier be present at one verbosity and forgotten at the other, exactly
 * as a second renderer would let one be present in one format and forgotten
 * in the other (LLR-SUM-02, LLR-SUM-09).
 *
 * The summary is zero, so that a zeroed configuration means the default.
 */
typedef enum {
	VERBOSITY_SUMMARY = 0, /* the summary tiers alone (HLR-150)      */
	VERBOSITY_VERBOSE      /* every tier of HLR-031 (HLR-151)        */
} Verbosity;

/* Walk the report model once, emitting the tiers the verbosity selects in the
 * fixed order, in the requested style (LLR-SUM-01, LLR-SUM-02, LLR-SUM-09).
 *
 * Returns 0 on success, non-zero if the stream reported a write failure — a
 * truncated report is never reported as success.
 */
int render_report(const Report *report, Style style, Verbosity verbosity,
                  FILE *out);

/* Render the aligned table (LLR-TBL-01). */
int format_table(const Report *report, Verbosity verbosity, FILE *out);

/* Render GitHub-Flavored Markdown, with functions grouped under the file
 * that defines them (LLR-MKD-01). */
int format_markdown(const Report *report, Verbosity verbosity, FILE *out);

#endif /* ELC_FORMAT_TEXT_H */
