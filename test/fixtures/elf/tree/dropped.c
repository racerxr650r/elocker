/* dropped.c — a translation unit the link never includes.
 *
 * Both functions are perfectly ordinary source and neither is in the image, so
 * a filtered run measures neither and lists both. The initialised object at
 * file scope is the other half of the case: the image's *function* set says
 * nothing about code that is not a function, so `dropped_counter` is retained
 * and counted as file-scope ELOC even though nothing else in this file is
 * (HLR-145).
 */

int dropped_counter = 1;

int unlinked_add(int a, int b)
{
	return a + b;
}

int unlinked_max(int a, int b)
{
	if (a > b)
		return a;
	return b;
}
