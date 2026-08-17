/* The subject of the conditional/ fixture group.
 *
 * Every conditional form elc can decide, and two it cannot, in one file so
 * that one run exercises all of them. Hand-counted in README.md beside this.
 */

#if 0
int never_built(void)
{
	return 1;
}
#endif

#ifdef FEATURE
int with_feature(void)
{
	return 2;
}
#else
int without_feature(void)
{
	return 3;
}
#endif

#ifndef LEAN
int fat(void)
{
	return 4;
}
#endif

#if VERSION > 2
int undecidable(void)
{
	return 5;
}
#endif

int always(void)
{
	return 0;
}
