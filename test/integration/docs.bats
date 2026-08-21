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

# --------------------------------------------------------------------------
# Prose currency (HLR-129, HLR-130).
#
# The option checks above are bidirectional and mechanical. Everything else
# in these documents is prose, and prose is where the drift actually happened:
# sections written during one phase kept describing that phase's behaviour
# after a later one replaced it. These tests fail the build on the two
# signatures that drift leaves behind.
# --------------------------------------------------------------------------

# The user documents describe the version they ship with. Development
# chronology is not something a user of the installed tool can act on, and a
# sentence deferring behaviour to "a later phase" is the exact shape a
# statement takes when it has outlived the phase that wrote it.
@test "no user document defers behaviour to a development phase" {
	local offenders=()
	for doc in "$MAN" "$MANUAL"; do
		if grep -qnEi 'later phase|a future phase|Phase [0-9]+|doc/SDP\.md' "$doc"; then
			offenders+=("$doc")
			grep -nEi 'later phase|a future phase|Phase [0-9]+|doc/SDP\.md' "$doc" >&2
		fi
	done
	[ "${#offenders[@]}" -eq 0 ] || {
		echo "phase vocabulary in user documentation: ${offenders[*]}" >&2
		echo "the manual and man page describe the delivered build (HLR-129)" >&2
		false
	}
}

# A query file is part of the contract a module author codes against, so a
# file shipped in the runtime and mentioned nowhere in the manual is a
# capability the reader cannot discover. Catches an added .scm as surely as
# it catches the two optional files that went unmentioned for two phases.
@test "every shipped query file is named in the user manual" {
	local missing=()
	while read -r scm; do
		[ -n "$scm" ] || continue
		grep -qF -- "$scm" "$MANUAL" || missing+=("$scm")
	done < <(find "$REPO_ROOT/runtime/queries" -name '*.scm' -not -path '*/rules/*' \
		-printf '%f\n' | sort -u)
	[ "${#missing[@]}" -eq 0 ] || {
		echo "shipped but undocumented in the manual: ${missing[*]}" >&2
		false
	}
}

# The manual states which languages ship. That claim is checked against what
# is actually in the runtime rather than against the last time somebody
# counted — a language withdrawn from the runtime left the prose behind.
@test "every language in the runtime is named in the user manual" {
	local missing=()
	while read -r lang; do
		[ -n "$lang" ] || continue
		grep -qiF -- "$lang" "$MANUAL" || missing+=("$lang")
	done < <(find "$REPO_ROOT/runtime/queries" -mindepth 1 -maxdepth 1 -type d \
		-printf '%f\n' | sort -u)
	[ "${#missing[@]}" -eq 0 ] || {
		echo "language shipped but not named in the manual: ${missing[*]}" >&2
		false
	}
}

# Traceability.md reports these four figures, and nothing else checks them
# against the artefacts they claim to count: a requirement added without
# bumping the block leaves every generated document quoting a stale total,
# and re-rendering cannot detect it because both sides read the same number.
@test "the counts block matches what Project.xml actually holds" {
	require_tool python3 "HLR-129 generated counts are current"
	run python3 - "$REPO_ROOT/doc/Project.xml" <<-'PY'
		import sys, xml.etree.ElementTree as ET
		root = ET.parse(sys.argv[1]).getroot()
		actual = {
		    "hlrs":       len(root.findall("hlrs/section/hlr")),
		    "llrs":       len(root.findall("llrs/function/llr")),
		    "tests":      len(root.findall("tests/file/test")),
		    "test_files": len(root.findall("tests/file")),
		}
		stated = {c.get("name"): int(c.get("value"))
		          for c in root.findall("metadata/counts/count")}
		bad = [f"{k}: states {stated.get(k)}, holds {v}"
		       for k, v in actual.items() if stated.get(k) != v]
		if bad:
		    print("\n".join(bad))
		    sys.exit(1)
	PY
	[ "$status" -eq 0 ] || {
		echo "doc/Project.xml <counts> is stale:" >&2
		echo "$output" >&2
		false
	}
}
