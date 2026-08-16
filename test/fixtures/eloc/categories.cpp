/* categories.cpp — one instance of each ELOC category, and one of each
 * exclusion. Hand-counted in README.md beside this file.
 */
#include <vector>
#define LIMIT 3

int bare_declaration;
int initialised_global = 1;
int prototype_only(int n);

int categories(int n)
{
	int i;
	int total = 0;

	for (i = 0; i < LIMIT; i++) {
		if (i == n)
			total += i;
		else if (i > n)
			break;
		else
			continue;
	}

	std::vector<int> values = {1, 2, 3};
	for (int v : values)
		total += v;

	switch (n) {
	case 0:
		total = 0;
		break;
	default:
		goto done;
	}

	try {
		if (total < 0)
			throw total;
	} catch (int e) {
		total = 0;
	}

done:
	prototype_only(total);
	return total;
}
