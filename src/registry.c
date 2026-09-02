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

#include <dirent.h>
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

#include "diag.h"
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
	[QUERY_DEADCODE]   = "deadcode.scm",
	[QUERY_CONDITIONALS] = "conditionals.scm",
	[QUERY_VISIBILITY] = "visibility.scm",
	[QUERY_SIGNATURE]  = "signature.scm"
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
static int registry_grow(void **items, size_t *capacity, size_t item_size)
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

/* Append one candidate to the comma-separated record of what was tried, so a
 * failure can name every location searched rather than only the last.
 */
static void note_candidate(char *tried, size_t tried_len, size_t *at,
                           const char *candidate)
{
	int w;

	if (*at >= tried_len)
		return;

	w = snprintf(tried + *at, tried_len - *at, "%s%s",
	             *at ? ", " : "", candidate);
	if (w > 0 && (size_t)w < tried_len - *at)
		*at += (size_t)w;
}

/* The first of the locations relative to the executable that is a directory,
 * recording each as it is tried. Returns 0 on success.
 */
static int runtime_dir_search(char *buf, size_t len, char *tried,
                              size_t tried_len)
{
	char   dir[PATH_MAX];
	size_t at = 0;

	if (executable_dir(dir, sizeof dir) != 0)
		return -1;

	for (size_t i = 0;
	     i < sizeof RUNTIME_RELATIVE / sizeof *RUNTIME_RELATIVE; i++) {
		struct stat st;
		int         n = snprintf(buf, len, "%s/%s", dir,
		                         RUNTIME_RELATIVE[i]);

		if (n < 0 || (size_t)n >= len)
			continue;

		note_candidate(tried, tried_len, &at, buf);

		if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode))
			return 0;
	}

	return -1;
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

	return runtime_dir_search(buf, len, tried, tried_len);
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
	    registry_grow((void **)&reg->map, &reg->map_capacity, sizeof *reg->map) != 0) {
		free(extension);
		free(language);
		return -1;
	}

	reg->map[reg->map_count].extension = extension;
	reg->map[reg->map_count].language  = language;
	reg->map_count++;
	return 0;
}

/* Advance past spaces and tabs. */
static char *skip_blanks(char *p)
{
	while (*p == ' ' || *p == '\t')
		p++;
	return p;
}

/* Advance to the end of the field starting at `p`: the next blank, the line
 * ending, or the end of the string. */
static char *field_end(char *p)
{
	while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
		p++;
	return p;
}

/* Split one line of the extension map into its two fields.
 *
 * Returns 0 with `*ext` and `*lang` pointing into the line, or -1 for a line
 * that names no mapping: a comment, a blank, or an extension with no language.
 * The last of those is reported — one malformed line is not a reason to discard
 * the rest of the map — which is why the path is needed here.
 */
static int split_map_line(char *line, const char *path, char **ext, char **lang)
{
	char *p = skip_blanks(line);

	if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
		return -1;

	*ext = p;
	p    = field_end(p);
	if (!*p || *p == '\n' || *p == '\r') {
		/* An extension with no language names nothing. Say so and carry
		 * on. */
		*p = '\0';
		diag_printf("elc: %s: no language for '%s'\n", path, *ext);
		return -1;
	}
	*p++ = '\0';

	p     = skip_blanks(p);
	*lang = p;
	p     = field_end(p);
	*p    = '\0';

	return (**ext && **lang) ? 0 : -1;
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
		diag_printf("elc: runtime directory path is too long\n");
		return -1;
	}

	fp = fopen(path, "r");
	if (!fp) {
		diag_printf("elc: %s: %s\n", path, strerror(errno));
		return -1;
	}

	while (getline(&line, &cap, fp) != -1) {
		char *ext;
		char *lang;

		if (split_map_line(line, path, &ext, &lang) != 0)
			continue;

		if (map_add(reg, ext, lang) != 0) {
			diag_printf("elc: out of memory reading the extension map\n");
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

/* Open the shared object and call its grammar entry point.
 *
 * ISO C forbids converting void * to a function pointer directly; the form
 * below is the POSIX-sanctioned one. dlerror() is cleared first and checked
 * after, because NULL is a legal result for a NULL symbol.
 */
static int grammar_load(const Registry *reg, LanguageModule *module,
                        const char *language)
{
	char        path[PATH_MAX];
	char        symbol[128];
	const char *error;
	const TSLanguage *(*entry)(void);
	int         n;

	n = snprintf(path, sizeof path, "%s/parsers/%s.so", reg->dir, language);
	if (n < 0 || (size_t)n >= sizeof path) {
		diag_printf("elc: %s: module path is too long\n", language);
		return -1;
	}

	module->dl_handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
	if (!module->dl_handle) {
		diag_printf("elc: %s: %s\n", language, dlerror());
		return -1;
	}

	n = snprintf(symbol, sizeof symbol, "tree_sitter_%s", language);
	if (n < 0 || (size_t)n >= sizeof symbol) {
		diag_printf("elc: %s: language name is too long\n",
		        language);
		return -1;
	}

	dlerror();
	*(void **)(&entry) = dlsym(module->dl_handle, symbol);
	error = dlerror();
	if (error) {
		diag_printf("elc: %s: %s\n", language, error);
		return -1;
	}
	if (!entry) {
		diag_printf("elc: %s: %s resolved to nothing\n", language,
		        symbol);
		return -1;
	}

	module->ts_lang = entry();
	if (!module->ts_lang) {
		diag_printf("elc: %s: grammar entry point produced no "
		        "language\n", language);
		return -1;
	}
	return 0;
}

/* Compile query file `i` of the contract against the loaded grammar.
 *
 * Returns 0 having either compiled it or established that an optional one is
 * absent, -1 where the language is unusable without it.
 */
static int query_load(const Registry *reg, LanguageModule *module,
                      const char *language, size_t i)
{
	char         path[PATH_MAX];
	char        *source;
	uint32_t     length       = 0;
	uint32_t     error_offset = 0;
	TSQueryError query_error  = TSQueryErrorNone;
	int          n;

	n = snprintf(path, sizeof path, "%s/queries/%s/%s", reg->dir, language,
	             QUERY_FILES[i]);
	if (n < 0 || (size_t)n >= sizeof path) {
		diag_printf("elc: %s: query path is too long\n", language);
		return -1;
	}

	source = read_file(path, &length);
	if (!source) {
		/* An optional query simply is not there. The language loses the
		 * analysis that reads it and keeps every other one; the
		 * consumer sees a NULL and says so in the report rather than
		 * reporting a clean result (HLR-139, LLR-RFP-10). */
		if (i >= QUERY_REQUIRED_COUNT) {
			module->queries[i] = NULL;
			return 0;
		}
		/* A module is required to supply all six. One missing makes the
		 * language unusable — reported, excluded, and survivable, never
		 * undefined (HLR-121, HLR-070). */
		diag_printf("elc: %s: %s: %s\n", language, QUERY_FILES[i],
		        strerror(errno));
		return -1;
	}

	module->queries[i] = ts_query_new(module->ts_lang, source, length,
	                                  &error_offset, &query_error);
	free(source);

	if (!module->queries[i]) {
		diag_printf("elc: %s: %s: %s at byte %u\n", language,
		        QUERY_FILES[i], query_error_text(query_error),
		        error_offset);
		return -1;
	}
	return 0;
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
	module->language_name = strdup(language);
	if (!module->language_name) {
		diag_printf("elc: out of memory loading a language module\n");
		return -1;
	}

	if (grammar_load(reg, module, language) != 0)
		return -1;

	for (size_t i = 0; i < QUERY_COUNT; i++)
		if (query_load(reg, module, language, i) != 0)
			return -1;

	module->usable = true;
	return 0;
}

/* The module for a language *by name*, loading it on first use.
 *
 * Separated from registry_for_path because a custom rule names its language
 * directly — by the directory holding it, or by the `lang:path` argument form
 * — and never by a file extension. One cache and one load path serve both, so
 * a rule cannot compile against a differently loaded copy of a grammar than
 * the analysis does (HLR-107, LLR-RLR-02).
 */
static const LanguageModule *module_for_language(Registry *reg,
                                                 const char *language)
{
	for (size_t i = 0; i < reg->module_count; i++)
		if (reg->modules[i].language_name &&
		    strcmp(reg->modules[i].language_name, language) == 0)
			return reg->modules[i].usable ? &reg->modules[i] : NULL;

	if (reg->module_count == reg->module_capacity &&
	    registry_grow((void **)&reg->modules, &reg->module_capacity,
	         sizeof *reg->modules) != 0) {
		diag_printf("elc: out of memory loading a language module\n");
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

const LanguageModule *registry_for_path(Registry *reg, const char *path)
{
	const char *language = language_for_path(reg, path);

	/* No mapping is not a failure: the caller skips the file and reports
	 * it skipped (HLR-012, LLR-RFP-05). */
	return language ? module_for_language(reg, language) : NULL;
}

/* ---------------------------------------------------------- custom rules -- */

/* The rule identity's first half: the file's basename with its extension
 * removed. The capture name supplies the second, so one file expresses as many
 * named rules as it holds captures (HLR-109). */
static char *rule_stem(const char *path)
{
	const char *slash = strrchr(path, '/');
	const char *base  = slash ? slash + 1 : path;
	const char *dot   = strrchr(base, '.');
	size_t      len   = dot ? (size_t)(dot - base) : strlen(base);
	char       *stem  = malloc(len + 1);

	if (!stem)
		return NULL;
	memcpy(stem, base, len);
	stem[len] = '\0';
	return stem;
}

/* Compile one rule file against a language already loaded, and record it.
 *
 * Returns 0 when the rule was added, and non-zero when it was not — leaving
 * the caller to decide what that means. It is the *provenance* of the file
 * that decides, not the failure: the same unreadable file is a user error from
 * the command line and a malformed component from the runtime location
 * (HLR-116), and a function that decided for itself could not serve both.
 */
static int rule_compile(Registry *reg, const LanguageModule *module,
                        const char *path)
{
	uint32_t     length       = 0;
	uint32_t     error_offset = 0;
	TSQueryError query_error   = TSQueryErrorNone;
	char        *source        = read_file(path, &length);

	if (!source) {
		diag_printf("elc: %s: %s\n", path, strerror(errno));
		return -1;
	}

	TSQuery *query = ts_query_new(module->ts_lang, source, length,
	                              &error_offset, &query_error);
	free(source);

	if (!query) {
		/* The reason in words and the byte, for the reason the built-in
		 * queries get both: the person acting on this message wrote the
		 * file, and a numeric code tells them nothing. */
		diag_printf("elc: %s: %s at byte %u\n", path,
		        query_error_text(query_error), error_offset);
		return -1;
	}

	if (reg->rule_count == reg->rule_capacity &&
	    registry_grow((void **)&reg->rules, &reg->rule_capacity,
	         sizeof *reg->rules) != 0) {
		ts_query_delete(query);
		diag_printf("elc: out of memory loading a custom rule\n");
		return -1;
	}

	CustomRule *rule = &reg->rules[reg->rule_count];

	memset(rule, 0, sizeof *rule);
	rule->stem     = rule_stem(path);
	rule->language = strdup(module->language_name);
	if (!rule->stem || !rule->language) {
		free(rule->stem);
		free(rule->language);
		ts_query_delete(query);
		diag_printf("elc: out of memory loading a custom rule\n");
		return -1;
	}
	rule->query = query;
	reg->rule_count++;
	return 0;
}

/* One `lang:path` argument, split at the first colon.
 *
 * The first, so that an absolute path keeps its own colons — `c:/opt/a:b.scm`
 * is the C language and a path, not a language called `c:/opt/a`. */
static int rule_load_named(Registry *reg, const char *argument)
{
	const char *colon = strchr(argument, ':');

	if (!colon || colon == argument || colon[1] == '\0') {
		diag_printf("elc: --rules '%s': expected lang:path\n",
		        argument);
		return -1;
	}

	char *language = strndup(argument, (size_t)(colon - argument));

	if (!language) {
		diag_printf("elc: out of memory reading a rule argument\n");
		return -1;
	}

	/* Whether the runtime knows this language at all, asked before any
	 * attempt to load it. A name the extension map has never heard of is
	 * almost always a typo, and letting it reach dlopen answers it with a
	 * message about a `.so` the user did not mention — two diagnostics for
	 * one mistake, the louder of them about the wrong thing. */
	bool known = false;

	for (size_t i = 0; i < reg->map_count && !known; i++)
		known = strcmp(reg->map[i].language, language) == 0;

	const LanguageModule *module = known ? module_for_language(reg, language)
	                                     : NULL;

	if (!module) {
		/* A language with no module is reported and skipped rather than
		 * compiled, and it is *not* fatal even from the command line:
		 * the rule file may be perfectly good, and what is missing is a
		 * language module — the same absence that makes a source file a
		 * skip rather than a failure (HLR-107, LLR-RLR-03). */
		diag_printf("elc: --rules %s: no language module for '%s'; "
		        "rule skipped\n", argument, language);
		free(language);
		return 0;
	}
	free(language);

	return rule_compile(reg, module, colon + 1);
}

static int by_name(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Every `.scm` in `dirpath`, by name.
 *
 * readdir yields whatever order the filesystem holds, and rule matches are
 * reported in the order the rules were loaded within a file. Sorted here,
 * once, so no property of a directory's layout reaches the output (HLR-032).
 */
static size_t rule_files(const char *dirpath, char ***out)
{
	DIR           *dir;
	struct dirent *entry;
	char         **names    = NULL;
	size_t         count    = 0;
	size_t         capacity = 0;

	*out = NULL;

	dir = opendir(dirpath);
	if (!dir)
		return 0;   /* a language with no rules directory has no rules */

	while ((entry = readdir(dir)) != NULL) {
		const char *dot = strrchr(entry->d_name, '.');

		if (!dot || strcmp(dot, ".scm") != 0)
			continue;
		if (count == capacity &&
		    registry_grow((void **)&names, &capacity,
		                  sizeof *names) != 0)
			break;
		names[count] = strdup(entry->d_name);
		if (!names[count])
			break;
		count++;
	}
	closedir(dir);

	if (count > 1)
		qsort(names, count, sizeof *names, by_name);

	*out = names;
	return count;
}

/* Compile each named rule against the language's module.
 *
 * A rule that will not compile is a malformed *component*, not a user error:
 * diagnose it, leave it out, and carry on with the rest (HLR-116, LLR-RLR-07).
 * The diagnostic `rule_compile` already emitted names the file.
 */
static void rules_compile_all(Registry *reg, const char *language,
                              const char *dirpath, char *const *names,
                              size_t count)
{
	const LanguageModule *module = NULL;

	for (size_t i = 0; i < count; i++) {
		char path[PATH_MAX];
		int  n;

		/* Deferred to the first rule actually found, so that a language
		 * with an empty rules directory is not loaded on its account. */
		if (!module) {
			module = module_for_language(reg, language);
			if (!module) {
				diag_printf("elc: %s: no usable language "
				        "module; its custom rules are "
				        "skipped\n", language);
				return;
			}
		}

		n = snprintf(path, sizeof path, "%s/%s", dirpath, names[i]);
		if (n < 0 || (size_t)n >= sizeof path)
			continue;

		(void)rule_compile(reg, module, path);
	}
}

/* Every `.scm` under `runtime/queries/<language>/rules/`, bound to that
 * language by the directory holding it (LLR-RLR-02).
 *
 * Nothing outside the runtime location is looked at: no working directory, no
 * analysis target, no dotfile. Two users running the same command on the same
 * tree must obtain the same result, and a rule picked up from a checkout would
 * make that false (HLR-110, LLR-RLR-05).
 */
static int rules_load_located(Registry *reg, const char *language)
{
	char    dirpath[PATH_MAX];
	char  **names;
	size_t  count;
	int     n;

	n = snprintf(dirpath, sizeof dirpath, "%s/queries/%s/rules", reg->dir,
	             language);
	if (n < 0 || (size_t)n >= sizeof dirpath)
		return 0;

	count = rule_files(dirpath, &names);
	rules_compile_all(reg, language, dirpath, names, count);

	for (size_t i = 0; i < count; i++)
		free(names[i]);
	free(names);
	return 0;
}

int registry_load_rules(Registry *reg, const ElcOptions *opts)
{
	/* The runtime location first, so that a rule named on the command line
	 * is loaded against a module the located rules have already exercised,
	 * and so the two provenances cannot interleave their diagnostics. */
	for (size_t i = 0; i < reg->map_count; i++) {
		bool seen = false;

		/* Several extensions map to one language; each language's rules
		 * directory is read once. */
		for (size_t j = 0; j < i && !seen; j++)
			seen = strcmp(reg->map[i].language,
			              reg->map[j].language) == 0;
		if (!seen)
			rules_load_located(reg, reg->map[i].language);
	}

	/* A rule named on the command line that cannot be read or will not
	 * compile is a user error, and ends the run before any file is
	 * analysed rather than after a report that quietly omitted it
	 * (HLR-116, LLR-RLR-06). */
	for (size_t i = 0; i < opts->rule_count; i++)
		if (rule_load_named(reg, opts->rules[i]) != 0)
			return -1;

	return 0;
}

/* ------------------------------------------------------------- lifecycle -- */

int registry_open(const ElcOptions *opts, Registry *out)
{
	char        dir[PATH_MAX];
	struct stat st;

	memset(out, 0, sizeof *out);

	char tried[PATH_MAX * 2];

	if (runtime_dir_resolve(dir, sizeof dir, tried, sizeof tried) != 0) {
		/* Naming every candidate, because the reader's next action is
		 * to put the runtime in one of them or to set the variable,
		 * and a message quoting a single path they never chose sends
		 * them to the wrong place. */
		if (tried[0])
			diag_printf("elc: no runtime directory at %s; set %s to "
			        "name one\n", tried, ELC_RUNTIME_DIR_ENV);
		else
			diag_printf("elc: cannot locate the runtime directory; "
			            "set " ELC_RUNTIME_DIR_ENV
			            " to name one\n");
		return -1;
	}

	if (stat(dir, &st) != 0) {
		diag_printf("elc: %s: %s\n", dir, strerror(errno));
		return -1;
	}
	if (!S_ISDIR(st.st_mode)) {
		diag_printf("elc: %s: not a directory\n", dir);
		return -1;
	}

	out->dir = strdup(dir);
	if (!out->dir) {
		diag_printf("elc: out of memory opening the runtime directory\n");
		return -1;
	}

	if (load_extension_map(out) != 0)
		goto fail;

	/* No particular language is required or assumed: whatever valid
	 * modules the location happens to hold is the set elc runs over
	 * (HLR-011, LLR-ROP-05). An empty map, though, means no file can ever
	 * be analysed, which is the state HLR-036 calls fatal. */
	if (out->map_count == 0) {
		diag_printf("elc: %s: the extension map names no "
		        "language\n", out->dir);
		goto fail;
	}

	/* One parser and one cursor for the whole run. Allocating either per
	 * file is expensive and buys nothing; reuse needs only
	 * ts_parser_set_language() per file. */
	out->parser = ts_parser_new();
	out->cursor = ts_query_cursor_new();
	if (!out->parser || !out->cursor) {
		diag_printf("elc: out of memory creating the parser\n");
		goto fail;
	}

	/* Last, because compiling a rule needs a loaded grammar and loading a
	 * grammar needs everything above. A command-line rule that will not
	 * compile fails the run here — before discovery, and therefore before
	 * any file is analysed, which is what HLR-116 asks for. */
	if (registry_load_rules(out, opts) != 0)
		goto fail;

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
	 * half of that order; the loop holds the rest.
	 *
	 * A custom rule's query points into a grammar exactly as a built-in
	 * one's does, so it is subject to the same ordering and goes with
	 * them — not afterwards, and not with the options that named it. */
	for (size_t i = 0; i < reg->rule_count; i++) {
		if (reg->rules[i].query) {
			ts_query_delete(reg->rules[i].query);
			reg->rules[i].query = NULL;
		}
		free(reg->rules[i].stem);
		free(reg->rules[i].language);
	}
	free(reg->rules);
	reg->rules         = NULL;
	reg->rule_count    = 0;
	reg->rule_capacity = 0;

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
