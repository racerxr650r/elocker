/* globals.c — the three verdicts on a global object.
 *
 * Hand-counted; see ../README.md.
 */

int solo_owned;
int shared_ok;
int hidden;

/* One function names it: MISRA C Rule 8.9 says it belongs at block scope. */
void owner(void)
{
	solo_owned = 1;
	solo_owned = solo_owned + 1;
}

/* Written and read across a call edge — ordinary shared state, and reported
 * as a measurement with no finding. */
void consumer(void)
{
	int local = shared_ok + 0;

	(void)local;
}

void producer(void)
{
	shared_ok = 2;
	consumer();
}

/* Written here, read there, and the two never call each other: the temporal
 * coupling MISRA C Rule 8.9 is concerned with. */
void island_a(void)
{
	hidden = 3;
}

void island_b(void)
{
	int local = hidden + 0;

	(void)local;
}
