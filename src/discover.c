/* discover.c — target validation, classification, and file discovery.
 *
 * Produces the de-duplicated, stably ordered file list every later stage
 * consumes (doc/SDD.md §5). Reads directory structure and file metadata
 * only: no source file is ever opened here for its contents.
 *
 * Two calls that look inconsistent are deliberately paired (HLR-069):
 * targets are classified with stat(2), which *follows* a symbolic link,
 * because a link named on the command line identifies its referent; the walk
 * uses FTS_PHYSICAL, which does not, because a cyclic directory link would
 * otherwise be traversed for ever. Changing either to match the other breaks
 * one half of the requirement.
 *
 * A directory target is offered to Git first and traversed from the
 * filesystem only when that does not apply. Enumerating what the repository
 * tracks yields `.gitignore` compliance for free: an ignored or untracked
 * path is simply absent from the tree at HEAD, with no exclusion list to
 * maintain (HLR-003).
 *
 * **Finding a repository is not sufficient reason to use one.**
 * `git_repository_open_ext` searches ancestors, so a target inside a
 * `.gitignore`d build directory, or anywhere at all beneath a
 * version-controlled home directory, finds a repository that tracks nothing
 * there. Enumerating it would report zero files for a directory full of
 * source — consistent with HLR-066 and thoroughly baffling. The applicability
 * test converts both into a filesystem traversal, which is what the user
 * meant (LLR-GIT-04).
 */

#include <errno.h>
#include <fts.h>
#include <git2.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "discover.h"
#include "elc.h"

/* What validation decided a target is, recorded once so that the walk does
 * not re-stat a path the validation pass already classified. */
enum { TARGET_FILE, TARGET_DIR };

/* --------------------------------------------------------------- growth ---
 *
 * Every dynamic array here doubles, and the realloc result goes into a
 * temporary that is checked before the original is overwritten. The
 * shorthand `x = realloc(x, n)` loses the allocation on failure and leaves a
 * dangling pointer, which is an HLR-125 violation.
 */
static int discover_grow(void **items, size_t *capacity, size_t item_size)
{
	size_t next = *capacity ? *capacity * 2 : 16;
	void  *bigger = realloc(*items, next * item_size);

	if (!bigger)
		return -1;

	*items    = bigger;
	*capacity = next;
	return 0;
}

/* ------------------------------------------------------------ file list ---
 *
 * Paths are canonicalised on the way in rather than on the way out, because
 * de-duplication is only meaningful over canonical paths: `elc a.c src/` must
 * count `a.c` once, and the two routes reach it by different spellings
 * (HLR-072, LLR-DSC-07).
 */
static int filelist_add(FileList *list, const char *path)
{
	char *canonical = realpath(path, NULL);

	if (!canonical) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		return -1;
	}

	if (list->count == list->capacity &&
	    discover_grow((void **)&list->paths, &list->capacity, sizeof *list->paths) != 0) {
		fprintf(stderr, "elc: out of memory recording %s\n", canonical);
		free(canonical);
		return -1;
	}

	list->paths[list->count++] = canonical;
	return 0;
}

int routelist_add(RouteList *list, const char *target, DiscoveryRoute route)
{
	char *owned;

	/* One record per directory, not one per argument. Two spellings of the
	 * same directory reach the same route by definition, so a second row
	 * would say nothing and would make `elc . "$PWD"` look like a run over
	 * two targets. The caller canonicalises before recording, which is
	 * what makes the comparison a comparison of directories rather than
	 * of strings. */
	for (size_t i = 0; i < list->count; i++)
		if (strcmp(list->items[i].target, target) == 0)
			return 0;

	owned = strdup(target);
	if (!owned)
		return -1;

	if (list->count == list->capacity &&
	    discover_grow((void **)&list->items, &list->capacity, sizeof *list->items) != 0) {
		free(owned);
		return -1;
	}

	list->items[list->count].target = owned;
	list->items[list->count].route  = route;
	list->count++;
	return 0;
}

void routelist_free(RouteList *list)
{
	if (!list)
		return;

	for (size_t i = 0; i < list->count; i++)
		free(list->items[i].target);
	free(list->items);
	list->items    = NULL;
	list->count    = 0;
	list->capacity = 0;
}

void filelist_free(FileList *list)
{
	if (!list)
		return;

	for (size_t i = 0; i < list->count; i++)
		free(list->paths[i]);
	free(list->paths);
	list->paths    = NULL;
	list->count    = 0;
	list->capacity = 0;
}

static int path_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Sort into byte order and collapse runs of equal paths.
 *
 * The sort is what makes the file list independent of the order fts(3)
 * happened to enumerate the tree in (HLR-033, LLR-DSC-08); collapsing the
 * runs it produces is what makes a file reached through two targets appear
 * once (HLR-072, LLR-DSC-07).
 */
static void filelist_sort_unique(FileList *list)
{
	if (list->count < 2)
		return;

	qsort(list->paths, list->count, sizeof *list->paths, path_cmp);

	size_t kept = 1;
	for (size_t i = 1; i < list->count; i++) {
		if (strcmp(list->paths[i], list->paths[kept - 1]) == 0)
			free(list->paths[i]);
		else
			list->paths[kept++] = list->paths[i];
	}
	list->count = kept;
}

/* ------------------------------------------------------ extension list ---- */

static int extlist_add(ExtensionList *list, const char *ext)
{
	char *copy;

	/* Accept an entry written with or without its leading dot, so the
	 * data file can be edited in either style without silently matching
	 * nothing. */
	if (ext[0] == '.') {
		copy = strdup(ext);
	} else {
		size_t n = strlen(ext);
		copy = malloc(n + 2);
		if (copy) {
			copy[0] = '.';
			memcpy(copy + 1, ext, n + 1);
		}
	}
	if (!copy)
		return -1;

	if (list->count == list->capacity &&
	    discover_grow((void **)&list->exts, &list->capacity, sizeof *list->exts) != 0) {
		free(copy);
		return -1;
	}

	list->exts[list->count++] = copy;
	return 0;
}

void binary_exts_free(ExtensionList *list)
{
	if (!list)
		return;

	for (size_t i = 0; i < list->count; i++)
		free(list->exts[i]);
	free(list->exts);
	list->exts     = NULL;
	list->count    = 0;
	list->capacity = 0;
}

int binary_exts_load(const char *runtime_dir, ExtensionList *out)
{
	char   path[PATH_MAX];
	FILE  *fp     = NULL;
	char  *line   = NULL;
	size_t cap    = 0;
	int    status = 0;

	memset(out, 0, sizeof *out);

	if (!runtime_dir) {
		fputs("elc: no runtime directory; "
		      "no extension is excluded\n", stderr);
		return 0;
	}

	int n = snprintf(path, sizeof path, "%s/binary.exts", runtime_dir);
	if (n < 0 || (size_t)n >= sizeof path) {
		fputs("elc: runtime directory path is too long; "
		      "no extension is excluded\n", stderr);
		return 0;
	}

	fp = fopen(path, "r");
	if (!fp) {
		/* Not fatal. Discovery still runs; nothing is excluded, and the
		 * user is told why rather than left with a report full of object
		 * files (LLR-EXT-02). */
		fprintf(stderr, "elc: %s: %s; no extension is excluded\n",
		        path, strerror(errno));
		return 0;
	}

	ssize_t len;
	while ((len = getline(&line, &cap, fp)) != -1) {
		char *p = line;

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == '\n' || *p == '\0')
			continue;

		char *end = p;
		while (*end && *end != ' ' && *end != '\t' && *end != '\n' &&
		       *end != '\r')
			end++;
		*end = '\0';

		if (*p && extlist_add(out, p) != 0) {
			fputs("elc: out of memory reading the binary-extension "
			      "list\n", stderr);
			status = -1;
			break;
		}
	}

	free(line);
	fclose(fp);

	if (status != 0)
		binary_exts_free(out);
	return status;
}

bool is_excluded_extension(const char *path, const ExtensionList *exts)
{
	if (!exts || exts->count == 0)
		return false;

	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;

	const char *dot = strrchr(base, '.');
	/* `dot == base` is a name that is nothing but a suffix — ".bashrc" —
	 * which has no extension to test. */
	if (!dot || dot == base || dot[1] == '\0')
		return false;

	for (size_t i = 0; i < exts->count; i++)
		if (strcasecmp(dot, exts->exts[i]) == 0)
			return true;

	return false;
}

/* ------------------------------------------------------- the Git route ---
 *
 * The filters applied here are the filesystem route's, plus one. Hidden
 * entries and binary extensions are excluded on both, because HLR-126 says a
 * directory target denotes the same set of files whichever route reaches it —
 * without that, analysing a repository would report `.gitignore` and every
 * workflow file, and the two routes would disagree about what a directory
 * *is*. What the Git route adds is exclusion by **content**: a tracked file
 * that git reports as binary is dropped whatever its extension says, which is
 * the exclusion no list can express (HLR-003, LLR-GIT-03).
 */

typedef struct {
	const char          *prefix;   /* repository-relative, "" for the root */
	size_t               prefix_len;
	const char          *workdir;  /* absolute, with a trailing slash      */
	const ExtensionList *exts;
	FileList            *out;
	size_t              *failures;
	git_repository      *repo;
	size_t               tracked;    /* blobs at or beneath the target     */
	size_t               appended;   /* of those, the ones to be analysed  */
} GitWalk;

/* True when any component of a repository-relative path is hidden. The
 * filesystem route skips a hidden directory and everything beneath it; the
 * tree walk sees full paths, so the whole path is what must be tested. */
static bool path_has_hidden_component(const char *path)
{
	for (const char *p = path; p && *p; ) {
		if (*p == '.')
			return true;
		p = strchr(p, '/');
		if (p)
			p++;
	}
	return false;
}

static int on_tree_entry(const char *root, const git_tree_entry *entry,
                         void *payload)
{
	GitWalk *walk = payload;
	char     path[PATH_MAX];
	char     full[PATH_MAX];

	if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB)
		return 0;   /* a tree: descend into it */

	int n = snprintf(path, sizeof path, "%s%s", root,
	                 git_tree_entry_name(entry));
	if (n < 0 || (size_t)n >= sizeof path)
		return 0;

	/* Scoping. The tree at HEAD is the whole repository, so without this
	 * `elc src/` analyses the entire project — and the two routes then
	 * disagree about what a directory target denotes (HLR-126). */
	if (walk->prefix_len && strncmp(path, walk->prefix, walk->prefix_len) != 0)
		return 0;

	/* Counted before the exclusions, and this is the distinction the
	 * applicability test turns on. "The repository tracks this directory"
	 * and "the repository yields a file worth analysing" are different
	 * questions, and answering the first with the second is wrong in a way
	 * that is easy to miss: a tracked directory holding only excluded
	 * files would be judged untracked, fall back to the filesystem walk,
	 * and analyse the untracked files the repository route exists to keep
	 * out. HLR-002 asks the first question, so that is the one counted. */
	walk->tracked++;

	if (path_has_hidden_component(path))
		return 0;
	if (is_excluded_extension(path, walk->exts))
		return 0;

	git_blob *blob = NULL;

	if (git_blob_lookup(&blob, walk->repo, git_tree_entry_id(entry)) != 0)
		return 0;

	int binary = git_blob_is_binary(blob);

	git_blob_free(blob);
	if (binary)
		return 0;

	n = snprintf(full, sizeof full, "%s%s", walk->workdir, path);
	if (n < 0 || (size_t)n >= sizeof full)
		return 0;

	if (filelist_add(walk->out, full) != 0)
		(*walk->failures)++;
	else
		walk->appended++;

	return 0;
}

long walk_git_tree(const char *target, const ExtensionList *exts,
                   FileList *out, size_t *failures)
{
	git_repository *repo     = NULL;
	git_object     *tree_obj = NULL;
	char            resolved[PATH_MAX];
	char            prefix[PATH_MAX];
	long            result   = -1;
	size_t          before   = out->count;

	if (git_repository_open_ext(&repo, target, 0, NULL) != 0)
		return -1;   /* no repository here or above: not an error */

	const char *wd = git_repository_workdir(repo);

	/* A bare repository has no working directory, so nothing on disk
	 * corresponds to its tree and the target cannot be inside it. */
	if (!wd)
		goto cleanup;

	/* The workdir libgit2 reports may reach the same place by a different
	 * spelling than the target does — a symbolic link on either path, or a
	 * linked worktree — so both are canonicalised and the prefix
	 * arithmetic below compares like with like. */
	char real_workdir[PATH_MAX];

	if (!realpath(target, resolved) || !realpath(wd, real_workdir))
		goto cleanup;

	size_t wd_len = strlen(real_workdir);

	/* A prefix test on strings is not a prefix test on paths: without the
	 * boundary check, a working tree at `/src/proj` would claim a target
	 * at `/src/project`, and the repository-relative path derived from it
	 * would be the tail `ect` — a path matching nothing, so the target
	 * would be reported as tracking nothing and quietly fall back. The
	 * upward search means the target is normally inside the tree anyway;
	 * this is for when it is not, which a linked worktree can arrange. */
	if (strncmp(resolved, real_workdir, wd_len) != 0 ||
	    (resolved[wd_len] != '/' && resolved[wd_len] != '\0'))
		goto cleanup;   /* the target is not inside this working tree */

	const char *relative = resolved + wd_len;

	while (*relative == '/')
		relative++;

	if (*relative == '\0') {
		prefix[0] = '\0';
	} else {
		int n = snprintf(prefix, sizeof prefix, "%s/", relative);
		if (n < 0 || (size_t)n >= sizeof prefix)
			goto cleanup;
	}

	if (git_revparse_single(&tree_obj, repo, "HEAD^{tree}") != 0)
		goto cleanup;   /* an unborn HEAD tracks nothing yet */

	/* realpath strips any trailing slash, so one is put back: the tree walk
	 * produces repository-relative paths and joining needs a separator. */
	char joined[PATH_MAX];

	int n = snprintf(joined, sizeof joined, "%s/", real_workdir);
	if (n < 0 || (size_t)n >= sizeof joined)
		goto cleanup;

	GitWalk walk = {
		.prefix     = prefix,
		.prefix_len = strlen(prefix),
		.workdir    = joined,
		.exts       = exts,
		.out        = out,
		.failures   = failures,
		.repo       = repo,
		.appended   = 0
	};

	if (git_tree_walk((const git_tree *)tree_obj, GIT_TREEWALK_PRE,
	                  on_tree_entry, &walk) != 0) {
		/* An abandoned walk leaves a partial enumeration behind, and
		 * the caller is about to run the filesystem walk over the same
		 * directory. Their union would be the tracked files found so
		 * far *plus* every untracked file — the one result neither
		 * route would produce. Discard what this walk added so the
		 * fallback starts from where it would have started. */
		while (out->count > before)
			free(out->paths[--out->count]);
		goto cleanup;
	}

	/* **Applicability is decided by the walk's result, not by a separate
	 * question asked first.** A repository that tracks nothing at or
	 * beneath the target is one the user did not mean — a `.gitignore`d
	 * build directory, or an unrelated repository several levels up.
	 * Reporting it inapplicable sends the caller to the filesystem, where
	 * the files actually are (LLR-GIT-04).
	 *
	 * The test is on what the repository *tracks*, not on what survived
	 * the exclusions: a tracked directory holding nothing but excluded
	 * files is still tracked, and must report zero rather than fall back
	 * and analyse the untracked files beside them. So an applicable
	 * repository may legitimately return 0. */
	result = walk.tracked > 0 ? (long)walk.appended : -1;

cleanup:
	if (tree_obj)
		git_object_free(tree_obj);
	git_repository_free(repo);
	return result;
}

/* -------------------------------------------------------- the walk ------- */

/* Hidden entries are excluded below the target, never at the target itself:
 * naming `.config/` on the command line is explicit, and the same reasoning
 * that resolves a symlinked target applies (HLR-005, HLR-039). */
static bool is_hidden(const FTSENT *ent)
{
	return ent->fts_level > 0 && ent->fts_name[0] == '.';
}

int walk_filesystem(const char *root, const ExtensionList *exts,
                    FileList *out, size_t *failures)
{
	char *const argv[] = { (char *)root, NULL };
	FTS        *fts;
	FTSENT     *ent;

	/* FTS_PHYSICAL, never FTS_LOGICAL: a directory reached through a
	 * symbolic link is not descended into, so a self-referential link
	 * cannot produce an unbounded walk (HLR-069, LLR-FTS-04).
	 * FTS_NOCHDIR keeps the process's working directory unchanged, so the
	 * relative paths the caller gave stay meaningful. */
	fts = fts_open(argv, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
	if (!fts) {
		fprintf(stderr, "elc: %s: %s\n", root, strerror(errno));
		return -1;
	}

	errno = 0;
	while ((ent = fts_read(fts)) != NULL) {
		switch (ent->fts_info) {
		case FTS_D:
			if (is_hidden(ent))
				fts_set(fts, ent, FTS_SKIP);
			break;

		case FTS_F:
			if (is_hidden(ent))
				break;
			if (is_excluded_extension(ent->fts_path, exts))
				break;
			if (filelist_add(out, ent->fts_path) != 0)
				(*failures)++;
			break;

		case FTS_SL:
		case FTS_SLNONE:
			/* Never followed during traversal. A link to a file
			 * inside the tree would double-count it; a link to a
			 * directory is the cyclic case above. A link named as a
			 * target is a different matter and is resolved by the
			 * classification pass (LLR-FTS-05, LLR-DSC-06). */
			break;

		case FTS_DNR:
		case FTS_ERR:
		case FTS_NS:
			/* Diagnose, skip that subtree, and carry on; the caller
			 * folds the count into the exit status (LLR-DSC-09). */
			fprintf(stderr, "elc: %s: %s\n", ent->fts_path,
			        strerror(ent->fts_errno));
			(*failures)++;
			break;

		default:
			break;
		}
		errno = 0;
	}

	if (errno != 0) {
		fprintf(stderr, "elc: %s: %s\n", root, strerror(errno));
		(*failures)++;
	}

	fts_close(fts);
	return 0;
}

/* ------------------------------------------------------ discover_targets -- */

int discover_targets(const ElcOptions *opts, const char *runtime_dir,
                     FileList *out, RouteList *routes, size_t *failures)
{
	ExtensionList  exts;
	unsigned char *kind   = NULL;
	int            status = 0;

	memset(out, 0, sizeof *out);
	memset(routes, 0, sizeof *routes);
	memset(&exts, 0, sizeof exts);
	*failures = 0;

	if (opts->target_count == 0)
		return 0;

	kind = malloc(opts->target_count);
	if (!kind) {
		fputs("elc: out of memory classifying targets\n", stderr);
		return -1;
	}

	/* Every target is validated before any of them is walked, so a report
	 * can never silently cover fewer targets than were named (HLR-062,
	 * LLR-DSC-01). stat(2) follows symbolic links, which is what resolves
	 * a link named directly as a target (LLR-DSC-06). */
	for (size_t i = 0; i < opts->target_count; i++) {
		struct stat st;

		if (stat(opts->targets[i], &st) != 0) {
			fprintf(stderr, "elc: %s: %s\n", opts->targets[i],
			        strerror(errno));
			status = -1;
			goto cleanup;
		}
		if (S_ISREG(st.st_mode)) {
			kind[i] = TARGET_FILE;
		} else if (S_ISDIR(st.st_mode)) {
			kind[i] = TARGET_DIR;
		} else {
			fprintf(stderr,
			        "elc: %s: not a regular file or directory\n",
			        opts->targets[i]);
			status = -1;
			goto cleanup;
		}

		/* A named target that cannot be read is rejected here rather
		 * than discovered halfway through the walk, because HLR-062
		 * admits no partial report. A file *within* a target that turns
		 * out to be unreadable is a different case: that is a per-file
		 * failure and the run continues (HLR-035). */
		if (access(opts->targets[i], R_OK) != 0) {
			fprintf(stderr, "elc: %s: %s\n", opts->targets[i],
			        strerror(errno));
			status = -1;
			goto cleanup;
		}
	}

	if (binary_exts_load(runtime_dir, &exts) != 0) {
		status = -1;
		goto cleanup;
	}

	/* One initialisation for the whole run, released once at the end.
	 * libgit2 refcounts both, so this is safe wherever it is called from —
	 * but per target it would be a needless pair of global operations. */
	git_libgit2_init();

	for (size_t i = 0; i < opts->target_count; i++) {
		if (kind[i] == TARGET_FILE) {
			/* A regular file is appended directly; no traversal is
			 * performed for it (HLR-001, LLR-DSC-04). */
			if (filelist_add(out, opts->targets[i]) != 0)
				(*failures)++;
			continue;
		}

		/* Git first, filesystem when it does not apply. A negative
		 * return is inapplicability rather than failure (LLR-DSC-05). */
		DiscoveryRoute route = ROUTE_REPOSITORY;

		if (walk_git_tree(opts->targets[i], &exts, out, failures) < 0) {
			route = ROUTE_FILESYSTEM;
			if (walk_filesystem(opts->targets[i], &exts, out,
			                    failures) != 0)
				(*failures)++;
		}

		/* Recorded canonical, as every other path in the report is. A
		 * target that cannot be canonicalised is recorded as it was
		 * given: the walk has already succeeded or failed on its own
		 * terms by this point, and a route the user cannot match to
		 * the argument they typed helps nobody. */
		char        canonical[PATH_MAX];
		const char *shown = realpath(opts->targets[i], canonical)
		                            ? canonical : opts->targets[i];

		if (routelist_add(routes, shown, route) != 0) {
			fputs("elc: out of memory recording a discovery route\n",
			      stderr);
			(*failures)++;
		}
	}

	git_libgit2_shutdown();

	filelist_sort_unique(out);

cleanup:
	binary_exts_free(&exts);
	free(kind);
	if (status != 0) {
		filelist_free(out);
		routelist_free(routes);
	}
	return status;
}
