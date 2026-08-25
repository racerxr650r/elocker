/* diag.c — the diagnostic stream, and the debug companion that records it.
 *
 * See doc/SDD.md §25 and the header for why this module holds the one piece of
 * global mutable state in `elc`, and what bounds the exception.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "diag.h"

/* A region the grammar could not follow can span a whole file. The log records
 * the head of it and says how much it left out, rather than copying the file. */
#define ELC_DIAG_MAX_LINES 20u

/* The companion, or NULL where the run asked for none.
 *
 * `static` and file-scoped: nothing outside this file can reach it, so the
 * exception to the no-global-state convention is confined to the module that
 * argues for it. */
static FILE *debug_file;

/* Timestamps are useful in a log nobody watched being produced, and are the
 * one thing here that must not vary between two runs of the *report*. They go
 * to the companion alone, never to standard error, so HLR-032's byte-identical
 * guarantee is untouched — the companion is a record of a run rather than a
 * result of it. */
static void stamp(void)
{
	time_t     now = time(NULL);
	struct tm  utc;
	char       buf[32];

	if (now == (time_t)-1 || !gmtime_r(&now, &utc))
		return;
	if (strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
		return;

	fprintf(debug_file, "%s  ", buf);
}

int diag_open(const char *path, int argc, char **argv)
{
	if (!path)
		return 0;

	debug_file = fopen(path, "w");
	if (!debug_file) {
		/* Straight to standard error: the companion that would have
		 * recorded this message is the thing that failed. */
		fprintf(stderr, "elc: %s: cannot open the debug file\n", path);
		return -1;
	}

	/* The command line first, because the first question asked of a log
	 * from a machine nobody has is what was actually run (HLR-194). */
	fputs("elc debug log\n", debug_file);
	stamp();
	fputs("invocation:", debug_file);
	for (int i = 0; i < argc; i++)
		fprintf(debug_file, " %s", argv[i] ? argv[i] : "");
	fputs("\n\n", debug_file);
	fflush(debug_file);
	return 0;
}

void diag_printf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	if (!debug_file)
		return;

	stamp();
	va_start(args, fmt);
	vfprintf(debug_file, fmt, args);
	va_end(args);

	/* Flushed at every message rather than at exit. A run that faults on a
	 * tree nobody can reproduce still leaves everything up to the fault on
	 * disk, which is the whole reason the companion exists. */
	fflush(debug_file);
}

void diag_detail(const char *fmt, ...)
{
	va_list args;

	if (!debug_file)
		return;

	fputs("    ", debug_file);
	va_start(args, fmt);
	vfprintf(debug_file, fmt, args);
	va_end(args);
	fflush(debug_file);
}

/* The bounds of one line of a mapping, which is not NUL-terminated. */
static void line_extent(const char *data, size_t length, uint32_t wanted,
                        size_t *from, size_t *to)
{
	uint32_t line = 1;
	size_t   at   = 0;

	while (at < length && line < wanted) {
		if (data[at] == '\n')
			line++;
		at++;
	}

	*from = at;
	while (at < length && data[at] != '\n')
		at++;
	*to = at;
}

void diag_parse_failure(const char *file, const char *data, size_t length,
                        uint32_t first_line, uint32_t last_line)
{
	if (!debug_file || !file || !data)
		return;

	stamp();
	fprintf(debug_file, "parse failure  %s:%" PRIu32, file, first_line);
	if (last_line > first_line)
		fprintf(debug_file, "-%" PRIu32, last_line);
	fputc('\n', debug_file);

	/* The source itself, which is the point: a grammar that fails on a
	 * construct nobody can share is debugged from the construct. Bounded,
	 * because a file the grammar could not follow at all would otherwise
	 * be copied whole into the log. */
	for (uint32_t line = first_line;
	     line <= last_line && line < first_line + ELC_DIAG_MAX_LINES;
	     line++) {
		size_t from, to;

		line_extent(data, length, line, &from, &to);
		if (from >= length)
			break;
		fprintf(debug_file, "    %6" PRIu32 " | %.*s\n", line,
		        (int)(to - from), data + from);
	}
	if (last_line >= first_line + ELC_DIAG_MAX_LINES)
		fprintf(debug_file, "    ... %" PRIu32 " further lines\n",
		        last_line - first_line - ELC_DIAG_MAX_LINES + 1);

	fflush(debug_file);
}

bool diag_active(void)
{
	return debug_file != NULL;
}

void diag_close(void)
{
	if (!debug_file)
		return;

	fclose(debug_file);
	debug_file = NULL;
}
