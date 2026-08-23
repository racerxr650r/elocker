/* hal.c — the bottom layer, calling back up into the one above it.
 *
 * `hal_init` calls `svc_open` and `hal_stop` calls `svc_close`, each of which
 * called it. Those two mutual pairs are what make the recovery view cyclic:
 * no topological ordering of it exists, so there is no layering to read off
 * and the cycles are reported in its place (HLR-172).
 *
 * The cycle survives purification rather than being broken by it, which is
 * the property this tree is here to pin. Nothing in it is classified — no
 * function has the authority of a sink, the betweenness and hub of a god
 * object, or a coreness below the second core — so the recovery view is the
 * call graph, cycles and all.
 *
 * Hand-worked; see ../../README.md.
 */

void svc_open(void);
void svc_close(void);

void hal_init(void)
{
	svc_open();
}

void hal_stop(void)
{
	svc_close();
}
