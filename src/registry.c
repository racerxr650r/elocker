/* registry.c — runtime-loaded language support.
 *
 * The boundary that keeps language knowledge out of the binary. Everything
 * language-specific — which grammar, which extensions, which constructs
 * matter — arrives as data from the runtime location (doc/SDD.md §6).
 *
 * Nothing in this file names a language, an extension, or a grammar node
 * type. The only fixed strings are the six query filenames and the
 * `tree_sitter_` symbol prefix, both of which are the published contract a
 * third party codes against (HLR-121, runtime/queries/README.md).
 */

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <tree_sitter/api.h>

#include "elc.h"
#include "registry.h"

/* The query files, indexed by QueryKind. Order matches the enum; the names are
 * the contract. Everything below QUERY_REQUIRED_COUNT must be present; the
 * rest may be absent (HLR-139). */
static const char *const QUERY_FILES[QUERY_COUNT] = {
	[QUERY_FUNCTIONS]  = "functions.scm",
	[QUERY_COMMENTS]   = "comments.scm",
	[QUERY_COMPLEXITY] = "complexity.scm",
	[QUERY_ELOC]       = "eloc.scm",
	[QUERY_CALLS]      = "calls.scm",
	[QUERY_GLOBALS]    = "globals.scm",
	[QUERY_DEADCODE]   = "deadcode.scm"
};

/* What ts_query_new() reported, in words. The numeric code alone tells the
 * author of a query file nothing, and this is the diagnostic they act on. */
static const char *query_error_text(TSQueryError error)
{
	switch (error) {
	case TSQueryErrorSyntax:    return "malformed query syntax";
	case TSQueryErrorNodeType:  return "no such node type in this grammar";
	case TSQueryErrorField:     return "no such field in this grammar";
	case TSQueryErrorCapture:   return "invalid capture";
	case TSQueryErrorStructure: return "pattern cannot match this grammar";
	case TSQueryErrorLanguage:  return "query is for a different language";
	default:                    return "invalid query";
	}
}

/* ---------------------------------------------------------------- growth --
 *
 * The realloc result goes into a temporary that is checked before the
 * original is overwritten. `x = realloc(x, n)` loses the allocation on
 * failure and leaves a dangling pointer, which is an HLR-125 violation.
 */
static int grow(void **items, size_t *capacity, size_t item_size)
{
	size_t next   = *capacity ? *capacity * 2 : 8;
	void  *bigger = realloc(*items, next * item_size);

	if (!bigger)
		return -1;

	*items    = bigger;
	*capacity = next;
	return 0;
}

/* ------------------------------------------------- the runtime location -- */

/* The paths tried relative to the executable, in order, when the environment
 * variable is unset.
 *
 * **Two, not one, and the second is the installed layout.** `make install`
 * puts the binary in `<prefix>/bin` and the runtime in
 * `<prefix>/share/elc/runtime`, because a tree of grammars and query files does
 * not belong in a directory of executables. A resolver that only looked beside
 * the binary would therefore fail on every installed copy while working
 * perfectly in the build tree, where the build creates a `runtime` symlink next
 * to `elc` — which is exactly how this went unnoticed.
 *
 * HLR-059 says "a path relative to the executable" and not "adjacent to" it,
 * so both are the requirement rather than an extension of it.
 *
 * The adjacent path is tried first: it is what a self-contained unpacked
 * distribution uses, and someone who has deliberately placed a runtime beside
 * the binary means it.
 */
static const char *const RUNTIME_RELATIVE[] = {
	"runtime",              /* unpacked beside the binary, and the build tree */
	"../share/elc/runtime"  /* the installed layout                           */
};

/* The directory holding this executable, or non-zero if it cannot be found. */
static int executable_dir(char *buf, size_t len)
{
	char    exe[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	char   *slash;

	if (n < 0)
		return -1;
	exe[n] = '\0';

	/* Derived from the executable rather than from argv[0], which a caller
	 * controls and can set to anything. */
	slash = strrchr(exe, '/');
	if (!slash)
		return -1;
	*slash = '\0';

	int m = snprintf(buf, len, "%s", exe);

	return (m < 0 || (size_t)m >= len) ? -1 : 0;
}

/* Resolve the runtime location, writing it to `buf`.
 *
 * Returns 0 with a location that exists, or non-zero. On failure `tried`
 * receives the candidates that were examined, so the diagnostic can name them:
 * a message quoting only the last one sends the reader to look in a directory
 * `elc` never expected the runtime to be in.
 */
static int runtime_dir_resolve(char *buf, size_t len, char *tried,
                               size_t tried_len)
{
	const char *env = getenv(ELC_RUNTIME_DIR_ENV);
	char        dir[PATH_MAX];
	size_t      at = 0;

	if (tried_len)
		tried[0] = '\0';

	/* The environment variable wins when both are present, and is used as
	 * given without being tested for existence: naming a location that is
	 * not there is a mistake worth reporting against that exact path
	 * rather than silently falling back to a directory the user did not
	 * ask for (HLR-059, LLR-ROP-02). */
	if (env && *env) {
		int n = snprintf(buf, len, "%s", env);

		return (n < 0 || (size_t)n >= len) ? -1 : 0;
	}

	if (executable_dir(dir, sizeof dir) != 0)
		return -1;

	for (size_t i = 0; i < sizeof RUNTIME_RELATIVE / sizeof *RUNTIME_RELATIVE;
	     i++) {
		struct stat st;
		int         n = snprintf(buf, len, "%s/%s", dir,
		                         RUNTIME_RELATIVE[i]);

		if (n < 0 || (size_t)n >= len)
			continue;

		if (at < tried_len) {
			int w = snprintf(tried + at, tried_len - at, "%s%s",
			                 at ? ", " : "", buf);

			if (w > 0 && (size_t)w < tried_len - at)
				at += (size_t)w;
		}

		if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode))
			return 0;
	}

	return -1;
}

const char *registry_runtime_dir(const Registry *reg)
{
	return reg ? reg->dir : NULL;
}

/* ---------------------------------------------------- the extension map -- */

static int map_add(Registry *reg, const char *ext, const char *lang)
{
	char *extension;
	char *language;

	/* Accept an entry written with or without its leading period, so the
	 * data file can be maintained in either style. */
	if (ext[0] == '.') {
		extension = strdup(ext);
	} else {
		size_t n = strlen(ext);
		extension = malloc(n + 2);
		if (extension) {
			extension[0] = '.';
			memcpy(extension + 1, ext, n + 1);
		}
	}
	if (!extension)
		return -1;

	language = strdup(lang);
	if (!language) {
		free(extension);
		return -1;
	}

	if (reg->map_count == reg->map_capacity &&
	    grow((void **)&reg->map, &reg->map_capacity, sizeof *reg->map) != 0) {
		free(extension);
		free(language);
		return -1;
	}

	reg->map[reg->map_count].extension = extension;
	reg->map[reg->map_count].language  = language;
	reg->map_count++;
	return 0;
}

static int load_extension_map(Registry *reg)
{
	char   path[PATH_MAX];
	FILE  *fp;
	char  *line   = NULL;
	size_t cap    = 0;
	int    status = 0;

	int n = snprintf(path, sizeof path, "%s/extensions.map", reg->dir);
	if (n < 0 || (size_t)n >= sizeof path) {
		fprintf(stderr, "elc: runtime directory path is too long\n");
		return -1;
	}

	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		return -1;
	}

	ssize_t len;
	while ((len = getline(&line, &cap, fp)) != -1) {
		char *p = line;

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
			continue;

		char *ext = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
			p++;
		if (!*p || *p == '\n' || *p == '\r') {
			/* An extension with no language names nothing. Say so
			 * and carry on; one malformed line is not a reason to
			 * discard the rest of the map. */
			*p = '\0';
			fprintf(stderr, "elc: %s: no language for '%s'\n",
			        path, ext);
			continue;
		}
		*p++ = '\0';

		while (*p == ' ' || *p == '\t')
			p++;
		char *lang = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
			p++;
		*p = '\0';

		if (*ext && *lang && map_add(reg, ext, lang) != 0) {
			fputs("elc: out of memory reading the extension map\n",
			      stderr);
			status = -1;
			break;
		}
	}

	free(line);
	fclose(fp);
	return status;
}

/* The language an extension maps to, or NULL. The map is runtime data; no
 * mapping is compiled into the executable (LLR-ROP-03). */
static const char *language_for_path(const Registry *reg, const char *path)
{
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;

	const char *dot = strrchr(base, '.');
	if (!dot || dot == base || dot[1] == '\0')
		return NULL;

	for (size_t i = 0; i < reg->map_count; i++)
		if (strcasecmp(dot, reg->map[i].extension) == 0)
			return reg->map[i].language;

	return NULL;
}

/* ------------------------------------------------------ language modules -- */

static char *read_file(const char *path, uint32_t *length)
{
	FILE *fp = fopen(path, "rb");
	char *buffer;
	long  size;

	if (!fp)
		return NULL;

	if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0) {
		fclose(fp);
		return NULL;
	}
	rewind(fp);

	buffer = malloc((size_t)size + 1);
	if (!buffer) {
		fclose(fp);
		return NULL;
	}

	if (size > 0 && fread(buffer, 1, (size_t)size, fp) != (size_t)size) {
		free(buffer);
		fclose(fp);
		return NULL;
	}
	buffer[size] = '\0';
	fclose(fp);

	*length = (uint32_t)size;
	return buffer;
}

/* Release one module. Queries first, then the handle: a TSQuery holds
 * pointers into the TSLanguage the handle unmaps (LLR-RCL-01). */
static void module_release(LanguageModule *module)
{
	for (size_t i = 0; i < QUERY_COUNT; i++) {
		if (module->queries[i]) {
			ts_query_delete(module->queries[i]);
			module->queries[i] = NULL;
		}
	}
	if (module->dl_handle) {
		dlclose(module->dl_handle);
		module->dl_handle = NULL;
	}
	free(module->language_name);
	module->language_name = NULL;
	module->ts_lang       = NULL;
	module->usable        = false;
}

/* Load the grammar and compile the query files: the six required ones, and
 * whichever optional ones the module supplies.
 *
 * Returns 0 with `module` populated and usable, or non-zero after a
 * diagnostic — in which case `module` is left named but unusable, so the
 * failure is reported once and not retried (HLR-070, LLR-RFP-06).
 */
static int module_load(const Registry *reg, LanguageModule *module,
                       const char *language)
{
	char        path[PATH_MAX];
	char        symbol[128];
	const char *error;

	module->language_name = strdup(language);
	if (!module->language_name) {
		fputs("elc: out of memory loading a language module\n", stderr);
		return -1;
	}

	int n = snprintf(path, sizeof path, "%s/parsers/%s.so", reg->dir,
	                 language);
	if (n < 0 || (size_t)n >= sizeof path) {
		fprintf(stderr, "elc: %s: module path is too long\n", language);
		return -1;
	}

	module->dl_handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
	if (!module->dl_handle) {
		fprintf(stderr, "elc: %s: %s\n", language, dlerror());
		return -1;
	}

	n = snprintf(symbol, sizeof symbol, "tree_sitter_%s", language);
	if (n < 0 || (size_t)n >= sizeof symbol) {
		fprintf(stderr, "elc: %s: language name is too long\n", language);
		return -1;
	}

	/* ISO C forbids converting void * to a function pointer directly; this
	 * is the POSIX-sanctioned form. dlerror() is cleared first and checked
	 * after, because NULL is a legal result for a NULL symbol. */
	const TSLanguage *(*entry)(void);
	dlerror();
	*(void **)(&entry) = dlsym(module->dl_handle, symbol);
	error = dlerror();
	if (error) {
		fprintf(stderr, "elc: %s: %s\n", language, error);
		return -1;
	}
	if (!entry) {
		fprintf(stderr, "elc: %s: %s resolved to nothing\n", language,
		        symbol);
		return -1;
	}

	module->ts_lang = entry();
	if (!module->ts_lang) {
		fprintf(stderr, "elc: %s: grammar entry point produced no "
		        "language\n", language);
		return -1;
	}

	for (size_t i = 0; i < QUERY_COUNT; i++) {
		char     *source;
		uint32_t  length      = 0;
		uint32_t  error_offset = 0;
		TSQueryError query_error = TSQueryErrorNone;

		n = snprintf(path, sizeof path, "%s/queries/%s/%s", reg->dir,
		             language, QUERY_FILES[i]);
		if (n < 0 || (size_t)n >= sizeof path) {
			fprintf(stderr, "elc: %s: query path is too long\n",
			        language);
			return -1;
		}

		source = read_file(path, &length);
		if (!source) {
			/* An optional query simply is not there. The language
			 * loses the analysis that reads it and keeps every
			 * other one; the consumer sees a NULL and says so in
			 * the report rather than reporting a clean result
			 * (HLR-139, LLR-RFP-10). */
			if (i >= QUERY_REQUIRED_COUNT) {
				module->queries[i] = NULL;
				continue;
			}
			/* A module is required to supply all six. One missing
			 * makes the language unusable — reported, excluded, and
			 * survivable, never undefined (HLR-121, HLR-070). */
			fprintf(stderr, "elc: %s: %s: %s\n", language,
			        QUERY_FILES[i], strerror(errno));
			return -1;
		}

		module->queries[i] = ts_query_new(module->ts_lang, source,
		                                  length, &error_offset,
		                                  &query_error);
		free(source);

		if (!module->queries[i]) {
			fprintf(stderr, "elc: %s: %s: %s at byte %u\n",
			        language, QUERY_FILES[i],
			        query_error_text(query_error), error_offset);
			return -1;
		}
	}

	module->usable = true;
	return 0;
}

const LanguageModule *registry_for_path(Registry *reg, const char *path)
{
	const char *language = language_for_path(reg, path);

	/* No mapping is not a failure: the caller skips the file and reports
	 * it skipped (HLR-012, LLR-RFP-05). */
	if (!language)
		return NULL;

	for (size_t i = 0; i < reg->module_count; i++)
		if (reg->modules[i].language_name &&
		    strcmp(reg->modules[i].language_name, language) == 0)
			return reg->modules[i].usable ? &reg->modules[i] : NULL;

	if (reg->module_count == reg->module_capacity &&
	    grow((void **)&reg->modules, &reg->module_capacity,
	         sizeof *reg->modules) != 0) {
		fputs("elc: out of memory loading a language module\n", stderr);
		return NULL;
	}

	LanguageModule *module = &reg->modules[reg->module_count];
	memset(module, 0, sizeof *module);

	if (module_load(reg, module, language) != 0) {
		/* Keep the entry so the diagnostic is emitted once rather than
		 * per file, but release what it acquired. */
		module_release(module);
		module->language_name = strdup(language);
		reg->module_count++;
		return NULL;
	}

	reg->module_count++;
	return module;
}

/* ------------------------------------------------------------- lifecycle -- */

int registry_open(const ElcOptions *opts, Registry *out)
{
	char        dir[PATH_MAX];
	struct stat st;

	(void)opts;   /* Custom rule paths reach here in Phase 14. */

	memset(out, 0, sizeof *out);

	char tried[PATH_MAX * 2];

	if (runtime_dir_resolve(dir, sizeof dir, tried, sizeof tried) != 0) {
		/* Naming every candidate, because the reader's next action is
		 * to put the runtime in one of them or to set the variable,
		 * and a message quoting a single path they never chose sends
		 * them to the wrong place. */
		if (tried[0])
			fprintf(stderr,
			        "elc: no runtime directory at %s; set %s to "
			        "name one\n", tried, ELC_RUNTIME_DIR_ENV);
		else
			fputs("elc: cannot locate the runtime directory; set "
			      ELC_RUNTIME_DIR_ENV " to name one\n", stderr);
		return -1;
	}

	if (stat(dir, &st) != 0) {
		fprintf(stderr, "elc: %s: %s\n", dir, strerror(errno));
		return -1;
	}
	if (!S_ISDIR(st.st_mode)) {
		fprintf(stderr, "elc: %s: not a directory\n", dir);
		return -1;
	}

	out->dir = strdup(dir);
	if (!out->dir) {
		fputs("elc: out of memory opening the runtime directory\n",
		      stderr);
		return -1;
	}

	if (load_extension_map(out) != 0)
		goto fail;

	/* No particular language is required or assumed: whatever valid
	 * modules the location happens to hold is the set elc runs over
	 * (HLR-011, LLR-ROP-05). An empty map, though, means no file can ever
	 * be analysed, which is the state HLR-036 calls fatal. */
	if (out->map_count == 0) {
		fprintf(stderr, "elc: %s: the extension map names no "
		        "language\n", out->dir);
		goto fail;
	}

	/* One parser and one cursor for the whole run. Allocating either per
	 * file is expensive and buys nothing; reuse needs only
	 * ts_parser_set_language() per file. */
	out->parser = ts_parser_new();
	out->cursor = ts_query_cursor_new();
	if (!out->parser || !out->cursor) {
		fputs("elc: out of memory creating the parser\n", stderr);
		goto fail;
	}

	return 0;

fail:
	registry_close(out);
	return -1;
}

void registry_close(Registry *reg)
{
	if (!reg)
		return;

	/* Order is load-bearing (LLR-RCL-01): every compiled query is deleted
	 * before the handle whose grammar it points into is closed, and the
	 * parser and cursor go in between. module_release() holds the query
	 * half of that order; the loop holds the rest. */
	for (size_t i = 0; i < reg->module_count; i++) {
		free(reg->modules[i].language_name);
		reg->modules[i].language_name = NULL;
		for (size_t q = 0; q < QUERY_COUNT; q++) {
			if (reg->modules[i].queries[q]) {
				ts_query_delete(reg->modules[i].queries[q]);
				reg->modules[i].queries[q] = NULL;
			}
		}
	}

	if (reg->cursor) {
		ts_query_cursor_delete(reg->cursor);
		reg->cursor = NULL;
	}
	if (reg->parser) {
		ts_parser_delete(reg->parser);
		reg->parser = NULL;
	}

	for (size_t i = 0; i < reg->module_count; i++) {
		if (reg->modules[i].dl_handle) {
			dlclose(reg->modules[i].dl_handle);
			reg->modules[i].dl_handle = NULL;
		}
	}

	free(reg->modules);
	reg->modules         = NULL;
	reg->module_count    = 0;
	reg->module_capacity = 0;

	for (size_t i = 0; i < reg->map_count; i++) {
		free(reg->map[i].extension);
		free(reg->map[i].language);
	}
	free(reg->map);
	reg->map          = NULL;
	reg->map_count    = 0;
	reg->map_capacity = 0;

	free(reg->dir);
	reg->dir = NULL;
}
