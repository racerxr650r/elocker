/* roots.c — the root set, and the two cases that decide whether the
 * dead-code claim is sound.
 *
 * Hand-counted; see ../README.md. Only `entry_main` is declared with --entry.
 */

static void used_helper(void)
{
	return;
}

/* Reached only through a stored function pointer, never called by name. It
 * must not be reported dead, and neither must what it calls. */
static void callback_callee(void)
{
	return;
}

static void callback(void)
{
	callback_callee();
}

typedef void (*handler)(void);

handler vector_table[] = { callback };

/* Two unused functions that call one another. A textual linter sees a caller
 * for each and reports neither; traversal reaches neither from any root. */
static void clique_a(void);

static void clique_b(void)
{
	clique_a();
}

static void clique_a(void)
{
	clique_b();
}

/* Unused and calling nothing: the easy case, here so the hard ones are not
 * the only evidence. */
static void orphan(void)
{
	return;
}

int entry_main(void)
{
	used_helper();
	return 0;
}
