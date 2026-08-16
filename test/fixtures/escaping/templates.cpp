/* templates.cpp — an identifier that carries the characters CSV and XML
 * reserve. Hand-counted in README.md beside this file.
 *
 * An explicit template specialisation names itself with its template
 * arguments, so `combine<int, long>` is one identifier containing a comma and
 * two angle brackets. This is the value HLR-064 and HLR-065 were written for,
 * and until C++ arrived no shipped language could produce one.
 */
template <typename A, typename B>
struct Pair {
	A first;
	B second;
};

template <typename A, typename B>
void combine(Pair<A, B> p)
{
	(void)p;
}

template <>
void combine<int, long>(Pair<int, long> p)
{
	(void)p;
}
