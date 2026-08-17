/* The deepest call chain: run -> helper -> step1 -> step2 -> step3 -> step4.
 *
 * Six layers, which is inside the accepted band, so the chain is *measured*
 * without also being a finding — which is the case worth pinning. The chain is
 * annotated because HLR-105 asks for it, not because it crossed a line.
 */

static int step4(int n)
{
	return n + 1;
}

static int step3(int n)
{
	return step4(n) * 2;
}

static int step2(int n)
{
	return step3(n) - 1;
}

static int step1(int n)
{
	return step2(n) / 2;
}

int helper(int n)
{
	return step1(n);
}
