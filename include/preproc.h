/* preproc.h — macro expansion through the language's own preprocessor.
 *
 * A macro standing where the grammar expects a keyword, a type or a string is
 * a parse error, and no grammar can fix it: `MACRO MACRO` is genuinely
 * ambiguous with `Type var`. The compiler's preprocessor resolves it exactly,
 * which is the only way to be right about a macro rather than lucky
 * (HLR-202).
 *
 * **Raw preprocessor output is not measurable.** Nineteen lines of C came back
 * as 829, with the functions of every header included appearing as functions
 * of the file under analysis and every line number moved. What makes the
 * output usable is the `# linenum "filename"` markers: they say which file
 * each run of lines came from, so everything the project did not write can be
 * discarded and the line numbering of what it did write restored (HLR-203,
 * HLR-204).
 *
 * **Failure is ordinary here, not exceptional.** A cross-compiled tree cannot
 * be preprocessed by a host toolchain at all, and a source tree analysed away
 * from its build environment is the normal condition of a metrics tool. Every
 * failure yields a null buffer and a status, and the caller parses the file as
 * written — the measurement `elc` produced before expansion existed (HLR-205).
 * Nothing here writes a file, and nothing here fails a run.
 */
#ifndef ELC_PREPROC_H
#define ELC_PREPROC_H

#include <stdbool.h>
#include <stddef.h>

/* Why a file was, or was not, expanded.
 *
 * Reported per file (HLR-206) because two files in one report may have been
 * measured two different ways and the figures do not say which: an expanded
 * file's macros are resolved, a fallen-back file's are not, and its
 * unparsed-line count may be non-zero for a reason that has nothing to do with
 * the code.
 */
typedef enum {
	PREPROC_EXPANDED = 0,   /* the buffer is the expansion               */
	PREPROC_OFF,            /* expansion was not attempted               */
	PREPROC_NO_COMPILER,    /* the child could not be started            */
	PREPROC_FAILED,         /* the preprocessor exited non-zero          */
	PREPROC_NOT_NAMED,      /* output held no marker for this file       */
	PREPROC_STATUS_COUNT
} PreprocStatus;

/* Which standard library a header belongs to. The path cannot decide it —
 * `<cstdio>` and `<stdio.h>` sit in the same directories — so it is decided by
 * name against the two standards' header sets (LLR-PRE-06).
 */
typedef enum {
	STDLIB_C = 0,
	STDLIB_CXX,
	STDLIB_KIND_COUNT
} StdlibKind;

typedef struct {
	char       *name;   /* the header as the marker named it; owned */
	StdlibKind  kind;
} StdlibHeader;

typedef struct {
	char          *text;         /* filtered buffer, or NULL to fall back */
	size_t         length;
	PreprocStatus  status;
	StdlibHeader  *headers;      /* sorted, unique; owned                 */
	size_t         header_count;
	size_t         cxx_count;    /* how many of those are C++             */
} PreprocResult;

const char *preproc_status_text(PreprocStatus s);
const char *stdlib_kind_name(StdlibKind k);

/* Expand one file and filter the result back down to it.
 *
 * `path` is the canonical path the marker stream is compared against, and
 * `language` selects the driver ("c", "cpp"). `cc` names the compiler, or is
 * NULL for the language's default.
 *
 * Returns 0 whether or not the expansion happened — `out->text` is NULL and
 * `out->status` says why when it did not. Non-zero is allocation failure
 * alone, and is also survivable: the caller falls back for that too.
 */
int preproc_expand(const char *path, const char *language, const char *cc,
                   const char *const *flags, size_t flag_count,
                   PreprocResult *out);

/* Filter an already-captured expansion. Separated from the subprocess so the
 * state machine — which is where every interesting failure lives — is testable
 * against a string rather than against an installed compiler.
 */
int preproc_filter(const char *expanded, size_t length, const char *path,
                   PreprocResult *out);

void preproc_result_free(PreprocResult *r);

#endif /* ELC_PREPROC_H */
