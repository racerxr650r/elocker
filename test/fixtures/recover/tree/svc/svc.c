/* svc.c — the middle layer, and the outlier that must not drag it down.
 *
 * `svc_open` and `svc_close` sit where a service layer sits: called from
 * above, calling into the hardware layer below. `svc_leaf` is the third
 * function and the reason this tree exists rather than a two-file one — it is
 * called *by* the layer beneath, so it lands at the very bottom of the
 * topological order while the rest of its directory sits near the top.
 *
 * A fold that placed a directory at its earliest or latest member would put
 * `svc/` below `hal/` on the strength of that one function. A fold that asks
 * where the bulk of the directory's edges point does not: `svc_leaf` carries
 * two of the directory's ten edge ends, and the other eight are where the
 * layer really is (HLR-172).
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

/* Called from the hardware layer and calling nothing. It is the one function
 * in the tree whose topological position is nowhere near its directory's. */
void svc_leaf(void)
{
	return;
}
