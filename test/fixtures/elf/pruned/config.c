/* config.c — a function no configuration builds, beside one every build does.
 *
 * `never_built` sits inside a constant condition, so it is pruned before the
 * image is consulted at all. That ordering is the point: a function this
 * configuration does not compile is not a function the linker discarded, and
 * reporting it as one would answer a question about the image with a fact
 * about the preprocessor (LLR-ANL-58).
 *
 * `active` is compiled by every configuration and is absent from the image
 * used to filter this directory, so it is reported — which is what keeps the
 * test from passing merely because nothing was reported at all.
 */

int active(void)
{
	return 1;
}

#if 0
int never_built(void)
{
	return 0;
}
#endif
