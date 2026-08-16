int shared_counter;

static int bump(int by)
{
	return by + 1;
}

void tick(void)
{
	shared_counter = bump(1);
	shared_counter = bump(2);
	external_log();
}
