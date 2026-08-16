/* fanout.c — one function per fan-out boundary value Phase 12 will band.
 *
 * Each caller invokes N distinct helpers, and calls the first of them a
 * second time. The repeat is deliberate: fan-out must stay N, because it
 * counts callees and not call sites.
 */

static void h01(void) { }
static void h02(void) { }
static void h03(void) { }
static void h04(void) { }
static void h05(void) { }
static void h06(void) { }
static void h07(void) { }
static void h08(void) { }
static void h09(void) { }
static void h10(void) { }
static void h11(void) { }
static void h12(void) { }
static void h13(void) { }
static void h14(void) { }
static void h15(void) { }
static void h16(void) { }

void fan02(void) { h01(); h02(); h01(); }
void fan03(void) { h01(); h02(); h03(); h01(); }
void fan07(void) { h01(); h02(); h03(); h04(); h05(); h06(); h07(); h01(); }
void fan08(void) { h01(); h02(); h03(); h04(); h05(); h06(); h07(); h08(); h01(); }
void fan10(void) { h01(); h02(); h03(); h04(); h05(); h06(); h07(); h08(); h09(); h10(); h01(); }
void fan11(void) { h01(); h02(); h03(); h04(); h05(); h06(); h07(); h08(); h09(); h10(); h11(); h01(); }
void fan15(void) { h01(); h02(); h03(); h04(); h05(); h06(); h07(); h08(); h09(); h10(); h11(); h12(); h13(); h14(); h15(); h01(); }
void fan16(void) { h01(); h02(); h03(); h04(); h05(); h06(); h07(); h08(); h09(); h10(); h11(); h12(); h13(); h14(); h15(); h16(); h01(); }
