/* test/unit/registry.c — unit tests for src/registry.c.
 *
 * The registry is the boundary that keeps language knowledge out of the
 * binary, so most of these build a runtime directory in a temporary place
 * and assert on what the registry makes of it. A mocked filesystem would
 * verify the mock; a runtime directory is three files and a symlink.
 *
 * The shipped grammar is reused rather than rebuilt: what is under test is
 * the loading, not the grammar.
 */

#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "elc.h"
#include "registry.h"

static char scratch[512];

static void remove_scratch(void)
{
	char command[600];

	if (scratch[0] == '\0')
		return;
	snprintf(command, sizeof command, "rm -rf -- '%s'", scratch);
	if (system(command) != 0)
		fprintf(stderr, "could not remove %s\n", scratch);
}

static const char *tmpdir(void)
{
	if (scratch[0] == '\0') {
		snprintf(scratch, sizeof scratch, "/tmp/elc-registry-XXXXXX");
		cr_assert_not_null(mkdtemp(scratch), "could not create a scratch dir");
		atexit(remove_scratch);
	}
	return scratch;
}

static void put(const char *path, const char *contents)
{
	FILE *fp = fopen(path, "w");

	cr_assert_not_null(fp, "could not write %s", path);
	fputs(contents, fp);
	fclose(fp);
}

/* A runtime directory carrying the shipped C grammar and query files, with
 * the given extension map. The caller may then break one piece of it. */
static const char *runtime_copy(const char *extension_map)
{
	static char dir[1024];
	char        command[2048];
	const char *source = getenv(ELC_RUNTIME_DIR_ENV);

	cr_assert_not_null(source,
	                   ELC_RUNTIME_DIR_ENV " must name the in-tree runtime");

	snprintf(dir, sizeof dir, "%s/runtime", tmpdir());
	snprintf(command, sizeof command, "cp -r '%s' '%s'", source, dir);
	cr_assert_eq(system(command), 0, "could not copy the runtime directory");

	char path[1200];
	snprintf(path, sizeof path, "%s/extensions.map", dir);
	put(path, extension_map);

	setenv(ELC_RUNTIME_DIR_ENV, dir, 1);
	return dir;
}

static ElcOptions empty_options(void)
{
	ElcOptions opts;

	memset(&opts, 0, sizeof opts);
	return opts;
}

/* ------------------------------------------------------- runtime location */

/* Verifies LLR-ROP-01, LLR-ROP-02: the environment variable names the
 * runtime location and wins over the path adjacent to the executable, which
 * for a unit binary does not exist at all. */
Test(registry, the_environment_variable_names_the_runtime_location)
{
	const char *dir  = runtime_copy(".c c\n");
	ElcOptions  opts = empty_options();
	Registry    reg;

	cr_assert_eq(registry_open(&opts, &reg), 0);
	cr_assert_str_eq(registry_runtime_dir(&reg), dir);
	registry_close(&reg);
}

/* Verifies LLR-ROP-04: an absent runtime location is fatal. */
Test(registry, an_absent_runtime_location_is_fatal)
{
	ElcOptions opts = empty_options();
	Registry   reg;
	char       absent[1024];

	snprintf(absent, sizeof absent, "%s/not-here", tmpdir());
	setenv(ELC_RUNTIME_DIR_ENV, absent, 1);

	cr_assert_neq(registry_open(&opts, &reg), 0,
	              "no analysis is possible without a runtime location");
	registry_close(&reg);
}

/* Verifies LLR-ROP-04: a runtime location that is not a directory is fatal
 * as surely as one that is missing. */
Test(registry, a_runtime_location_that_is_a_file_is_fatal)
{
	ElcOptions opts = empty_options();
	Registry   reg;
	char       path[1024];

	snprintf(path, sizeof path, "%s/a-file", tmpdir());
	put(path, "not a directory\n");
	setenv(ELC_RUNTIME_DIR_ENV, path, 1);

	cr_assert_neq(registry_open(&opts, &reg), 0);
	registry_close(&reg);
}

/* Verifies LLR-ROP-04: an extension map naming no language leaves the run
 * unable to analyse anything, which is the state HLR-036 calls fatal. */
Test(registry, an_extension_map_naming_no_language_is_fatal)
{
	ElcOptions opts = empty_options();
	Registry   reg;

	runtime_copy("# nothing but a comment\n");

	cr_assert_neq(registry_open(&opts, &reg), 0);
	registry_close(&reg);
}

/* Verifies LLR-ROP-03: the mapping comes from runtime data. Pointing an
 * unrelated extension at the C grammar proves the mapping is read rather
 * than assumed — no table in the executable associates it. */
Test(registry, the_extension_map_is_runtime_data)
{
	ElcOptions opts = empty_options();
	Registry   reg;

	runtime_copy(".wibble c\n");
	cr_assert_eq(registry_open(&opts, &reg), 0);

	cr_assert_not_null(registry_for_path(&reg, "/a/b/thing.wibble"),
	                   "an extension mapped by data must resolve");
	cr_assert_null(registry_for_path(&reg, "/a/b/thing.c"),
	               "an extension the data does not map must not resolve");

	registry_close(&reg);
}

/* Verifies LLR-ROP-03: the map ignores blanks and comments, and accepts an
 * extension written with or without its leading period. */
Test(registry, the_extension_map_tolerates_comments_and_bare_extensions)
{
	ElcOptions opts = empty_options();
	Registry   reg;

	runtime_copy("# a comment\n\n  c   c\n.H c\n");
	cr_assert_eq(registry_open(&opts, &reg), 0);

	cr_assert_not_null(registry_for_path(&reg, "a.c"),
	                   "an entry written without its period still matches");
	cr_assert_not_null(registry_for_path(&reg, "a.h"),
	                   "an extension is matched without regard to case");

	registry_close(&reg);
}

/* ------------------------------------------------------ language modules */

/* Verifies LLR-RFP-01, LLR-RFP-03: a file's language comes from its
 * extension alone, and the module is loaded on first use. */
Test(registry, a_module_is_loaded_on_first_use_of_its_extension)
{
	ElcOptions            opts = empty_options();
	Registry              reg;
	const LanguageModule *module;

	runtime_copy(".c c\n");
	cr_assert_eq(registry_open(&opts, &reg), 0);

	module = registry_for_path(&reg, "/a/b/main.c");
	cr_assert_not_null(module);
	cr_assert_str_eq(module->language_name, "c");
	cr_assert_not_null(module->ts_lang, "the grammar entry point resolved");
	for (size_t i = 0; i < QUERY_COUNT; i++)
		cr_assert_not_null(module->queries[i],
		                   "all six queries compile (HLR-121)");

	registry_close(&reg);
}

/* Verifies LLR-RFP-02: a language is loaded at most once per run, so a
 * mixed-language target needs one pass rather than one per language. */
Test(registry, a_language_is_loaded_at_most_once)
{
	ElcOptions            opts = empty_options();
	Registry              reg;
	const LanguageModule *first;
	const LanguageModule *second;

	runtime_copy(".c c\n.h c\n");
	cr_assert_eq(registry_open(&opts, &reg), 0);

	first  = registry_for_path(&reg, "/a/one.c");
	second = registry_for_path(&reg, "/a/two.h");

	cr_assert_not_null(first);
	cr_assert_eq(first, second,
	             "the second lookup must return the cached module");

	registry_close(&reg);
}

/* Verifies LLR-RFP-05: an unmapped extension yields no module, so the
 * caller skips the file rather than failing. */
Test(registry, an_unmapped_extension_yields_no_module)
{
	ElcOptions opts = empty_options();
	Registry   reg;

	runtime_copy(".c c\n");
	cr_assert_eq(registry_open(&opts, &reg), 0);

	cr_assert_null(registry_for_path(&reg, "/a/b/README.md"));
	cr_assert_null(registry_for_path(&reg, "/a/b/Makefile"),
	               "a name with no extension maps to nothing");

	registry_close(&reg);
}

/* Verifies LLR-RFP-06: a module whose grammar is absent is reported and
 * excluded, not fatal. */
Test(registry, an_absent_grammar_makes_the_language_unusable)
{
	ElcOptions opts = empty_options();
	Registry   reg;
	char       path[1200];

	const char *dir = runtime_copy(".c c\n");
	snprintf(path, sizeof path, "%s/parsers/c.so", dir);
	cr_assert_eq(unlink(path), 0);

	cr_assert_eq(registry_open(&opts, &reg), 0,
	             "opening the registry does not load any grammar");
	cr_assert_null(registry_for_path(&reg, "a.c"));

	registry_close(&reg);
}

/* Verifies LLR-RFP-06, LLR-RFP-08: a module omitting a required query file
 * is unusable — handled, not undefined. */
Test(registry, a_missing_query_file_makes_the_language_unusable)
{
	ElcOptions opts = empty_options();
	Registry   reg;
	char       path[1200];

	const char *dir = runtime_copy(".c c\n");
	snprintf(path, sizeof path, "%s/queries/c/globals.scm", dir);
	cr_assert_eq(unlink(path), 0);

	cr_assert_eq(registry_open(&opts, &reg), 0);
	cr_assert_null(registry_for_path(&reg, "a.c"),
	               "all six query files are required (HLR-121)");

	registry_close(&reg);
}

/* Verifies LLR-RFP-06: an unparseable query file makes the language
 * unusable rather than crashing or being ignored. */
Test(registry, an_invalid_query_makes_the_language_unusable)
{
	ElcOptions opts = empty_options();
	Registry   reg;
	char       path[1200];

	const char *dir = runtime_copy(".c c\n");
	snprintf(path, sizeof path, "%s/queries/c/functions.scm", dir);
	put(path, "(no_such_node_type) @function.name\n");

	cr_assert_eq(registry_open(&opts, &reg), 0);
	cr_assert_null(registry_for_path(&reg, "a.c"));

	registry_close(&reg);
}

/* Verifies LLR-RFP-06: the failure is reported once and not retried. A
 * second lookup must find the module already marked unusable rather than
 * attempting the load again. */
Test(registry, an_unusable_language_is_not_retried)
{
	ElcOptions opts = empty_options();
	Registry   reg;
	char       path[1200];

	const char *dir = runtime_copy(".c c\n.h c\n");
	snprintf(path, sizeof path, "%s/parsers/c.so", dir);
	cr_assert_eq(unlink(path), 0);

	cr_assert_eq(registry_open(&opts, &reg), 0);
	cr_assert_null(registry_for_path(&reg, "a.c"));
	cr_assert_null(registry_for_path(&reg, "b.h"));

	cr_assert_eq(reg.module_count, 1,
	             "the failed language is recorded once, not once per file");

	registry_close(&reg);
}

/* Verifies LLR-ROP-05: no particular language is required. A runtime
 * directory whose map names a language with no files present still opens;
 * the registry succeeds over whatever is there. */
Test(registry, no_particular_language_is_required)
{
	ElcOptions opts = empty_options();
	Registry   reg;

	runtime_copy(".zz absent-language\n");

	cr_assert_eq(registry_open(&opts, &reg), 0,
	             "the registry does not verify any language up front");
	cr_assert_null(registry_for_path(&reg, "a.zz"));

	registry_close(&reg);
}

Test(registry, close_is_safe_on_null_and_on_a_zeroed_registry)
{
	Registry reg;

	memset(&reg, 0, sizeof reg);
	registry_close(NULL);
	registry_close(&reg);
	cr_assert(1, "teardown must be safe on every path");
}
