/* The three shapes a macro takes where the grammar expects something else.
 *
 * Expected values are hand-counted from the *expanded* equivalent in
 * expanded.c beside this file, never from elc's own output: a fixture that
 * agrees with the implementation by construction asserts nothing.
 */
#define local      static
#define BOLD       "\033[1m"
#define FG_BLUE    "\033[34m"
#define RESET      "\033[0m"

/* Shape 2: a storage-class macro in front of a declaration. */
local int branchy(int n)
{
	if (n > 0)
		return n;
	if (n < -1)
		return -n;
	return 0;
}

/* Shape 1: macros expanding to string literals at the head of a
 * concatenation. Two of them lead, which is what the grammar rejects. */
void report(int n)
{
	printf(BOLD FG_BLUE "value: %d" RESET "\n", branchy(n));
}

/* Shape 3: a macro carrying the declarator of a file-scope definition,
 * which leaves the grammar an initialiser list against a bare name. */
#define ARR        int table[3]
ARR = { 1, 2, 3 };
