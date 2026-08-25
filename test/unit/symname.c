/* test/unit/symname.c — unit tests for src/symname.c.
 *
 * The reduction is a pure function from string to string, so it is pinned
 * here rather than through an image: every interesting case is a spelling,
 * and requiring a compiler to produce a spelling would test the compiler
 * (doc/STP.md §2.2).
 *
 * The cases that matter are the ones where two spellings of one name have to
 * meet — DWARF's `f<int>` against the source's `f` — and the ones where a
 * name only looks like it needs reducing, which is the whole `operator`
 * family.
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>

#include "symname.h"

static void reduces(const char *name, const char *expected)
{
	char *got = symname_reduce(name);

	if (!expected) {
		cr_assert_null(got, "'%s' should reduce to nothing", name);
		free(got);
		return;
	}

	cr_assert_not_null(got, "'%s' should reduce to '%s'", name, expected);
	cr_assert_str_eq(got, expected, "'%s' reduced to '%s', expected '%s'",
	                 name, got, expected);
	free(got);
}

/* Verifies LLR-SNM-01: the case Phase 26 exists for. DWARF records a template
 * under its instantiated name and the source declares the bare one; without
 * this they never meet, and elc reports an image as carrying no debug
 * information about a function it describes completely. */
Test(symname, a_template_instantiation_reduces_to_its_bare_name)
{
	reduces("serialize_seq<int>", "serialize_seq");
	reduces("Copy_seq<FACE::Sequence<int> >", "Copy_seq");
	reduces("void foo<int>(int)", "foo");
}

/* Verifies LLR-SNM-01: the trap in stripping a template argument list. Four of
 * these end in `>` and none of them opens anything, so a reduction that
 * truncated at a trailing bracket would leave `operator` — or nothing. */
Test(symname, the_operator_family_survives_intact)
{
	reduces("operator<", "operator<");
	reduces("operator<<", "operator<<");
	reduces("operator<=", "operator<=");
	reduces("operator>", "operator>");
	reduces("operator>>", "operator>>");
	reduces("operator->", "operator->");
	reduces("operator()", "operator()");
	reduces("ns::C::operator<<", "operator<<");
}

/* Verifies LLR-SNM-01: a qualification is not part of the identifier the
 * report presents (HLR-014), and the source side reaches the debug-information
 * lookup spelled `S::method` while DWARF spells the same function `method`. */
Test(symname, a_qualification_is_stripped)
{
	reduces("S::method", "method");
	reduces("ns::C::f", "f");
	reduces("plain", "plain");
}

/* Verifies LLR-SNM-02: nothing to compare is not an identifier. A caller that
 * received an empty string would key a map on it and match every other name
 * that reduced to nothing. */
Test(symname, a_name_with_nothing_left_reduces_to_nothing)
{
	reduces("", NULL);
	reduces("()", NULL);
}
