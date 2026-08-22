#!/usr/bin/env bats
# test/fixtures/reachability.bats — the root set, dead code by traversal, and
# global state (STP §5).
#
# Expected values are worked out by hand and justified in reachability/README.md
# beside this file. Never regenerate them from elc's output.
#
# The product's headline claim lives here. Two tests decide whether it is sound
# rather than merely plausible: the clique that must be reported, and the
# address-taken callback that must not be.

setup() {
	load "../helpers/common"
	TREE="$BATS_TEST_DIRNAME/reachability/tree"
}

# The unreachable functions, by name, in report order. Scoped to the section
# and terminated at its blank line: names appear in half a dozen others.
unreachable() {
	printf '%s\n' "$output" |
		awk '/^Unreachable functions/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /^  \// { print $2 }'
}

reach_heading() {
	printf '%s\n' "$output" | awk '/^Unreachable functions/ { print; exit }'
}

unreachable_globals() {
	printf '%s\n' "$output" |
		awk '/^Unreachable globals/ { f = 1; next } f && /^$/ { f = 0 }
		     f && NF && $1 != "Object" && $1 !~ /^-+$/ { print $1 }'
}

# One global's row from the Global state section: writers, readers, finding.
global_row() {
	printf '%s\n' "$output" |
		awk -v want="$1" '/^Global state/ { f = 1; next } f && /^$/ { f = 0 }
		                  f && $1 == want { $1 = ""; sub(/^ +/, ""); print }'
}

scope_heading() {
	printf '%s\n' "$output" | awk '/^Cross-scope access/ { print; exit }'
}

cross_scope() {
	printf '%s\n' "$output" |
		awk '/^Cross-scope access/ { f = 1; next } f && /^$/ { f = 0 }
		     f && NF && $1 != "From" && $1 !~ /^-+$/ { print }'
}

# ------------------------------------------------------- the root set --

@test "HLR-097: a clique of unused functions calling one another is reported" {
	# The case textual linters get wrong. Each of the pair has a caller, so
	# "no caller" finds neither; no path reaches the pair from any root.
	elc --verbose --entry entry_main "$TREE/roots.c"
	assert_success
	assert_equal "$(unreachable)" "clique_b
clique_a
orphan"
}

@test "HLR-096: a function reachable only through an address-taken pointer is NOT reported" {
	# The case that makes the dead-code claim sound. `callback` appears
	# nowhere as a call — only as an initialiser of a vector table — and
	# reporting it dead would tell a user to delete an interrupt handler.
	elc --entry entry_main "$TREE/roots.c"
	assert_success
	# Counted against the extracted names: bats-assert's line matchers read
	# the last `run`, so a here-string would assert nothing at all.
	run bash -c 'grep -c -x callback <<<"$0" || true' "$(unreachable)"
	assert_output "0"
}

@test "LLR-RTS-02: an address-taken function is a root, not merely an exception" {
	# `callback_callee` is called only from `callback`. An implementation
	# that excluded address-taken functions from the report without
	# traversing *from* them would report it dead — wrong in exactly the
	# way that matters.
	elc --entry entry_main "$TREE/roots.c"
	assert_success
	run bash -c 'grep -c -x callback_callee <<<"$0" || true' "$(unreachable)"
	assert_output "0"
}

@test "HLR-096: functions reached from the declared entry point are not reported" {
	elc --entry entry_main "$TREE/roots.c"
	assert_success
	run bash -c 'grep -c -x -e entry_main -e used_helper <<<"$0" || true' \
		"$(unreachable)"
	assert_output "0"
}

@test "HLR-115: with no entry points declared, nothing is reported unreachable" {
	# The requirement is explicit that elc must not report every function
	# as unreachable for want of a declaration.
	elc "$TREE/roots.c"
	assert_success
	assert_equal "$(unreachable)" ""
	assert_equal "$(reach_heading)" \
		"Unreachable functions (omitted: no entry points declared, see --entry)"
}

@test "HLR-115: a declared entry point matching nothing says so differently" {
	# "You declared nothing" and "what you declared is not here" call for
	# different actions from the reader.
	elc --entry no_such_symbol "$TREE/roots.c"
	assert_success
	assert_equal "$(unreachable)" ""
	assert_equal "$(reach_heading)" \
		"Unreachable functions (omitted: no declared entry point matches an analysed function)"
}

@test "LLR-CTR-09: omitting reachability does not omit its neighbours" {
	# A run with no --entry still gets its global-access map.
	elc "$TREE/globals.c"
	assert_success
	assert_output --partial "solo_owned"
}

# ---------------------------------------------------- unreachable data --

@test "HLR-096: a global touched only by unreachable functions is unreachable" {
	elc --verbose --entry data_main "$TREE/unreachable.c"
	assert_success
	assert_equal "$(unreachable_globals)" "touched_by_dead"
}

@test "LLR-UGL-01: a global a reachable function touches is not condemned" {
	elc --entry data_main "$TREE/unreachable.c"
	assert_success
	run bash -c 'grep -c -x touched_by_live <<<"$0" || true' \
		"$(unreachable_globals)"
	assert_output "0"
}

@test "HLR-115: with no entry points, no global is reported unreachable either" {
	elc "$TREE/unreachable.c"
	assert_success
	assert_equal "$(unreachable_globals)" ""
}

# -------------------------------------------------------- global state --

@test "HLR-091: every global reports its writers and its readers" {
	elc --verbose "$TREE/globals.c"
	assert_success
	assert_output --partial "producer"
	assert_output --partial "consumer"

	local row
	row="$(global_row shared_ok)"
	assert_equal "${row%% *}" "producer"
}

@test "HLR-092: a global touched by one function is flagged for scope reduction" {
	# The case the edge table cannot see: one function means no
	# writer-to-reader edge exists at all.
	elc --verbose "$TREE/globals.c"
	assert_success
	assert_output --partial \
		"scope reduction — one function names it (MISRA C Rule 8.9)"

	local row
	row="$(global_row solo_owned)"
	assert_equal "${row#*MISRA }" "C Rule 8.9)"
}

@test "HLR-093: a global spanning disconnected regions is a hidden channel" {
	elc --verbose "$TREE/globals.c"
	assert_success
	assert_output --partial \
		"hidden channel — {island_a} {island_b} never call each other"
}

@test "HLR-093: shared state within one call-connected region is not a channel" {
	# producer calls consumer, so the two lie in one region and their
	# shared variable is a design rather than a coupling nobody declared.
	# Without this the finding would fire on every shared variable.
	elc --verbose "$TREE/globals.c"
	assert_success

	# The finding column of this one row must be empty. Asserted on the row
	# rather than on the output, which does carry both phrases for the other
	# two objects — and which is what a here-string would have matched.
	# awk rebuilds the record with single spaces when it clears a
	# field, so the row reads as two words and nothing else.
	assert_equal "$(global_row shared_ok)" "producer consumer"
}

@test "LLR-GLB-04: both findings carry their published source" {
	elc --verbose "$TREE/globals.c"
	assert_success

	local cited
	cited="$(printf '%s\n' "$output" |
		awk '/^Global state/ { f = 1; next } f && /^$/ { f = 0 }
		     f && /MISRA C Rule 8.9/ { n++ } END { print n + 0 }')"
	assert_equal "$cited" "2"
}

# ------------------------------------------------------ scope isolation --

@test "HLR-094: a call crossing a declared scope boundary is reported" {
	elc --verbose --scope "host:*/scopes/host/*" --scope "target:*/scopes/target/*" \
		"$TREE/scopes"
	assert_success
	assert_output --partial "host  host_drives  target  target_entry  call"
}

@test "HLR-094: a shared global crossing a boundary is reported too" {
	# The reason the requirement exists. A scope that never calls into
	# another but writes a variable the other reads has not been isolated,
	# and an implementation checking only calls would report this clean.
	elc --verbose --scope "host:*/scopes/host/*" --scope "target:*/scopes/target/*" \
		"$TREE/scopes"
	assert_success
	assert_output --partial "host  host_writes  target  target_reads  mailbox"
}

@test "HLR-094: exactly the two crossings are reported" {
	elc --verbose --scope "host:*/scopes/host/*" --scope "target:*/scopes/target/*" \
		"$TREE/scopes"
	assert_success

	local rows
	rows="$(cross_scope | wc -l)"
	assert_equal "$rows" "2"
	assert_equal "$(scope_heading)" "Cross-scope access (2)"
}

@test "HLR-115: with no scopes declared the analysis is omitted with a reason" {
	elc "$TREE/scopes"
	assert_success
	assert_equal "$(scope_heading)" \
		"Cross-scope access (omitted: no execution scopes declared, see --scope)"
	assert_equal "$(cross_scope)" ""
}

@test "HLR-094: an edge within one scope is not a crossing" {
	# Both files in one scope: the same calls and the same shared variable,
	# and nothing to report.
	elc --scope "everything:*/scopes/*" "$TREE/scopes"
	assert_success
	assert_equal "$(cross_scope)" ""
}

@test "HLR-094: a file matching no declaration is outside the partition" {
	# The user said nothing about the target directory, so inventing a
	# boundary around it would report violations against a partition nobody
	# drew.
	elc --scope "host:*/scopes/host/*" "$TREE/scopes"
	assert_success
	assert_equal "$(cross_scope)" ""
}

@test "HLR-063: a malformed scope declaration is a usage error" {
	run "$ELC" --scope "no-colon" "$TREE/scopes"
	assert_equal "$status" 2
	assert_output --partial "is not an execution scope"
}

# --------------------------------------------------------- determinism --

@test "HLR-032: the state sections survive a record round trip byte-identically" {
	elc --entry entry_main -f xml -o "$BATS_TEST_TMPDIR/rec.xml" "$TREE/roots.c"
	assert_success

	run "$ELC" --entry entry_main -f md "$TREE/roots.c"
	assert_success
	printf '%s\n' "$output" > "$BATS_TEST_TMPDIR/live.md"

	run "$ELC" --from-xml "$BATS_TEST_TMPDIR/rec.xml"
	assert_success
	printf '%s\n' "$output" > "$BATS_TEST_TMPDIR/regen.md"

	run diff "$BATS_TEST_TMPDIR/live.md" "$BATS_TEST_TMPDIR/regen.md"
	assert_success
}

@test "HLR-032: a global-state finding survives the round trip" {
	elc --verbose -f xml -o "$BATS_TEST_TMPDIR/g.xml" "$TREE/globals.c"
	assert_success

	run "$ELC" --verbose --from-xml "$BATS_TEST_TMPDIR/g.xml"
	assert_success
	assert_output --partial \
		"hidden channel — {island_a} {island_b} never call each other"
	assert_output --partial "MISRA C Rule 8.9"
}
