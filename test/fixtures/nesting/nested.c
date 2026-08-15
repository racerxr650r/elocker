/* nested.c — statements and decision points attributed to the innermost
 * enclosing function. Hand-counted in README.md beside this file.
 */
int outer(int seed)
{
	int total = seed;

	int middle(int a)
	{
		int inner(int b)
		{
			return b > 0 ? b * 2 : 0;
		}

		if (a > 0)
			return inner(a) + 1;
		return 0;
	}

	if (seed > 0 && seed < 100)
		total += middle(seed);
	return total;
}
