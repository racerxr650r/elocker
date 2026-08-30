#!/usr/bin/env bats
# test/fixtures/html.bats — the interactive containment hierarchy (STP §5).
#
# Expected values are worked out by hand and justified in html/README.md beside
# the tree. Never regenerate them from elc's output.
#
# Two claims are kept apart throughout. That the payload is *well-formed* is
# settled by handing it to a JSON parser, never by matching text that looks
# like JSON — and that distinction is load-bearing here rather than stylistic,
# because the failure LLR-HTM-03 guards against produces text that is valid
# JSON and a syntax error once embedded. That the *hierarchy* says what the
# analyses found is settled against the hand-worked table in the README.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/html/tree"
	HTML="$BATS_TEST_TMPDIR/report.html"
}

# The format is selected by the extension and by nothing else — there is no
# option asking for it, which is the whole of HLR-215's selection rule.
run_elc() {
	run bash -c '"$0" -o "$1" \
		--stratum "app:*/app/*" --stratum "hal:*/hal/*" "$2" \
		2>/dev/null' "$ELC" "$HTML" "$TREE"
}

# The embedded document, extracted the way a browser reaches it: everything
# between the assignment and the statement that ends it. Emitted to stdout so a
# parser can read it.
payload() {
	grep '^const graphData = ' "$HTML" |
		sed -e 's/^const graphData = //' -e 's/;$//'
}

# Parse the payload and print one line per element. Python is already a
# required tool (the spec linter and the renderers are Python), so this adds no
# dependency — and a real parser is the point: it fails on exactly the embedded
# text a browser would fail on.
elements() {
	payload | python3 -c '
import json, sys
for e in json.load(sys.stdin):
    d = e["data"]
    if "source" in d:
        print("edge", d["source"], d["target"])
    else:
        print(d["tier"], d["id"], d.get("label", ""), d.get("parent", "-"))
'
}

# ------------------------------------------------------------- the artefact

@test "html: the .html extension selects the format, with no option asking" {
	run_elc
	assert_success
	[ -f "$HTML" ]
	run head -c 15 "$HTML"
	assert_output --partial "<!DOCTYPE html>"
}

@test "html: there is no --html option" {
	# The extension has already said what the format is. A flag saying it
	# again is the disagreement HLR-149 exists to prevent, arriving as a
	# third spelling (HLR-215).
	run "$ELC" --html -o "$HTML" "$TREE"
	assert_failure
	assert_output --partial "unrecognised option '--html'"
}

@test "html: --format html selects it for a report on standard output" {
	# Standard output has no filename and therefore no extension, so the
	# option is the means there — the rule every other format follows
	# (HLR-149).
	run bash -c '"$0" -f html "$1" 2>/dev/null' "$ELC" "$TREE"
	assert_success
	assert_output --partial "<!DOCTYPE html>"
	assert_output --partial "const graphData = "
}

@test "html: --format and a disagreeing extension are refused" {
	run "$ELC" -f md -o "$HTML" "$TREE"
	assert_failure
	assert_output --partial "disagree"
}

@test "html: the format is refused with --from-xml, not ignored" {
	# A record carries findings, not topology, so there is nothing to draw.
	# The user is told why rather than left to discover an empty drawing
	# (HLR-122, LLR-CLI-15).
	run "$ELC" --from-xml /dev/null -o "$HTML"
	assert_failure
	assert_output --partial "html format cannot be combined with --from-xml"
}

# -------------------------------------------------------------- the payload

@test "html: the embedded payload parses as JSON" {
	run_elc
	assert_success
	run bash -c 'grep "^const graphData = " "$0" |
		sed -e "s/^const graphData = //" -e "s/;$//" |
		python3 -c "import json,sys; json.load(sys.stdin)"' "$HTML"
	assert_success
}

@test "html: the payload carries no raw angle bracket or ampersand" {
	run_elc
	assert_success
	run bash -c 'grep "^const graphData = " "$0" |
		sed -e "s/^const graphData = //" -e "s/;$//" |
		grep -c "[<&]" || true' "$HTML"
	assert_output "0"
}

# ---------------------------------------------------------- the three tiers

@test "html: one node per declared stratum, and none for an undeclared one" {
	run_elc
	assert_success
	run elements
	assert_success

	# Two layers: `vendor/` is matched by no stratum and gets none.
	assert_line "layer layer_0 app -"
	assert_line "layer layer_1 hal -"
	refute_line --partial "layer layer_2"
}

@test "html: each file names the layer it was declared in" {
	run_elc
	assert_success
	run elements
	assert_success
	# The labels are exact, not anchored tails: every component shares the
	# tree's own directory, so the label is the path below it (LLR-CYT-02).
	assert_line "file file_0 app/main.c layer_0"
	assert_line "file file_1 hal/port.c layer_1"
}

@test "html: a file matching no stratum has no parent at all" {
	run_elc
	assert_success
	run elements
	assert_success
	# `-` is this helper's stand-in for an absent key, not an empty one:
	# the element carries no `parent` member (LLR-CYT-02).
	assert_line "file file_2 vendor/blob.c -"
}

@test "html: the label sheds the shared prefix and path keeps it" {
	# The label is for reading and `path` is the record: on every file
	# node the full path is the shed prefix followed by the label, so
	# nothing the label dropped is lost (LLR-CYT-02).
	run_elc
	assert_success
	run bash -c 'grep "^const graphData = " "$0" |
		sed -e "s/^const graphData = //" -e "s/;$//" |
		python3 -c "
import json, sys
for e in json.load(sys.stdin):
    d = e[\"data\"]
    if d.get(\"tier\") == \"file\":
        assert d[\"path\"].endswith(\"/\" + d[\"label\"]), d
        assert \"/tree/\" in d[\"path\"], d
print(\"ok\")
"' "$HTML"
	assert_success
	assert_output "ok"
}

@test "html: each function names the file that defines it" {
	run_elc
	assert_success
	run elements
	assert_success
	assert_line "function func_0 run file_0"
	assert_line "function func_1 boot file_0"
	assert_line "function func_2 hal_open file_1"
	assert_line "function func_3 hal_close file_1"
	assert_line "function func_4 vendor_init file_2"
}

@test "html: the node counts are the hand-worked ones" {
	run_elc
	assert_success
	# 2 layers, 3 files, 5 functions — html/README.md.
	run elements
	assert_success
	assert_equal "$(echo "$output" | grep -c '^layer ')"    "2"
	assert_equal "$(echo "$output" | grep -c '^file ')"     "3"
	assert_equal "$(echo "$output" | grep -c '^function ')" "5"
}

# -------------------------------------------------------------- the edges

@test "html: every edge joins two functions, and there are four of them" {
	run_elc
	assert_success
	run elements
	assert_success

	assert_line "edge func_1 func_0"
	assert_line "edge func_0 func_2"
	assert_line "edge func_0 func_3"
	assert_line "edge func_2 func_4"
	assert_equal "$(echo "$output" | grep -c '^edge ')" "4"
}

@test "html: no meta-edge is emitted between containers" {
	run_elc
	assert_success
	run elements
	assert_success
	# Three of the four edges cross a container boundary, and none of them
	# produces an edge naming a file or a layer: the viewer derives those
	# when it collapses (HLR-214).
	refute_line --partial "edge file_"
	refute_line --partial "edge layer_"
	run bash -c 'grep -cE "\"(source|target)\":\"(file|layer)_" "$0" || true' \
		"$HTML"
	assert_output "0"
}

# ------------------------------------------------------------------ the page

@test "html: the page loads the viewer and opens collapsed" {
	run_elc
	assert_success
	run cat "$HTML"
	assert_success
	assert_output --partial \
		"https://unpkg.com/cytoscape/dist/cytoscape.min.js"
	assert_output --partial \
		"https://unpkg.com/cytoscape-expand-collapse/cytoscape-expand-collapse.js"
	assert_output --partial "algorithm: 'layered'"
	assert_output --partial "'elk.hierarchyHandling': 'INCLUDE_CHILDREN'"
	assert_output --partial "fisheye: true"
	# The files, and only the files: a layer is context to be read, not a
	# box to be opened (HLR-216).
	assert_output --partial "api.collapse(cy.nodes('[tier = \"file\"]'));"
	refute_output --partial "api.collapseAll();"
	# The fit follows each layout as it settles rather than racing the two
	# asynchronous ones, and the reader's first gesture ends it — a fit
	# placed between them frames a drawing that is mid-flight (LLR-HTM-04).
	assert_output --partial "cy.on('layoutstop', refit)"
	assert_output --partial "cy.one('tap', function () { cy.off('layoutstop', refit); })"
	# The descent is bound by the page, not by whatever the extension's
	# current release happens to bind (HLR-216) — and on the file tier
	# alone, so a tap on a layer or a function reaches no handler.
	assert_output --partial "cy.on('tap', 'node[tier = \"file\"]', function"
}

@test "html: the artefact is byte-identical across runs" {
	run_elc
	assert_success
	cp "$HTML" "$BATS_TEST_TMPDIR/first.html"
	rm -f "$HTML"
	run_elc
	assert_success
	run diff "$BATS_TEST_TMPDIR/first.html" "$HTML"
	assert_success
}

@test "html: with no strata declared the hierarchy has two tiers" {
	run bash -c '"$0" -o "$1" "$2" 2>/dev/null' \
		"$ELC" "$HTML" "$TREE"
	assert_success
	run elements
	assert_success
	refute_line --partial "layer "
	assert_equal "$(echo "$output" | grep -c '^file ')" "3"
	# Every file is a root container, since none was placed.
	assert_equal "$(echo "$output" | grep -c '^file .* -$')" "3"
}

@test "html: the drawing carries the findings the report states" {
	# The same annotation the .dot companion draws, on the same nodes: a
	# drawing that showed the topology and withheld every judgement would
	# be half the artefact (HLR-217).
	run_elc
	assert_success
	run bash -c 'grep "^const graphData = " "$0" |
		sed -e "s/^const graphData = //" -e "s/;$//" |
		python3 -c "
import json, sys
sev = 0
for e in json.load(sys.stdin):
    d = e[\"data\"]
    if d.get(\"severity\"):
        sev += 1
        assert d[\"severity\"] in (\"info\", \"warning\", \"critical\"), d
        assert d.get(\"finding\"), d
    for m in (\"unreachable\", \"recursive\", \"deepest\", \"hidden\", \"soleUser\"):
        assert d.get(m, True) is True, d
print(\"annotated\", sev)
"' "$HTML"
	assert_success
	assert_output --partial "annotated"
}

@test "html: the page states its own key" {
	# The .dot companion explains itself in a header comment; this one
	# explains itself above the drawing, where a reader sent the file
	# alone will meet it (HLR-217, LLR-HTM-06).
	run_elc
	assert_success
	run cat "$HTML"
	assert_success
	assert_output --partial 'id="legend"'
	assert_output --partial "warning"
	assert_output --partial "critical"
	assert_output --partial "recursive"
	assert_output --partial "unreachable"
	assert_output --partial "deepest call chain"
	assert_output --partial "hidden channel"
	assert_output --partial "sole namer of a global"
	# The severity pigments are the ones the .dot writer uses, so a reader
	# does not learn two colour schemes for one judgement.
	assert_output --partial "#f7e0b0"
	assert_output --partial "#f6c7c7"
}
