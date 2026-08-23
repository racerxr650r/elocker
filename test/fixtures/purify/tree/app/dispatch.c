/* dispatch.c — the planted god object.
 *
 * It calls widely and everything reaching the feature layer passes through it,
 * so it takes both the highest betweenness and the highest hub score in the
 * tree. That pairing is the whole of HLR-169: betweenness alone does not
 * separate a monolithic dispatcher from a legitimate waypoint a layering ought
 * to keep, and the hub score is what does.
 *
 * Hand-worked; see ../../README.md.
 */

void feat_a(void);
void feat_b(void);
void feat_c(void);
void util_log(const char *message);

void dispatch(int kind)
{
	if (kind == 0)
		feat_a();
	else if (kind == 1)
		feat_b();
	else
		feat_c();
	util_log("dispatched");
}
