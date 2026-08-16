/* only.c — a component nothing depends on, depending on nothing.
 *
 * Ca and Ce are both zero, so Instability is genuinely undefined rather than
 * zero, one, or a division error (HLR-082).
 *
 * Two mutually recursive functions live here on purpose: they are a recursion
 * finding and they are NOT a component cycle, because a file does not depend
 * on itself (HLR-083, LLR-CYC-03).
 */

static void ping(void);

static void pong(void)
{
	ping();
}

static void ping(void)
{
	pong();
}

int lone_entry(void)
{
	return 0;
}
