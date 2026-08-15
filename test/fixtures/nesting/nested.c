/* nested.c — statements attributed to the innermost enclosing function.
 * Hand-counted in README.md beside this file.
 */
int outer(int seed)
{
	int total = seed;

	int middle(int a)
	{
		int inner(int b)
		{
			return b * 2;
		}

		return inner(a) + 1;
	}

	total += middle(seed);
	return total;
}
