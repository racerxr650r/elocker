/* The entry point, and one function nothing reaches.
 *
 * `run` is what the suite declares with --entry, which makes it the root
 * reachability is measured from; `orphan` is defined, never called, and never
 * has its address taken, so no path reaches it (HLR-096).
 */

extern int helper(int n);
extern int reader(void);

static int orphan(int n)
{
	return n * 3;
}

int run(int n)
{
	return helper(n) + reader();
}
