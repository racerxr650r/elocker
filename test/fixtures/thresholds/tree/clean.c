/* clean.c — a project with nothing outside any band.
 *
 * Hand-counted; see ../README.md. Every measurement here sits inside its
 * accepted range, so the Findings section must be empty — which is a result,
 * not an absence of analysis.
 */

static void leaf_a(void) { return; }
static void leaf_b(void) { return; }
static void leaf_c(void) { return; }

int clean_entry(void)
{
	leaf_a();
	leaf_b();
	leaf_c();
	return 0;
}
