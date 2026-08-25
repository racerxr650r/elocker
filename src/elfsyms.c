/* elfsyms.c — the function set of a linked image.
 *
 * Reads the function symbols an image defines and answers whether a source
 * function appears among them (doc/SDD.md §18).
 *
 * **The image is evidence, not configuration.** Conditional compilation
 * re-decides the conditions a build resolved, from definitions the user
 * restates; an image was produced by the real toolchain with the real flags
 * and says which functions survived. Neither subsumes the other, and this
 * module answers only the question the image can answer: which functions the
 * build kept. It says nothing about which lines inside one were compiled out.
 *
 * `libelf` supplies the container parsing, for the reason `igraph` supplies
 * the graph algorithms (HLR-113): ELF is a well-specified format with a mature
 * implementation, and hand-rolling one would put endianness, class, and
 * section-header handling into this project's defect surface for no benefit.
 * The demangler costs no library at all — `__cxa_demangle` is part of the C++
 * runtime, which is on the link line because `igraph` is partly C++ inside.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gelf.h>
#include <libelf.h>

#include "diag.h"
#include "elfsyms.h"
#include "symname.h"

/* The Itanium C++ ABI demangler, declared here rather than included from
 * <cxxabi.h>: that header is C++ and this translation unit is C. The symbol is
 * the whole of the dependency, and one declaration is less surface than a
 * compiler mode nothing else in the project uses (HLR-142). */
extern char *__cxa_demangle(const char *mangled, char *buffer, size_t *length,
                            int *status);


char *resolved_name(const char *linkage)
{
	if (!linkage || !*linkage)
		return NULL;

	/* The scheme is detected from the name rather than from a language the
	 * user states: an image does not say which compiler produced which
	 * symbol, and a mixed-language image is ordinary (LLR-SYM-02). */
	if (linkage[0] != '_' || (linkage[1] != 'Z' && linkage[1] != 'R'))
		/* C, and `extern "C"` alike: the linkage name is the source
		 * name, and returning it unchanged is the whole of the rule
		 * (LLR-SYM-01). */
		return strdup(linkage);

	int   status    = 0;
	char *demangled = __cxa_demangle(linkage, NULL, NULL, &status);

	/* A scheme with no decoder to hand resolves to nothing and is counted,
	 * so a language whose compiler uses one reports a large unresolved
	 * count rather than a filter built from guesses (LLR-SYM-04). Rust's
	 * v0 scheme lands here wherever the runtime is not new enough to
	 * decode it. */
	if (status != 0 || !demangled) {
		free(demangled);
		return NULL;
	}

	char *reduced = symname_reduce(demangled);

	free(demangled);
	return reduced;
}

/* ------------------------------------------------------------ the image -- */

static int elfsyms_by_string(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static int names_add(SymbolSet *set, char *name)
{
	if (set->count == set->capacity) {
		size_t next  = set->capacity ? set->capacity * 2 : 64;
		char **grown = realloc(set->names, next * sizeof *grown);

		if (!grown)
			return -1;
		set->names    = grown;
		set->capacity = next;
	}

	set->names[set->count++] = name;
	return 0;
}

/* Sort on the resolved name and drop the duplicates.
 *
 * Both halves matter. The sort is what keeps symbol-table order out of the
 * output and makes membership a binary search (LLR-ELF-05); the de-duplication
 * is not cosmetic either, since one source name reaches the table more than
 * once whenever a C++ overload set or a local symbol and its global alias both
 * appear.
 */
static void sort_and_dedupe(SymbolSet *set)
{
	/* qsort with a null base is undefined even at a count of zero, and an
	 * image whose every symbol went unresolved reaches here with exactly
	 * that shape. */
	if (set->count < 2) {
		if (set->count == 0) {
			free(set->names);
			set->names    = NULL;
			set->capacity = 0;
		}
		return;
	}

	qsort(set->names, set->count, sizeof *set->names, elfsyms_by_string);

	size_t kept = 1;

	for (size_t i = 1; i < set->count; i++) {
		if (strcmp(set->names[i], set->names[kept - 1]) == 0) {
			free(set->names[i]);
			continue;
		}
		set->names[kept++] = set->names[i];
	}
	set->count = kept;
}

/* Open the image the user named and confirm it is one this build can read.
 *
 * The image the user named and nothing else: no toolchain utility is invoked,
 * no image is searched for, and no debugging information is required (HLR-141,
 * LLR-ELF-03, LLR-ELF-04).
 *
 * Returns 0 with `*fd` and `*elf` owned by the caller, or -1 after a
 * diagnostic.
 */
static int open_image(const char *path, int *fd, Elf **elf)
{
	*fd  = open(path, O_RDONLY);
	if (*fd < 0) {
		diag_printf("elc: %s: %s\n", path, strerror(errno));
		return -1;
	}

	*elf = elf_begin(*fd, ELF_C_READ, NULL);
	if (!*elf || elf_kind(*elf) != ELF_K_ELF) {
		/* An archive, a linker script, a shell script, a core file, or
		 * a source file the user meant to pass as a target. Named,
		 * because the user named it and the failure is theirs to
		 * correct (HLR-146, LLR-ELF-06). */
		diag_printf("elc: %s: not an object file\n", path);
		return -1;
	}

	if (gelf_getclass(*elf) == ELFCLASSNONE) {
		diag_printf("elc: %s: an object file of a class this build does not "
		        "read\n", path);
		return -1;
	}

	return 0;
}

/* The symbol table to read, and its header.
 *
 * `.symtab` where the image has one and `.dynsym` where it does not. `.dynsym`
 * holds only the dynamically exported subset, so an image reduced to it yields
 * a smaller set and a correspondingly larger unmatched list — which the report
 * states rather than leaving to be inferred (HLR-143, LLR-ELF-01).
 *
 * Returns NULL when the image has neither, or when the header cannot be read.
 */
static Elf_Scn *symbol_section(Elf *elf, GElf_Shdr *shdr)
{
	Elf_Scn *symtab = NULL;
	Elf_Scn *dynsym = NULL;
	Elf_Scn *scn    = NULL;
	Elf_Scn *chosen;

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		GElf_Shdr this_shdr;

		if (!gelf_getshdr(scn, &this_shdr))
			continue;
		if (this_shdr.sh_type == SHT_SYMTAB && !symtab)
			symtab = scn;
		else if (this_shdr.sh_type == SHT_DYNSYM && !dynsym)
			dynsym = scn;
	}

	chosen = symtab ? symtab : dynsym;
	if (chosen && !gelf_getshdr(chosen, shdr))
		return NULL;
	return chosen;
}

/* Record one symbol if it is a function the image itself defines.
 *
 * Both halves of the test matter. Without the type test an object and a
 * function of the same name are indistinguishable; without the definedness test
 * every function the image *calls* out to a shared library counts as one the
 * image contains, and the filter then retains source the build never compiled
 * (LLR-ELF-02).
 *
 * Returns 1 when the symbol was a function, 0 when it was not, and -1 after a
 * diagnostic when the name could not be recorded.
 */
static int take_symbol(Elf *elf, const GElf_Shdr *shdr, const GElf_Sym *sym,
                       SymbolSet *out)
{
	const char *name;
	char       *resolved;

	if (GELF_ST_TYPE(sym->st_info) != STT_FUNC)
		return 0;
	if (sym->st_shndx == SHN_UNDEF)
		return 0;

	name = elf_strptr(elf, shdr->sh_link, sym->st_name);
	if (!name || !*name)
		return 0;

	resolved = resolved_name(name);
	if (!resolved) {
		out->unresolved++;
		return 1;
	}
	if (names_add(out, resolved) != 0) {
		free(resolved);
		diag_printf("elc: out of memory reading the image\n");
		return -1;
	}
	return 1;
}

/* Every function symbol in the chosen section, and the count of them.
 *
 * Returns 0, or -1 after a diagnostic.
 */
static int read_symbols(Elf *elf, Elf_Scn *chosen, const GElf_Shdr *shdr,
                        SymbolSet *out, size_t *functions)
{
	Elf_Data *data = NULL;

	*functions = 0;

	while (chosen && (data = elf_getdata(chosen, data)) != NULL) {
		size_t entries = shdr->sh_entsize
		                         ? data->d_size / shdr->sh_entsize : 0;

		for (size_t i = 0; i < entries; i++) {
			GElf_Sym sym;
			int      taken;

			if (!gelf_getsym(data, (int)i, &sym))
				continue;

			taken = take_symbol(elf, shdr, &sym, out);
			if (taken < 0)
				return -1;
			*functions += (size_t)taken;
		}
	}

	return 0;
}

int elfsyms_open(const char *path, SymbolSet *out)
{
	Elf      *elf       = NULL;
	Elf_Scn  *chosen    = NULL;
	GElf_Shdr shdr;
	size_t    functions = 0;
	int       fd        = -1;
	int       status    = -1;

	memset(out, 0, sizeof *out);

	/* Required before any other libelf call, and idempotent: a second run
	 * in one process is not an error. */
	if (elf_version(EV_CURRENT) == EV_NONE) {
		diag_printf("elc: %s: libelf is of an unusable version\n",
		        path);
		return -1;
	}

	out->path = strdup(path);
	if (!out->path) {
		diag_printf("elc: out of memory reading the image\n");
		return -1;
	}

	if (open_image(path, &fd, &elf) != 0)
		goto cleanup;

	chosen = symbol_section(elf, &shdr);

	if (read_symbols(elf, chosen, &shdr, out, &functions) != 0)
		goto cleanup;

	/* An empty function set is not an empty project. Filtering every
	 * function away would report a code base containing none, which is a
	 * confidently wrong result no reader could tell from a correct one, so
	 * the stripped image ends the run (HLR-146, LLR-ELF-07). */
	if (functions == 0) {
		diag_printf("elc: %s: no function symbols; a stripped image defines "
		        "nothing to filter by\n", path);
		goto cleanup;
	}

	sort_and_dedupe(out);

	/* Read from the descriptor already open, which is what keeps the image
	 * opened once and nothing beside it (HLR-141). An image carrying no
	 * debug information yields an empty set and is not a failure: HLR-141
	 * forbids requiring it, so its absence costs the line granularity and
	 * nothing else (HLR-153). */
	if (dwarfline_read(elf, &out->lines, &out->origins) != 0) {
		diag_printf("elc: out of memory reading the image's line "
		      "information\n");
		goto cleanup;
	}

	status = 0;

cleanup:
	if (elf)
		elf_end(elf);
	if (fd >= 0)
		close(fd);
	if (status != 0)
		elfsyms_free(out);
	return status;
}

bool elfsyms_defines_in(const SymbolSet *set, const char *function,
                        const char *file)
{
	if (!elfsyms_defines(set, function))
		return false;

	/* The image defines *a* function of this name. Which definitions it
	 * kept is a question only the debug information can answer, and where
	 * it answers, the answer governs: two translation units defining a
	 * `static helper` produce two symbols the link may keep or drop
	 * independently, and matching on the name retains or discards both
	 * together — one of them wrongly (HLR-193).
	 *
	 * Where it cannot answer, the name alone is the best that can be said,
	 * and `report_check_image_ambiguity` has already refused the run if
	 * that was not good enough. */
	if (!file || !dwarfline_knows(&set->origins, function))
		return true;

	return dwarfline_places(&set->origins, function, file);
}

bool elfsyms_defines(const SymbolSet *set, const char *function)
{
	if (!set || !function || set->count == 0)
		return false;

	/* The source name is reduced by the same function the linkage name
	 * was, so `Widget::size` in the source and `_ZNK6Widget4sizeEv` in the
	 * image meet in one form (HLR-142, LLR-SYM-03). */
	char *key = symname_reduce(function);
	bool  hit;

	if (!key)
		return false;

	/* bsearch, because the set is sorted; a linear scan would make a
	 * filtered run quadratic in the size of the image. */
	hit = bsearch(&key, set->names, set->count, sizeof *set->names,
	              elfsyms_by_string) != NULL;
	free(key);
	return hit;
}

size_t elfsyms_unresolved(const SymbolSet *set)
{
	return set ? set->unresolved : 0;
}

void elfsyms_free(SymbolSet *set)
{
	if (!set)
		return;
	for (size_t i = 0; i < set->count; i++)
		free(set->names[i]);
	free(set->names);
	free(set->path);
	dwarfline_free(&set->lines);
	originmap_free(&set->origins);
	memset(set, 0, sizeof *set);
}
