/* hal.c — the bottom layer.
 *
 * Called from the service layer, and calling back into `svc_leaf` — a
 * completion callback, which is an ordinary shape in a layered embedded
 * program and is *not* a cycle: `svc_leaf` calls nothing, so the graph stays
 * a DAG and a layering still exists.
 *
 * Hand-worked; see ../../README.md.
 */

void svc_leaf(void);

void hal_init(void)
{
	svc_leaf();
}

void hal_stop(void)
{
	svc_leaf();
}
