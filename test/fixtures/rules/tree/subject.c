/* The subject of the rules/ fixture group.
 *
 * Deliberately ordinary C. What is being tested is the rule mechanism, not
 * elc's opinion of this code — elc has none about it, which is the point
 * (HLR-111).
 */

#include <stdlib.h>

void *grab(int n)
{
	void *p = malloc((size_t)n);

	if (!p)
		goto fail;
	return p;

fail:
	return NULL;
}

void release(void *p)
{
	free(p);
}

int classify(int n)
{
	if (n < 0)
		goto negative;
	return 1;

negative:
	return -1;
}
