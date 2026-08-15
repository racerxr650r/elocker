/* format_xml.h — the saved record.
 *
 * The only module that both produces and consumes a serialised form: it
 * writes the complete record of a run (HLR-054) and reads one back to drive
 * the regeneration mode (HLR-055), without touching a source file
 * (doc/SDD.md §16).
 *
 * The asymmetry between the two halves is deliberate. Writing is hand-rolled
 * text emission, because emission needs only correct escaping and a writer
 * library would be a dependency for no benefit. Reading is streamed through
 * a hardened parser, because that input is a file the user supplies and may
 * not be one `elc` wrote.
 */
#ifndef ELC_FORMAT_XML_H
#define ELC_FORMAT_XML_H

#include <stdio.h>

#include "elc.h"
#include "report.h"

/* Serialise the complete report model, unfiltered by the threshold, with a
 * format-version identifier in the root (LLR-XWR-01 – LLR-XWR-04).
 *
 * Returns 0 on success, non-zero if the stream reported a write failure.
 */
int xml_write_report(const Report *report, FILE *out);

/* Emit text with every character carrying structural meaning in XML escaped
 * (LLR-ESC-01).
 *
 * Exposed because the requirement is about *every* value passing through it,
 * which is worth testing directly against the characters that break a
 * document: an ampersand, an angle bracket, a quotation mark.
 */
void write_escaped(const char *value, FILE *out);

/* Reconstruct a report model from a saved record.
 *
 * The threshold in `opts` is the one applied, whatever was in force when the
 * record was written (HLR-057). The model is assembled by the same code that
 * assembles a live run, which is what makes the regenerated report
 * byte-identical rather than merely similar (HLR-056).
 *
 * Returns 0 on success. Returns non-zero, after a diagnostic, when the input
 * is not well-formed, does not match `elc`'s own structure, or carries a
 * format version this build does not support — with no best-effort partial
 * conversion attempted, since a partly reconstructed report is
 * indistinguishable from a complete one once rendered (HLR-058).
 */
int xml_read_report(const char *path, const ElcOptions *opts, Report *out);

#endif /* ELC_FORMAT_XML_H */
