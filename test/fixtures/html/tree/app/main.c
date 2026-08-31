/* app/main.c — the top layer. Two functions, three outgoing calls.
 *
 * `run` calls `hal_open` and `hal_close`, both in the layer below. `boot`
 * calls `run`, which is an edge inside this file and inside this layer — the
 * case that must NOT produce a meta-edge when the layer is collapsed, since
 * the viewer derives containment edges for itself (HLR-214).
 */
void hal_open(void);
void hal_close(void);

void run(void)
{
	hal_open();
	hal_close();
}

void boot(void)
{
	run();
}
