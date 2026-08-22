/* opaque.c — a translation unit the fixture compiles without debug
 * information, linked into the same image as covered.c.
 *
 * It contributes no line entries at all, so a rule keyed on the absence of a
 * line would find every line here uncompiled and delete the file. Absence
 * from a mapping that never described this file is evidence of nothing, and
 * this is the file that says so: its ELOC must be exactly what it is without
 * any image, and it must be counted among those whose coverage could not be
 * established (HLR-154, HLR-155).
 *
 * It is the dangerous case in the phase, and it is dangerous because it is
 * silent — the wrong answer here is a smaller report that is internally
 * consistent.
 */

int opaque(int y)
{
	int n = y;

	n += 2;
	n += 3;
	return n;
}
