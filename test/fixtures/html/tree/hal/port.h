/* hal/port.h — declarations only, and that is the point.
 *
 * A C header of this shape defines no function, so it becomes a component
 * the graph holds with no node in it. It is discovered, measured and counted
 * in the report like any other file — and it is *not drawn*, because a box
 * that can hold no node and join no edge states nothing (LLR-CYT-02).
 *
 * The `.dot` companion has never drawn such a component; this file is what
 * holds the interactive drawing to the same answer.
 */

void hal_open(void);
void hal_close(void);
