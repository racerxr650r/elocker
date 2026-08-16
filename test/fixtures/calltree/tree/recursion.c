/* recursion.c — direct and mutual recursion, the two MISRA C Rule 17.2 cares
 * about, in the smallest form that produces each.
 */

static int countdown(int n);

static int self_calling(int n)
{
	return n <= 1 ? 1 : n * self_calling(n - 1);
}

static int bounce(int n)
{
	return countdown(n - 1);
}

static int countdown(int n)
{
	return n <= 0 ? 0 : bounce(n);
}

int recursive_entry(int n) { return self_calling(n) + countdown(n); }
