/* opaque.s — a function symbol in a mangling this build does not decode.
 *
 * `_RNvCs1234_7mycrate3foo` is Rust's v0 scheme, which the C++ runtime's
 * Itanium demangler rejects. It is written in assembly rather than produced by
 * a Rust compiler for the reason the images are built rather than committed: a
 * fixture nobody can review is no fixture, and requiring a second toolchain to
 * run the suite would leave the case unverified wherever that toolchain is
 * absent.
 *
 * The symbol needs a name, a type, and a definition — no instruction, since
 * nothing ever calls it. That also keeps the file free of any architecture's
 * mnemonics.
 */
	.text
	.globl	_RNvCs1234_7mycrate3foo
	.type	_RNvCs1234_7mycrate3foo, @function
_RNvCs1234_7mycrate3foo:
	.byte	0
	.size	_RNvCs1234_7mycrate3foo, .-_RNvCs1234_7mycrate3foo
