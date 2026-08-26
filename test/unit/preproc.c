/* test/unit/preproc.c — unit tests for the #line filter in src/preproc.c.
 *
 * The filter is tested against captured preprocessor output rather than by
 * running a compiler, and the split is deliberate: every interesting failure
 * in this module is a property of the marker stream, and a marker stream is a
 * string. Running gcc to obtain one would test gcc, would need a toolchain on
 * the runner, and would make the cases that matter — a marker that rewinds, a
 * name that is escaped, output naming the file nowhere — nearly impossible to
 * produce on purpose (doc/STP.md §2.2).
 *
 * The end-to-end path is exercised in test/fixtures/preproc.bats, where a
 * compiler can be required and skipped for.
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "preproc.h"

/* How many lines the filtered buffer holds, which is what every location in
 * the report is counted in. */
static size_t lines_of(const PreprocResult *r)
{
	size_t n = 0;

	if (!r->text)
		return 0;
	for (const char *p = r->text; *p; p++)
		n += (*p == '\n');
	return n;
}

/* Verifies LLR-PRE-03: only the lines attributed to the file under analysis
 * survive. Without this a header contributes its functions, its lines and its
 * complexity to whichever file included it — figures that are larger,
 * internally consistent, and about a program nobody wrote. */
Test(preproc, only_the_analysed_file_survives)
{
	static const char in[] =
		"# 1 \"app.c\"\n"
		"int mine(void) { return 1; }\n"
		"# 1 \"/usr/include/stdio.h\" 1 3 4\n"
		"int theirs(void) { return 2; }\n"
		"# 2 \"app.c\" 2\n"
		"int also_mine(void) { return 3; }\n";
	PreprocResult r = { 0 };

	cr_assert_eq(preproc_filter(in, sizeof in - 1, "app.c", &r), 0);
	cr_assert_not_null(r.text);
	cr_assert_not_null(strstr(r.text, "mine"), "the file's own code is kept");
	cr_assert_null(strstr(r.text, "theirs"), "the header's code is not");
	preproc_result_free(&r);
}

/* Verifies LLR-PRE-04: a retained line sits at its own line number. Every
 * figure elc reports is line-based, so a filter that concatenated what it kept
 * would displace every function range and every finding by whatever it
 * discarded above them — undetectable to a reader, and so impossible to
 * discount. */
Test(preproc, a_retained_line_keeps_its_line_number)
{
	static const char in[] =
		"# 1 \"/usr/include/stdio.h\" 1 3 4\n"
		"int theirs(void);\n"
		"# 40 \"app.c\" 2\n"
		"int mine(void) { return 1; }\n";
	PreprocResult r = { 0 };

	cr_assert_eq(preproc_filter(in, sizeof in - 1, "app.c", &r), 0);
	cr_assert_not_null(r.text);

	/* 39 pad lines, then the line itself. */
	cr_assert_eq(lines_of(&r), 40, "expected the line at 40, buffer holds %zu",
	             lines_of(&r));

	size_t before = (size_t)(strstr(r.text, "mine") - r.text);
	size_t nl     = 0;

	for (size_t i = 0; i < before; i++)
		nl += (r.text[i] == '\n');
	cr_assert_eq(nl, 39, "mine should sit on line 40, sits on %zu", nl + 1);
	preproc_result_free(&r);
}

/* Verifies LLR-PRE-04: padding never runs backwards. A marker announcing an
 * earlier line occurs where a macro expansion spans lines and the preprocessor
 * resynchronises; acting on it would let the filter overwrite a line already
 * written, and a buffer that can rewind is one whose contents depend on the
 * order the markers happened to arrive. */
Test(preproc, a_marker_that_rewinds_is_ignored)
{
	static const char in[] =
		"# 10 \"app.c\"\n"
		"int later(void) { return 1; }\n"
		"# 2 \"app.c\"\n"
		"int earlier(void) { return 2; }\n";
	PreprocResult r = { 0 };

	cr_assert_eq(preproc_filter(in, sizeof in - 1, "app.c", &r), 0);
	cr_assert_not_null(r.text);
	cr_assert_not_null(strstr(r.text, "later"));
	cr_assert_not_null(strstr(r.text, "earlier"),
	                   "the line is kept; only the rewind is refused");
	cr_assert(strstr(r.text, "later") < strstr(r.text, "earlier"),
	          "order follows the stream, never the announced number");
	preproc_result_free(&r);
}

/* Verifies LLR-PRE-04: the physical lines one source line's expansion was
 * spread across are given back as one. `return NULL;` reaches the buffer as
 * three lines where the source had one, and without this every location below
 * the first such expansion is displaced by however many the file accumulated —
 * a drift that grows down the file and that no reader could detect. */
Test(preproc, a_split_expansion_is_rejoined_onto_its_own_line)
{
	static const char in[] =
		"# 1 \"app.c\"\n"
		"int first(void) { return 1; }\n"
		"int second(void) { return\n"
		"       ((void *)0)\n"
		"            ; }\n"
		"# 4 \"app.c\"\n"
		"int third(void) { return 3; }\n";
	PreprocResult r = { 0 };

	cr_assert_eq(preproc_filter(in, sizeof in - 1, "app.c", &r), 0);
	cr_assert_not_null(r.text);

	/* third() is line 4 of the source and must be line 4 of the buffer. */
	size_t before = (size_t)(strstr(r.text, "third") - r.text);
	size_t nl     = 0;

	for (size_t i = 0; i < before; i++)
		nl += (r.text[i] == '\n');
	cr_assert_eq(nl, 3, "third should sit on line 4, sits on %zu", nl + 1);

	/* And nothing was thrown away to get there. */
	cr_assert_not_null(strstr(r.text, "((void *)0)"));
	preproc_result_free(&r);
}

/* Verifies LLR-PRE-05: output naming the file nowhere is a failure, not an
 * empty file. A zero-line measurement of a file that has lines is the silent
 * wrong answer this module exists not to produce — and in the report it is
 * indistinguishable from a file that is genuinely empty. */
Test(preproc, output_naming_the_file_nowhere_is_a_failure)
{
	static const char in[] =
		"# 1 \"/usr/include/stdio.h\" 1 3 4\n"
		"int theirs(void);\n";
	PreprocResult r = { 0 };

	cr_assert_eq(preproc_filter(in, sizeof in - 1, "app.c", &r), 0);
	cr_assert_null(r.text, "a null buffer is the signal to fall back");
	cr_assert_eq(r.status, PREPROC_NOT_NAMED);
	preproc_result_free(&r);
}

/* Verifies LLR-PRE-03: the marker stream is the only authority. A line of
 * program text that looks like a marker must not change what is measured —
 * otherwise a string constant in somebody's source decides which of their code
 * elc reports on. */
Test(preproc, program_text_resembling_a_marker_changes_nothing)
{
	static const char in[] =
		"# 1 \"app.c\"\n"
		"const char *s = \"# 1 \\\"/usr/include/stdio.h\\\"\";\n"
		"int mine(void) { return 1; }\n";
	PreprocResult r = { 0 };

	cr_assert_eq(preproc_filter(in, sizeof in - 1, "app.c", &r), 0);
	cr_assert_not_null(r.text);
	cr_assert_not_null(strstr(r.text, "mine"),
	                   "the state never left appending");
	preproc_result_free(&r);
}

/* Verifies LLR-PRE-03: a quoted name is unescaped before comparison. A path
 * holding a quote or a backslash reaches the marker escaped, and compared raw
 * would never equal the path elc holds — so the file would silently fall back
 * for a reason nothing in the output explains. */
Test(preproc, an_escaped_name_compares_equal)
{
	static const char in[] =
		"# 1 \"/tmp/od\\\\d/app.c\"\n"
		"int mine(void) { return 1; }\n";
	PreprocResult r = { 0 };

	cr_assert_eq(preproc_filter(in, sizeof in - 1, "/tmp/od\\d/app.c", &r),
	             0);
	cr_assert_not_null(r.text, "the escaped marker named this file");
	cr_assert_not_null(strstr(r.text, "mine"));
	preproc_result_free(&r);
}

/* Verifies LLR-PRE-06: the headers the expansion drew on are recorded, and the
 * two standard libraries told apart. The path cannot decide it — <cstdio> and
 * <stdio.h> sit in the same directories — so the classification is by name. */
Test(preproc, standard_headers_are_recorded_and_classified)
{
	static const char in[] =
		"# 1 \"app.cpp\"\n"
		"# 1 \"/usr/include/c++/13/iostream\" 1 3\n"
		"# 1 \"/usr/include/c++/13/cstdio\" 1 3\n"
		"# 1 \"/usr/include/stdio.h\" 1 3 4\n"
		"# 1 \"/home/u/proj/mine.h\" 1\n"
		"# 2 \"app.cpp\" 2\n"
		"int mine(void) { return 1; }\n";
	PreprocResult r = { 0 };
	size_t        cxx = 0, c = 0;

	cr_assert_eq(preproc_filter(in, sizeof in - 1, "app.cpp", &r), 0);
	for (size_t i = 0; i < r.header_count; i++) {
		cxx += (r.headers[i].kind == STDLIB_CXX);
		c   += (r.headers[i].kind == STDLIB_C);
	}
	cr_assert_eq(cxx, 2, "iostream and cstdio are C++, got %zu", cxx);
	cr_assert_eq(c, 1, "stdio.h is C, got %zu", c);
	cr_assert_eq(r.header_count, 3,
	             "the project's own header is not a standard one");
	preproc_result_free(&r);
}

/* Verifies LLR-PRE-06: a header reached twice is recorded once. A translation
 * unit re-enters a header every time an include guard is re-evaluated, and a
 * list counting those would report a dependence proportional to the include
 * graph rather than to the code. */
Test(preproc, a_header_reached_twice_is_recorded_once)
{
	static const char in[] =
		"# 1 \"app.c\"\n"
		"# 1 \"/usr/include/stdio.h\" 1 3 4\n"
		"# 1 \"/usr/include/stdio.h\" 1 3 4\n"
		"# 2 \"app.c\" 2\n"
		"int mine(void) { return 1; }\n";
	PreprocResult r = { 0 };

	cr_assert_eq(preproc_filter(in, sizeof in - 1, "app.c", &r), 0);
	cr_assert_eq(r.header_count, 1, "recorded %zu times", r.header_count);
	preproc_result_free(&r);
}
