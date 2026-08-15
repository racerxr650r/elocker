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
 * Phase 1 routes every directory target to the filesystem walk. The Git
 * route — repository detection, applicability, and tracked-blob enumeration
 * (LLR-DSC-05, LLR-GIT-01 – LLR-GIT-04) — arrives in Phase 7.
 */

#include <errno.h>
#include <fts.h>
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
static int grow(void **items, size_t *capacity, size_t item_size)
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
	    grow((void **)&list->paths, &list->capacity, sizeof *list->paths) != 0) {
		fprintf(stderr, "elc: out of memory recording %s\n", canonical);
		free(canonical);
		return -1;
	}

	list->paths[list->count++] = canonical;
	return 0;
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

/* Resolve the runtime location: the environment variable when set, otherwise
 * the runtime directory adjacent to the executable (HLR-059).
 *
 * Phase 2's registry_open() owns this resolution for language modules; Phase
 * 1 needs only binary.exts and there is no registry yet, so the resolution
 * lives here until the registry can supply it (doc/notes.md §3).
 */
static int runtime_dir(char *buf, size_t len)
{
	const char *env = getenv(ELC_RUNTIME_DIR_ENV);

	if (env && *env) {
		int n = snprintf(buf, len, "%s", env);
		return (n < 0 || (size_t)n >= len) ? -1 : 0;
	}

	char    exe[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);

	if (n < 0)
		return -1;
	exe[n] = '\0';

	char *slash = strrchr(exe, '/');
	if (!slash)
		return -1;
	*slash = '\0';

	int m = snprintf(buf, len, "%s/runtime", exe);
	return (m < 0 || (size_t)m >= len) ? -1 : 0;
}

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
	    grow((void **)&list->exts, &list->capacity, sizeof *list->exts) != 0) {
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

int binary_exts_load(ExtensionList *out)
{
	char   dir[PATH_MAX];
	char   path[PATH_MAX];
	FILE  *fp     = NULL;
	char  *line   = NULL;
	size_t cap    = 0;
	int    status = 0;

	memset(out, 0, sizeof *out);

	if (runtime_dir(dir, sizeof dir) != 0) {
		fputs("elc: cannot locate the runtime directory; "
		      "no extension is excluded\n", stderr);
		return 0;
	}

	int n = snprintf(path, sizeof path, "%s/binary.exts", dir);
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

int discover_targets(const ElcOptions *opts, FileList *out, size_t *failures)
{
	ExtensionList  exts;
	unsigned char *kind   = NULL;
	int            status = 0;

	memset(out, 0, sizeof *out);
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

	if (binary_exts_load(&exts) != 0) {
		status = -1;
		goto cleanup;
	}

	for (size_t i = 0; i < opts->target_count; i++) {
		if (kind[i] == TARGET_FILE) {
			/* A regular file is appended directly; no traversal is
			 * performed for it (HLR-001, LLR-DSC-04). */
			if (filelist_add(out, opts->targets[i]) != 0)
				(*failures)++;
		} else if (walk_filesystem(opts->targets[i], &exts, out,
		                           failures) != 0) {
			(*failures)++;
		}
	}

	filelist_sort_unique(out);

cleanup:
	binary_exts_free(&exts);
	free(kind);
	if (status != 0)
		filelist_free(out);
	return status;
}
