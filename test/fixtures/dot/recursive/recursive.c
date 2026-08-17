/* Mutual recursion, in a tree of its own.
 *
 * Separate from `tree/` and not by preference: recursion makes the call
 * depth unbounded, so a tree containing it has no deepest chain to measure.
 * One tree cannot demonstrate both annotations, which is a fact about the
 * measurements rather than about the drawing (HLR-087, HLR-089).
 */

static void pong(int n);

static void ping(int n)
{
	if (n > 0)
		pong(n - 1);
}

static void pong(int n)
{
	if (n > 0)
		ping(n - 1);
}

void kick(int n)
{
	ping(n);
}
