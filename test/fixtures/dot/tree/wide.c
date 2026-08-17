/* A function whose fan-out crosses the published warning band.
 *
 * Eleven distinct callees. The bands are exhaustive and 11 is the first value
 * that warns, so this is the smallest tree that produces a fan-out finding at
 * all (HLR-086); one fewer leaf and the drawing would carry no threshold
 * annotation to check.
 */

static int w01(void) { return 1; }
static int w02(void) { return 2; }
static int w03(void) { return 3; }
static int w04(void) { return 4; }
static int w05(void) { return 5; }
static int w06(void) { return 6; }
static int w07(void) { return 7; }
static int w08(void) { return 8; }
static int w09(void) { return 9; }
static int w10(void) { return 10; }
static int w11(void) { return 11; }

int reader(void)
{
	return w01() + w02() + w03() + w04() + w05() + w06() +
	       w07() + w08() + w09() + w10() + w11();
}
