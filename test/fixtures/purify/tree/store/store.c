/* store.c — two ordinary functions.
 *
 * Neither is classified: each is called once, calls the sink once, and sits in
 * the same core as the rest of the connected centre. They are here because a
 * fixture in which every function is classified would pass against an
 * implementation that classified everything.
 *
 * Hand-worked; see ../../README.md.
 */

void util_log(const char *message);

void store_put(int value)
{
	(void)value;
	util_log("put");
}

int store_get(void)
{
	util_log("get");
	return 0;
}
