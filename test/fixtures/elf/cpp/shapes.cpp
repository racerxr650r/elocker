/* shapes.cpp — the Itanium C++ ABI case.
 *
 * Every name here reaches the image encoded: `area` is recorded as
 * `_ZNK8geometry4Rect4areaEv` and demangles to `geometry::Rect::area() const`,
 * which is a qualified name and a signature where the report presents the
 * identifier alone. Matching raw linkage names would retain nothing at all
 * here, and matching a demangled one without reducing it would match nothing
 * either (HLR-142).
 *
 * `perimeter` is defined and never called, so an unoptimised build emits no
 * out-of-line copy of it and the image does not define it — the finding the
 * option exists to produce, from a build that discarded it rather than from a
 * traversal that could not reach it.
 */
namespace geometry {

class Rect {
public:
	Rect(int w, int h) : w_(w), h_(h) {}

	int area() const
	{
		return w_ * h_;
	}

	int perimeter() const
	{
		return 2 * (w_ + h_);
	}

private:
	int w_;
	int h_;
};

int scale(const Rect &r, int by)
{
	return r.area() * by;
}

}  /* namespace geometry */

int main(void)
{
	geometry::Rect r(3, 4);

	return geometry::scale(r, 2) - 24;
}
