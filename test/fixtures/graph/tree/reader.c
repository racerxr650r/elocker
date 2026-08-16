static void (*hook)(void);

int report(void)
{
	return shared_counter;
}

void install(void)
{
	hook = report;
}
