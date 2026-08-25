/* dwarfline.c — the source lines a build compiled, read from the image.
 *
 * The finer of the two granularities a linked image answers at (doc/SDD.md
 * §18). `elfsyms.c` reads the symbol table and says which functions the link
 * kept; this reads the debug line information, where the build wrote any, and
 * says which lines inside a kept function the compiler emitted (HLR-153).
 *
 * **Two rules govern everything here, and both are about what absence means.**
 *
 * A line the mapping does not name produced no instruction — *in a file the
 * mapping describes*. In a file it never described, absence is evidence of
 * nothing at all, and treating it as evidence would silently delete measured
 * code. So coverage is established per file first, and only then is a line
 * within it judged (HLR-154). That is the same asymmetry HLR-133 draws for a
 * conditional region elc could not decide and HLR-138 for a language with no
 * dead-code query, applied to a third kind of evidence.
 *
 * And an image with no line information at all is not a failure. HLR-141
 * forbids *requiring* debug information; its absence costs this granularity
 * and nothing else, so the set comes back empty and the run proceeds exactly
 * as it did before this module existed.
 */

#include <stdlib.h>
#include <string.h>

#include <dwarf.h>            /* DW_AT_* */
#include <elfutils/libdw.h>

#include "dwarfline.h"
#include "symname.h"

/* ------------------------------------------------------------- utilities -- */

static int dwarfline_grow(void **items, size_t *capacity, size_t item_size)
{
	size_t next   = *capacity ? *capacity * 2 : 16;
	void  *bigger = realloc(*items, next * item_size);

	if (!bigger)
		return -1;

	*items    = bigger;
	*capacity = next;
	return 0;
}

static int by_dwarf_line(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a;
	uint32_t y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

static int by_dwarf_path(const void *a, const void *b)
{
	const CoveredFile *x = a;
	const CoveredFile *y = b;

	return strcmp(x->path, y->path);
}

/* The length `out` would have with its last component removed, or `at`
 * unchanged where there is nothing to remove.
 *
 * A leading ".." on a relative path has nothing to cancel and is kept, as is a
 * ".." following another, so that two such paths still compare equal to each
 * other.
 */
static size_t without_last_component(const char *out, size_t at)
{
	size_t back = at;

	if (back > 0 && out[back - 1] == '/')
		back--;
	while (back > 0 && out[back - 1] != '/')
		back--;

	if (back == at ||
	    (at - back == 3 && memcmp(out + back, "..", 2) == 0))
		return at;

	return back;
}

/* Append one path component, resolving "." and ".." against what is already
 * written. Returns the new length.
 */
static size_t append_component(char *out, size_t at, const char *seg,
                               size_t len)
{
	/* "." contributes nothing. */
	if (len == 1 && seg[0] == '.')
		return at;

	/* ".." removes the previous component, where there is one to remove. */
	if (len == 2 && seg[0] == '.' && seg[1] == '.') {
		size_t back = without_last_component(out, at);

		if (back != at)
			return back;
	}

	memcpy(out + at, seg, len);
	return at + len;
}

/* ------------------------------------------------------- path normalising --
 *
 * A compiler records a file name that may be relative to the unit's
 * compilation directory, and may carry `.` and `..` components from however
 * the build invoked it. `elc`'s own paths are canonical and absolute, so the
 * two have to be brought to one form before they can be compared.
 *
 * **The normalisation is lexical, and that is deliberate.** `realpath(3)`
 * would resolve symbolic links and give a better answer for the unusual
 * build, at the cost of stat-ing every path the image happens to name —
 * headers under `/usr/include` among them — which is filesystem work on files
 * the user did not name, for a module whose whole contract is that it reads
 * the image and nothing else (HLR-141). Lexical normalisation keeps the
 * answer a property of the image's bytes.
 *
 * The cost is a build that reaches its sources through a symbolic link: the
 * two spellings do not meet, the file reports as uncovered, and nothing is
 * pruned in it. That is the safe direction — the count of HLR-155 says the
 * coverage was not established, and no measured line is deleted on evidence
 * that did not describe it (LLR-DWL-02).
 */
static char *normalised(const char *path)
{
	size_t len = strlen(path);
	char  *out = malloc(len + 1);
	size_t at  = 0;

	if (!out)
		return NULL;

	for (size_t i = 0; i < len; ) {
		size_t start;

		/* Collapse a run of separators to one. */
		if (path[i] == '/') {
			if (at == 0 || out[at - 1] != '/')
				out[at++] = '/';
			i++;
			continue;
		}

		start = i;
		while (i < len && path[i] != '/')
			i++;

		at = append_component(out, at, path + start, i - start);
	}

	/* A trailing separator is not part of a file's identity. */
	while (at > 1 && out[at - 1] == '/')
		at--;

	out[at] = '\0';
	return out;
}

/* The unit's file name made absolute against its compilation directory.
 *
 * `dwarf_linesrc` already joins a relative file name to its directory-table
 * entry, so what arrives here is absolute for most builds and relative to the
 * compilation directory for the rest — a build invoked as `cc -c src/a.c`
 * from the tree root being the ordinary case.
 */
static char *absolute(const char *src, const char *comp_dir)
{
	if (src[0] == '/' || !comp_dir || comp_dir[0] == '\0')
		return normalised(src);

	size_t dlen  = strlen(comp_dir);
	size_t slen  = strlen(src);
	char  *joined = malloc(dlen + slen + 2);
	char  *out;

	if (!joined)
		return NULL;

	memcpy(joined, comp_dir, dlen);
	joined[dlen] = '/';
	memcpy(joined + dlen + 1, src, slen + 1);

	out = normalised(joined);
	free(joined);
	return out;
}

/* ------------------------------------------------------------ collection -- */

/* The entry for this path, or a newly appended empty one.
 *
 * A linear search, because a compilation unit names a handful of files and the
 * set is sorted only once the whole read is over — sorting as it grew would
 * cost more than the search saves.
 */
static CoveredFile *file_for(LineCoverage *out, char *path)
{
	for (size_t i = 0; i < out->count; i++)
		if (strcmp(out->files[i].path, path) == 0) {
			free(path);
			return &out->files[i];
		}

	if (out->count == out->capacity &&
	    dwarfline_grow((void **)&out->files, &out->capacity,
	                   sizeof *out->files) != 0)
		return NULL;

	CoveredFile *file = &out->files[out->count];

	memset(file, 0, sizeof *file);
	file->path = path;
	out->count++;
	return file;
}

static int line_add(CoveredFile *file, uint32_t line)
{
	if (file->count == file->capacity &&
	    dwarfline_grow((void **)&file->lines, &file->capacity,
	                   sizeof *file->lines) != 0)
		return -1;

	file->lines[file->count++] = line;
	return 0;
}

/* Sort each file's lines and collapse the repeats.
 *
 * A line program names one line once per instruction sequence attributed to
 * it, so the raw list is long and heavily repeated. De-duplicating makes the
 * membership test a binary search and makes the set independent of how many
 * sequences the compiler emitted.
 */
static void compact(LineCoverage *out)
{
	for (size_t f = 0; f < out->count; f++) {
		CoveredFile *file = &out->files[f];
		size_t       kept = 0;

		if (file->count == 0)
			continue;

		qsort(file->lines, file->count, sizeof *file->lines, by_dwarf_line);
		for (size_t i = 0; i < file->count; i++)
			if (i == 0 || file->lines[i] != file->lines[kept - 1])
				file->lines[kept++] = file->lines[i];
		file->count = kept;
	}

	if (out->count > 1)
		qsort(out->files, out->count, sizeof *out->files, by_dwarf_path);
}

/* ------------------------------------------------------- function origins --
 *
 * Which source file the debug information places each function in, so that the
 * `--elf` filter can match a source function to an image symbol by name *and*
 * file rather than by name alone (HLR-193).
 *
 * Read from the subprogram DIEs rather than from the line table and a symbol
 * address. `DW_AT_decl_file` says where a function was *written*, which is the
 * question being asked; an address lookup would answer where its first
 * instruction ended up, which inlining and identical-code folding make a
 * different question with a plausible wrong answer.
 */
static int origin_grow(OriginMap *out)
{
	size_t          next   = out->capacity ? out->capacity * 2 : 64;
	FunctionOrigin *bigger = realloc(out->items, next * sizeof *bigger);

	if (!bigger)
		return -1;
	out->items    = bigger;
	out->capacity = next;
	return 0;
}

/* Record one definition, unless the same pair is already held.
 *
 * A linear scan while the map is unsorted, for the reason the covered-file
 * lookup is: sorting as it grows would cost more than the search saves, and
 * the map is sorted once at the end. The same pair arrives repeatedly —
 * a header included by twenty units names its inline functions twenty times —
 * and each is the same fact stated again.
 */
static int origin_add(OriginMap *out, const char *name, char *file)
{
	for (size_t i = 0; i < out->count; i++)
		if (strcmp(out->items[i].name, name) == 0 &&
		    strcmp(out->items[i].file, file) == 0) {
			free(file);
			return 0;
		}

	if (out->count == out->capacity && origin_grow(out) != 0) {
		free(file);
		return -1;
	}

	out->items[out->count].name = strdup(name);
	if (!out->items[out->count].name) {
		free(file);
		return -1;
	}
	out->items[out->count].file = file;
	out->count++;
	return 0;
}

typedef struct {
	OriginMap  *out;
	const char *comp_dir;
	int         status;
} OriginWalk;

static int on_subprogram(Dwarf_Die *die, void *arg)
{
	OriginWalk *walk = arg;
	const char *name = dwarf_diename(die);
	const char *decl;
	char       *path;
	char       *key;
	int         rc;

	/* A declaration with no code is not a definition, and the question is
	 * which file *defines* the function. `dwarf_getfuncs` reaches abstract
	 * instances and external declarations too, and either would place a
	 * name in a file that merely mentioned it. */
	if (!name || !dwarf_hasattr(die, DW_AT_low_pc))
		return DWARF_CB_OK;

	decl = dwarf_decl_file(die);
	if (!decl)
		return DWARF_CB_OK;

	/* Keyed on the reduced form, because that is the form the source side
	 * is compared in. DWARF records a template instantiation under its
	 * instantiated name — `serialize_seq<int>` — while the source declares
	 * `serialize_seq`, and a map keyed on the raw spelling answers "no
	 * such function" for one the image describes completely (HLR-200). */
	key = symname_reduce(name);
	if (!key)
		return DWARF_CB_OK;

	path = absolute(decl, walk->comp_dir);
	if (!path) {
		free(key);
		walk->status = -1;
		return DWARF_CB_ABORT;
	}

	/* `origin_add` takes the path and copies the key, so the key is ours
	 * to release either way. */
	rc = origin_add(walk->out, key, path);
	free(key);
	if (rc != 0) {
		walk->status = -1;
		return DWARF_CB_ABORT;
	}

	return DWARF_CB_OK;
}

/* One compilation unit's subprograms into the origin map. */
static int unit_origins(Dwarf_Die *cu, OriginMap *out)
{
	Dwarf_Attribute attr;
	OriginWalk      walk = { out, NULL, 0 };

	if (dwarf_attr(cu, DW_AT_comp_dir, &attr))
		walk.comp_dir = dwarf_formstring(&attr);

	out->present = true;
	dwarf_getfuncs(cu, on_subprogram, &walk, 0);
	return walk.status;
}

/* By name, then by file: the name is what a lookup binary-searches on, and the
 * file orders the run of definitions sharing one name so that two runs over
 * one image produce the same map (HLR-032). */
static int by_origin_name(const void *a, const void *b)
{
	const FunctionOrigin *x = a;
	const FunctionOrigin *y = b;
	int                   c = strcmp(x->name, y->name);

	return c ? c : strcmp(x->file, y->file);
}

/* The first entry bearing this name, or the map's end. */
static size_t origin_lower_bound(const OriginMap *origins, const char *name)
{
	size_t low  = 0;
	size_t high = origins->count;

	while (low < high) {
		size_t mid = low + (high - low) / 2;

		if (strcmp(origins->items[mid].name, name) < 0)
			low = mid + 1;
		else
			high = mid;
	}
	return low;
}

bool dwarfline_any(const OriginMap *origins)
{
	return origins && origins->count > 0;
}

bool dwarfline_knows(const OriginMap *origins, const char *function)
{
	size_t at;
	char  *key;
	bool   hit;

	if (!origins || !function || origins->count == 0)
		return false;

	/* Reduced on the way in, because the map is keyed reduced. The source
	 * side reaches here as the declarator `elc` parsed — `serialize_seq`,
	 * or `S::method` — and the map holds what DWARF recorded (HLR-200). */
	key = symname_reduce(function);
	if (!key)
		return false;

	at  = origin_lower_bound(origins, key);
	hit = at < origins->count &&
	      strcmp(origins->items[at].name, key) == 0;
	free(key);
	return hit;
}

bool dwarfline_places(const OriginMap *origins, const char *function,
                      const char *file)
{
	char *key;
	bool  hit = false;

	if (!origins || !function || !file || origins->count == 0)
		return false;

	key = symname_reduce(function);
	if (!key)
		return false;

	for (size_t i = origin_lower_bound(origins, key);
	     i < origins->count &&
	     strcmp(origins->items[i].name, key) == 0; i++)
		if (strcmp(origins->items[i].file, file) == 0) {
			hit = true;
			break;
		}

	free(key);
	return hit;
}

void originmap_free(OriginMap *origins)
{
	if (!origins)
		return;

	for (size_t i = 0; i < origins->count; i++) {
		free(origins->items[i].name);
		free(origins->items[i].file);
	}
	free(origins->items);
	memset(origins, 0, sizeof *origins);
}

/* One compilation unit's line table into the coverage set. */
static int unit_lines(Dwarf_Die *cu, LineCoverage *out)
{
	Dwarf_Lines *lines = NULL;
	size_t       count = 0;
	Dwarf_Attribute attr;
	const char  *comp_dir = NULL;

	/* A unit with no line programme contributes nothing and is not an
	 * error: a translation unit compiled without debug information is
	 * ordinary, and its files stay uncovered (HLR-154). */
	if (dwarf_getsrclines(cu, &lines, &count) != 0 || count == 0)
		return 0;

	if (dwarf_attr(cu, DW_AT_comp_dir, &attr))
		comp_dir = dwarf_formstring(&attr);

	/* Reaching the unit at all means the image described *something*, so
	 * the distinction between "no debug information" and "debug
	 * information about other code" is recorded here rather than inferred
	 * from an empty set later. */
	out->present = true;

	for (size_t i = 0; i < count; i++) {
		Dwarf_Line *line = dwarf_onesrcline(lines, i);
		const char *src;
		int         number = 0;
		char       *path;
		CoveredFile *file;

		if (!line)
			continue;

		/* The end-of-sequence marker carries the address one past the
		 * last instruction and names no line of source. Counting it
		 * would mark a line compiled on the strength of a marker
		 * rather than of an instruction. */
		bool end = false;

		if (dwarf_lineendsequence(line, &end) != 0 || end)
			continue;

		src = dwarf_linesrc(line, NULL, NULL);
		if (!src || dwarf_lineno(line, &number) != 0 || number <= 0)
			continue;

		path = absolute(src, comp_dir);
		if (!path)
			return -1;

		file = file_for(out, path);
		if (!file) {
			free(path);
			return -1;
		}

		if (line_add(file, (uint32_t)number) != 0)
			return -1;
	}

	return 0;
}

int dwarfline_read(void *elf, LineCoverage *out, OriginMap *origins)
{
	Dwarf     *dw;
	Dwarf_Off  offset      = 0;
	Dwarf_Off  next        = 0;
	size_t     header_size = 0;
	int        status      = -1;

	memset(out, 0, sizeof *out);
	if (origins)
		memset(origins, 0, sizeof *origins);

	if (!elf)
		return 0;

	/* `dwarf_begin_elf` reads the sections of the descriptor it is given
	 * and nothing else. The `Dwfl` interface above it is the one that
	 * would follow a `.gnu_debuglink` or a build-id into a separate-debug
	 * directory, and it is not used here for exactly that reason
	 * (HLR-141, LLR-DWL-01). */
	dw = dwarf_begin_elf((Elf *)elf, DWARF_C_READ, NULL);
	if (!dw)
		return 0;   /* no debug information; not a failure */

	while (dwarf_nextcu(dw, offset, &next, &header_size, NULL, NULL,
	                    NULL) == 0) {
		Dwarf_Die  storage;
		Dwarf_Die *cu = dwarf_offdie(dw, offset + header_size, &storage);

		if (cu && unit_lines(cu, out) != 0)
			goto cleanup;
		if (cu && origins && unit_origins(cu, origins) != 0)
			goto cleanup;

		offset = next;
	}

	compact(out);
	if (origins && origins->count > 1)
		qsort(origins->items, origins->count, sizeof *origins->items,
		      by_origin_name);
	status = 0;

cleanup:
	dwarf_end(dw);
	if (status != 0) {
		dwarfline_free(out);
		originmap_free(origins);
	}
	return status;
}

/* ---------------------------------------------------------------- queries -- */

static const CoveredFile *lookup(const LineCoverage *coverage, const char *path)
{
	size_t low  = 0;
	size_t high = coverage->count;

	while (low < high) {
		size_t mid = low + (high - low) / 2;
		int    cmp = strcmp(coverage->files[mid].path, path);

		if (cmp == 0)
			return &coverage->files[mid];
		if (cmp < 0)
			low = mid + 1;
		else
			high = mid;
	}

	return NULL;
}

bool dwarfline_covers(const LineCoverage *coverage, const char *path)
{
	if (!coverage || !path || coverage->count == 0)
		return false;

	return lookup(coverage, path) != NULL;
}

bool dwarfline_compiled(const LineCoverage *coverage, const char *path,
                        uint32_t line)
{
	const CoveredFile *file;
	size_t             low;
	size_t             high;

	if (!coverage || !path)
		return false;

	file = lookup(coverage, path);
	if (!file)
		return false;   /* uncovered; the caller must have asked first */

	low  = 0;
	high = file->count;
	while (low < high) {
		size_t mid = low + (high - low) / 2;

		if (file->lines[mid] == line)
			return true;
		if (file->lines[mid] < line)
			low = mid + 1;
		else
			high = mid;
	}

	return false;
}

void dwarfline_free(LineCoverage *coverage)
{
	if (!coverage)
		return;

	for (size_t i = 0; i < coverage->count; i++) {
		free(coverage->files[i].path);
		free(coverage->files[i].lines);
	}
	free(coverage->files);
	memset(coverage, 0, sizeof *coverage);
}
