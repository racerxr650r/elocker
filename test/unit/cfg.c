/* test/unit/cfg.c — unit tests for src/cfg.c (HLR-229).
 *
 * Driven through `analyze_file` rather than by hand-building a graph. The
 * control flow under test is what the *parse* produces, and a hand-built graph
 * would be testing this file's idea of what an `if` looks like rather than
 * tree-sitter's — which is the disagreement that would actually cost a
 * finding. The blocks are an implementation detail; whether a lock escapes is
 * the requirement.
 */
#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "analyze.h"
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

static const char *source_holding(const char *contents)
{
	static char path[1024];

	if (scratch[0] == '\0') {
		snprintf(scratch, sizeof scratch, "/tmp/elc-cfg-XXXXXX");
		cr_assert_not_null(mkdtemp(scratch), "no scratch dir");
		atexit(remove_scratch);
	}

	snprintf(path, sizeof path, "%s/subject.c", scratch);

	FILE *fp = fopen(path, "w");
	cr_assert_not_null(fp, "could not write %s", path);
	fputs(contents, fp);
	fclose(fp);
	return path;
}

/* Whether `name` in `contents` leaves a critical section held on some path. */
static bool leaks(const char *contents, const char *name)
{
	Registry     reg;
	ElcOptions   opts  = { 0 };
	FileMetrics *m     = NULL;
	FileFacts   *facts = NULL;
	bool         found = false;
	bool         leaked = false;

	memset(&opts, 0, sizeof opts);
	cr_assert_eq(registry_open(&opts, &reg), 0,
	             ELC_RUNTIME_DIR_ENV " must name a usable runtime");
	cr_assert_eq(analyze_file(&reg, &opts, NULL, source_holding(contents),
	                          &m, &facts), 0);

	for (size_t i = 0; i < m->function_count; i++)
		if (strcmp(m->functions[i].name, name) == 0) {
			leaked = m->functions[i].leaks_lock;
			found  = true;
		}

	cr_assert(found, "%s was not reported", name);
	filefacts_free(facts);
	filemetrics_free(m);
	registry_close(&reg);
	return leaked;
}

/* Whether `name`'s control flow could be built at all. */
static bool complete(const char *contents, const char *name)
{
	Registry     reg;
	ElcOptions   opts  = { 0 };
	FileMetrics *m     = NULL;
	FileFacts   *facts = NULL;
	bool         ok    = false;

	memset(&opts, 0, sizeof opts);
	cr_assert_eq(registry_open(&opts, &reg), 0);
	cr_assert_eq(analyze_file(&reg, &opts, NULL, source_holding(contents),
	                          &m, &facts), 0);

	for (size_t i = 0; i < m->function_count; i++)
		if (strcmp(m->functions[i].name, name) == 0)
			ok = m->functions[i].cfg_complete;

	filefacts_free(facts);
	filemetrics_free(m);
	registry_close(&reg);
	return ok;
}

#define LOCK   "int pthread_mutex_lock(void *);\n" \
               "int pthread_mutex_unlock(void *);\n" \
               "static int m;\n"

/* Verifies LLR-CFG-01 and LLR-CFG-03: the case the analysis exists for.
 *
 * A guard clause between the acquisition and the release. A model treating a
 * function as one block reports this safe, which is why the early return is
 * the first thing the graph has to get right. */
Test(cfg, an_early_return_between_acquire_and_release_leaks)
{
	cr_assert(leaks(LOCK
		"int f(int x) {\n"
		"	pthread_mutex_lock(&m);\n"
		"	if (x)\n"
		"		return 1;\n"
		"	pthread_mutex_unlock(&m);\n"
		"	return 0;\n"
		"}\n", "f"),
		"the path through the guard clause holds the lock at exit");
}

/* Verifies LLR-CFG-03: the converse, and what stops the analysis reporting
 * every function that locks at all. */
Test(cfg, a_release_on_every_path_does_not_leak)
{
	cr_assert_not(leaks(LOCK
		"int f(int x) {\n"
		"	pthread_mutex_lock(&m);\n"
		"	if (x) {\n"
		"		pthread_mutex_unlock(&m);\n"
		"		return 1;\n"
		"	}\n"
		"	pthread_mutex_unlock(&m);\n"
		"	return 0;\n"
		"}\n", "f"));
}

/* Verifies LLR-CFG-03: the case the visited set's key decides.
 *
 * A search keyed on the block alone reaches the loop head twice — once holding
 * and once not — and answers from whichever state it arrived in first,
 * reporting a leak that is not there. Keyed on the block *and* the held count,
 * it does not. */
Test(cfg, a_lock_taken_and_released_inside_a_loop_does_not_leak)
{
	cr_assert_not(leaks(LOCK
		"int f(int n) {\n"
		"	int i;\n"
		"	for (i = 0; i < n; i++) {\n"
		"		pthread_mutex_lock(&m);\n"
		"		pthread_mutex_unlock(&m);\n"
		"	}\n"
		"	return 0;\n"
		"}\n", "f"));
}

/* Verifies LLR-CFG-04: one finding per function, however many routes fail.
 *
 * Three branches above one leaking return are eight failing routes and one
 * defect. Asserted as the boolean the function carries, which is what makes
 * the finding one. */
Test(cfg, a_function_is_reported_once_however_many_paths_fail)
{
	cr_assert(leaks(LOCK
		"int f(int a, int b, int c) {\n"
		"	pthread_mutex_lock(&m);\n"
		"	if (a) { }\n"
		"	if (b) { }\n"
		"	if (c) { }\n"
		"	if (a && b)\n"
		"		return 1;\n"
		"	pthread_mutex_unlock(&m);\n"
		"	return 0;\n"
		"}\n", "f"));
}

/* Verifies LLR-CFG-02: an unmodelled construct is declared, not ignored.
 *
 * `goto` needs a label table and a second pass. A graph that silently dropped
 * the edge would report the path through the label as absent — which is to say
 * it would report the function safe because it could not see the way it fails.
 * The caller reports such a function as not analysed (HLR-138). */
Test(cfg, a_construct_the_module_does_not_describe_is_not_analysed)
{
	cr_assert_not(complete(LOCK
		"int f(int x) {\n"
		"	pthread_mutex_lock(&m);\n"
		"	if (x)\n"
		"		goto out;\n"
		"	pthread_mutex_unlock(&m);\n"
		"out:\n"
		"	return 0;\n"
		"}\n", "f"),
		"a function containing goto must not be claimed as analysed");
}
