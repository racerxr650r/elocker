/* a.c — a small subject with something in every tier of the report. */
int global_state = 1;

int branchy(int n)
{
	if (n > 0 && n < 10)
		return n;
	while (n--)
		global_state++;
	return global_state;
}

int plain(void)
{
	return 0;
}
