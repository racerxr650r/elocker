/* main.c — the two functions that reach the dispatcher.
 *
 * `main` calls `boot`, and both call `dispatch`. Two callers rather than one
 * is what gives the dispatcher shortest paths to lie on: with a single entry
 * its betweenness would come from one source and the fixture would not
 * distinguish a dispatcher from an ordinary first step.
 *
 * Hand-worked; see ../../README.md.
 */

void dispatch(int kind);

/* Reached from main, and reaching the dispatcher itself. `main -> boot` is
 * the one call edge the recovery view keeps, which is what makes the retained
 * edge count assert something rather than always being zero. */
void boot(void)
{
	dispatch(0);
}

int main(void)
{
	boot();
	dispatch(1);
	return 0;
}
