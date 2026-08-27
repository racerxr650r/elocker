/* The hand-expanded equivalent of shapes.c, which is what the repaired file
 * must measure. Written out by hand rather than generated, so the two are
 * independent statements of the same expectation.
 */
static int branchy2(int n)
{
	if (n > 0)
		return n;
	if (n < -1)
		return -n;
	return 0;
}

void report2(int n)
{
	printf("\033[1m" "\033[34m" "value: %d" "\033[0m" "\n", branchy2(n));
}

int table2[3] = { 1, 2, 3 };
