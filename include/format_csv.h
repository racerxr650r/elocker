/* format_csv.h — the RFC 4180 renderer.
 *
 * The flat, unfiltered, machine-facing view: one record per function, with
 * no threshold applied and no architectural findings, which are not
 * expressible as a single flat record set (HLR-028, doc/SDD.md §15).
 */
#ifndef ELC_FORMAT_CSV_H
#define ELC_FORMAT_CSV_H

#include <stdio.h>

#include "report.h"

/* Write the header row and one record per function (LLR-CSV-01).
 *
 * Returns 0 on success, non-zero if the stream reported a write failure.
 */
int format_csv(const Report *report, FILE *out);

/* Emit one field, quoting it and doubling any embedded quote when it
 * contains a comma, a quote, or a line break (RFC 4180, LLR-FLD-01).
 *
 * Exposed because the requirement it implements is about *every* field
 * passing through it, and that is worth testing directly against the values
 * that break a record: a template signature carrying a comma, a name
 * carrying a quote.
 */
void write_field(const char *value, FILE *out);

#endif /* ELC_FORMAT_CSV_H */
