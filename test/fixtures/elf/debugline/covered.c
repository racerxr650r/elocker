/* covered.c — lines a build did not compile, inside a function it kept.
 *
 * The region is guarded by a symbol the fixture's build never defines, and
 * `elc` is never told about it either. That is the whole point: with no `-D`
 * on the command line the condition is one `elc` cannot decide, so Phase 15
 * leaves the region **active** and counts it undecided (HLR-133). Every line
 * of it therefore survives into the measurement, and only the image's line
 * information can say the build compiled none of them.
 *
 * A fixture using `#if 0` would prove nothing. That condition is decidable
 * from the source alone, so the region is already gone before the image is
 * consulted, and the test would pass against an implementation that read no
 * debug information at all.
 *
 * `always` is compiled by every build and is kept whole, so a run that pruned
 * the file wholesale fails here rather than passing quietly.
 */

int always(int x)
{
	int n = x;

	n += 1;
	return n;
}

int guarded(int x)
{
	int total = 0;

	total += x;
#ifdef ELC_FIXTURE_FEATURE
	total += 100;
	total += 200;
	total += 300;
#endif
	total += 1;
	return total;
}

int main(void)
{
	return always(1) + guarded(2);
}
