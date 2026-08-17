/* test/unit/elfsyms.c — unit tests for src/elfsyms.c.
 *
 * Name resolution is tested here and the image reading is tested against real
 * images in test/fixtures/elf.bats, and the split is deliberate: a linkage
 * name is a string, and a string is cheapest to pin at the unit level, while
 * an image is a file a compiler has to produce and belongs where a compiler
 * can be required and skipped for (doc/STP.md §2.2, §5).
 */

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elfsyms.h"

/* A resolution asserted by value, with the allocation released. `expected` of
 * NULL asserts that the scheme is one this build does not decode. */
static void resolves(const char *linkage, const char *expected)
{
	char *got = resolved_name(linkage);

	if (!expected) {
		cr_assert_null(got, "'%s' should resolve to nothing", linkage);
		free(got);
		return;
	}

	cr_assert_not_null(got, "'%s' should resolve to '%s'", linkage,
	                   expected);
	cr_assert_str_eq(got, expected, "'%s' resolved to '%s', expected '%s'",
	                 linkage, got, expected);
	free(got);
}

/* Verifies LLR-SYM-01: an unencoded linkage name is its source name. */
Test(elfsyms, an_unencoded_name_is_returned_unchanged)
{
	resolves("main", "main");
	resolves("elc_analyse_file", "elc_analyse_file");
	/* The `extern "C"` case is the same case: a C++ definition declared
	 * that way reaches the image under a name nothing encoded. */
	resolves("c_linkage_entry", "c_linkage_entry");
}

/* Verifies LLR-SYM-02: the scheme is detected from the name, not from a
 * language the user states. Nothing here says "this is C++". */
Test(elfsyms, the_scheme_is_detected_from_the_name)
{
	resolves("_ZN2ns1C1fEi", "f");
	resolves("_Z3fooi", "foo");
}

/* Verifies LLR-SYM-03: a decoded name is reduced to the identifier the report
 * presents, since the report presents the function name alone (HLR-014). */
Test(elfsyms, a_decoded_name_is_reduced_to_its_identifier)
{
	/* ns::C::f(int) const — a qualification, a signature, and a cv
	 * qualifier, none of which the report presents. */
	resolves("_ZNK2ns1C1fEi", "f");
	/* A constructor names itself. */
	resolves("_ZN2ns1CC2Ev", "C");
	/* A destructor keeps its tilde: the source writes `~C` and so does the
	 * report, so reducing it to `C` would match the constructor instead. */
	resolves("_ZN2ns1CD2Ev", "~C");
	/* A template's demangling carries a return type in front of the name,
	 * which is not part of it. */
	resolves("_Z3fooIiEvT_", "foo");
}

/* Verifies LLR-SYM-03: Rust's legacy scheme is Itanium-shaped and ends in a
 * disambiguating hash, which is not a path component a reader would
 * recognise. */
Test(elfsyms, a_rust_legacy_hash_suffix_is_not_the_name)
{
	resolves("_ZN9rustcrate4func17h0123456789abcdefE", "func");
	/* A hash-shaped component that is not one — the wrong length — is a
	 * name like any other, so the rule cannot be "drop the last
	 * component". */
	resolves("_ZN9rustcrate4func2h0E", "h0");
}

/* Verifies LLR-SYM-04: a scheme with no decoder resolves to nothing and is
 * counted, rather than matched against a guess. */
Test(elfsyms, an_undecodable_scheme_resolves_to_nothing)
{
	/* Rust v0, which the Itanium demangler rejects. */
	resolves("_RNvCs1234_7mycrate3foo", NULL);
	/* A name that begins like a mangling and is not one. */
	resolves("_Znot a mangled name", NULL);
}

/* Verifies LLR-SYM-03: an operator keeps its punctuation. The reduction has to
 * step over the token whole, or the angle brackets of `operator>>` unbalance
 * the scan and the qualification is never stripped. */
Test(elfsyms, an_operator_keeps_its_name)
{
	resolves("_ZN1SrsEi", "operator>>");
	resolves("_ZN1SplERKS_", "operator+");
	resolves("_ZN1SclEv", "operator()");
}

/* A SymbolSet built by hand, sorted as elfsyms_open leaves one. */
static void set_of(SymbolSet *set, const char *const *names, size_t count)
{
	memset(set, 0, sizeof *set);
	set->names = calloc(count, sizeof *set->names);
	cr_assert_not_null(set->names);
	for (size_t i = 0; i < count; i++) {
		set->names[i] = strdup(names[i]);
		cr_assert_not_null(set->names[i]);
	}
	set->count    = count;
	set->capacity = count;
}

/* Verifies LLR-SYM-03: both sides of the comparison are reduced. Reducing only
 * the image's side would make every qualified source name a mismatch. */
Test(elfsyms, the_source_name_is_reduced_before_it_is_compared)
{
	static const char *const names[] = { "apply", "size" };
	SymbolSet                set;

	set_of(&set, names, 2);

	/* What tree-sitter-cpp captures for an out-of-line definition. */
	cr_assert(elfsyms_defines(&set, "Widget::size"));
	/* And for an explicit specialisation, which names itself with its
	 * template arguments (HLR-064). */
	cr_assert(elfsyms_defines(&set, "apply<int, long>"));
	cr_assert(!(elfsyms_defines(&set, "absent")));

	elfsyms_free(&set);
}

/* Verifies LLR-ELF-05: membership is a binary search over the sorted set, so
 * every member is found wherever it sits in it. */
Test(elfsyms, every_member_of_the_set_is_found)
{
	static const char *const names[] = { "aa", "bb", "cc", "dd", "ee" };
	SymbolSet                set;

	set_of(&set, names, 5);
	for (size_t i = 0; i < 5; i++)
		cr_assert(elfsyms_defines(&set, names[i]),
		          "'%s' is in the set", names[i]);
	cr_assert(!(elfsyms_defines(&set, "zz")));
	elfsyms_free(&set);
}

/* An empty set answers no rather than searching a null array, which is
 * undefined however empty it is (HLR-124). */
Test(elfsyms, an_empty_set_answers_without_searching)
{
	SymbolSet set = { 0 };

	cr_assert(!(elfsyms_defines(&set, "anything")));
	cr_assert_eq(elfsyms_unresolved(&set), 0);
	elfsyms_free(&set);
}

/* Verifies LLR-ELF-06: an image that is not there is a diagnosed failure, and
 * leaves nothing behind to release. */
Test(elfsyms, an_absent_image_fails_and_owns_nothing)
{
	SymbolSet set;

	cr_assert_neq(elfsyms_open("/nonexistent/elc-test-image", &set), 0);
	cr_assert_eq(set.count, 0);
	cr_assert_null(set.names);
	elfsyms_free(&set);
}

/* Verifies LLR-ELF-06: a file that is not an object file fails the same way,
 * rather than being read as one and yielding nothing. */
Test(elfsyms, a_file_that_is_not_an_object_file_fails)
{
	char      path[] = "/tmp/elc-not-an-image-XXXXXX";
	SymbolSet set;
	int       fd = mkstemp(path);

	cr_assert_geq(fd, 0);
	cr_assert_eq(write(fd, "int main(void) { return 0; }\n", 29), 29);
	close(fd);

	cr_assert_neq(elfsyms_open(path, &set), 0);
	cr_assert_eq(set.count, 0);
	elfsyms_free(&set);
	unlink(path);
}
