/* dwarfline.h — the source lines a build compiled, read from the image.
 *
 * dwarfline.c reads the debug line information a linked image carries, where
 * it carries any, and answers which source lines this build produced an
 * instruction for (doc/SDD.md §18, HLR-153). It is the finer of the two
 * granularities an image answers at: `elfsyms.c` says which *functions* the
 * link kept, and this says which *lines* inside a kept function the compiler
 * emitted.
 *
 * **Read from the image the user named and from nothing else.** The low-level
 * DWARF interface is used deliberately: it works from the ELF descriptor
 * `elfsyms.c` already holds, whereas the higher-level one would follow a
 * `.gnu_debuglink` or a build-id into a separate-debug directory the user
 * never named — which HLR-141 forbids outright. Nothing here opens, stats, or
 * resolves a path against the filesystem, so the answer depends on the
 * image's bytes and nothing about the machine reading it (LLR-DWL-01).
 *
 * **Absence of a line is evidence only where coverage was established.** A
 * translation unit compiled without debug information contributes no entries
 * at all, so a rule keyed on absence alone would delete an entire file. Every
 * query here is therefore in two parts — is this file covered, and is this
 * line within it compiled — and the first governs the second (HLR-154).
 */
#ifndef ELC_DWARFLINE_H
#define ELC_DWARFLINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One source file the image's line information covers, and the lines within
 * it that produced at least one instruction.
 *
 * The line list is ascending and de-duplicated, so membership is a binary
 * search: a line program emits an entry per instruction sequence and names
 * the same line many times over.
 */
typedef struct {
	char     *path;     /* as the image records it, normalised; owned */
	uint32_t *lines;    /* ascending, de-duplicated; owned            */
	size_t    count;
	size_t    capacity;
} CoveredFile;

/* Every file the image's line information covers.
 *
 * `present` distinguishes an image that carried no line information at all
 * from one whose line information covered no analysed file. Both prune
 * nothing, and they are different claims about why: the first is a build made
 * without debug information, the second a build whose debug information
 * describes other code. Only the second says anything about the target.
 */
typedef struct {
	CoveredFile *files;   /* sorted by path; owned */
	size_t       count;
	size_t       capacity;
	bool         present;
} LineCoverage;

/* One function definition the image's debug information records: the name, and
 * the source file it was written in.
 *
 * A **pair**, and that is the whole design. Keying by name alone would make
 * two translation units defining a `static helper` collapse into one unusable
 * entry — which is precisely the case the map exists to resolve, since the
 * debug information carries a separate subprogram for each and knows exactly
 * which file each was written in (HLR-193).
 */
typedef struct {
	char *name;   /* as the debug information records it; owned */
	char *file;   /* absolute and normalised; owned             */
} FunctionOrigin;

/* Every function definition the image's debug information places.
 *
 * The map exists so that the `--elf` filter of HLR-140 can match a source
 * function to an image symbol by *name and file* rather than by name alone.
 * Two translation units defining a `static helper` produce two symbols the
 * link may keep or drop independently, and a filter matching on the name
 * retains or discards both together — one of them wrongly.
 *
 * `present` distinguishes an image carrying no debug information from one
 * whose debug information places no function, exactly as `LineCoverage` does
 * and for the same reason: only the second says anything about the target.
 */
typedef struct {
	FunctionOrigin *items;   /* sorted by name, then file; owned */
	size_t          count;
	size_t          capacity;
	bool            present;
} OriginMap;

/* Read the line information of an already-opened image.
 *
 * `elf` is the `Elf *` handle `elfsyms.c` holds, passed opaquely so that no
 * consumer of this header links a DWARF library merely to ask whether a line
 * was compiled — the same reason the SDG carries its graph object as `void *`.
 *
 * Returns 0. An image carrying no line information is **not** a failure and
 * yields an empty set with `present` false, since HLR-141 forbids requiring
 * debug information: its absence costs the finer granularity and nothing
 * else. Non-zero is returned only on allocation failure.
 */
int dwarfline_read(void *elf, LineCoverage *out, OriginMap *origins);

/* Whether the image described any function's origin at all.
 *
 * The question `dwarfline_knows` cannot answer. A false result from that call
 * has two causes with different remedies — the image carries no debug
 * information, or it carries plenty and simply describes no definition of this
 * name — and a diagnostic that names one condition while observing the other
 * sends its reader somewhere there is nothing to find (HLR-201).
 */
bool dwarfline_any(const OriginMap *origins);

/* Whether the debug information records this function at all, in any file.
 *
 * The question that decides whether the map can be trusted for a name. False
 * means the image was built without debug information, or built without it for
 * the unit that defines this function — and in either case a caller with two
 * candidate definitions has nothing to choose between them (HLR-193).
 */
bool dwarfline_knows(const OriginMap *origins, const char *function);

/* Whether the debug information records this function as written in this file.
 *
 * Only meaningful where `dwarfline_knows` is true for the same name: false is
 * returned for a name the map does not hold, and a caller that skipped the
 * first test would read that as "not defined here" for every function of an
 * image built without debug information. The two are separate calls so the
 * distinction cannot be made by accident — the same shape `dwarfline_covers`
 * and `dwarfline_compiled` take, for the same reason.
 */
bool dwarfline_places(const OriginMap *origins, const char *function,
                      const char *file);

/* Release the map and every name and path it owns. Safe on NULL, and safe
 * twice. */
void originmap_free(OriginMap *origins);

/* Whether the image's line information covers this source file.
 *
 * False for a file whose translation unit was compiled without debug
 * information, for a file the image's line information does not mention, and
 * for every run with no image. No line in such a file may be excluded, and
 * the file is counted among those whose coverage could not be established
 * (HLR-154, HLR-155).
 */
bool dwarfline_covers(const LineCoverage *coverage, const char *path);

/* Whether this build compiled an instruction for this line of this file.
 *
 * Only meaningful where `dwarfline_covers` is true for the same path; false
 * is returned for an uncovered file, and a caller that skipped the coverage
 * test would read that as "not compiled" for every line of it. The two are
 * separate calls rather than one so that the distinction cannot be made by
 * accident.
 */
bool dwarfline_compiled(const LineCoverage *coverage, const char *path,
                        uint32_t line);

/* Release the coverage set and every path and line list it owns. Safe on
 * NULL, and safe twice. */
void dwarfline_free(LineCoverage *coverage);

#endif /* ELC_DWARFLINE_H */
