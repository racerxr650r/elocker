/* firmware.c — the target half.
 *
 * `target_entry` is called from the host scope, and `target_reads` reads a
 * variable the host scope writes. Both cross the boundary, and by different
 * means: a scope that only shares a variable has not been isolated either.
 */

extern int mailbox;

void target_reads(void)
{
	int local = mailbox + 0;

	(void)local;
}

void target_entry(void)
{
	target_reads();
}
