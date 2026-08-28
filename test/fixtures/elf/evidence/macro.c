/* macro.c — a function the grammar cannot see and the image can place.
 *
 * `ELC_FIXTURE_HANDLER` expands to a whole function definition, which is what
 * `ISR(USART0_DRE_vect)` does on an AVR target. Tree-sitter finds no function
 * at that line — it is looking at the macro — and repair cannot help, because
 * repair does not know the macro *defines* one. Only the debug information
 * says a function was written there (HLR-212).
 *
 * The suite reads this file with `--no-expand`, which is the state a
 * cross-compiled tree is in for real: where the preprocessor can be run and
 * succeeds, the definition is expanded into place and parsed like any other,
 * and there is nothing left to recover. Both halves are asserted.
 */

int branched(int x);
int dark(int y);

#define ELC_FIXTURE_HANDLER(name) \
	void name(void) { volatile int seen = 1; (void)seen; }

int reached(int x)
{
	return x + 1;
}

ELC_FIXTURE_HANDLER(from_macro);

int main(void)
{
	from_macro();
	return reached(1) + branched(2) + dark(3);
}
