#!/usr/bin/env bats
# test/fixtures/recover.bats — architecture recovery and the manifest (STP §5).
#
# Expected values are worked out by hand and justified in recover/README.md
# beside this file. Never regenerate them from elc's output.
#
# The case that matters most in this file asserts two things at once and would
# be worthless asserting either alone: a run over the layered tree recovers a
# three-layer architecture *and* leaves the architecture-conformance section
# omitted for want of a declaration. A proposal is never the baseline it is
# measured against (HLR-173), and a tool marking its own homework would pass
# every other test here.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/recover/tree"
	CYCLIC="$BATS_TEST_DIRNAME/recover/cyclic"
	PURIFY="$BATS_TEST_DIRNAME/purify/tree"
}

# The layer a directory was placed at, or empty where it was not placed. The
# section is scoped and terminated at its blank line: a path appears in half a
# dozen other sections, and an unterminated extractor reads whichever comes
# next.
layer_of() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Architecture recovery \(/ { f = 1; next }
		                  f && /^$/ { f = 0 }
		                  f && $2 ~ ("/" want "$") { print $1 }'
}

# The proposal, as the one line beginning with --stratum.
proposal() {
	printf '%s\n' "$output" | awk '/^  --stratum /{ sub(/^ +/, ""); print; exit }'
}

recovery_heading() {
	printf '%s\n' "$output" |
		awk '/^Architecture recovery \(/ { print; exit }'
}

# ------------------------------------------------------ the recovered layering

@test "HLR-172: a plainly layered tree recovers that layering" {
	# app calls svc calls hal, and the fold puts the three directories in
	# that order. Worked by hand in README.md from the topological
	# positions and the degree of each function.
	elc --verbose "$TREE"
	assert_success
	assert_equal "$(layer_of app)" "0"
	assert_equal "$(layer_of svc)" "1"
	assert_equal "$(layer_of hal)" "2"
}

@test "HLR-172: one function reaching far down does not drag its directory" {
	# svc_leaf is called from the layer *below* its own, so it sits last in
	# the topological order while the rest of svc/ sits near the top. A fold
	# placing a directory at its latest member would put svc/ below hal/ and
	# propose an architecture upside down in its middle.
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "3 layers over 3 directories"
	[ "$(layer_of svc)" -lt "$(layer_of hal)" ]
}

@test "HLR-172: an architecture orders directories, not functions" {
	# Seven functions, three rows. A proposal listing functions would be a
	# topological order wearing a layering's name.
	elc --verbose "$TREE"
	assert_success
	assert_equal "$(layer_of app)" "0"
	refute_output --partial "svc_leaf, "
	printf '%s\n' "$output" |
		awk '/^Architecture recovery \(/ { f = 1; next }
		     f && /^$/ { exit } f && /svc_open/ { exit 1 }'
}

@test "HLR-172: a cyclic recovery view reports the cycles instead" {
	# hal calls back up into svc, so all four functions lie in one strongly
	# connected component and no topological ordering exists. Ordering the
	# graph anyway would present an invention as a reading.
	elc --verbose "$CYCLIC"
	assert_success
	assert_output --partial "omitted: the recovery view is cyclic"
	assert_output --partial "svc_open"
	assert_equal "$(layer_of app)" ""
}

@test "HLR-172: a cycle is reported as a set, not as a path" {
	# A strongly connected component is a set: every member reaches every
	# other, but the decomposition yields no order, and an arrow chain would
	# assert a path that may not exist. The rule the recursion report
	# already follows.
	elc --verbose "$CYCLIC"
	assert_success
	local cycle
	cycle="$(printf '%s\n' "$output" |
		awk '/^Architecture recovery \(/ { f = 1; next }
		     f && /^$/ { f = 0 } f && $1 == "cycle" { print }')"
	[ -n "$cycle" ]
	refute [ -n "$(printf '%s\n' "$cycle" | grep -- '->')" ]
}

@test "HLR-172: the proposal states what was masked and what excluded" {
	# A layering recovered from a graph with parts of it set aside is a
	# claim about that graph and not about the program, so the heading says
	# how much was set aside before the rows say anything.
	elc --verbose "$PURIFY"
	assert_success
	assert_output --partial "2 functions masked and 1 excluded"
}

# --------------------------------------------------- a proposal, not a baseline

@test "HLR-173: recovering a layering leaves conformance omitted" {
	# The case the whole phase turns on. elc recovers a three-layer
	# architecture from this tree with complete confidence, and still
	# reports the conformance analyses as omitted for want of a declaration
	# — because the strata of HLR-078 are the sole baseline and nothing elc
	# derives from a graph becomes one (HLR-115, HLR-173).
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "3 layers over 3 directories"
	assert_output --partial \
		"Architecture conformance (omitted: no architectural strata declared"
	refute_output --partial "Layering violations"
}

@test "HLR-173: the matrix subjects are directories, not recovered layers" {
	# The one part of the conformance section a missing declaration does not
	# silence, and the distinction is the requirement rather than an
	# exception to it: the matrix counts call edges between subjects the
	# discovery stage established, and a matrix whose rows were read off the
	# graph it arranges would make every project look layered.
	elc --verbose "$TREE"
	assert_success
	assert_output --partial \
		"Dependency structure matrix (directories: no strata declared"
}

@test "HLR-173: the proposal is rendered as arguments" {
	# Adoption is a copy rather than a transcription, and the argument list
	# is the boundary the requirement draws in the one form a reader cannot
	# mistake for a measurement.
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "--stratum-order 'app>svc>hal'"
	assert_output --partial "elc never applies it"
}

@test "HLR-173: the emitted proposal is accepted by elc unmodified" {
	# Copied, not retyped: the line is passed back through a shell exactly
	# as it was printed. The quoting is load-bearing — an unquoted order
	# would be read as a redirection, create files named after the layers,
	# and hand elc a partial order it rejects.
	elc --verbose "$TREE"
	assert_success
	local line
	line="$(proposal)"
	[ -n "$line" ]

	cd "$BATS_TEST_TMPDIR"
	# Interpolated into a command string and handed to a shell, which is
	# exactly how a user adopts it: the shell performs the quote removal,
	# and elc sees the patterns and the order the report printed.
	run bash -c "\"$ELC\" $line \"$TREE\" > /dev/null"
	assert_success
	# And nothing was created by a shell reading part of it as a
	# redirection.
	assert_equal "$(ls "$BATS_TEST_TMPDIR")" ""
}

@test "HLR-173: adopting the proposal is what makes it a baseline" {
	# Passed back, the same layering becomes the declaration the conformance
	# analyses measure against — and the declaring is the user's.
	elc --verbose "$TREE"
	assert_success
	local line
	line="$(proposal)"

	run bash -c "\"$ELC\" --verbose $line \"$TREE\""
	assert_success
	refute_output --partial \
		"Architecture conformance (omitted: no architectural strata declared"
	assert_output --partial "Architecture conformance (over"
}

# ------------------------------------------------------------- the manifest --

@test "HLR-175: the manifest is written beside the report and named from it" {
	# The one companion rule, applied again: the extension is substituted on
	# the report's own path, and the manifest accepts no path of its own
	# (HLR-119).
	local out="$BATS_TEST_TMPDIR/report.md"

	elc -o "$out" --write-manifest "$PURIFY"
	assert_success
	[ -f "$BATS_TEST_TMPDIR/report.manifest.json" ]
	grep -q '"manifest-version": 1' "$BATS_TEST_TMPDIR/report.manifest.json"
	grep -q '"class": "god object"' "$BATS_TEST_TMPDIR/report.manifest.json"
	# And it ends with a newline. The file is meant to be hand-edited and
	# kept under version control, and a text file without a final newline
	# is one every such tool complains about — Jansson's own file writer
	# does not add one.
	assert_equal "$(tail -c 1 "$BATS_TEST_TMPDIR/report.manifest.json")" ""
}

@test "HLR-119: no output path, no manifest" {
	# There is no path to derive a name from, so nothing is written and
	# nothing is said about it — the rule every companion follows (HLR-104).
	cd "$BATS_TEST_TMPDIR"
	elc --write-manifest "$PURIFY"
	assert_success
	assert_equal "$(ls "$BATS_TEST_TMPDIR")" ""
}

@test "HLR-177: a manifest keeping the dispatcher changes the layering" {
	# The planted god object's edges are masked by default. Told to keep it
	# in the view, elc reads a different layering off the graph — which is
	# what makes the manifest worth having: the classification is a
	# heuristic, and only the user knows whether their dispatcher is a
	# monolith or a state machine doing its job.
	local m="$BATS_TEST_TMPDIR/keep.json"

	elc --verbose "$PURIFY"
	assert_success
	local before
	before="$(proposal)"

	cat > "$m" <<-'JSON'
	{
	  "manifest-version": 1,
	  "classifications": [
	    { "function": "dispatch", "class": "god object", "mask": false }
	  ]
	}
	JSON

	elc --verbose --manifest "$m" "$PURIFY"
	assert_success
	refute_output --partial "$before"
	assert_output --partial "--stratum-order"
}

@test "HLR-177: the report says which classifications came from the manifest" {
	# A reader of this section is being asked to judge whether the masking
	# was right, and cannot do that without knowing which rows are elc's own
	# reading of the graph and which are their team's correction of it.
	local m="$BATS_TEST_TMPDIR/keep.json"

	cat > "$m" <<-'JSON'
	{
	  "manifest-version": 1,
	  "classifications": [
	    { "function": "dispatch", "class": "god object", "mask": false }
	  ]
	}
	JSON

	elc --verbose --manifest "$m" "$PURIFY"
	assert_success
	assert_output --regexp "dispatch.*god object.*kept in the view.*manifest"
	assert_output --regexp "util_log.*utility sink.*incoming edges masked.*computed"
}

@test "HLR-177: a manifest naming an unknown function is reported and ignored" {
	# Analysing one directory of a project whose manifest covers all of it
	# is ordinary use, exactly as a declared entry point matching nothing is
	# (LLR-CTR-08). Reported, because a statement that matched nothing is
	# far more often a typo than a deliberate partial run.
	local m="$BATS_TEST_TMPDIR/stale.json"

	cat > "$m" <<-'JSON'
	{
	  "manifest-version": 1,
	  "classifications": [
	    { "function": "long_gone", "class": "god object" }
	  ]
	}
	JSON

	run "$ELC" --manifest "$m" "$PURIFY"
	assert_success
	assert_output --partial "no analysed file defines"
	assert_output --partial "the statement is ignored"
}

@test "HLR-176: a malformed manifest ends the run" {
	# The user named the file, so the failure is theirs to correct — the
	# provenance rule HLR-116 draws for a custom rule named on the command
	# line. The diagnostic quotes the line and column, because a person who
	# hand-edited the file needs to be told where they broke it.
	local m="$BATS_TEST_TMPDIR/broken.json"

	printf '{ "manifest-version": 1, "classifications": [\n' > "$m"

	elc --manifest "$m" "$PURIFY"
	assert_failure 2
	assert_output --partial "broken.json:"
}

@test "HLR-176: well-formed JSON that is not a manifest ends the run too" {
	# Jansson says the file is JSON; whether it is a *manifest* is elc's
	# judgement, and both failures are refused the same way.
	local version="$BATS_TEST_TMPDIR/nover.json"
	local class="$BATS_TEST_TMPDIR/badclass.json"
	local later="$BATS_TEST_TMPDIR/later.json"

	printf '{ "classifications": [] }\n' > "$version"
	elc --manifest "$version" "$PURIFY"
	assert_failure 2
	assert_output --partial "not a purification manifest"

	printf '{"manifest-version":1,"classifications":[{"function":"dispatch","class":"demigod"}]}\n' \
		> "$class"
	elc --manifest "$class" "$PURIFY"
	assert_failure 2
	assert_output --partial "a class this build does not know"

	printf '{"manifest-version":99,"classifications":[]}\n' > "$later"
	elc --manifest "$later" "$PURIFY"
	assert_failure 2
	assert_output --partial "is not one this build reads"
}

@test "HLR-176: a manifest that is not named is not read" {
	# The zero-configuration guarantee is unchanged by this phase. Every
	# name a tool might look under is planted in the working directory, in
	# the target, and in an ancestor of both, and the output is identical to
	# its absence (HLR-039).
	local ancestor="$BATS_TEST_TMPDIR/ancestor"
	local copy="$ancestor/tree"
	mkdir -p "$ancestor"
	cp -r "$PURIFY" "$copy"

	cd "$ancestor"
	elc --verbose "$copy"
	assert_success
	local clean="$output"

	local decoy='{"manifest-version":1,"classifications":[{"function":"dispatch","class":"ordinary"}]}'
	printf '%s\n' "$decoy" > "$ancestor/manifest.json"
	printf '%s\n' "$decoy" > "$ancestor/elc.manifest.json"
	printf '%s\n' "$decoy" > "$ancestor/.elc-manifest.json"
	# Inside the target, hidden names only: a plain .json file there is
	# *discovered* and listed as skipped for want of a language module,
	# which is HLR-012 working and would mask what this case is asking
	# (HLR-005).
	printf '%s\n' "$decoy" > "$copy/.manifest.json"
	printf '%s\n' "$decoy" > "$copy/.elc-manifest.json"
	printf '%s\n' "$decoy" > "$copy/app/.manifest.json"

	elc --verbose "$copy"
	assert_success
	assert_equal "$output" "$clean" \
		"a manifest not named on the command line must produce output \
identical to its absence"
}

@test "HLR-175: a written manifest is accepted back unmodified" {
	# The round trip is the reason the format goes through a library in both
	# directions: a hand-rolled writer paired with a library reader would be
	# two implementations of one format with elc on both ends of the
	# disagreement.
	local out="$BATS_TEST_TMPDIR/report.md"
	local m="$BATS_TEST_TMPDIR/report.manifest.json"

	elc -o "$out" --write-manifest "$PURIFY"
	assert_success

	elc --verbose --manifest "$m" "$PURIFY"
	assert_success
	# Every row it names is now attributed to the manifest, and the masking
	# it recorded is the masking that happens.
	assert_output --partial "9 functions retained, 12 call edges masked"
	refute_output --partial "computed"
}

# ------------------------------------------------------------ the drawings --

@test "HLR-178: the two drawings are written beside the report" {
	local out="$BATS_TEST_TMPDIR/report.md"

	elc -o "$out" --purify-dot "$PURIFY"
	assert_success
	[ -f "$BATS_TEST_TMPDIR/report.raw.dot" ]
	[ -f "$BATS_TEST_TMPDIR/report.purified.dot" ]
	# And neither replaces the annotated call tree, which answers a
	# different question and is enabled by a different default (HLR-102).
	[ -f "$BATS_TEST_TMPDIR/report.dot" ]
}

@test "HLR-178: a masked node is drawn detached rather than deleted" {
	# A drawing that removed the masked nodes could not show what
	# purification did, which is the entire reason there are two of them.
	local out="$BATS_TEST_TMPDIR/report.md"

	elc -o "$out" --purify-dot "$PURIFY"
	assert_success

	local purified="$BATS_TEST_TMPDIR/report.purified.dot"
	local raw="$BATS_TEST_TMPDIR/report.raw.dot"

	# The dispatcher is node 0 in this tree; it is present, greyed, labelled
	# with its class, and holds no edge in either direction.
	grep -q 'n0 \[label="dispatch' "$purified"
	grep -q '(god object)' "$purified"
	refute [ -n "$(grep -E '^[[:space:]]+n0 -> ' "$purified")" ]
	refute [ -n "$(grep -E ' -> n0;' "$purified")" ]

	# The raw drawing is the graph as built: the dispatcher's edges are all
	# there, and no classification is drawn on it.
	[ -n "$(grep -E '^[[:space:]]+n0 -> ' "$raw")" ]
	refute [ -n "$(grep '(god object)' "$raw")" ]
	# Fifteen call edges in the tree, twelve of them masked (purify/README).
	assert_equal "$(grep -c ' -> ' "$raw")" "15"
	assert_equal "$(grep -c ' -> ' "$purified")" "3"
}

@test "HLR-119: no output path, no drawings" {
	cd "$BATS_TEST_TMPDIR"
	elc --purify-dot "$PURIFY"
	assert_success
	assert_equal "$(ls "$BATS_TEST_TMPDIR")" ""
}

# ------------------------------------------------------------- the report --

@test "HLR-150: the recovery section is a detail tier" {
	elc "$TREE"
	assert_success
	assert_equal "$(recovery_heading)" ""

	elc --verbose "$TREE"
	assert_success
	[ -n "$(recovery_heading)" ]
}

@test "HLR-031: both human formats present the recovered layering" {
	elc --verbose "$TREE"
	assert_success
	assert_output --partial "Architecture recovery"

	elc --verbose -f md "$TREE"
	assert_success
	assert_output --partial "## Architecture recovery"
}

@test "HLR-054: the record carries the proposal and regenerates it" {
	# A record has no graph to re-order, so a proposal absent from it is one
	# a regenerated report cannot present. And it is a proposal in the
	# record too: nothing reads it back as a declaration, so the conformance
	# analyses stay exactly as omitted as they were.
	local record="$BATS_TEST_TMPDIR/record.xml"

	run bash -c '"$0" -f xml "$1" > "$2"' "$ELC" "$TREE" "$record"
	assert_success
	grep -q '<recovery state="proposed"' "$record"
	grep -q 'directory="[^"]*/svc" layer="1"' "$record"
	grep -q '<proposal arguments=' "$record"

	elc --verbose --from-xml "$record"
	assert_success
	assert_output --partial "3 layers over 3 directories"
	assert_output --partial "--stratum-order"
	assert_output --partial \
		"Architecture conformance (omitted: no architectural strata declared"
}

@test "HLR-179: two runs over the same tree propose one layering" {
	elc --verbose "$TREE"
	assert_success
	local first="$output"

	elc --verbose "$TREE"
	assert_success
	assert_equal "$output" "$first"
}

@test "HLR-038: the recovery section goes to the results destination" {
	# A run redirecting its report to a file must not have a second report
	# appear on the terminal, and the recovery section is a result like
	# every other.
	local out="$BATS_TEST_TMPDIR/report.txt"

	run bash -c '"$0" --verbose -o "$1" "$2"' "$ELC" "$out" "$TREE"
	assert_success
	assert_output ""
	grep -q "^Architecture recovery" "$out"
}
