/* test/unit/dwarfline.c — unit tests for src/dwarfline.c.
 *
 * The queries are tested here against a coverage set built by hand, and the
 * *reading* of a real image is tested in test/fixtures/elf.bats — the same
 * split elfsyms.c takes, and for the same reason: a lookup is arithmetic over
 * a structure and is cheapest to pin at the unit level, while an image is a
 * file a compiler has to produce and belongs where a compiler can be required
 * and skipped for (doc/STP.md §2.2, §5).
 *
 * The two-part contract is what most needs a test of its own. `covers` and
 * `compiled` are separate calls so that "this file was never described" cannot
 * be confused with "this line produced nothing", and the tests below assert
 * that a caller who skipped the first would be told every line of an uncovered
 * file was uncompiled (HLR-154).
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "dwarfline.h"

/* ------------------------------------------------------------ scaffolding */

/* Append one file and its compiled lines. The set `dwarfline_read` produces is
 * sorted by path and each line list ascending and de-duplicated; these are
 * given in that form, since the queries are what is under test and not the
 * ordering the reader imposes. */
static void add_file(LineCoverage *c, const char *path,
                     const uint32_t *lines, size_t count)
{
	CoveredFile *grown = realloc(c->files, (c->count + 1) * sizeof *grown);

	cr_assert_not_null(grown);
	c->files = grown;

	CoveredFile *file = &c->files[c->count];

	memset(file, 0, sizeof *file);
	file->path = strdup(path);
	cr_assert_not_null(file->path);

	if (count) {
		file->lines = calloc(count, sizeof *file->lines);
		cr_assert_not_null(file->lines);
		memcpy(file->lines, lines, count * sizeof *lines);
	}
	file->count    = count;
	file->capacity = count;
	c->count++;
	c->present = true;
}

/* --------------------------------------------------------------- coverage */

/* Verifies LLR-DWL-07: the two questions the origin map answers are separate
 * calls, and the second is meaningful only where the first is true.
 *
 * A single call returning false would conflate "the debug information says
 * this function is not in that file" with "there is no debug information",
 * and a caller that made the distinction by accident would exclude every
 * function of an image built without it.
 */
Test(dwarfline, an_empty_origin_map_knows_and_places_nothing)
{
	OriginMap m = { 0 };

	cr_assert_not(dwarfline_knows(&m, "helper"));
	cr_assert_not(dwarfline_places(&m, "helper", "/p/a.c"));

	/* NULL is the empty map, for the reason it is everywhere else here:
	 * a run with no image asks the question and must not crash on it. */
	cr_assert_not(dwarfline_knows(NULL, "helper"));
	cr_assert_not(dwarfline_places(NULL, "helper", "/p/a.c"));
	cr_assert_not(dwarfline_knows(&m, NULL));

	originmap_free(&m);
	originmap_free(NULL);
	originmap_free(&m);   /* safe twice */
}

Test(dwarfline, a_file_the_mapping_describes_is_covered)
{
	LineCoverage c = { 0 };
	const uint32_t lines[] = { 3, 4, 7 };

	add_file(&c, "/p/a.c", lines, 3);

	cr_assert(dwarfline_covers(&c, "/p/a.c"));
	cr_assert_not(dwarfline_covers(&c, "/p/b.c"),
	              "a file the mapping never named is not covered");

	dwarfline_free(&c);
}

Test(dwarfline, an_empty_set_covers_nothing)
{
	LineCoverage c = { 0 };

	/* An image carrying no debug information at all. Every file is
	 * uncovered, nothing is pruned, and the run reports what it reported
	 * before this mechanism existed (HLR-153). */
	cr_assert_not(dwarfline_covers(&c, "/p/a.c"));
	cr_assert_not(dwarfline_covers(NULL, "/p/a.c"));
	cr_assert_not(dwarfline_covers(&c, NULL));
}

/* ------------------------------------------------------------ compiled -- */

Test(dwarfline, a_line_the_mapping_names_was_compiled)
{
	LineCoverage c = { 0 };
	const uint32_t lines[] = { 3, 4, 7, 11, 12 };

	add_file(&c, "/p/a.c", lines, 5);

	cr_assert(dwarfline_compiled(&c, "/p/a.c", 3), "first");
	cr_assert(dwarfline_compiled(&c, "/p/a.c", 7), "middle");
	cr_assert(dwarfline_compiled(&c, "/p/a.c", 12), "last");

	cr_assert_not(dwarfline_compiled(&c, "/p/a.c", 5),
	              "a gap in the middle produced no instruction");
	cr_assert_not(dwarfline_compiled(&c, "/p/a.c", 1), "before the first");
	cr_assert_not(dwarfline_compiled(&c, "/p/a.c", 99), "past the last");

	dwarfline_free(&c);
}

Test(dwarfline, an_uncovered_file_answers_not_compiled_for_every_line)
{
	LineCoverage c = { 0 };
	const uint32_t lines[] = { 3, 4 };

	add_file(&c, "/p/a.c", lines, 2);

	/* **The failure mode the two-part contract exists to prevent.** A
	 * caller that asked only this question would find every line of an
	 * uncovered file uncompiled and delete the file — absence from a
	 * mapping that never described it read as evidence about it. The
	 * answer here is deliberately the unsafe one; `dwarfline_covers` is
	 * what makes asking it safe, and it has to be asked first (HLR-154).
	 */
	cr_assert_not(dwarfline_compiled(&c, "/p/b.c", 3));
	cr_assert_not(dwarfline_compiled(&c, "/p/b.c", 4));
	cr_assert_not(dwarfline_covers(&c, "/p/b.c"),
	              "and this is the call that tells them apart");

	dwarfline_free(&c);
}

Test(dwarfline, several_files_are_each_looked_up_on_their_own_lines)
{
	LineCoverage c = { 0 };
	const uint32_t a[] = { 10 };
	const uint32_t b[] = { 20 };

	/* Sorted by path, which is what the binary search over the file table
	 * requires and what `dwarfline_read` produces. */
	add_file(&c, "/p/a.c", a, 1);
	add_file(&c, "/p/b.c", b, 1);

	cr_assert(dwarfline_compiled(&c, "/p/a.c", 10));
	cr_assert(dwarfline_compiled(&c, "/p/b.c", 20));
	cr_assert_not(dwarfline_compiled(&c, "/p/a.c", 20),
	              "one file's lines are not another's");
	cr_assert_not(dwarfline_compiled(&c, "/p/b.c", 10));

	dwarfline_free(&c);
}

Test(dwarfline, a_covered_file_with_no_lines_compiled_nothing)
{
	LineCoverage c = { 0 };

	/* Covered and empty is a different claim from uncovered, and both are
	 * representable: the mapping described this file and recorded no
	 * instruction for any line of it. */
	add_file(&c, "/p/a.c", NULL, 0);

	cr_assert(dwarfline_covers(&c, "/p/a.c"));
	cr_assert_not(dwarfline_compiled(&c, "/p/a.c", 1));

	dwarfline_free(&c);
}

/* ------------------------------------------------------------- lifetime -- */

Test(dwarfline, free_is_safe_on_null_and_twice)
{
	LineCoverage c = { 0 };
	const uint32_t lines[] = { 1 };

	dwarfline_free(NULL);

	add_file(&c, "/p/a.c", lines, 1);
	dwarfline_free(&c);
	dwarfline_free(&c);

	cr_assert_eq(c.count, 0);
	cr_assert_null(c.files);
}

Test(dwarfline, reading_a_null_image_yields_an_empty_set)
{
	LineCoverage c;

	/* Not a failure. An image with no debug information is ordinary, and
	 * HLR-141 forbids requiring it: the set comes back empty and the run
	 * proceeds at function granularity alone. */
	cr_assert_eq(dwarfline_read(NULL, &c, NULL), 0);
	cr_assert_eq(c.count, 0);
	cr_assert_not(c.present);

	dwarfline_free(&c);
}
