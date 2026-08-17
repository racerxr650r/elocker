/* shim.c — a definition of a function the image imports rather than defines.
 *
 * `kept.c` calls printf, so the image carries a printf symbol of function type
 * whose section index is SHN_UNDEF. Without that test this definition would be
 * retained, and the filter would then keep source the build never compiled —
 * which is the failure LLR-ELF-02 exists to prevent. This file is parsed and
 * never compiled, so the redefinition it would be to a compiler is of no
 * consequence to a reader of it.
 */

int printf(const char *fmt, ...)
{
	return fmt ? 0 : -1;
}
