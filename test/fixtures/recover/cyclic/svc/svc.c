/* svc.c — the middle layer of the cyclic tree.
 *
 * Hand-worked; see ../../README.md.
 */

void hal_init(void);
void hal_stop(void);

void svc_open(void)
{
	hal_init();
	hal_stop();
}

void svc_close(void)
{
	hal_init();
	hal_stop();
}
