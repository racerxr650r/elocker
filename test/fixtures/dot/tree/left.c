/* Half of a component dependency cycle.
 *
 * left.c calls into right.c and right.c calls back into left.c, but no
 * *function* calls itself even indirectly: left_hi calls right_lo, and
 * right_hi calls left_lo. The call graph is acyclic and the component graph is
 * not, which is the distinction the three graph views exist to keep (HLR-083
 * against HLR-089) — and the reason this tree can carry both a cycle
 * annotation and a measured deepest chain at once.
 */

extern void right_lo(void);

void left_hi(void)
{
	right_lo();
}

void left_lo(void)
{
}
