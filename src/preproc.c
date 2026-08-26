/* preproc.c — macro expansion through the language's own preprocessor.
 *
 * See include/preproc.h for why this exists and why every failure in it is
 * survivable.
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "preproc.h"

/* ------------------------------------------------------------- statuses -- */

const char *preproc_status_text(PreprocStatus s)
{
	switch (s) {
	case PREPROC_EXPANDED:    return "expanded";
	case PREPROC_OFF:         return "expansion not attempted";
	case PREPROC_NO_COMPILER: return "no preprocessor available";
	case PREPROC_FAILED:      return "the preprocessor rejected the file";
	case PREPROC_NOT_NAMED:   return "the expansion named no line of it";
	case PREPROC_STATUS_COUNT:
	default:                  return "";
	}
}

const char *stdlib_kind_name(StdlibKind k)
{
	switch (k) {
	case STDLIB_C:   return "C";
	case STDLIB_CXX: return "C++";
	case STDLIB_KIND_COUNT:
	default:         return "";
	}
}

/* --------------------------------------------------------------- buffer -- */

typedef struct {
	char   *data;
	size_t  length;
	size_t  capacity;
	size_t  lines;     /* newlines written, so the next line is lines + 1 */
	bool    failed;
} Buffer;

static bool buffer_reserve(Buffer *b, size_t extra)
{
	size_t want = b->length + extra + 1;
	char  *grown;

	if (b->failed)
		return false;
	if (want <= b->capacity)
		return true;

	while (b->capacity < want)
		b->capacity = b->capacity ? b->capacity * 2 : 8192;

	grown = realloc(b->data, b->capacity);
	if (!grown) {
		b->failed = true;
		return false;
	}
	b->data = grown;
	return true;
}

static void buffer_append(Buffer *b, const char *text, size_t len)
{
	if (!buffer_reserve(b, len))
		return;
	memcpy(b->data + b->length, text, len);
	b->length += len;
	b->data[b->length] = '\0';
	for (size_t i = 0; i < len; i++)
		b->lines += (text[i] == '\n');
}

/* Blank lines up to `line`, so the next append lands at that line number.
 *
 * The whole of HLR-204 is here. Every figure elc reports is line-based, so a
 * filter that merely concatenated what it kept would displace every function
 * range and every finding by however much it discarded above them — and a
 * reader cannot detect that to discount it.
 *
 * Never backwards: a marker announcing a line already passed occurs where a
 * macro expansion spans lines and the preprocessor resynchronises, and acting
 * on it would let the filter overwrite a line already written.
 */
/* Give back blank lines already written, down to `line` − 1.
 *
 * The preprocessor announces a file, emits a blank filler line, and announces
 * the same file again before the content — so the filter has written a line
 * before it learns where the content actually starts. Without this every
 * retained line sits one lower than it should, and HLR-204 is quietly broken
 * on every file.
 *
 * Only blank lines are given back. A line holding anything is never withdrawn,
 * which is what keeps the "never rewind" rule of LLR-PRE-04 intact: the buffer
 * still cannot lose a measured line, it can only decline padding it turns out
 * not to need.
 */
static void buffer_rewind_to(Buffer *b, size_t line)
{
	size_t want = line ? line - 1 : 0;

	while (b->lines > want && b->length > 0 &&
	       b->data[b->length - 1] == '\n' &&
	       (b->length == 1 || b->data[b->length - 2] == '\n')) {
		b->length--;
		b->lines--;
	}
	if (b->data)
		b->data[b->length] = '\0';
}

static void buffer_pad_to(Buffer *b, size_t line)
{
	if (line == 0 || line - 1 <= b->lines)
		return;

	size_t want = (line - 1) - b->lines;

	if (!buffer_reserve(b, want))
		return;
	memset(b->data + b->length, '\n', want);
	b->length += want;
	b->lines  += want;
	b->data[b->length] = '\0';
}

/* -------------------------------------------------------------- headers -- */

/* The C and C++ standard library header sets.
 *
 * Listed rather than derived from the path, because the path cannot tell them
 * apart: a C++ implementation's <cstdio> and C's <stdio.h> sit side by side
 * under the same system directories (LLR-PRE-06).
 */
static const char *const C_HEADERS[] = {
	"assert.h", "complex.h", "ctype.h", "errno.h", "fenv.h", "float.h",
	"inttypes.h", "iso646.h", "limits.h", "locale.h", "math.h",
	"setjmp.h", "signal.h", "stdalign.h", "stdarg.h", "stdatomic.h",
	"stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h",
	"stdnoreturn.h", "string.h", "tgmath.h", "threads.h", "time.h",
	"uchar.h", "wchar.h", "wctype.h",
};

static const char *const CXX_HEADERS[] = {
	"algorithm", "any", "array", "atomic", "bitset", "charconv", "chrono",
	"codecvt", "complex", "condition_variable", "deque", "exception",
	"execution", "filesystem", "format", "forward_list", "fstream",
	"functional", "future", "initializer_list", "iomanip", "ios",
	"iosfwd", "iostream", "istream", "iterator", "limits", "list",
	"locale", "map", "memory", "memory_resource", "mutex", "new",
	"numeric", "optional", "ostream", "queue", "random", "ratio",
	"regex", "scoped_allocator", "set", "shared_mutex", "span",
	"sstream", "stack", "stdexcept", "streambuf", "string",
	"string_view", "system_error", "thread", "tuple", "type_traits",
	"typeindex", "typeinfo", "unordered_map", "unordered_set", "utility",
	"valarray", "variant", "vector",
};

/* The last path component, which is what a header set is keyed on. */
static const char *basename_of(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? slash + 1 : path;
}

static bool in_set(const char *const *set, size_t n, const char *name)
{
	for (size_t i = 0; i < n; i++)
		if (strcmp(set[i], name) == 0)
			return true;
	return false;
}

/* Whether this path is a standard-library header, and which library's.
 *
 * A C++ header carries no extension, which is what makes the <cstdio> family
 * recognisable: `c` followed by a C header's stem.
 */
static bool stdlib_classify(const char *path, StdlibKind *kind)
{
	const char *base = basename_of(path);

	if (in_set(CXX_HEADERS, sizeof CXX_HEADERS / sizeof *CXX_HEADERS,
	           base)) {
		*kind = STDLIB_CXX;
		return true;
	}

	if (base[0] == 'c' && !strchr(base, '.')) {
		char stem[64];
		int  n = snprintf(stem, sizeof stem, "%s.h", base + 1);

		if (n > 0 && (size_t)n < sizeof stem &&
		    in_set(C_HEADERS, sizeof C_HEADERS / sizeof *C_HEADERS,
		           stem)) {
			*kind = STDLIB_CXX;
			return true;
		}
	}

	if (in_set(C_HEADERS, sizeof C_HEADERS / sizeof *C_HEADERS, base)) {
		*kind = STDLIB_C;
		return true;
	}

	return false;
}

/* Record a header once. Linear because a translation unit reaches a handful
 * of standard headers and the list is rendered in the order it is built. */
static bool headers_add(PreprocResult *out, const char *path, StdlibKind kind)
{
	const char   *base = basename_of(path);
	StdlibHeader *grown;

	for (size_t i = 0; i < out->header_count; i++)
		if (strcmp(out->headers[i].name, base) == 0)
			return true;

	grown = realloc(out->headers,
	                (out->header_count + 1) * sizeof *out->headers);
	if (!grown)
		return false;
	out->headers = grown;

	out->headers[out->header_count].name = strdup(base);
	if (!out->headers[out->header_count].name)
		return false;
	out->headers[out->header_count].kind = kind;
	out->header_count++;
	out->cxx_count += (kind == STDLIB_CXX);
	return true;
}

/* --------------------------------------------------------------- markers -- */

/* Whether this line is a preprocessor line marker, and what it names.
 *
 * The grammar is `# linenum "filename" flags`. Recognised on structure alone —
 * `#`, a space, a digit — because after expansion the only directives left are
 * the markers the preprocessor emitted itself, and a heuristic over the text
 * would be a guess about source elc did not write (LLR-PRE-03).
 *
 * `name` receives the unescaped file name, which matters for a path holding a
 * quote or a backslash: it reaches the marker escaped and would otherwise
 * never compare equal to the path elc holds.
 */
static bool marker_parse(const char *line, size_t len, size_t *number,
                         char *name, size_t name_cap)
{
	size_t i = 0;
	size_t n = 0;
	size_t out = 0;

	if (len < 4 || line[0] != '#' || line[1] != ' ')
		return false;

	i = 2;
	if (i >= len || !isdigit((unsigned char)line[i]))
		return false;
	while (i < len && isdigit((unsigned char)line[i]))
		n = n * 10 + (size_t)(line[i++] - '0');

	while (i < len && line[i] == ' ')
		i++;
	if (i >= len || line[i] != '"')
		return false;
	i++;

	while (i < len && line[i] != '"') {
		char c = line[i];

		if (c == '\\' && i + 1 < len)
			c = line[++i];
		if (out + 1 >= name_cap)
			return false;
		name[out++] = c;
		i++;
	}
	if (i >= len || line[i] != '"')
		return false;

	name[out] = '\0';
	*number   = n;
	return true;
}

/* ---------------------------------------------------------------- filter -- */

int preproc_filter(const char *expanded, size_t length, const char *path,
                   PreprocResult *out)
{
	Buffer     buf     = { 0 };
	bool       append  = false;
	bool       named   = false;
	char       name[4096];
	const char *p      = expanded;
	const char *end    = expanded + length;

	while (p < end) {
		const char *nl  = memchr(p, '\n', (size_t)(end - p));
		size_t      len = nl ? (size_t)(nl - p) : (size_t)(end - p);
		size_t      number;

		if (marker_parse(p, len, &number, name, sizeof name)) {
			/* The marker stream is the only authority on what is
			 * copied. Nothing about a line's content changes the
			 * state it arrives in (HLR-203). */
			append = strcmp(name, path) == 0;
			if (append) {
				named = true;
				buffer_rewind_to(&buf, number);
				buffer_pad_to(&buf, number);
			} else {
				StdlibKind kind;

				if (stdlib_classify(name, &kind) &&
				    !headers_add(out, name, kind)) {
					free(buf.data);
					return -1;
				}
			}
		} else if (append) {
			buffer_append(&buf, p, len);
			buffer_append(&buf, "\n", 1);
		}

		if (!nl)
			break;
		p = nl + 1;
	}

	if (buf.failed) {
		free(buf.data);
		return -1;
	}

	/* Output naming the file nowhere is a failure, not an empty file. A
	 * zero-line measurement of a file that has lines is the silent wrong
	 * answer this module exists not to produce (LLR-PRE-05). */
	if (!named) {
		free(buf.data);
		out->status = PREPROC_NOT_NAMED;
		return 0;
	}

	out->text   = buf.data;
	out->length = buf.length;
	out->status = PREPROC_EXPANDED;
	return 0;
}

/* ------------------------------------------------------------ subprocess -- */

/* The driver for a language, or NULL where elc does not expand it. */
static const char *driver_for(const char *language, const char *cc)
{
	if (cc && *cc)
		return cc;
	if (language && strcmp(language, "c") == 0)
		return "gcc";
	if (language && (strcmp(language, "cpp") == 0 ||
	                 strcmp(language, "c++") == 0))
		return "g++";
	return NULL;
}

/* Quote one argument for the shell `popen` runs.
 *
 * Single quotes, with an embedded quote written as '\''. A path elc was given
 * may hold a space, a quote, or a `$`, and an unquoted one would be split,
 * globbed, or expanded — which would run the preprocessor over a file the user
 * did not name.
 */
static bool shell_quote(const char *s, char *out, size_t cap)
{
	size_t o = 0;

	if (o + 1 >= cap)
		return false;
	out[o++] = '\'';
	for (const char *p = s; *p; p++) {
		if (*p == '\'') {
			if (o + 4 >= cap)
				return false;
			memcpy(out + o, "'\\''", 4);
			o += 4;
		} else {
			if (o + 1 >= cap)
				return false;
			out[o++] = *p;
		}
	}
	if (o + 2 > cap)
		return false;
	out[o++] = '\'';
	out[o]   = '\0';
	return true;
}

int preproc_expand(const char *path, const char *language, const char *cc,
                   const char *const *flags, size_t flag_count,
                   PreprocResult *out)
{
	const char *driver = driver_for(language, cc);
	char        quoted[8192];
	char        command[9216];
	Buffer      raw = { 0 };
	FILE       *pipe;
	char        chunk[65536];
	size_t      got;
	int         status;
	int         rc;

	if (!out)
		return -1;
	memset(out, 0, sizeof *out);
	out->status = PREPROC_OFF;

	if (!driver || !path)
		return 0;
	if (!shell_quote(path, quoted, sizeof quoted))
		return 0;

	/* `-C` keeps comments. No figure depends on them today — they are
	 * excluded from effective lines rather than counted — and they are
	 * kept so the parsed buffer differs from the source only where the
	 * expansion required it (HLR-204).
	 * stderr is discarded because the preprocessor's complaints are about
	 * a build configuration elc does not have and cannot fix, and on a
	 * cross-compiled tree there would be pages of them per file. */
	{
		size_t off = 0;
		int    n   = snprintf(command, sizeof command, "%s -E -C",
		                      driver);

		if (n < 0 || (size_t)n >= sizeof command)
			return 0;
		off = (size_t)n;

		/* Forwarded verbatim, each quoted so a flag holding a space or
		 * a `$` reaches the preprocessor as one argument and not as
		 * shell syntax (LLR-PRE-02). */
		for (size_t i = 0; i < flag_count; i++) {
			char q[4096];

			if (!shell_quote(flags[i], q, sizeof q))
				return 0;
			n = snprintf(command + off, sizeof command - off,
			             " %s", q);
			if (n < 0 || (size_t)n >= sizeof command - off)
				return 0;
			off += (size_t)n;
		}

		n = snprintf(command + off, sizeof command - off,
		             " %s 2>/dev/null", quoted);
		if (n < 0 || (size_t)n >= sizeof command - off)
			return 0;
	}

	pipe = popen(command, "r");
	if (!pipe) {
		out->status = PREPROC_NO_COMPILER;
		return 0;
	}

	/* Drained to end-of-file *before* the status is collected. The other
	 * order deadlocks on any file whose expansion exceeds the pipe
	 * capacity — which is every C++ file that includes anything — with the
	 * child blocked writing and the parent blocked waiting (LLR-PRE-01). */
	while ((got = fread(chunk, 1, sizeof chunk, pipe)) > 0)
		buffer_append(&raw, chunk, got);

	status = pclose(pipe);

	if (raw.failed) {
		free(raw.data);
		return -1;
	}
	if (status != 0 || !raw.data) {
		free(raw.data);
		/* 127 is the shell's "command not found", which is a different
		 * condition from a preprocessor that ran and objected — the
		 * first is answered by installing a toolchain and the second
		 * is not, and HLR-206 exists to keep them apart. */
		out->status = (WIFEXITED(status) && WEXITSTATUS(status) == 127)
		                      ? PREPROC_NO_COMPILER
		                      : status != 0 ? PREPROC_FAILED
		                                    : PREPROC_NO_COMPILER;
		return 0;
	}

	rc = preproc_filter(raw.data, raw.length, path, out);
	free(raw.data);
	return rc;
}

void preproc_result_free(PreprocResult *r)
{
	if (!r)
		return;
	for (size_t i = 0; i < r->header_count; i++)
		free(r->headers[i].name);
	free(r->headers);
	free(r->text);
	r->text         = NULL;
	r->headers      = NULL;
	r->header_count = 0;
	r->cxx_count    = 0;
	r->length       = 0;
}
