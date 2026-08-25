/* diag.h — the diagnostic stream, and the debug companion that records it.
 *
 * Every message `elc` writes to standard error goes through this module, so
 * that `--dbg` can keep a copy of the run beside the report (HLR-194). A
 * message written straight to `stderr` reaches the terminal and nothing else,
 * and the debug companion exists for the runs nobody can reproduce — a tree on
 * someone else's machine, a grammar failing on source that cannot be shared.
 *
 * **This module holds the one piece of global mutable state in `elc`, and the
 * exception is deliberate and bounded.** The convention everywhere else is
 * that everything a function needs arrives through its arguments; seventy-nine
 * functions across thirteen modules diagnose, and threading a sink to each
 * would put a parameter on every caller between `main` and a parse error. Two
 * properties make the exception safe rather than merely convenient:
 *
 *   * **It is write-only.** Nothing reads the log back. No measurement, no
 *     finding, and no exit status depends on it, so it cannot change what a
 *     run reports — which is what the convention protects.
 *   * **`elc` is single-threaded by requirement** (HLR-041), so the races a
 *     mutable global usually invites do not arise.
 *
 * The log is written **as messages occur** rather than buffered and flushed at
 * exit. That is the property the feature exists for: a run that segfaults or
 * is killed on a tree nobody can reproduce still leaves everything up to the
 * fault on disk, where a buffer flushed at exit would lose precisely the run
 * worth debugging.
 */
#ifndef ELC_DIAG_H
#define ELC_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Open the debug companion, or leave the module inert.
 *
 * `path` is the companion name derived from the report's output path by the
 * rule of HLR-119, or NULL where the run asked for no debug file — in which
 * case every call below writes to standard error alone, exactly as a direct
 * `fprintf` did.
 *
 * A companion that cannot be opened is a diagnostic and not a failure: the
 * user asked for a report and a debug file, and losing the second is no reason
 * to withhold the first (LLR-DOT-05 draws the same line for the `.dot`).
 *
 * Returns 0, or -1 having written the diagnostic.
 */
int diag_open(const char *path, int argc, char **argv);

/* Write one diagnostic to standard error, and to the debug companion where one
 * is open. The format is `printf`'s, and callers pass the message they would
 * have passed to `fprintf(stderr, ...)`. */
void diag_printf(const char *fmt, ...);

/* Record something the debug companion should carry that standard error should
 * not: the detail HLR-194 allows a message to be accompanied by, and the parse
 * failures of HLR-195, which are far too voluminous for a terminal.
 *
 * A no-op where no companion is open, so a caller need not ask first.
 */
void diag_detail(const char *fmt, ...);

/* Record one region of source the grammar could not parse, with the offending
 * lines themselves (HLR-195).
 *
 * `data` is the file's contents as mapped and `length` its size, since the
 * mapping is not NUL-terminated. The lines are one-based and inclusive, as a
 * reader sees them in an editor.
 */
void diag_parse_failure(const char *file, const char *data, size_t length,
                        uint32_t first_line, uint32_t last_line);

/* Whether a debug companion is open, for a caller deciding whether to do work
 * whose only purpose is to fill it. */
bool diag_active(void);

/* Close the companion. Safe with none open, and safe twice. */
void diag_close(void);

#endif /* ELC_DIAG_H */
