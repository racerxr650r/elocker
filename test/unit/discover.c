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
#include <git2.h>
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

/* A runtime directory carrying a known exclusion list, so the tests do not
 * depend on the shipped one and no test reads a list compiled into the
 * binary — there is none (LLR-EXT-01).
 *
 * The path is returned rather than published through the environment: since
 * Phase 2 the location is resolved once, by the registry, and handed to
 * discovery (LLR-ROP-01). */
static const char *runtime_with(const char *dir, const char *exts)
{
	static char path[1024];

	snprintf(path, sizeof path, "%s/rt", dir);
	cr_assert_eq(mkdir(path, 0755), 0);
	put(path, "binary.exts", exts);
	return path;
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

	const char *rt = runtime_with(dir, ".png\n.zip\n");
	cr_assert_eq(binary_exts_load(rt, &exts), 0);

	cr_assert(is_excluded_extension("/a/b/logo.png", &exts));
	cr_assert(is_excluded_extension("archive.zip", &exts));
	cr_assert_not(is_excluded_extension("/a/b/main.c", &exts));

	binary_exts_free(&exts);
}

Test(discover, excluded_extension_ignores_case)
{
	ExtensionList exts;
	const char   *dir = tmptree();

	const char *rt = runtime_with(dir, ".png\n");
	cr_assert_eq(binary_exts_load(rt, &exts), 0);

	cr_assert(is_excluded_extension("LOGO.PNG", &exts),
	          "one entry must cover every spelling of the extension");

	binary_exts_free(&exts);
}

Test(discover, a_name_without_an_extension_is_not_excluded)
{
	ExtensionList exts;
	const char   *dir = tmptree();

	const char *rt = runtime_with(dir, ".png\n");
	cr_assert_eq(binary_exts_load(rt, &exts), 0);

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

	const char *rt = runtime_with(dir, "# a comment\n\n  .png\nzip\n");
	cr_assert_eq(binary_exts_load(rt, &exts), 0);

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

	/* `dir` holds no binary.exts. */
	cr_assert_eq(binary_exts_load(dir, &exts), 0,
	             "discovery continues without an exclusion list");
	cr_assert_eq(exts.count, 0);

	binary_exts_free(&exts);
}

/* ----------------------------------------------- discover_targets --------- */

Test(discover, a_regular_file_target_is_appended_directly)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        target[1024];

	const char *rt = runtime_with(dir, "");
	put(dir, "a.c", "one\ntwo\n");
	snprintf(target, sizeof target, "%s/a.c", dir);

	const char *targets[] = { target };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 1);
	cr_assert_eq(failures, 0);
	cr_assert(list_has_suffix(&list, "/a.c"));

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, a_missing_target_is_rejected_with_an_empty_list)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        target[1024];

	const char *rt = runtime_with(dir, "");
	snprintf(target, sizeof target, "%s/absent.c", dir);

	const char *targets[] = { target };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_neq(discover_targets(&opts, rt, &list, &routes, &failures), 0,
	              "a target that does not exist ends the run (HLR-062)");
	cr_assert_eq(list.count, 0, "no partial file list survives rejection");

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, a_target_that_is_neither_file_nor_directory_is_rejected)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        target[1024];

	const char *rt = runtime_with(dir, "");
	snprintf(target, sizeof target, "%s/pipe", dir);
	cr_assert_eq(mkfifo(target, 0644), 0, "could not create a FIFO");

	const char *targets[] = { target };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_neq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 0);

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, every_target_is_validated_before_any_is_walked)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        good[1024], bad[1024];

	const char *rt = runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(good, sizeof good, "%s/tree", dir);
	put(good, "a.c", "one\n");
	snprintf(bad, sizeof bad, "%s/absent", dir);

	const char *targets[] = { good, bad };
	ElcOptions  opts      = options_for(targets, 2);

	cr_assert_neq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 0,
	             "a valid target must not be walked when a later one is "
	             "invalid (HLR-062)");

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, a_file_reached_through_two_targets_appears_once)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        tree[1024], file[1024];

	const char *rt = runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "a.c", "one\n");
	snprintf(file, sizeof file, "%s/a.c", tree);

	const char *targets[] = { file, tree };
	ElcOptions  opts      = options_for(targets, 2);

	cr_assert_eq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 1,
	             "canonicalisation before de-duplication is what makes "
	             "`elc a.c src/` count a.c once (HLR-072)");

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, the_file_list_is_sorted_into_byte_order)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        tree[1024];

	const char *rt = runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "z.c", "one\n");
	put(tree, "a.c", "one\n");
	put(tree, "m.c", "one\n");

	const char *targets[] = { tree };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 3);
	for (size_t i = 1; i < list.count; i++)
		cr_assert(strcmp(list.paths[i - 1], list.paths[i]) < 0,
		          "the list must not depend on enumeration order "
		          "(HLR-033)");

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, hidden_entries_and_binary_extensions_are_excluded)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        tree[1024], hidden[1024];

	const char *rt = runtime_with(dir, ".png\n");
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

	cr_assert_eq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 1, "only a.c survives filtering");
	cr_assert(list_has_suffix(&list, "/a.c"));

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, the_walk_descends_into_subdirectories)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        tree[1024], deep[1024];

	const char *rt = runtime_with(dir, "");
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

	cr_assert_eq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 3, "the traversal is recursive");
	cr_assert(list_has_suffix(&list, "/sub/deeper/bottom.c"));

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, a_hidden_directory_named_as_the_target_is_traversed)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        hidden[1024];

	const char *rt = runtime_with(dir, "");
	subdir(dir, ".config");
	snprintf(hidden, sizeof hidden, "%s/.config", dir);
	put(hidden, "a.c", "one\n");

	const char *targets[] = { hidden };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 1,
	             "the hidden-entry exclusion applies below the target, not "
	             "to the target itself, which was named explicitly");

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, files_and_directories_are_classified_independently)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        tree[1024], file[1024];

	const char *rt = runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "walked.c", "one\n");
	put(dir, "named.c", "one\n");
	snprintf(file, sizeof file, "%s/named.c", dir);

	const char *targets[] = { file, tree };
	ElcOptions  opts      = options_for(targets, 2);

	cr_assert_eq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 2,
	             "each target is routed on its own type, so files and "
	             "directories may be intermixed");
	cr_assert(list_has_suffix(&list, "/named.c"));
	cr_assert(list_has_suffix(&list, "/walked.c"));

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, a_cyclic_directory_symlink_terminates)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        tree[1024], loop[1024];

	const char *rt = runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "a.c", "one\n");
	snprintf(loop, sizeof loop, "%s/loop", tree);
	cr_assert_eq(symlink(tree, loop), 0, "could not create the cycle");

	const char *targets[] = { tree };
	ElcOptions  opts      = options_for(targets, 1);

	/* Reaching this assertion at all is the result: a logical walk would
	 * not return (HLR-069, LLR-FTS-04). */
	cr_assert_eq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 1, "the linked directory is not descended");

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, a_symbolic_link_named_as_a_target_is_resolved)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        real[1024], link[1024];

	const char *rt = runtime_with(dir, "");
	put(dir, "a.c", "one\n");
	snprintf(real, sizeof real, "%s/a.c", dir);
	snprintf(link, sizeof link, "%s/link.c", dir);
	cr_assert_eq(symlink(real, link), 0);

	const char *targets[] = { link };
	ElcOptions  opts      = options_for(targets, 1);

	cr_assert_eq(discover_targets(&opts, rt, &list, &routes, &failures), 0);
	cr_assert_eq(list.count, 1);
	cr_assert(list_has_suffix(&list, "/a.c"),
	          "a link named explicitly identifies its referent (HLR-069)");

	filelist_free(&list);
	routelist_free(&routes);
}

Test(discover, a_path_that_cannot_be_canonicalised_is_a_per_file_failure)
{
	const char *dir = tmptree();
	FileList    list;
	RouteList   routes;
	size_t      failures = 0;
	char        tree[1024];

	const char *rt = runtime_with(dir, "");
	subdir(dir, "tree");
	snprintf(tree, sizeof tree, "%s/tree", dir);
	put(tree, "a.c", "one\n");

	const char *targets[] = { tree };
	ElcOptions  opts      = options_for(targets, 1);

	realpath_should_fail = 1;
	int rc = discover_targets(&opts, rt, &list, &routes, &failures);
	realpath_should_fail = 0;

	cr_assert_eq(rc, 0, "the run continues past a per-file failure");
	cr_assert_eq(list.count, 0);
	cr_assert_geq(failures, 1,
	              "the failure is recorded so the exit status reflects it");

	filelist_free(&list);
	routelist_free(&routes);
}

/* ------------------------------------------------ walk_git_tree ---------- */

/* Initialise a repository at <dir>/<name> and return its path. Nothing is
 * committed: the caller populates the tree and calls commit_all().
 *
 * Shelling out to git rather than constructing the objects with libgit2 is
 * deliberate: the point of these tests is that elc agrees with *git* about
 * what is tracked, and a repository built with the same library elc reads it
 * with could agree by construction. The identity is pinned and signing
 * disabled for the reasons set out in test/fixtures/repo/build.sh.
 */
static const char *repo_at(const char *dir, const char *name)
{
	static char path[1024];
	char        command[2048];

	snprintf(path, sizeof path, "%s/%s", dir, name);
	cr_assert_eq(mkdir(path, 0755), 0, "could not create %s", path);
	snprintf(command, sizeof command,
	         "git -c init.defaultBranch=main init -q '%s'", path);
	cr_assert_eq(system(command), 0, "could not initialise a repository");
	return path;
}

/* Commit the named paths, which are relative to the repository root. */
static void commit(const char *repo, const char *paths)
{
	char command[4096];

	snprintf(command, sizeof command,
	         "cd '%s' && git -c user.name=t -c user.email=t@invalid "
	         "-c commit.gpgsign=false add %s && "
	         "git -c user.name=t -c user.email=t@invalid "
	         "-c commit.gpgsign=false commit -q -m t",
	         repo, paths);
	cr_assert_eq(system(command), 0, "could not commit %s", paths);
}

/* libgit2 is initialised per test rather than per process: Criterion gives
 * each test its own process, so there is no global to share, and the refcount
 * makes the pairing cheap. discover_targets does the same once for a whole
 * run (LLR-DSC-05). */
static void git_up(void)   { cr_assert_geq(git_libgit2_init(), 1); }
static void git_down(void) { git_libgit2_shutdown(); }

Test(discover, git_walk_yields_the_tracked_files)
{
	const char   *dir = tmptree();
	ExtensionList exts;
	FileList      list     = { 0 };
	size_t        failures = 0;

	const char *rt = runtime_with(dir, "");
	cr_assert_eq(binary_exts_load(rt, &exts), 0);

	const char *repo = repo_at(dir, "r");
	put(repo, "a.c", "one\n");
	put(repo, "b.c", "two\n");
	commit(repo, "a.c");

	git_up();
	long appended = walk_git_tree(repo, &exts, &list, &failures);
	git_down();

	cr_assert_eq(appended, 1, "only the committed file is enumerated");
	cr_assert_eq(list.count, 1);
	cr_assert(list_has_suffix(&list, "/a.c"));
	cr_assert_not(list_has_suffix(&list, "/b.c"),
	              "an untracked file is not in the tree at HEAD (HLR-003)");

	filelist_free(&list);
	binary_exts_free(&exts);
}

Test(discover, git_walk_is_scoped_to_the_target)
{
	const char   *dir = tmptree();
	ExtensionList exts;
	FileList      list     = { 0 };
	size_t        failures = 0;
	char          scope[1024];

	const char *rt = runtime_with(dir, "");
	cr_assert_eq(binary_exts_load(rt, &exts), 0);

	const char *repo = repo_at(dir, "r");
	subdir(repo, "src");
	subdir(repo, "docs");
	char sub[1024];
	snprintf(sub, sizeof sub, "%s/src", repo);
	put(sub, "a.c", "one\n");
	snprintf(sub, sizeof sub, "%s/docs", repo);
	put(sub, "d.c", "two\n");

	commit(repo, "-A");

	snprintf(scope, sizeof scope, "%s/src", repo);

	git_up();
	long appended = walk_git_tree(scope, &exts, &list, &failures);
	git_down();

	cr_assert_eq(appended, 1,
	             "the tree at HEAD is the whole repository; naming a "
	             "subdirectory must analyse that subdirectory alone "
	             "(HLR-126)");
	cr_assert(list_has_suffix(&list, "/src/a.c"));
	cr_assert_not(list_has_suffix(&list, "/docs/d.c"));

	filelist_free(&list);
	binary_exts_free(&exts);
}

Test(discover, git_walk_declines_a_directory_in_no_repository)
{
	const char   *dir = tmptree();
	ExtensionList exts;
	FileList      list     = { 0 };
	size_t        failures = 0;

	const char *rt = runtime_with(dir, "");
	cr_assert_eq(binary_exts_load(rt, &exts), 0);

	subdir(dir, "plain");
	char plain[1024];
	snprintf(plain, sizeof plain, "%s/plain", dir);
	put(plain, "a.c", "one\n");

	git_up();
	long appended = walk_git_tree(plain, &exts, &list, &failures);
	git_down();

	cr_assert_lt(appended, 0,
	             "a negative return is inapplicability, which is what "
	             "makes the caller fall back rather than fail (HLR-004)");
	cr_assert_eq(list.count, 0, "nothing is appended when the route declines");
	cr_assert_eq(failures, 0, "declining is not a failure");

	filelist_free(&list);
	binary_exts_free(&exts);
}

Test(discover, git_walk_declines_a_target_the_repository_does_not_track)
{
	const char   *dir = tmptree();
	ExtensionList exts;
	FileList      list     = { 0 };
	size_t        failures = 0;
	char          untracked[1024];

	const char *rt = runtime_with(dir, "");
	cr_assert_eq(binary_exts_load(rt, &exts), 0);

	const char *repo = repo_at(dir, "r");
	put(repo, "a.c", "one\n");
	commit(repo, "a.c");

	/* A directory inside the work tree that HEAD knows nothing about —
	 * the build directory, and the version-controlled home directory,
	 * both reduced to their essential shape (HLR-002). */
	subdir(repo, "build");
	snprintf(untracked, sizeof untracked, "%s/build", repo);
	put(untracked, "gen.c", "generated\n");

	git_up();
	long appended = walk_git_tree(untracked, &exts, &list, &failures);
	git_down();

	cr_assert_lt(appended, 0,
	             "an enclosing repository that does not track the target "
	             "is disregarded (HLR-002)");
	cr_assert_eq(list.count, 0);

	filelist_free(&list);
	binary_exts_free(&exts);
}

Test(discover, a_tracked_directory_of_excluded_files_stays_on_the_git_route)
{
	const char   *dir = tmptree();
	ExtensionList exts;
	FileList      list     = { 0 };
	size_t        failures = 0;

	const char *rt = runtime_with(dir, ".png\n");
	cr_assert_eq(binary_exts_load(rt, &exts), 0);

	const char *repo = repo_at(dir, "r");
	put(repo, "logo.png", "excluded by extension\n");
	commit(repo, "logo.png");
	put(repo, "untracked.c", "int u(void) { return 0; }\n");

	git_up();
	long appended = walk_git_tree(repo, &exts, &list, &failures);
	git_down();

	/* Applicability is about what the repository tracks, not about what
	 * survived the exclusions. Deciding it on the latter would send this
	 * target to the filesystem walk, which would then analyse the
	 * untracked file the repository route exists to keep out. */
	cr_assert_eq(appended, 0,
	             "an applicable repository may yield nothing (HLR-002)");
	cr_assert_eq(list.count, 0);

	filelist_free(&list);
	binary_exts_free(&exts);
}

/* ----------------------------------------------------- RouteList --------- */

Test(discover, a_route_is_recorded_for_each_target)
{
	RouteList routes = { 0 };

	cr_assert_eq(routelist_add(&routes, "/a", ROUTE_REPOSITORY), 0);
	cr_assert_eq(routelist_add(&routes, "/b", ROUTE_FILESYSTEM), 0);

	cr_assert_eq(routes.count, 2);

	/* Naming one target twice records it once: the second row would
	 * carry the same route and make one run look like two. */
	cr_assert_eq(routelist_add(&routes, "/a", ROUTE_REPOSITORY), 0);
	cr_assert_eq(routes.count, 2, "a repeated target is recorded once");

	cr_assert_str_eq(routes.items[0].target, "/a");
	cr_assert_eq(routes.items[0].route, ROUTE_REPOSITORY);
	cr_assert_str_eq(routes.items[1].target, "/b");
	cr_assert_eq(routes.items[1].route, ROUTE_FILESYSTEM);

	routelist_free(&routes);
	cr_assert_eq(routes.count, 0, "a released list is left usable, not stale");
}

Test(discover, the_route_list_owns_the_target_it_records)
{
	RouteList routes = { 0 };
	char      target[16];

	strcpy(target, "/a");
	cr_assert_eq(routelist_add(&routes, target, ROUTE_REPOSITORY), 0);
	strcpy(target, "/zzzz");

	cr_assert_str_eq(routes.items[0].target, "/a",
	                 "the record must not alias the caller's buffer: the "
	                 "report outlives discovery");

	routelist_free(&routes);
}

Test(discover, filelist_free_is_safe_on_null)
{
	filelist_free(NULL);
	binary_exts_free(NULL);
	routelist_free(NULL);
	cr_assert(1, "releasing a null collection must not fault");
}
