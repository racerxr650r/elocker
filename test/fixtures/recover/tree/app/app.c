/* app.c — the top of the plainly layered tree.
 *
 * Two functions, each calling both service entry points and nothing else. Two
 * rather than one is what gives the layer a *position* rather than a point:
 * a directory whose single function happened to sort first would place the
 * layer correctly for the wrong reason, and the fixture could not tell the
 * fold from the ordering underneath it.
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
