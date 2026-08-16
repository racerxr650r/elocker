/* b.c — the other half. Together with a.c this is both a recursion
 * finding and a component cycle, because the two facts are different.
 */

void a_side(void);

void b_side(void)
{
	a_side();
}
