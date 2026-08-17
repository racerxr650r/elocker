/* An #elif chain, in a directory of its own so that the totals of tree/ stay
 * hand-checkable against a single table.
 *
 * The chain is what makes "the alternative" more than the #else: for the
 * leading #if, the alternative is the whole rest of the chain.
 */

#if defined(ALPHA)
int alpha(void)
{
	return 1;
}
#elif defined(BETA)
int beta(void)
{
	return 2;
}
#else
int neither(void)
{
	return 3;
}
#endif
