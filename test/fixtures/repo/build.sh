#!/bin/sh
# test/fixtures/repo/build.sh — construct the repository fixture at $1.
#
# This group's tree cannot be checked in the way every other fixture group's
# tree is: a directory containing `.git/` is a repository, and git will not
# track one inside another. So the fixture ships as a builder, run into the
# test's own temporary directory, and the README beside this script documents
# the tree it produces and the values hand-counted from it.
#
# Building it rather than committing it has a second benefit worth stating,
# because it is the more important one: these tests never look at elocker's
# own repository. A test that ran `elc` against the checkout would depend on
# the working tree being clean, on which files happen to be staged, and on
# whoever ran it not having a stash in flight. It would pass on a laptop and
# fail in CI for reasons having nothing to do with the code under test.
#
# Every git invocation pins the identity and disables signing. A committer
# identity is required, and inheriting the developer's is how a fixture
# quietly becomes machine-dependent; a global `commit.gpgsign = true` would
# make this script prompt for a passphrase, which in CI means hanging until
# the job times out.

set -eu

root="$1"
git="git -c user.name=elc-fixture -c user.email=fixture@example.invalid \
        -c commit.gpgsign=false -c gpg.format=openpgp"

mkdir -p "$root/src" "$root/docs" "$root/build"

# Tracked, and the whole of what `elc` should analyse.
printf 'int a(void)\n{\n\treturn 0;\n}\n'  > "$root/src/a.c"
printf 'int b(void) {\n\treturn 1;\n}\n'   > "$root/src/b.c"
printf 'int d(void) { return 2; }\n'       > "$root/docs/d.c"

# Tracked, but excluded for reasons the README sets out one by one.
printf 'int hidden(void) { return 3; }\n'  > "$root/.hidden.c"
printf 'not really an image\n'             > "$root/logo.png"
printf 'int x;\n\000\001\002\000binary\n'  > "$root/src/blob.c"

# Present in the working tree, absent from the repository.
printf 'int untracked(void) { return 4; }\n' > "$root/src/untracked.c"
printf 'int generated(void) { return 5; }\n' > "$root/build/gen.c"
printf 'build/\nsrc/untracked.c\n'           > "$root/.gitignore"

$git -C "$root" init -q
$git -C "$root" add .gitignore .hidden.c logo.png src/a.c src/b.c src/blob.c \
                    docs/d.c
$git -C "$root" commit -q -m 'the fixture tree'
