/* kept.c — the translation unit the fixture image is built from.
 *
 * Every function here survives, so a filtered run reports all three and lists
 * none of them absent. `scale` is static, which is the case that distinguishes
 * .symtab from .dynsym: a local function is in the one and not in the other,
 * so a reader taking only the dynamic table would report it missing from an
 * image that plainly contains it.
 *
 * `measure` calls a function the image does not define, which is the case
 * HLR-144 turns on: the callee is filtered out, so the call resolves to
 * nothing and is counted unresolved rather than resolved to a function that
 * is not in the image.
 */
#include <stdio.h>

/* Defined in dropped.c, which the image is not built from. */
int unlinked_add(int a, int b);

int kept_counter = 0;

static int scale(int n)
{
	if (n > 2)
		return n * 2;
	return n;
}

int measure(int n)
{
	kept_counter = unlinked_add(scale(n), 1);
	return kept_counter;
}

int main(void)
{
	printf("%d\n", measure(3));
	return 0;
}
