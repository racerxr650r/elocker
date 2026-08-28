/* dark.c — the file whose regions must stay undecidable.
 *
 * Compiled WITHOUT -g into the same image, so the line information never
 * describes it. Its guarded region is the same shape as one in `branched.c`
 * and must reach the opposite answer: no coverage, no evidence, and the region
 * stays active and counted undecided (HLR-154, HLR-211).
 *
 * It is the dangerous case, and it is dangerous because it is silent. A rule
 * keyed on the absence of a line would find every region of this file
 * uncompiled and delete each one, and the result would be a smaller report
 * that is internally consistent and gives no sign it is wrong.
 */

int dark(int y)
{
	int n = y;

#ifdef ELC_FIXTURE_FEATURE
	n += 5;
#endif
	n += 2;
	return n;
}
