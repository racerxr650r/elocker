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
	OUT="$BATS_TEST_TMPDIR/report.md"
	HTML="$BATS_TEST_TMPDIR/report.html"
}

run_elc() {
	run bash -c '"$0" -o "$1" --html \
		--stratum "app:*/app/*" --stratum "hal:*/hal/*" "$2" \
		2>/dev/null' "$ELC" "$OUT" "$TREE"
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

@test "html: the companion is written beside the report and named from it" {
	run_elc
	assert_success
	[ -f "$HTML" ]
}

@test "html: no companion is written when the report goes to standard output" {
	cd "$BATS_TEST_TMPDIR"
	run bash -c '"$0" --html --stratum "app:*/app/*" "$1" >/dev/null 2>&1' \
		"$ELC" "$TREE"
	assert_success
	[ ! -f "$BATS_TEST_TMPDIR/report.html" ]
	# And nothing else was written either: the name is derived from the
	# report's path, and there is none (HLR-104, HLR-119).
	run bash -c 'ls "$BATS_TEST_TMPDIR"/*.html 2>/dev/null | wc -l'
	assert_output "0"
}

@test "html: an explicit request with --from-xml is refused, not ignored" {
	# A record carries findings, not topology, so there is nothing to draw.
	# The user is told why rather than left to discover the absence
	# (HLR-122, LLR-CLI-15).
	run "$ELC" --html --from-xml /dev/null -o "$OUT"
	assert_failure
	assert_output --partial "--html cannot be combined with --from-xml"
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
	assert_line --regexp '^file file_0 .*/app/main\.c layer_0$'
	assert_line --regexp '^file file_1 .*/hal/port\.c layer_1$'
}

@test "html: a file matching no stratum has no parent at all" {
	run_elc
	assert_success
	run elements
	assert_success
	# `-` is this helper's stand-in for an absent key, not an empty one:
	# the element carries no `parent` member (LLR-CYT-02).
	assert_line --regexp '^file file_2 .*/vendor/blob\.c -$'
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
	assert_output --partial "name: 'cose'"
	assert_output --partial "fisheye: true"
	assert_output --partial "animate: true"
	assert_output --partial "api.collapseAll();"
	# The descent is bound by the page, not by whatever the extension's
	# current release happens to bind (HLR-216).
	assert_output --partial "cy.on('tap', 'node:parent'"
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
	run bash -c '"$0" -o "$1" --html "$2" 2>/dev/null' \
		"$ELC" "$OUT" "$TREE"
	assert_success
	run elements
	assert_success
	refute_line --partial "layer "
	assert_equal "$(echo "$output" | grep -c '^file ')" "3"
	# Every file is a root container, since none was placed.
	assert_equal "$(echo "$output" | grep -c '^file .* -$')" "3"
}
