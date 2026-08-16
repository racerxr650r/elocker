/* app.c — the top layer. Depends downward on hal, and once on drv directly.
 *
 * Hand-counted; see ../../README.md.
 */

void hal_read(void);
void hal_write(void);
void drv_poke(void);

void app_run(void)
{
	hal_read();
	hal_write();
	hal_read();       /* twice: one dependency, not two */
}

/* The skip-level call: app reaches past hal into drv (HLR-079). It descends
 * two layers and inverts nothing. */
void app_shortcut(void)
{
	drv_poke();
}

/* Called from hal, which is the direction-inverted case (HLR-118). It
 * ascends one layer and skips nothing. */
void app_notify(void)
{
	return;
}
