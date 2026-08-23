/* log.c — the planted utility sink.
 *
 * Six functions call it and it calls nothing, so its hub score is exactly zero
 * and its authority is the highest in the tree. Its *incoming* edges are what
 * fuse the callers into one region, and those are what the recovery view masks
 * — the node itself stays, and so does anything it calls (HLR-168).
 *
 * Hand-worked; see ../../README.md.
 */

void util_log(const char *message)
{
	(void)message;
}
