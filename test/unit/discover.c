/* test/unit/discover.c — unit tests for src/discover.c.
 *
 * One Criterion binary per src/ module, linked against that module. Tests
 * register automatically, so a test cannot be written and silently never run
 * (doc/STP.md §2.2).
 *
 * Discovery is filesystem behaviour, so most of these build a small tree in a
 * temporary directory and assert on what comes back. That is deliberate: a
 * mocked directory walk would verify the mock. Only the one failure that
 * cannot be provoked from the filesystem — a canonicalisation that fails on a
 * path that exists — is reached with a link-time wrapper.
 */

#include <criterion/criterion.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "discover.h"
#include "elc.h"

/* --------------------------------------------------------------- --wrap ---
 *
 * `volatile` is load-bearing, not decoration: the compiler treats a wrapped
 * library function as a builtin that cannot read the caller's globals, judges
 * the arming store dead, and removes it. The interception then silently does
 * not happen and the test fails for a reason that looks unrelated
 * (doc/STP.md §2.2).
 */
extern char *__real_realpath(const char *, char *);

static volatile int realpath_should_fail;

char *__wrap_realpath(const char *path, char *resolved)
{
	if (realpath_should_fail) {
		errno = ENOMEM;
		return NULL;
	}
	return __real_realpath(path, resolved);
}

/* ------------------------------------------------------------- scaffolding */

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

/* A private directory for one test, removed when that test's process exits.
 * Criterion gives each test its own process, so no two tests collide. */
static const char *tmptree(void)
{
	snprintf(scratch, sizeof scratch, "/tmp/elc-discover-XXXXXX");
	cr_assert_not_null(mkdtemp(scratch), "could not create a scratch tree");
	atexit(remove_scratch);
	return scratch;
}

static void put(const char *dir, const char *name, const char *contents)
{
	char  path[1024];
	FILE *fp;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	fp = fopen(path, "w");
	cr_assert_not_null(fp, "could not write %s", path);
	fputs(contents, fp);
	fclose(fp);
}

static void subdir(const char *dir, const char *name)
{
	char path[1024];

	snprintf(path, sizeof path, "%s/%s", dir, name);
	cr_assert_eq(mkdir(path, 0755), 0, "could not create %s", path);
}

/* Point elc at a runtime directory carrying a known exclusion list, so the
 * tests do not depend on the shipped one and no test reads a list compiled
 * into the binary — there is none (LLR-EXT-01). */
static void runtime_with(const char *dir, const char *exts)
{
	char path[1024];

	snprintf(path, sizeof path, "%s/rt", dir);
	cr_assert_eq(mkdir(path, 0755), 0);
	put(path, "binary.exts", exts);
	setenv(ELC_RUNTIME_DIR_ENV, path, 1);
}

static ElcOptions options_for(const char **targets, size_t count)
{
	ElcOptions opts;

	memset(&opts, 0, sizeof opts);
	opts.mode         = MODE_ANALYSE;
	opts.targets      = targets;
	opts.target_count = count;
	return opts;
}

static bool list_has_suffix(const FileList *list, const char *suffix)
{
	for (size_t i = 0; i < list->count; i++) {
		size_t plen = strlen(list->paths[i]);
		size_t slen = strlen(suffix);

		if (plen >= slen &&
		    strcmp(list->paths[i] + plen - slen, suffix) == 0)
			return true;
	}
	return false;
}

/* --------------------------------------------- is_excluded_extension ------ */

Test(discover, excluded_extension_matches_the_runtime_list)
{
	ExtensionList exts;
	const char   *dir = tmptree();

	runtime_with(dir, ".png\n.zip\n");
	cr_assert_eq(binary_exts_load(&exts), 0);

	cr_assert(is_excluded_extension("/a/b/logo.png", &exts));
	cr_assert(is_excluded_extension("archive.zip", &exts));
	cr_assert_not(is_excluded_extension("/a/b/main.c", &exts));

	binary_exts_free(&exts);
}

Test(discover, excluded_extension_ignores_case)
{
	ExtensionList exts;
	const char   *dir = tmptree();

	runtime_with(dir, ".png\n");
	cr_assert_eq(binary_exts_load(&exts), 0);

	cr_assert(is_excluded_extension("LOGO.PNG", &exts),
	          "one entry must cover every spelling of the extension");

	binary_exts_free(&exts);
}

Test(discover, a_name_without_an_extension_is_not_excluded)
{
	ExtensionList exts;
	const char   *dir = tmptree();

	runtime_with(dir, ".png\n");
	cr_assert_eq(binary_exts_load(&exts), 0);

	cr_assert_not(is_excluded_extension("/a/b.d/Makefile", &exts),
	              "a dot in a directory component is not an extension");
	cr_assert_not(is_excluded_extension("/a/b/.bashrc", &exts),
	              "a name that is nothing but a suffix has no extension");

	binary_exts_free(&exts);
}

Test(discover, an_empty_exclusion_list_excludes_nothing)
{
	ExtensionList exts;

	memset(&exts, 0, sizeof exts);
	cr_assert_not(is_excluded_extension("logo.png", &exts));
}

/* ------------------------------------------------- binary_exts_load ------- */

Test(discover, extension_list_skips_comments_and_blank_lines)
{
	ExtensionList exts;
	const char   *dir = tmptree();

	runtime_with(dir, "# a comment\n\n  .png\nzip\n");
	cr_assert_eq(binary_exts_load(&exts), 0);

	cr_assert_eq(exts.count, 2, "comments and blank lines are not entries");
	cr_assert(is_excluded_extension("a.png", &exts));
	cr_assert(is_excluded_extension("a.zip", &exts),
	          "an entry written without its leading dot still matches");

	binary_exts_free(&exts);
}

Test(discover, a_missing_extension_list_is_not_fatal)
{
	ExtensionList exts;
	const char   *dir = tmptree();

	setenv(ELC_RUNTIME_DIR_ENV, dir, 1);   /* holds no binary.exts */

	cr_assert_eq(binary_exts_load(&exts), 0,
	             "discovery continues without an exclusion list");
	cr_assert_eq(exts.count, 0);

	binary_exts_free(&exts);
}

/* ----------------------------------------------- discover_targets --------- */

Test(discover, a_regular_file_target_is_appended_directly)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        target[1024];

	runtime_with(dir, "");
	put(dir, "a.c", "one\ntwo\n");
	snprintf(target, sizeof target, "%s/a.c", dir);

	const char *targets[] = { target };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 1);
	cr_assert_eq(failures, 0);
	cr_assert(list_has_suffix(&list, "/a.c"));

	filelist_free(&list);
}

Test(discover, a_missing_target_is_rejected_with_an_empty_list)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        target[1024];

	runtime_with(dir, "");
	snprintf(target, sizeof target, "%s/absent.c", dir);

	const char *targets[] = { target };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_neq(discover_targets(&opts, &list, &failures), 0,
	              "a target that does not exist ends the run (HLR-062)");
	cr_assert_eq(list.count, 0, "no partial file list survives rejection");

	filelist_free(&list);
}

Test(discover, a_target_that_is_neither_file_nor_directory_is_rejected)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        target[1024];

	runtime_with(dir, "");
	snprintf(target, sizeof target, "%s/pipe", dir);
	cr_assert_eq(mkfifo(target, 0644), 0, "could not create a FIFO");

	const char *targets[] = { target };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_neq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 0);

	filelist_free(&list);
}

Test(discover, every_target_is_validated_before_any_is_walked)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        good[1024], bad[1024];

	runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(good, sizeof good, "%s/tree", dir);
	put(good, "a.c", "one\n");
	snprintf(bad, sizeof bad, "%s/absent", dir);

	const char *targets[] = { good, bad };
	ElcOptions  opts      = options_for(targets, 2);

	cr_assert_neq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 0,
	             "a valid target must not be walked when a later one is "
	             "invalid (HLR-062)");

	filelist_free(&list);
}

Test(discover, a_file_reached_through_two_targets_appears_once)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        tree[1024], file[1024];

	runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "a.c", "one\n");
	snprintf(file, sizeof file, "%s/a.c", tree);

	const char *targets[] = { file, tree };
	ElcOptions  opts      = options_for(targets, 2);

	cr_assert_eq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 1,
	             "canonicalisation before de-duplication is what makes "
	             "`elc a.c src/` count a.c once (HLR-072)");

	filelist_free(&list);
}

Test(discover, the_file_list_is_sorted_into_byte_order)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        tree[1024];

	runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "z.c", "one\n");
	put(tree, "a.c", "one\n");
	put(tree, "m.c", "one\n");

	const char *targets[] = { tree };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 3);
	for (size_t i = 1; i < list.count; i++)
		cr_assert(strcmp(list.paths[i - 1], list.paths[i]) < 0,
		          "the list must not depend on enumeration order "
		          "(HLR-033)");

	filelist_free(&list);
}

Test(discover, hidden_entries_and_binary_extensions_are_excluded)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        tree[1024], hidden[1024];

	runtime_with(dir, ".png\n");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "a.c", "one\n");
	put(tree, "logo.png", "one\n");
	put(tree, ".elcrc", "one\n");
	subdir(tree, ".hidden");
	snprintf(hidden, sizeof hidden, "%s/.hidden", tree);
	put(hidden, "secret.c", "one\n");

	const char *targets[] = { tree };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 1, "only a.c survives filtering");
	cr_assert(list_has_suffix(&list, "/a.c"));

	filelist_free(&list);
}

Test(discover, the_walk_descends_into_subdirectories)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        tree[1024], deep[1024];

	runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "top.c", "one\n");
	subdir(tree, "sub");
	snprintf(deep, sizeof deep, "%s/sub", tree);
	put(deep, "middle.c", "one\n");
	subdir(deep, "deeper");
	snprintf(deep, sizeof deep, "%s/sub/deeper", tree);
	put(deep, "bottom.c", "one\n");

	const char *targets[] = { tree };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 3, "the traversal is recursive");
	cr_assert(list_has_suffix(&list, "/sub/deeper/bottom.c"));

	filelist_free(&list);
}

Test(discover, a_hidden_directory_named_as_the_target_is_traversed)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        hidden[1024];

	runtime_with(dir, "");
	subdir(dir, ".config");
	snprintf(hidden, sizeof hidden, "%s/.config", dir);
	put(hidden, "a.c", "one\n");

	const char *targets[] = { hidden };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 1,
	             "the hidden-entry exclusion applies below the target, not "
	             "to the target itself, which was named explicitly");

	filelist_free(&list);
}

Test(discover, files_and_directories_are_classified_independently)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        tree[1024], file[1024];

	runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "walked.c", "one\n");
	put(dir, "named.c", "one\n");
	snprintf(file, sizeof file, "%s/named.c", dir);

	const char *targets[] = { file, tree };
	ElcOptions  opts      = options_for(targets, 2);

	cr_assert_eq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 2,
	             "each target is routed on its own type, so files and "
	             "directories may be intermixed");
	cr_assert(list_has_suffix(&list, "/named.c"));
	cr_assert(list_has_suffix(&list, "/walked.c"));

	filelist_free(&list);
}

Test(discover, a_cyclic_directory_symlink_terminates)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        tree[1024], loop[1024];

	runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "a.c", "one\n");
	snprintf(loop, sizeof loop, "%s/loop", tree);
	cr_assert_eq(symlink(tree, loop), 0, "could not create the cycle");

	const char *targets[] = { tree };
	ElcOptions  opts      = options_for(targets, 1);

	/* Reaching this assertion at all is the result: a logical walk would
	 * not return (HLR-069, LLR-FTS-04). */
	cr_assert_eq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 1, "the linked directory is not descended");

	filelist_free(&list);
}

Test(discover, a_symbolic_link_named_as_a_target_is_resolved)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        real[1024], link[1024];

	runtime_with(dir, "");
	put(dir, "a.c", "one\n");
	snprintf(real, sizeof real, "%s/a.c", dir);
	snprintf(link, sizeof link, "%s/link.c", dir);
	cr_assert_eq(symlink(real, link), 0);

	const char *targets[] = { link };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, &list, &failures), 0);
	cr_assert_eq(list.count, 1);
	cr_assert(list_has_suffix(&list, "/a.c"),
	          "a link named explicitly identifies its referent (HLR-069)");

	filelist_free(&list);
}

Test(discover, a_path_that_cannot_be_canonicalised_is_a_per_file_failure)
{
	const char *dir = tmptree();
	FileList    list;
	size_t      failures = 0;
	char        tree[1024];

	runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "a.c", "one\n");

	const char *targets[] = { tree };
	ElcOptions  opts      = options_for(targets, 1);

	realpath_should_fail = 1;
	int rc = discover_targets(&opts, &list, &failures);
	realpath_should_fail = 0;

	cr_assert_eq(rc, 0, "the run continues past a per-file failure");
	cr_assert_eq(list.count, 0);
	cr_assert_geq(failures, 1,
	              "the failure is recorded so the exit status reflects it");

	filelist_free(&list);
}

Test(discover, filelist_free_is_safe_on_null)
{
	filelist_free(NULL);
	binary_exts_free(NULL);
	cr_assert(1, "releasing a null collection must not fault");
}
