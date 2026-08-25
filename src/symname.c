/* symname.c — reducing a name to the identifier the report presents.
 *
 * See include/symname.h for why this is a module of its own rather than a
 * static helper inside elfsyms.c.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "symname.h"

/* ---------------------------------------------------------- the reduction --
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

/* Whether the `(` at `i` opens the parameter list, and how far past it to step
 * where it does not.
 *
 * Two parenthesised things are part of the name rather than a signature: the
 * literal "(anonymous namespace)" a demangling inserts for internal linkage,
 * and the empty pair of `operator()`, whose parameter list is the next one
 * along.
 */
static bool paren_opens_signature(const char *s, size_t i, size_t *skip)
{
	static const char anon[] = "(anonymous namespace)";
	size_t            back   = i;

	*skip = 0;

	if (strncmp(s + i, anon, sizeof anon - 1) == 0) {
		*skip = sizeof anon - 2;   /* the caller's loop adds one */
		return false;
	}

	while (back > 0 && s[back - 1] == ' ')
		back--;

	if (back >= 8 && strncmp(s + back - 8, "operator", 8) == 0 &&
	    (back == 8 || !ident_char(s[back - 9]))) {
		if (s[i + 1] == ')')
			*skip = 1;
		return false;
	}

	return true;
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
	int    angle = 0;
	size_t i     = 0;

	for (; s[i]; i++) {
		if (s[i] == '<') {
			angle++;
		} else if (s[i] == '>') {
			if (angle)
				angle--;
		} else if (s[i] == '(' && angle == 0) {
			size_t skip;

			if (paren_opens_signature(s, i, &skip))
				return i;
			i += skip;
		}
	}

	return i;
}

/* The offset past an `operator` token beginning at `i`, or `i` where none
 * begins there.
 *
 * Stepped over whole because the token's own name may contain `<`, `>` or `:`
 * — `operator<`, `operator>>`, `operator->` — none of which may be read as
 * template nesting or as a scope separator.
 */
static size_t skip_operator_token(const char *s, size_t len, size_t i)
{
	if (len - i < 8 || strncmp(s + i, "operator", 8) != 0)
		return i;
	if (i != 0 && ident_char(s[i - 1]))
		return i;

	i += 8;
	while (i < len && s[i] != ' ' &&
	       !(s[i] == ':' && i + 1 < len && s[i + 1] == ':'))
		i++;

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
		size_t past = skip_operator_token(s, len, i);

		if (past != i) {
			i = past;
		} else if (s[i] == '<') {
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

/* The length with a trailing template argument list removed.
 *
 * And only where it closes: the final `>` of `operator>>` opens nothing, and
 * truncating there would leave an operator with no name.
 */
static size_t without_template_args(const char *name, size_t start, size_t len)
{
	int    angle = 0;
	size_t i     = len;

	if (len <= start || name[len - 1] != '>')
		return len;

	while (i-- > start) {
		if (name[i] == '>')
			angle++;
		else if (name[i] == '<' && --angle == 0)
			return i;
	}

	return len;
}

/* The offset past whatever return type a template's demangling carries in
 * front of the name — `void foo<int>(int)`.
 *
 * An operator keeps its space, `operator new` being one name and not two.
 */
static size_t after_return_type(const char *name, size_t start, size_t len)
{
	if (len - start >= 8 && strncmp(name + start, "operator", 8) == 0)
		return start;

	for (size_t i = len; i-- > start; )
		if (name[i] == ' ')
			return i + 1;

	return start;
}

char *symname_reduce(const char *name)
{
	size_t len   = signature_start(name);
	size_t start = identifier_start(name, len);
	char  *copy;

	if (rust_hash(name + start, len - start) && start >= 2) {
		len   = start - 2;
		start = identifier_start(name, len);
	}

	len   = without_template_args(name, start, len);
	start = after_return_type(name, start, len);

	if (len <= start)
		return NULL;

	copy = malloc(len - start + 1);
	if (!copy)
		return NULL;

	memcpy(copy, name + start, len - start);
	copy[len - start] = '\0';
	return copy;
}
