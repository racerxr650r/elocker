/* A region that is not compiled inside a function that is.
 *
 * The function survives; the call, the decision point and the global access
 * inside the dead region must not. HLR-132 says "every reported metric and
 * every graph fact", and a pruning that only removed whole functions would
 * pass every other case in this group and fail this one.
 */

int shared_flag;

int helper(void)
{
	return 1;
}

int caller(void)
{
	int total = 0;

#if 0
	if (shared_flag)
		total += helper();
#endif

	return total;
}
