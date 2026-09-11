#!/usr/bin/env bats
# test/fixtures/globals.bats — the declared-global table and the cross
# references between a finding and the row describing its subject.
#
# Expected values are hand-counted from the fixture below and never
# regenerated from elc's output.

setup() {
	load "../helpers/common"

	TREE="$BATS_TEST_TMPDIR/tree"
	mkdir -p "$TREE"

	# Three globals in declaration order, with three shapes a C module
	# captures differently: a plain object, an initialised one, and an
	# array. `shared` is written by one function and read by another, so
	# the global-state analysis has something to say about it.
	{
		printf 'unsigned int shared;\n'
		printf 'static int counter = 0;\n'
		printf 'char buffer[16];\n'
		printf '\n'
		printf 'void produce(void)\n{\n\tshared = 1;\n\tcounter++;\n}\n'
		printf '\n'
		printf 'int consume(void)\n{\n\treturn (int)shared + counter;\n}\n'
		printf '\n'
		printf 'int main(void)\n{\n\tproduce();\n\treturn consume();\n}\n'
	} > "$TREE/a.c"
}

# The Globals section's rows, as `file:line name type`.
globals_of() {
	awk '/^Globals [(]/ { f = 1; next } f && /^$/ { f = 0 }
	     f && $1 != "File" && $1 !~ /^-/ {
	             out = $1 " " $2;
	             for (i = 3; i <= NF; i++) out = out " " $i;
	             print out }' <<<"$output"
}

# The same rows read off the Markdown form, with the anchors of HLR-241
# stripped: they are addressed to the renderer and occupy no column. Needed
# because a report regenerated from a record replays in that form.
md_globals_of() {
	awk -F'|' '/^## / { f = ($0 ~ /^## Globals( \([0-9]+\))?$/) }
	           f && /^\| / && $2 !~ /^ *File *$/ && $2 !~ /^ *-+ *$/ {
	                   out = "";
	                   for (i = 2; i <= 4; i++) {
	                           t = $i;
	                           gsub(/<[^>]*>/, "", t);
	                           gsub(/^ +| +$/, "", t);
	                           out = out (i > 2 ? " " : "") t;
	                   }
	                   print out }' <<<"$output"
}

# --- the table (HLR-242) ----------------------------------------------------

@test "HLR-242: every declared global is reported with its place and type" {
	elc "$TREE"
	assert_success

	# Hand-counted: three declarations, on lines 1, 2 and 3, in the order
	# the source writes them.
	local rows
	rows="$(globals_of | sed "s|$TREE/||")"
	assert_equal "$rows" "a.c:1 shared unsigned int
a.c:2 counter int
a.c:3 buffer char"
}

@test "HLR-242: the table is present without a graph to build it from" {
	# No entry point and no image: what a project declares is a fact about
	# the source, not a product of the analysis.
	elc "$TREE"
	assert_success
	assert_line --regexp "^Globals \([0-9]+\)$"
}

@test "HLR-242: the table follows the files it draws from" {
	elc "$TREE"
	assert_success

	local order
	order="$(grep -E '^(Files|Globals|Functions) \(' <<<"$output" |
		sed -E 's/ \([0-9]+\)$//')"
	assert_equal "$order" "Files
Globals
Functions"
}

@test "HLR-242: the table is in both compositions" {
	# A summary tier, so it is not one of the sections --verbose restores.
	elc "$TREE"
	assert_success
	assert_line --regexp "^Globals \("

	elc --verbose "$TREE"
	assert_success
	assert_line --regexp "^Globals \("
}

@test "HLR-242: every format carries the objects" {
	elc -f md "$TREE"
	assert_success
	assert_line --regexp "^## Globals \([0-9]+\)$"

	# CSV names the kind of row in a column of its own rather than starting
	# a second table, which no consumer could load.
	elc -f csv "$TREE"
	assert_success
	assert_line --index 0 --regexp "^record,"
	assert_line --regexp "^global,[^,]*a\.c:1,shared,"

	elc -f xml "$TREE"
	assert_success
	assert_output --partial "<globals>"
	assert_output --regexp "<declared-global [^>]*name=\"shared\""
}

@test "HLR-242, HLR-056: the objects survive a record round trip" {
	local record="$BATS_TEST_TMPDIR/r.xml"

	# Compared in the form a regenerated report replays in, so the two
	# sides are the same rendering of the same rows.
	elc -f md "$TREE"
	local direct
	direct="$(md_globals_of)"
	[ -n "$direct" ]

	run bash -c '"$0" -f xml "$1" > "$2" 2>/dev/null' "$ELC" "$TREE" "$record"
	assert_success

	run bash -c '"$0" --from-xml "$1" 2>/dev/null' "$ELC" "$record"
	assert_success
	assert_equal "$(md_globals_of)" "$direct"
}

# --- the cross references (HLR-241) -----------------------------------------

@test "HLR-241: a finding about an object links to the row describing it" {
	# `shared` carries a global-state finding, and the object is a row in
	# the Globals table — so the finding's subject is a reference to it.
	elc -f md "$TREE"
	assert_success

	assert_output --partial "](#elc-obj-shared)"
	assert_output --partial 'id="elc-obj-shared"'
}

@test "HLR-241: the row links back to the finding that names it" {
	elc -f md "$TREE"
	assert_success
	assert_output --partial 'id="elc-finding-shared"'
	assert_output --partial "](#elc-finding-shared)"
}

@test "HLR-241: every link in the report resolves to an anchor" {
	# The property the whole scheme rests on: a reference that lands
	# nowhere is worse than a plain name, because it promises a row and
	# then fails to produce one.
	for verbosity in "" "--verbose"; do
		elc -f md $verbosity "$TREE"
		assert_success

		local targets anchors dangling
		targets="$(grep -oE '\(#elc-[a-z0-9-]+\)' <<<"$output" |
			tr -d '(#)' | sort -u)"
		anchors="$(grep -oE 'id="elc-[a-z0-9-]+"' <<<"$output" |
			sed 's/id="//; s/"//' | sort -u)"
		dangling="$(comm -23 <(printf '%s\n' "$targets") \
			<(printf '%s\n' "$anchors"))"
		assert_equal "$dangling" ""
	done
}

@test "HLR-241: no anchor is written twice" {
	# A duplicate anchor resolves to whichever copy the renderer saw
	# first, which is a link that works by accident.
	elc --verbose -f md "$TREE"
	assert_success

	local dupes
	dupes="$(grep -oE 'id="elc-[a-z0-9-]+"' <<<"$output" | sort | uniq -d)"
	assert_equal "$dupes" ""
}

@test "HLR-241: an anchor carries both spellings" {
	# `id` is what current renderers resolve a fragment against; `name` is
	# the older form some pipelines still emit. One without the other is a
	# link that works in some readers and silently fails in others.
	elc -f md "$TREE"
	assert_success
	assert_output --regexp '<a id="elc-obj-shared" name="elc-obj-shared">'
}

@test "HLR-241: a subject with no row of its own is left as text" {
	# The call graph is the subject of the call-depth finding and is a row
	# in no table, so it is named rather than linked.
	elc --verbose -f md "$TREE"
	assert_success
	refute_output --partial "[call graph](#"
}

@test "HLR-241: the aligned table carries no markup" {
	# The references are the Markdown report's alone: a terminal has
	# nothing to click, and an anchor there would be markup printed at a
	# reader.
	elc --verbose "$TREE"
	assert_success
	refute_output --partial "<a id="
	refute_output --partial "](#elc-"
}

# --- the place a finding is about (HLR-243) ---------------------------------

@test "HLR-243: the findings table has a column for the place" {
	# Structural, and separate from the rows below: the column is present
	# whether or not this run's findings have a place to put in it, so a
	# reader never has to wonder whether the report would have said.
	elc "$TREE"
	assert_success
	assert_line --regexp "^ +Severity +Measurement +Subject +Where +Detail"
}

@test "HLR-243: a library call names its call site, not just the callee" {
	# The case the column exists for: the finding is attributed to the
	# callee, which is defined in no file this run analysed and is a row in
	# no table — so a cross-reference cannot reach it and the location is
	# the only thing that says where the call is.
	printf '#include <stdio.h>\nvoid emit(void)\n{\n\tprintf("x");\n}\n' \
		> "$TREE/lib.c"
	elc "$TREE"
	assert_success

	assert_line --regexp "misra library +printf +.*lib\.c:4"
}

@test "HLR-243: a finding about no single place leaves the column empty" {
	# A component's instability and the depth of a call graph are
	# properties of a structure rather than of a line. An invented
	# location would be one a reader could not act on.
	elc --verbose "$TREE"
	assert_success
	refute_line --regexp "call depth +call graph +[^ ]"
}

@test "HLR-243, HLR-219: the location is never wrapped" {
	# A path broken across two lines is not a path. Provoked with a
	# directory deep enough to push the line past the bound.
	local deep="$BATS_TEST_TMPDIR/a-deliberately-long/directory/path/src"
	mkdir -p "$deep"
	printf '#include <stdio.h>\nvoid emit(void)\n{\n\tprintf("x");\n}\n' \
		> "$deep/lib.c"

	elc "$deep"
	assert_success

	# The whole location on one line, colon and number attached.
	assert_line --regexp "$deep/lib\.c:4"
}
