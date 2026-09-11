/* test/fixtures/concurrency/handlers.c — the second thread of control, in the
 * shape a bare-metal target actually writes it (HLR-227 – HLR-234).
 *
 * Hand counts, and the reasoning for each, in README.md beside this file.
 *
 * **Nothing here is expanded.** `elc` runs no preprocessor (HLR-135), so `ISR`
 * and `ATOMIC_BLOCK` reach the grammar as written. That is the whole point of
 * the fixture: both parse without error into shapes that are not what they
 * appear to be, and getting either one wrong is silent.
 *
 * The definitions below stand in for <avr/interrupt.h> and <util/atomic.h> so
 * the file compiles on the host — an image is built from it, and a fixture that
 * could not be linked could not verify the join by place (HLR-233).
 */

/* ISR(vec) { ... } expands to a definition the vector table names. The name it
 * defines is deliberately *not* the name written at the call site, which is the
 * rename the linkage join has to survive. */
#define ISR(vec)               void vec##_isr(void)
#define ATOMIC_BLOCK(kind)     for (int _once = 1; _once; _once = 0)
#define ATOMIC_RESTORESTATE    0

/* Shared with the handler below and correctly qualified. */
static volatile unsigned ticks;

/* Reached from the application and from a handler, so re-entrant. */
static void bump(unsigned by)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
	{
		ticks += by;
	}
}

/* Reached from the handler alone, so not re-entrant: nothing interrupts it in
 * the middle of itself. */
static void note_overflow(void)
{
	ticks = 0;
}

/* The handler. Nothing in this file calls it, its definition is written by a
 * macro, and the linker keeps it as `TIMER_vect_isr`. */
ISR(TIMER_vect)
{
	bump(1);
	if (ticks > 1000u)
		note_overflow();
}

/* An ordinary callback: registered by its address, and dispatched from the
 * application's own loop just below. Its thread of control is the
 * application's, which is exactly what elc cannot tell from the source — so
 * nothing it touches may be reported as confined or as shared. */
static volatile unsigned pending;

static void on_idle(void)
{
	pending++;
}

static void dispatch(void (*fn)(void))
{
	fn();
}

int main(void)
{
	dispatch(on_idle);
	bump(2);
	return (int)ticks;
}
