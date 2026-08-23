/* app.c — the top of the cyclic tree.
 *
 * Identical to the layered tree's top layer. What differs is below it, so
 * that the two trees differ in exactly one property: whether the recovery
 * view still holds a cycle after purification.
 *
 * Hand-worked; see ../../README.md.
 */

void svc_open(void);
void svc_close(void);

void app_start(void)
{
	svc_open();
	svc_close();
}

void app_stop(void)
{
	svc_open();
	svc_close();
}
