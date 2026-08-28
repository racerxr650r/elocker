/* hal/port.c — the middle layer. Two functions, one outgoing call.
 *
 * `hal_open` calls `vendor_init`, which is the only edge reaching the
 * undeclared layer — so it is the edge that proves an unparented container
 * still receives its function nodes and its edges (HLR-213).
 */
void vendor_init(void);

void hal_open(void)
{
	vendor_init();
}

void hal_close(void)
{
}
