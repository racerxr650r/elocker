/* nested.cpp — decision points inside a lambda belong to the enclosing
 * named function. Hand-counted in README.md beside this file.
 */
int outer(int seed)
{
	int total = seed;
	auto twice = [](int x) { return x > 0 ? x * 2 : 0; };

	if (total > 0 && total < 100)
		total = twice(total);
	return total;
}
