/* harness.c — the host half of a shared memory map.
 *
 * Hand-counted; see ../../../README.md.
 */

int mailbox;

void host_writes(void)
{
	mailbox = 1;
}

void host_calls_in(void);

void target_entry(void);

void host_drives(void)
{
	target_entry();
}
