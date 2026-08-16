/* hal.c — the middle layer.
 *
 * Hand-counted; see ../../README.md.
 */

void drv_poke(void);
void app_notify(void);

void hal_read(void)
{
	drv_poke();
}

void hal_write(void)
{
	drv_poke();
}

/* Calling upward into the application layer inverts the declared direction
 * without bypassing any intervening layer (HLR-118). */
void hal_callback(void)
{
	app_notify();
}
