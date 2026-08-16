/* depth.c — a straight call chain, and a shallower branch beside it.
 *
 * The branch exists so that "deepest" means something: a traversal taking the
 * first edge rather than the deepest would report the wrong chain and the
 * right length.
 */

static int level4(int x) { return x + 1; }

static int level3(int x) { return level4(x); }

static int level2(int x) { return level3(x); }

static int shallow(int x) { return x; }

int entry_main(int x) { return shallow(x) + level2(x); }
