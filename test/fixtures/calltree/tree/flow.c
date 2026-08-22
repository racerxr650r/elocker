/* flow.c — fan-in, and the Henry-Kafura value formed from it.
 *
 * Every function here sits at a hand-chosen point of the call graph, so that
 * the table in README.md can be checked line by line against the source. The
 * shape is deliberate in three ways:
 *
 *   * `hub` is the only function with a caller *and* a callee, so it is the
 *     only one whose Henry-Kafura value can be non-zero. Both ends of the
 *     graph are present beside it, and both must print 0.
 *   * `hub` calls `leaf_a` and `leaf_b` twice each. Fan-out must stay 2, and
 *     the four call statements must still count four toward ELOC — the two
 *     figures are counted differently and the repeat is what separates them.
 *   * `flow_entry` is long and wide and scores nothing, because nothing calls
 *     it. That is the property a reader misreads as an absence of code.
 */

static void leaf_a(void) { }

static void leaf_b(void) { }

static void leaf_c(void) { }

static void hub(void)
{
	leaf_a();
	leaf_b();
	leaf_a();
	leaf_b();
}

static void caller_one(void) { hub(); }

static void caller_two(void) { hub(); }

static void caller_three(void)
{
	hub();
	leaf_c();
}

void flow_entry(void)
{
	caller_one();
	caller_two();
	caller_three();
}
