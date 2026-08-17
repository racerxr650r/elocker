/* bands.c — one function per fan-out band boundary.
 *
 * Hand-counted; see ../README.md. The eight values are the ones
 * `calltree/fanout.c` pins as *measurements*; here they are banded.
 */

static void h01(void) { return; }
static void h02(void) { return; }
static void h03(void) { return; }
static void h04(void) { return; }
static void h05(void) { return; }
static void h06(void) { return; }
static void h07(void) { return; }
static void h08(void) { return; }
static void h09(void) { return; }
static void h10(void) { return; }
static void h11(void) { return; }
static void h12(void) { return; }
static void h13(void) { return; }
static void h14(void) { return; }
static void h15(void) { return; }
static void h16(void) { return; }

static void band_below(void)
{
	h01();
	h02();
}

static void band_healthy_low(void)
{
	h01();
	h02();
	h03();
}

static void band_healthy_high(void)
{
	h01();
	h02();
	h03();
	h04();
	h05();
	h06();
	h07();
}

static void band_acceptable_low(void)
{
	h01();
	h02();
	h03();
	h04();
	h05();
	h06();
	h07();
	h08();
}

static void band_acceptable_high(void)
{
	h01();
	h02();
	h03();
	h04();
	h05();
	h06();
	h07();
	h08();
	h09();
	h10();
}

static void band_warn_low(void)
{
	h01();
	h02();
	h03();
	h04();
	h05();
	h06();
	h07();
	h08();
	h09();
	h10();
	h11();
}

static void band_warn_high(void)
{
	h01();
	h02();
	h03();
	h04();
	h05();
	h06();
	h07();
	h08();
	h09();
	h10();
	h11();
	h12();
	h13();
	h14();
	h15();
}

static void band_critical_one(void)
{
	h01();
	h02();
	h03();
	h04();
	h05();
	h06();
	h07();
	h08();
	h09();
	h10();
	h11();
	h12();
	h13();
	h14();
	h15();
	h16();
}

int bands_entry(void)
{
	band_below();
	band_healthy_low();
	band_healthy_high();
	band_acceptable_low();
	band_acceptable_high();
	band_warn_low();
	band_warn_high();
	band_critical_one();
	return 0;
}
