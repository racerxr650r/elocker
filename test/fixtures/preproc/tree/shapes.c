/* The macro shapes tree-sitter's C grammar cannot follow, and a header whose
 * content must not reach this file's measurements.
 *
 * Expected values are hand-counted from expanded.c beside this file, never
 * from elc's own output: a fixture that agrees with the implementation by
 * construction asserts nothing.
 */
#include "local.h"

#define local      static
#define BOLD       "\033[1m"
#define FG_BLUE    "\033[34m"
#define RESET      "\033[0m"

/* A storage-class macro in front of a declaration. */
local int branchy(int n)
{
	if (n > 0)
		return n;
	if (n < -1)
		return -n;
	return 0;
}

/* Macros expanding to string literals at the head of a concatenation. Two of
 * them lead, which is what the grammar rejects. */
void report(int n)
{
	printf(BOLD FG_BLUE "value: %d" RESET "\n", branchy(n));
}
