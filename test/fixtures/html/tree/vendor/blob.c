/* vendor/blob.c — matched by no declared stratum.
 *
 * One function. Its file node must carry no `parent` key at all: the user
 * declared nothing about this directory, and inventing a layer for it would
 * report a structure nobody drew (HLR-213, HLR-078).
 */
void vendor_init(void)
{
}
