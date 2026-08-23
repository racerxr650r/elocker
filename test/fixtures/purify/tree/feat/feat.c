/* feat.c — the feature layer, and the one peripheral function.
 *
 * `helper_c` is called by exactly one function and calls nothing, so its
 * undirected degree is one and it lies in the first core alone. It is
 * *excluded* from the recovery view rather than placed at the bottom of it: a
 * function elc did not consider is not a function elc put at the edge of the
 * architecture (HLR-170).
 *
 * Hand-worked; see ../../README.md.
 */

void helper_c(void);
void store_put(int value);
int  store_get(void);
void util_log(const char *message);

void feat_a(void)
{
	store_put(1);
	util_log("a");
}

void feat_b(void)
{
	(void)store_get();
	util_log("b");
}

/* The only caller of helper_c, and the reason helper_c has a degree at all. */
void feat_c(void)
{
	helper_c();
	util_log("c");
}

void helper_c(void)
{
	return;
}
