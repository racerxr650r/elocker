#!/usr/bin/env bats
# test/fixtures/concurrency.bats — the second thread of control, end to end
# (STP §5, HLR-227 – HLR-234).
#
# Expected values are worked out by hand and justified in concurrency/README.md
# beside this file. Never regenerate them from elc's output.
#
# **Measured with --no-expand, and that is the requirement rather than a
# convenience.** A cross-compiled target's headers are not on the host, so `elc`
# measures every such file as written — all 44 of `avrOS`, on the run that
# prompted this suite. Expanded, `ISR(TIMER_vect)` is an ordinary definition and
# nothing below is under test.

setup() {
	load "../helpers/common"
	SRC="$BATS_TEST_DIRNAME/concurrency/handlers.c"
	OUT="$BATS_TEST_TMPDIR/report.txt"
	IMAGE="$BATS_TEST_TMPDIR/handlers"
}

# elc over the fixture, unexpanded, with the report kept for the extractors.
report() {
	elc --entry main --no-expand --verbose "$@" "$SRC"
	printf '%s\n' "$output" > "$OUT"
}

# No image is committed: a binary in the repository is a fixture nobody can
# review. A host without a working compiler skips, naming what went unverified.
build_image() {
	cc -O0 -g -o "$IMAGE" "$SRC" 2>/dev/null || \
		skip "cc cannot build the fixture image here: the linkage join of HLR-233 is unverified"
}

# The functions elc reported, by name, in report order.
functions_of() {
	awk '/^Functions [(]/ {s=1; next}
	     s && /^$/ {exit}
	     s && $1 == "File" {next}
	     s && /^  [^ -]/ {print $3}' "$OUT" | LC_ALL=C sort | tr '\n' ' '
}

# One function's mark in the Reent column — "I", "R", or empty.
mark_of() {
	awk -v want="$1" '/^Functions [(]/ {s=1; next}
	                  s && /^$/ {exit}
	                  s && $3 == want {print ($5 == "I" || $5 == "R") ? $5 : ""}' \
		"$OUT"
}

# One summary figure, by the name in its left-hand column.
summary_of() {
	awk -v want="$2" '$0 ~ "^  " want "  *[0-9]+$" {print $NF}' "$1"
}

# The Role and "Root by" of one Concurrency row, single-spaced. Taken as fields
# rather than by column, since the alignment moves with the longest path in the
# table and a fixed offset would break on a different checkout directory.
concurrency_row() {
	awk -v want="$1" '/^Concurrency/ {s=1; next}
	                  s && /^$/ {exit}
	                  s && $1 == want {
	                      out = "";
	                      for (i = 3; i <= NF; i++)
	                          out = out (i > 3 ? " " : "") $i;
	                      print out
	                  }' "$OUT"
}

unreachable_of() {
	awk '/^Unreachable Functions/ {s=1; next}
	     s && /^$/ {exit}
	     s && /^  \// {print $2}' "$OUT" | LC_ALL=C sort | tr '\n' ' '
}

# --------------------------------------------------- the definition itself --

@test "HLR-233: a macro-written definition is reported as a function" {
	# The failure this fixture exists for. Unexpanded, ISR(TIMER_vect) parses
	# without error into a shape no ordinary function pattern matches — so
	# before this the handler was not a function at all, its statements were
	# file-scope ELOC, and its calls had no caller.
	report
	assert_success
	assert_equal "$(functions_of)" \
		"TIMER_vect bump dispatch main note_overflow on_idle "
	assert_equal "$(summary_of "$OUT" Functions)" "6"
}

@test "HLR-233: the macro-written definition's calls resolve to it" {
	# The consequence, asserted separately: a body outside every function
	# makes its calls unresolved, which is what hid the handler's reach into
	# the rest of the program. Both of the handler's calls now attach to it,
	# so its fan-out is two.
	report
	assert_success
	run awk '/^Functions [(]/ {s=1} s && $3 == "TIMER_vect" {print $10}' "$OUT"
	assert_output "2"

	# One call site stays unresolved and must: `dispatch` calls through a
	# pointer, which has no name to resolve and never had one (HLR-077).
	# Before the handler was a function, three were unresolved.
	assert_equal "$(summary_of "$OUT" "Unresolved calls")" "1"
}

@test "HLR-233: the handler is external, since a vector table resolves it" {
	report
	assert_success
	run awk '/^Functions [(]/ {s=1} s && $3 == "TIMER_vect" {print $4}' "$OUT"
	assert_output "public"
}

# ----------------------------------------------------- the root, and its kind

@test "HLR-233: the macro shape admits an asynchronous root with no other evidence" {
	# No --elf and no --isr-regex: the source carries the evidence on its own,
	# which is what makes this origin worth having.
	report
	assert_success
	assert_output --partial "2 asynchronous roots"
	assert_equal "$(concurrency_row TIMER_vect)" \
		"interrupt handler macro definition"
}

@test "HLR-233: an address taken is not reported as an interrupt handler" {
	# on_idle is dispatched by main through a pointer, on the application's
	# own thread. Merging the two origins would make every finding built on
	# either as strong as the weaker one.
	report
	assert_success
	assert_equal "$(concurrency_row on_idle)" \
		"asynchronous root address taken"
}

# ------------------------------------------------------------- the marks --

@test "HLR-233: the function table marks a handler I and a re-entrant R" {
	report
	assert_success
	assert_equal "$(mark_of TIMER_vect)" "I"
	assert_equal "$(mark_of bump)" "R"
}

@test "HLR-228: a function only the handler reaches is not marked" {
	# The intersection is the property. An implementation taking the union
	# passes every test above and fails this one.
	report
	assert_success
	assert_equal "$(mark_of note_overflow)" ""
	assert_equal "$(mark_of main)" ""
}

@test "HLR-233: a re-entrant mark names the root it is attributed to" {
	# bump is reached from the vector and from main. The row says which, and
	# says that the root is a handler rather than a callback — the same
	# distinction the root rows carry, on the finding that inherits it.
	report
	assert_success
	assert_equal "$(concurrency_row bump)" "re-entrant via TIMER_vect"
}

# ----------------------------------------------------------- reachability --

@test "HLR-233: an asynchronous root is a reachability root" {
	# Nothing calls a handler — half of what makes it one — so without this
	# the handler and everything below it is reported dead. elc said exactly
	# that of eleven vectors while listing the same functions as roots on the
	# next page: two tables of one run contradicting each other.
	report
	assert_success
	assert_equal "$(unreachable_of)" ""
	assert_output --partial "and every asynchronous root"
}

# ----------------------------------------------------------- the qualifier --

@test "HLR-231: a global only a callback touches draws no confinement warning" {
	# `pending` is volatile and reached from on_idle alone. A callback's
	# thread of control is not established, so the object is neither shown
	# confined nor shown shared — and advising the removal of a qualifier on
	# that evidence is the false positive this stratification closes.
	report
	assert_success
	refute_output --partial "confined qualifier          pending"
}

@test "HLR-230: the handler's shared object is qualified, so nothing is reported" {
	# `ticks` is written by the application and by the handler and carries
	# the qualifier, so the sound finding does not fire. Asserted so that the
	# test above cannot pass against an implementation that reports nothing.
	report
	assert_success
	refute_output --partial "shared state"
}

# ------------------------------------------------------- the linkage join --

@test "HLR-233: a renamed handler survives the image filter" {
	# ISR(vec) defines vec##_isr, so the image says TIMER_vect_isr and the
	# source says TIMER_vect. Matching by name discards the handler as absent
	# from an image that plainly contains it; the join is by the line the
	# debug information places the definition on.
	build_image
	report --elf "$IMAGE"
	assert_success
	assert_equal "$(summary_of "$OUT" Functions)" "6"
	refute_output --partial "TIMER_vect                       "
}

@test "HLR-233: the record carries the name the linker kept it by" {
	build_image
	elc --entry main --no-expand --elf "$IMAGE" -f xml "$SRC"
	assert_success
	assert_output --partial 'name="TIMER_vect"'
	assert_output --partial 'linkage="TIMER_vect_isr"'
	assert_output --partial 'macro-defined="1"'
}

# --------------------------------------------------- the scoped guard -----

@test "HLR-234: a scoped guard is recorded as a critical section" {
	# ATOMIC_BLOCK is a macro taking a block: a nested definition to the
	# grammar, never a call, so the call pattern that used to name it could
	# match nothing. bump is the only function that guards anything.
	elc --entry main --no-expand -f xml "$SRC"
	assert_success
	run bash -c 'printf "%s\n" "$1" | grep -c "critical-section=\"1\""' _ "$output"
	assert_output "1"
}

@test "HLR-234: a scoped guard is not reported as a leaked lock" {
	# Balanced by construction: it releases on every exit, `return` included.
	# Recorded as an acquire/release pair it would report a leak on the one
	# idiom that cannot leak.
	report
	assert_success
	refute_output --partial "holding a critical section"
}
