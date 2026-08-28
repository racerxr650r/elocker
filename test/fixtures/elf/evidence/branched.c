/* branched.c — regions the source cannot decide and the image can.
 *
 * Compiled WITH -g, and never with `ELC_FIXTURE_FEATURE` defined; `elc` is
 * never told about that symbol either. So both regions here are undecidable
 * from the source (HLR-133), and the only thing that can settle them is what
 * the build compiled (HLR-211).
 *
 * Both regions carry an alternative, which is the strongest form the evidence
 * takes: exactly one of the two branches produced instructions, and the image
 * says which. One is written so the region is inactive and one so it is
 * active, because the two dispositions prune opposite halves and a rule that
 * only ever answered one of them would pass a test for the other by doing
 * nothing.
 */

int branched(int x)
{
	int total = 0;

	total += x;
#ifdef ELC_FIXTURE_FEATURE
	total += 100;
	total += 200;
#else
	total += 7;
#endif
#ifndef ELC_FIXTURE_FEATURE
	total += 3;
#else
	total += 400;
#endif
	return total;
}
