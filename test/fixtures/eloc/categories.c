/* categories.c — one instance of each ELOC category, and one of each
 * exclusion. Hand-counted in README.md beside this file.
 */
#include <stddef.h>
#define LIMIT 3

int bare_declaration;
int initialised_global = 1;
int prototype_only(void);

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

	while (total > LIMIT) {
		total--;
	}

	switch (n) {
	case 0:
		total = 0;
		break;
	default:
		goto done;
	}

done:
	prototype_only();
	return total;
}
