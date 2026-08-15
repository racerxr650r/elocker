# `escaping/` — the characters that corrupt a document quietly

The fixture header for this group. Asserted by
[`../escaping.bats`](../escaping.bats).

CSV and XML both have characters that mean something structural. A field
containing a comma is two fields; a value containing `<` opens a tag. Neither
failure announces itself: the CSV still parses, with the wrong shape, and the
XML fails somewhere far from the value that broke it.

## The natural source is a C++ template signature

`foo<int, long>` is one identifier carrying a comma *and* two angle brackets.
It is the case both requirements were written for (HLR-064, HLR-065), and
`elc` cannot produce it yet — C has no templates, and C++ arrives in Phase 6.

Waiting for Phase 6 would mean shipping two formats whose escaping has never
met a value that needs it. This group closes that gap two ways.

## Where the hostile values come from

**A path, not an identifier.** A directory may be named anything a filesystem
permits, and `elc` reports paths in every format. A directory named

```text
tmpl<int, long> & "quoted"
```

puts a comma, both angle brackets, an ampersand, and a quotation mark through
the whole pipeline — discovery, the report model, and every renderer — with no
grammar involved. It is a real end-to-end path today, not a stand-in.

The tree is built by the suite rather than committed, because a directory name
containing a quotation mark is portable to fewer tools than it is to
filesystems, and a fixture that cannot be checked out is not a fixture.

**The escaping functions directly.** `write_field` and `write_escaped` are
unit-tested against the values themselves — a template signature, an already
escaped ampersand, a field with a comma and a quote and a newline at once.
That is where the shape of the output is pinned; this group checks that the
values reach those functions from every path that emits them.

## The cases

| Case | Expected |
| ---- | -------- |
| CSV over a hostile path | parses back to the same field count as an ordinary run |
| CSV over a hostile path | the path arrives intact, comma and all |
| XML over a hostile path | `xmllint` accepts it |
| XML over a hostile path | the path arrives intact after unescaping |
| a round trip through the record | Markdown identical to a direct run |

The last is the one that catches an asymmetry: a value escaped on the way out
and not unescaped on the way in survives `xmllint` and still corrupts the
report.

## The identifier case, closed

`templates.cpp` supplies what the path could only stand in for. An explicit
template specialisation names itself with its template arguments, so

```cpp
template <> void combine<int, long>(Pair<int, long> p) { ... }
```

is reported under the name `combine<int, long>` — **one identifier carrying a
comma and two angle brackets**, produced by the analyser rather than
constructed by the suite.

| Value | Expected |
| ----- | -------- |
| Physical lines | **25** |
| File ELOC | **2** |
| Functions | **2** — `combine` and `combine<int, long>` |

The cases it closes:

| Format | Assertion |
| ------ | --------- |
| CSV | every row is 7 fields, and the name reads back as one value |
| XML | emitted as `combine&lt;int, long&gt;`, never raw |
| XML | `xmllint` accepts the document |
| record | Markdown regenerated from it matches a direct run |

Adding this required no change to the escaping code — which is the result
worth having. The functions were written against the values in Phase 5 and
met the real one two phases later without alteration.
