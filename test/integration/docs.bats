#!/usr/bin/env bats
# test/integration/docs.bats — the documentation test (HLR-129, HLR-130).
#
# elc's usage summary is the *reference*: it is generated from the same table
# that parses, so it cannot drift from what elc accepts (doc/SDD.md §4,
# "Option List as Single Reference"). Both documents are checked against it.
#
# This catches an option added without being documented, or documented without
# being accepted. It cannot check that a new *format* or *finding category*
# was described — that part is the author's (STP §2.3 step 7).

setup() {
	load "../helpers/common"
	MAN="$REPO_ROOT/doc/elc.1"
	MANUAL="$REPO_ROOT/doc/User_Manual.md"
}

# Long-option names elc actually accepts, taken from its usage summary.
usage_options() {
	"$ELC" --help 2>/dev/null | grep -oE -- '--[a-z][a-z0-9-]*' | sort -u
}

# roff escapes a literal hyphen as "\-", so the man source must be
# de-escaped before option names can be matched in it.
man_text() {
	sed 's/\\-/-/g' "$MAN"
}

@test "the man page exists" {
	[ -f "$MAN" ]
}

@test "the user manual exists" {
	[ -f "$MANUAL" ]
}

@test "the man page renders without diagnostic" {
	require_tool man "HLR-128 man page renders"
	run man --warnings -E UTF-8 -l "$MAN"
	assert_success
	[ -z "$stderr" ] || [ "${stderr:-}" = "" ]
}

@test "the usage summary advertises at least one option" {
	run usage_options
	assert_success
	[ -n "$output" ]
}

@test "every option in the usage summary appears in the man page" {
	local missing=()
	while read -r opt; do
		[ -n "$opt" ] || continue
		man_text | grep -qF -- "$opt" || missing+=("$opt")
	done < <(usage_options)
	[ "${#missing[@]}" -eq 0 ] || {
		echo "undocumented in doc/elc.1: ${missing[*]}" >&2
		false
	}
}

@test "every option in the usage summary appears in the user manual" {
	local missing=()
	while read -r opt; do
		[ -n "$opt" ] || continue
		grep -qF -- "$opt" "$MANUAL" || missing+=("$opt")
	done < <(usage_options)
	[ "${#missing[@]}" -eq 0 ] || {
		echo "undocumented in doc/User_Manual.md: ${missing[*]}" >&2
		false
	}
}

@test "every long option the man page documents is accepted by elc" {
	local accepted undocumented=()
	accepted="$(usage_options)"
	while read -r opt; do
		[ -n "$opt" ] || continue
		grep -qxF -- "$opt" <<<"$accepted" || undocumented+=("$opt")
	done < <(man_text | grep -oE -- '--[a-z][a-z0-9-]*' | sort -u)
	[ "${#undocumented[@]}" -eq 0 ] || {
		echo "documented but not accepted: ${undocumented[*]}" >&2
		false
	}
}

@test "both documents describe the exit-status scheme" {
	man_text | grep -qi "exit status"
	grep -qi "exit status" "$MANUAL"
}
