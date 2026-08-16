/* unreachable.c — data condemned by the same traversal.
 *
 * Hand-counted; see ../README.md. Declared entry point: `data_main`.
 */

int touched_by_dead;
int touched_by_live;

/* Nothing calls this, so the object it is the only accessor of is unreachable
 * too — the storage goes with the code (HLR-096). */
static void dead_writer(void)
{
	touched_by_dead = 1;
	touched_by_dead = touched_by_dead + 1;
}

int data_main(void)
{
	touched_by_live = 1;
	return touched_by_live + 0;
}
