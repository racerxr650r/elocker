# `reachability/` — the root set, dead code by traversal, and global state

The fixture header for this group. Its expected values are worked out by hand
here and asserted by [`../reachability.bats`](../reachability.bats); they are
never regenerated from `elc`'s own output, which would make the fixture agree
with the implementation by construction and assert nothing (STP §2.4).

This group covers the product's headline claim — *this function is dead* — and
the two cases that decide whether it is sound rather than merely plausible: a
clique of unused functions that must be reported, and an address-taken callback
that must not be.

## The tree

```text
tree/
├── roots.c          the root set: entry points ∪ address-taken functions
├── globals.c        the three verdicts on a global object
├── unreachable.c    data condemned by the same traversal
└── scopes/
    ├── host/harness.c      one declared execution scope
    └── target/firmware.c   the other
```

Each file is measured on its own, because reachability is a whole-graph answer:
analysing two of them together would join their graphs and change every
expectation.

## `roots.c` — what is dead, and what only looks it

Run with `--entry entry_main` and nothing else.

| Function | Reported unreachable | Why |
| -------- | -------------------- | --- |
| `entry_main` | no | the declared root |
| `used_helper` | no | called from it |
| `clique_b` | **yes** | line 32 |
| `clique_a` | **yes** | line 37 |
| `orphan` | **yes** | line 44 |
| `callback` | **no** | its address is taken |
| `callback_callee` | **no** | reached *through* that root |

**The clique is why this analysis exists.** `clique_a` and `clique_b` call each
other and nothing else calls either. A textual linter looking for "a function
with no caller" finds a caller for each and reports neither; a traversal from
the root set reaches neither, because no path leads into the pair. Three
unreachable functions is the expected count, and two of them are the clique.

**`callback` is the case that makes the claim sound.** It appears nowhere as a
call — only as an initialiser of `vector_table`. Its address is taken, so it is
a root, and reporting it dead would tell a user to delete an interrupt handler.
The asymmetry is deliberate: an extra root can only shrink the unreachable set,
whereas a missing one produces a false claim of death (HLR-096).

**`callback_callee` is what proves `callback` is a *root* rather than merely an
exception.** It is called only from `callback`. An implementation that removed
address-taken functions from the report without traversing from them would
report `callback_callee` dead, and would be wrong in exactly the way that
matters.

**With no `--entry` at all, none of the above is reported.** The section states
that it was omitted and why. `elc` never reports every function as unreachable
for want of a declaration (HLR-115).

## `globals.c` — the three verdicts

Needs no declaration; global access is measured on every run.

| Object | Writers | Readers | Verdict |
| ------ | ------- | ------- | ------- |
| `solo_owned` | `owner` | `owner` | **scope reduction** |
| `shared_ok` | `producer` | `consumer` | *none* — ordinary shared state |
| `hidden` | `island_a` | `island_b` | **hidden channel** |

`solo_owned` is the case the edge table cannot see. A global edge joins a
writer to a reader, and an object touched by one function produces no edge at
all — so an analysis reading only the edges finds no scope-reduction candidate
anywhere, which is why the graph carries the access records separately.

`shared_ok` and `hidden` differ in one respect and it is the whole test:
`producer` calls `consumer`, so the two lie in one weakly connected region of
the call graph and their shared variable is a design. `island_a` and
`island_b` never call each other, so the same shape of sharing is temporal
coupling in which execution order silently governs correctness. The finding
names the disconnected participants — `{island_a} {island_b}` — because the
grouping *is* the finding.

## `unreachable.c` — the data goes with the code

Run with `--entry data_main`.

| Object | Touched by | Unreachable |
| ------ | ---------- | ----------- |
| `touched_by_dead` | `dead_writer` (unreachable) | **yes** |
| `touched_by_live` | `data_main` (the root) | no |

An object every one of whose accessors is unreachable is unreachable itself.
An object *no* analysed function touches is deliberately **not** claimed: it may
be written from file scope or from a translation unit outside the target, and
the asymmetry that governs functions governs storage too.

## `scopes/` — crossing a declared boundary

Run over `tree/scopes` with two declarations:

```sh
elc --scope 'host:*/scopes/host/*' --scope 'target:*/scopes/target/*' tree/scopes
```

| From | To | Via |
| ---- | -- | --- |
| `host_drives` | `target_entry` | a call |
| `host_writes` | `target_reads` | the shared object `mailbox` |

**Both kinds are reported, and the second is the reason the requirement
exists.** A scope that never calls into another but writes a variable the other
reads has not been isolated — that is precisely the overlapping memory map
HLR-094 is about, and an implementation checking only call edges would report
the arrangement clean.

With no `--scope` declared the section states that it was omitted, exactly as
reachability does without `--entry`.
