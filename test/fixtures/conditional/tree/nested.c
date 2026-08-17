/* Nesting, which is where an undecided count is easiest to get wrong.
 *
 * The inner region is undecidable on its own terms, but it lies inside a
 * region that is not compiled at all — so it is not counted as undecided
 * either. A region nobody builds has no condition worth reporting.
 */

#if 0
#ifdef INNER
int inner_yes(void)
{
	return 1;
}
#else
int inner_no(void)
{
	return 2;
}
#endif
#endif

int outer(void)
{
	return 0;
}
