/* The other half of the component dependency cycle. See left.c. */

extern void left_lo(void);

void right_hi(void)
{
	left_lo();
}

void right_lo(void)
{
}
