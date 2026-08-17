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

#include "elfsyms.h"

/* The Itanium C++ ABI demangler, declared here rather than included from
 * <cxxabi.h>: that header is C++ and this translation unit is C. The symbol is
 * the whole of the dependency, and one declaration is less surface than a
 * compiler mode nothing else in the project uses (HLR-142). */
extern char *__cxa_demangle(const char *mangled, char *buffer, size_t *length,
                            int *status);

/* ------------------------------------------------------- name resolution --
 *
 * A demangled name is not yet a match. The Itanium ABI yields
 * `ns::C::f(int) const`, and the report presents the identifier alone
 * (HLR-014). Both sides of the comparison are therefore reduced to the same
 * form; reducing only one would make every qualified name a mismatch.
 */

static bool ident_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_';
}

/* Where the parameter list of a demangled name begins, or its length when it
 * has none.
 *
 * Two parentheses are part of a *name* rather than of a signature, and both
 * would otherwise truncate it to nothing: `operator()` and the
 * `(anonymous namespace)` an internal-linkage C++ definition is qualified by.
 * Each is stepped over rather than counted.
 */
static size_t signature_start(const char *s)
{
	static const char anon[] = "(anonymous namespace)";
	int    angle = 0;
	size_t i     = 0;

	for (; s[i]; i++) {
		if (s[i] == '<') {
			angle++;
		} else if (s[i] == '>') {
			if (angle)
				angle--;
		} else if (s[i] == '(' && angle == 0) {
			size_t back = i;

			if (strncmp(s + i, anon, sizeof anon - 1) == 0) {
				i += sizeof anon - 2;   /* the loop adds one */
				continue;
			}

			while (back > 0 && s[back - 1] == ' ')
				back--;
			if (back >= 8 && strncmp(s + back - 8, "operator", 8) == 0 &&
			    (back == 8 || !ident_char(s[back - 9]))) {
				/* `operator()`: the empty pair is the name, and
				 * the parameter list is the next one along. */
				if (s[i + 1] == ')')
					i++;
				continue;
			}

			return i;
		}
	}

	return i;
}

/* Where the last `::`-separated component of a qualified name begins.
 *
 * The scan steps over an `operator` token whole, because the punctuation that
 * follows one is part of the name: without that, `ns::S::operator>>` leaves an
 * unbalanced angle depth and the qualification is never stripped.
 */
static size_t identifier_start(const char *s, size_t len)
{
	size_t start = 0;
	int    angle = 0;

	for (size_t i = 0; i < len; ) {
		if (len - i >= 8 && strncmp(s + i, "operator", 8) == 0 &&
		    (i == 0 || !ident_char(s[i - 1]))) {
			i += 8;
			while (i < len && s[i] != ' ' &&
			       !(s[i] == ':' && i + 1 < len && s[i + 1] == ':'))
				i++;
			continue;
		}
		if (s[i] == '<') {
			angle++;
			i++;
		} else if (s[i] == '>') {
			if (angle)
				angle--;
			i++;
		} else if (angle == 0 && s[i] == ':' && i + 1 < len &&
		           s[i + 1] == ':') {
			i += 2;
			start = i;
		} else {
			i++;
		}
	}

	return start;
}

/* Rust's legacy mangling is Itanium-shaped and ends in a disambiguating hash:
 * `crate::func::h0123456789abcdef`. The hash is not a path component a reader
 * would recognise, so the component before it is the name (LLR-SYM-03). */
static bool rust_hash(const char *s, size_t len)
{
	if (len != 17 || s[0] != 'h')
		return false;
	for (size_t i = 1; i < len; i++)
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
			return false;
	return true;
}

/* Reduce a name — demangled or written in source — to the identifier the
 * report presents. Returns NULL on allocation failure or where nothing is
 * left to compare. */
static char *reduce_to_identifier(const char *name)
{
	size_t len   = signature_start(name);
	size_t start = identifier_start(name, len);

	if (rust_hash(name + start, len - start) && start >= 2) {
		len   = start - 2;
		start = identifier_start(name, len);
	}

	/* A trailing template argument list, and only where it closes: the
	 * final `>` of `operator>>` opens nothing, and truncating there would
	 * leave an operator with no name. */
	if (len > start && name[len - 1] == '>') {
		int    angle = 0;
		size_t i     = len;

		while (i-- > start) {
			if (name[i] == '>') {
				angle++;
			} else if (name[i] == '<') {
				if (--angle == 0) {
					len = i;
					break;
				}
			}
		}
	}

	/* Whatever return type a template's demangling carries in front of the
	 * name — `void foo<int>(int)`. An operator keeps its space, `operator
	 * new` being one name and not two. */
	if (!(len - start >= 8 && strncmp(name + start, "operator", 8) == 0))
		for (size_t i = len; i-- > start; )
			if (name[i] == ' ') {
				start = i + 1;
				break;
			}

	if (len <= start)
		return NULL;

	char *copy = malloc(len - start + 1);

	if (!copy)
		return NULL;
	memcpy(copy, name + start, len - start);
	copy[len - start] = '\0';
	return copy;
}

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

	char *reduced = reduce_to_identifier(demangled);

	free(demangled);
	return reduced;
}

/* ------------------------------------------------------------ the image -- */

static int by_string(const void *a, const void *b)
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

	qsort(set->names, set->count, sizeof *set->names, by_string);

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

int elfsyms_open(const char *path, SymbolSet *out)
{
	Elf      *elf       = NULL;
	Elf_Scn  *symtab    = NULL;
	Elf_Scn  *dynsym    = NULL;
	Elf_Scn  *chosen    = NULL;
	Elf_Scn  *scn       = NULL;
	Elf_Data *data      = NULL;
	GElf_Shdr shdr;
	size_t    functions = 0;
	int       fd        = -1;
	int       status    = -1;

	memset(out, 0, sizeof *out);

	/* Required before any other libelf call, and idempotent: a second run
	 * in one process is not an error. */
	if (elf_version(EV_CURRENT) == EV_NONE) {
		fprintf(stderr, "elc: %s: libelf is of an unusable version\n",
		        path);
		return -1;
	}

	out->path = strdup(path);
	if (!out->path) {
		fputs("elc: out of memory reading the image\n", stderr);
		return -1;
	}

	/* The image the user named and nothing else: no toolchain utility is
	 * invoked, no image is searched for, and no debugging information is
	 * required (HLR-141, LLR-ELF-03, LLR-ELF-04). */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		goto cleanup;
	}

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (!elf || elf_kind(elf) != ELF_K_ELF) {
		/* An archive, a linker script, a shell script, a core file, or
		 * a source file the user meant to pass as a target. Named,
		 * because the user named it and the failure is theirs to
		 * correct (HLR-146, LLR-ELF-06). */
		fprintf(stderr, "elc: %s: not an object file\n", path);
		goto cleanup;
	}

	if (gelf_getclass(elf) == ELFCLASSNONE) {
		fprintf(stderr,
		        "elc: %s: an object file of a class this build does not "
		        "read\n", path);
		goto cleanup;
	}

	/* `.symtab` where the image has one and `.dynsym` where it does not.
	 * `.dynsym` holds only the dynamically exported subset, so an image
	 * reduced to it yields a smaller set and a correspondingly larger
	 * unmatched list — which the report states rather than leaving to be
	 * inferred (HLR-143, LLR-ELF-01). */
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
	if (chosen && !gelf_getshdr(chosen, &shdr))
		chosen = NULL;

	while (chosen && (data = elf_getdata(chosen, data)) != NULL) {
		size_t entries = shdr.sh_entsize
		                         ? data->d_size / shdr.sh_entsize : 0;

		for (size_t i = 0; i < entries; i++) {
			GElf_Sym    sym;
			const char *name;
			char       *resolved;

			if (!gelf_getsym(data, (int)i, &sym))
				continue;

			/* Both halves of the test matter. Without the type
			 * test an object and a function of the same name are
			 * indistinguishable; without the definedness test
			 * every function the image *calls* out to a shared
			 * library counts as one the image contains, and the
			 * filter then retains source the build never compiled
			 * (LLR-ELF-02). */
			if (GELF_ST_TYPE(sym.st_info) != STT_FUNC)
				continue;
			if (sym.st_shndx == SHN_UNDEF)
				continue;

			name = elf_strptr(elf, shdr.sh_link, sym.st_name);
			if (!name || !*name)
				continue;

			functions++;

			resolved = resolved_name(name);
			if (!resolved) {
				out->unresolved++;
				continue;
			}
			if (names_add(out, resolved) != 0) {
				free(resolved);
				fputs("elc: out of memory reading the image\n",
				      stderr);
				goto cleanup;
			}
		}
	}

	/* An empty function set is not an empty project. Filtering every
	 * function away would report a code base containing none, which is a
	 * confidently wrong result no reader could tell from a correct one, so
	 * the stripped image ends the run (HLR-146, LLR-ELF-07). */
	if (functions == 0) {
		fprintf(stderr,
		        "elc: %s: no function symbols; a stripped image defines "
		        "nothing to filter by\n", path);
		goto cleanup;
	}

	sort_and_dedupe(out);
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

bool elfsyms_defines(const SymbolSet *set, const char *function)
{
	if (!set || !function || set->count == 0)
		return false;

	/* The source name is reduced by the same function the linkage name
	 * was, so `Widget::size` in the source and `_ZNK6Widget4sizeEv` in the
	 * image meet in one form (HLR-142, LLR-SYM-03). */
	char *key = reduce_to_identifier(function);
	bool  hit;

	if (!key)
		return false;

	/* bsearch, because the set is sorted; a linear scan would make a
	 * filtered run quadratic in the size of the image. */
	hit = bsearch(&key, set->names, set->count, sizeof *set->names,
	              by_string) != NULL;
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
	memset(set, 0, sizeof *set);
}
