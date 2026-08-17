/* A hidden channel: one object joining two functions that never call each
 * other, in two regions of the call graph with no path between them
 * (HLR-093). Neither is reachable from the declared entry point either, so
 * both carry two annotations at once — which is the property worth pinning,
 * since each rides a different Graphviz attribute and neither may overwrite
 * the other.
 */

int shared_flag;

void producer(void)
{
	shared_flag = 1;
}

int consumer(void)
{
	return shared_flag;
}
