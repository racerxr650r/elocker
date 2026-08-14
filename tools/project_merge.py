#!/usr/bin/env python3
"""Deterministic three-way structural merger for ``doc/Project.xml``
(Phase 5.5 — Stage A in SDP §5.9.2 / SDD §9).

The merger operates on lxml trees rather than the plain-dict
projection because Stage A's contract is to *preserve* the on-disk
formatting (comments, CDATA, attribute order, whitespace) the form
webview write path also preserves (HLR-018). The dict projection
from :func:`render_doc.parse_project_to_dict` is one-way and would
silently rewrite developer-authored markup.

Stage A handles every conflict it can without semantic reasoning:

* New items added on either side that do not collide are unioned.
* Items added on both sides with the same id but different content
  trigger an id collision: ours keeps the id, the theirs-side copy
  is reallocated to the next free id (existing ids never move,
  HLR-005).
* ``<traces>`` blocks are unioned by ``(target, ref)`` so two
  branches that each add a different upstream link both survive.
* Items removed on one side and unchanged on the other are removed.
* Items removed on one side and modified on the other become a
  residual ``modify_delete`` conflict for Stage B (the AI layer or
  manual resolution in the merge editor).
* Item bodies edited on both sides become a residual ``body``
  conflict.
* ``<metadata><counts>`` is recomputed from the merged tree so the
  informational counts stay accurate.

The function returns the merged XML bytes plus a list of structured
``Conflict`` records. The TS layer drives Stage B by feeding each
record through one of the four ``merge.*`` AI intents and
substituting the resolved content back into the merged tree before
opening VS Code's three-way merge editor (per HLR-034).

Acceptance contract (SDP §5.9 Phase 5.5):

* Two branches each adding a non-overlapping HLR to the same section
  produce a clean merge with **no** AI involvement and pass lint.
* Two branches editing the same `<hlr>` body produce a residual
  `body` conflict (handled by Stage B).
* The post-merge `Project.xml` lints with no *new* errors compared
  to the union of the two parents' errors.
"""
from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Iterable

try:
    from lxml import etree
except ImportError as exc:  # pragma: no cover - lxml is required
    raise RuntimeError(
        "tools.project_merge requires lxml (pip install lxml)"
    ) from exc

from render_doc import PROJECT_XSD
from lint_project import lint as _lint, DEFAULT_XSD as _LINT_DEFAULT_XSD


# --------------------------------------------------------------------- #
# Container registry                                                    #
# --------------------------------------------------------------------- #
#
# A "container" is a parent element that holds an ordered list of
# children identified by a key attribute. Adds/removes inside a
# container are first-class merge operations; bodies of individual
# items are merged as opaque text (with byte-equality comparison
# falling back to a residual `body` conflict).
#
# This table is intentionally narrow: it covers every payload that
# Phase 5 surfaces in the tree, the form panels, and the AI intent
# matrix. Anything outside is treated as opaque content and a
# divergence becomes a single residual `body` conflict on the
# enclosing element.

_CONTAINERS: tuple[tuple[str, str, str], ...] = (
    # (parent xpath relative to root, child tag, key attribute)
    ("hlrs/section",          "hlr",     "id"),
    ("llrs/function",         "llr",     "id"),
    ("tests/file",            "test",    "name"),
    ("sdd/modules",           "module",  "path"),
    ("stp",                   "fixture", "id"),
)

# Group containers by parent path so we can match the section/function/
# file dimension first, then merge the inner item list.
_GROUP_CONTAINERS: tuple[tuple[str, str, str], ...] = (
    ("hlrs",     "section",  "number"),
    ("llrs",     "function", "name"),
    ("tests",    "file",     "path"),
    ("sdd",      "modules",  None),     # singleton wrapper, no key
)


# --------------------------------------------------------------------- #
# Result types                                                          #
# --------------------------------------------------------------------- #

@dataclass
class Conflict:
    """One residual merge conflict that Stage A could not resolve.

    ``kind`` is one of:

    * ``"body"``           — same item, body edited on both sides.
    * ``"modify_delete"``  — one side removed an item the other modified.
    * ``"id_collision"``   — both sides added items with the same id but
                             different content; ours keeps the id, theirs
                             is renumbered to ``rename_to``.
    * ``"trace"``          — same item, traces edited on both sides
                             with overlapping but non-identical
                             changes (after the union pass).
    * ``"schema_bump"``    — `schema_version` differs across all three
                             inputs.
    """
    kind: str
    container: str             # human-readable container path
    key: str                   # the id/name/path of the conflicting item
    type: str                  # complex-type hint for AI intent dispatch
    base: str | None = None    # serialised content (xml string) on base
    ours: str | None = None    # serialised content on ours
    theirs: str | None = None  # serialised content on theirs
    rename_to: str | None = None
    note: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class MergeResult:
    merged_xml: str
    residual_conflicts: list[Conflict] = field(default_factory=list)
    lint: dict[str, Any] = field(default_factory=dict)
    auto_resolved: int = 0
    refused: bool = False
    refusal: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "merged_xml": self.merged_xml,
            "residual_conflicts": [c.to_dict() for c in self.residual_conflicts],
            "lint": self.lint,
            "auto_resolved": self.auto_resolved,
            "refused": self.refused,
            "refusal": self.refusal,
        }


class MergeError(RuntimeError):
    pass


# --------------------------------------------------------------------- #
# Public entry point                                                    #
# --------------------------------------------------------------------- #

def merge_three_way(
    base: str | bytes | Path | None,
    ours: str | bytes | Path,
    theirs: str | bytes | Path,
    *,
    xsd_path: str | Path = _LINT_DEFAULT_XSD,
) -> MergeResult:
    """Run Stage A: deterministic structural merge.

    ``base`` is the common ancestor blob (`git show :1:Project.xml`),
    ``ours`` is the index/HEAD side (`:2:`), and ``theirs`` is the
    incoming branch (`:3:`). Each may be a Path, bytes, or string of
    XML. ``base`` of None / empty signals an unavailable merge base
    (rebase, octopus, cherry-pick without base) — Stage A refuses
    rather than silently picking one side, per HLR-034.
    """
    if base is None:
        return MergeResult(
            merged_xml="",
            refused=True,
            refusal=(
                "Stage A refuses: the Git merge base is unavailable "
                "(rebase, octopus, or cherry-pick without a base). "
                "Resolve manually in the merge editor."
            ),
        )
    base_bytes = _read(base)
    if not base_bytes.strip():
        return MergeResult(
            merged_xml="",
            refused=True,
            refusal=(
                "Stage A refuses: the Git merge base is empty. "
                "Resolve manually in the merge editor."
            ),
        )
    ours_bytes = _read(ours)
    theirs_bytes = _read(theirs)

    parser = etree.XMLParser(
        remove_blank_text=False,
        remove_comments=False,
        strip_cdata=False,
        resolve_entities=False,
    )
    base_root = etree.fromstring(base_bytes, parser)
    ours_root = etree.fromstring(ours_bytes, parser)
    theirs_root = etree.fromstring(theirs_bytes, parser)

    conflicts: list[Conflict] = []
    auto = _Counter()

    # 1. schema_version handling.
    _merge_schema_version(base_root, ours_root, theirs_root, conflicts)

    # 2. Walk the registered group containers (hlrs/section,
    #    llrs/function, tests/file, sdd/modules) and recurse into
    #    each item container.
    for parent_xp, child_tag, key_attr in _GROUP_CONTAINERS:
        _merge_group(
            base_root, ours_root, theirs_root,
            parent_xp=parent_xp,
            group_tag=child_tag,
            group_key=key_attr,
            conflicts=conflicts,
            auto=auto,
        )

    # 3. Recompute metadata/counts.
    _recompute_counts(ours_root)

    merged_bytes = etree.tostring(
        ours_root,
        xml_declaration=True,
        encoding="UTF-8",
    )
    merged_xml = merged_bytes.decode("utf-8")

    # 4. Lint the merged output (best-effort; lint failure does not
    #    sink the merge — Stage A is responsible for structure, not
    #    semantics).
    lint_dict: dict[str, Any] = {}
    try:
        import tempfile, os
        fd, tmp = tempfile.mkstemp(prefix=".project_merge_", suffix=".xml")
        try:
            with os.fdopen(fd, "wb") as f:
                f.write(merged_bytes)
            findings = _lint(Path(tmp), Path(xsd_path))
            lint_dict = findings.to_dict()
            lint_dict["ok"] = not findings.errors
        finally:
            try:
                os.unlink(tmp)
            except OSError:
                pass
    except Exception as exc:  # pragma: no cover - defensive
        lint_dict = {
            "errors": [f"lint failed: {exc}"],
            "warnings": [],
            "notes": [],
            "items": [],
            "ok": False,
        }

    return MergeResult(
        merged_xml=merged_xml,
        residual_conflicts=conflicts,
        lint=lint_dict,
        auto_resolved=auto.value,
    )


# --------------------------------------------------------------------- #
# Internals                                                             #
# --------------------------------------------------------------------- #

class _Counter:
    __slots__ = ("value",)
    def __init__(self): self.value = 0
    def inc(self, n: int = 1): self.value += n


def _read(source: str | bytes | Path) -> bytes:
    if isinstance(source, bytes):
        return source.lstrip()
    if isinstance(source, Path):
        return source.read_bytes().lstrip()
    if isinstance(source, str):
        if not source.strip():
            return b""
        s = source.lstrip()
        if s.startswith("<"):
            return s.encode("utf-8")
        return Path(source).read_bytes().lstrip()
    raise TypeError(f"unsupported source type: {type(source).__name__}")


def _xml_eq(a: "etree._Element | None", b: "etree._Element | None") -> bool:
    """Byte-level equality comparison after canonicalisation."""
    if a is None and b is None:
        return True
    if a is None or b is None:
        return False
    return etree.tostring(a) == etree.tostring(b)


def _serialise(elem: "etree._Element | None") -> str | None:
    if elem is None:
        return None
    return etree.tostring(elem, pretty_print=False).decode("utf-8")


def _merge_schema_version(
    base: "etree._Element",
    ours: "etree._Element",
    theirs: "etree._Element",
    conflicts: list[Conflict],
) -> None:
    bv = base.get("schema_version")
    ov = ours.get("schema_version")
    tv = theirs.get("schema_version")
    if ov == tv:
        return
    if ov == bv:
        ours.set("schema_version", tv or "")
        return
    if tv == bv:
        return  # ours already wins
    # Three-way divergence — surface as a residual conflict.
    conflicts.append(Conflict(
        kind="schema_bump",
        container="/project",
        key="schema_version",
        type="Project",
        base=bv,
        ours=ov,
        theirs=tv,
        note="schema_version differs on all three sides",
    ))


def _merge_group(
    base_root: "etree._Element",
    ours_root: "etree._Element",
    theirs_root: "etree._Element",
    *,
    parent_xp: str,
    group_tag: str,
    group_key: str | None,
    conflicts: list[Conflict],
    auto: _Counter,
) -> None:
    """Merge the children of a wrapper element keyed by ``group_key``.

    For ``hlrs/section`` this dispatches into per-section item merges
    keyed on ``hlr/@id``. For singletons (``sdd/modules``) we collapse
    to a single virtual group.
    """
    base_parent = base_root.find(parent_xp)
    ours_parent = ours_root.find(parent_xp)
    theirs_parent = theirs_root.find(parent_xp)
    if ours_parent is None and theirs_parent is None:
        return
    if ours_parent is None:
        # Branch added the entire wrapper. Append it under root.
        ours_root.append(deepcopy(theirs_parent))
        auto.inc()
        return
    if theirs_parent is None:
        return

    if group_key is None:
        # Singleton wrapper: merge a single virtual group.
        _merge_item_container(
            base_parent, ours_parent, theirs_parent,
            container_path=f"/{parent_xp}",
            conflicts=conflicts,
            auto=auto,
        )
        return

    # Index groups by key.
    base_groups = _index(base_parent, group_tag, group_key)
    ours_groups = _index(ours_parent, group_tag, group_key)
    theirs_groups = _index(theirs_parent, group_tag, group_key)

    # Add any groups present in theirs but not in ours.
    for k, t_elem in theirs_groups.items():
        if k in ours_groups:
            continue
        if k in base_groups and _xml_eq(t_elem, base_groups[k]):
            # Removed on ours, unchanged on theirs → respect deletion.
            continue
        ours_parent.append(deepcopy(t_elem))
        ours_groups[k] = ours_parent.find(f"./{group_tag}[@{group_key}={_q(k)}]")
        auto.inc()

    # Recurse into each shared group.
    for k, ours_group in list(ours_groups.items()):
        base_group = base_groups.get(k)
        theirs_group = theirs_groups.get(k)
        if theirs_group is None:
            if base_group is not None and _xml_eq(ours_group, base_group):
                # Removed on theirs, unchanged on ours → drop.
                ours_parent.remove(ours_group)
                auto.inc()
            # else ours-side modification preserved (modify wins over delete
            # would be the wrong default; we keep ours and surface the
            # divergence as a conflict only if theirs side actually deleted
            # something that base contained).
            elif base_group is not None and not _xml_eq(ours_group, base_group):
                conflicts.append(Conflict(
                    kind="modify_delete",
                    container=f"/{parent_xp}/{group_tag}[@{group_key}={_q(k)}]",
                    key=str(k),
                    type=group_tag.capitalize(),
                    base=_serialise(base_group),
                    ours=_serialise(ours_group),
                    theirs=None,
                    note="theirs deleted, ours modified",
                ))
            continue
        _merge_item_container(
            base_group, ours_group, theirs_group,
            container_path=f"/{parent_xp}/{group_tag}[@{group_key}={_q(k)}]",
            conflicts=conflicts,
            auto=auto,
        )


def _merge_item_container(
    base_parent: "etree._Element | None",
    ours_parent: "etree._Element",
    theirs_parent: "etree._Element",
    *,
    container_path: str,
    conflicts: list[Conflict],
    auto: _Counter,
) -> None:
    """Merge the items under a shared parent.

    Looks up which item tag/key applies to this container path from
    :data:`_CONTAINERS`. If no entry matches, falls back to a body
    merge of the whole element.
    """
    item_tag, key_attr = _container_for(container_path)
    if item_tag is None:
        _merge_opaque(
            base_parent, ours_parent, theirs_parent,
            container_path=container_path,
            conflicts=conflicts,
            auto=auto,
        )
        return

    base_items = _index(base_parent, item_tag, key_attr) if base_parent is not None else {}
    ours_items = _index(ours_parent, item_tag, key_attr)
    theirs_items = _index(theirs_parent, item_tag, key_attr)

    # Allocate next free ids if id collisions on a key like "id" require
    # renumbering the theirs side. We use the union of keys across all
    # three trees as the "in use" set.
    in_use: set[str] = set()
    for d in (base_items, ours_items, theirs_items):
        in_use.update(d.keys())

    # Pass 1: items added on theirs but not on ours.
    for k, t_item in list(theirs_items.items()):
        if k in ours_items:
            continue
        if k in base_items and _xml_eq(t_item, base_items[k]):
            # Removed on ours unchanged on theirs → drop.
            continue
        # New on theirs.
        ours_parent.append(deepcopy(t_item))
        ours_items[k] = ours_parent.find(f"./{item_tag}[@{key_attr}={_q(k)}]")
        auto.inc()

    # Pass 2: items in both ours and theirs — body merge.
    for k, ours_item in list(ours_items.items()):
        base_item = base_items.get(k)
        theirs_item = theirs_items.get(k)
        if theirs_item is None:
            # Either removed on theirs or never added there.
            if base_item is not None and _xml_eq(ours_item, base_item):
                ours_parent.remove(ours_item)
                auto.inc()
            elif base_item is not None and not _xml_eq(ours_item, base_item):
                conflicts.append(Conflict(
                    kind="modify_delete",
                    container=container_path,
                    key=str(k),
                    type=item_tag.capitalize(),
                    base=_serialise(base_item),
                    ours=_serialise(ours_item),
                    theirs=None,
                    note="theirs deleted, ours modified",
                ))
            continue

        if _xml_eq(ours_item, theirs_item):
            continue  # identical edits or no edits

        # Branched modify/add cases.
        if base_item is None:
            # Both added, same key, different content → id collision.
            new_key = _allocate_free_id(k, in_use)
            in_use.add(new_key)
            renamed = deepcopy(theirs_item)
            renamed.set(key_attr, new_key)
            ours_parent.append(renamed)
            conflicts.append(Conflict(
                kind="id_collision",
                container=container_path,
                key=str(k),
                type=item_tag.capitalize(),
                base=None,
                ours=_serialise(ours_item),
                theirs=_serialise(theirs_item),
                rename_to=new_key,
                note=f"both branches added {k!r}; theirs renamed to {new_key!r}",
            ))
            auto.inc()
            continue

        if _xml_eq(ours_item, base_item):
            # Only theirs changed → take theirs.
            _replace_in_place(ours_parent, ours_item, deepcopy(theirs_item))
            ours_items[k] = ours_parent.find(f"./{item_tag}[@{key_attr}={_q(k)}]")
            auto.inc()
            continue
        if _xml_eq(theirs_item, base_item):
            # Only ours changed → keep ours.
            continue

        # Both sides changed differently. Try to resolve at finer
        # granularity: traces unioned by (target,ref); body residual.
        if _try_field_level_merge(
            base_item, ours_item, theirs_item,
            container_path=container_path,
            key=str(k),
            type_name=item_tag.capitalize(),
            conflicts=conflicts,
            auto=auto,
        ):
            continue

        conflicts.append(Conflict(
            kind="body",
            container=container_path,
            key=str(k),
            type=item_tag.capitalize(),
            base=_serialise(base_item),
            ours=_serialise(ours_item),
            theirs=_serialise(theirs_item),
            note="both sides edited the body",
        ))


def _merge_opaque(
    base: "etree._Element | None",
    ours: "etree._Element",
    theirs: "etree._Element",
    *,
    container_path: str,
    conflicts: list[Conflict],
    auto: _Counter,
) -> None:
    if _xml_eq(ours, theirs):
        return
    if base is not None and _xml_eq(ours, base):
        # Replace ours' children with theirs' children in place.
        for c in list(ours):
            ours.remove(c)
        for c in list(theirs):
            ours.append(deepcopy(c))
        ours.text = theirs.text
        auto.inc()
        return
    if base is not None and _xml_eq(theirs, base):
        return
    conflicts.append(Conflict(
        kind="body",
        container=container_path,
        key="",
        type=ours.tag.capitalize(),
        base=_serialise(base),
        ours=_serialise(ours),
        theirs=_serialise(theirs),
        note="both sides edited an opaque container",
    ))


def _try_field_level_merge(
    base_item: "etree._Element",
    ours_item: "etree._Element",
    theirs_item: "etree._Element",
    *,
    container_path: str,
    key: str,
    type_name: str,
    conflicts: list[Conflict],
    auto: _Counter,
) -> bool:
    """Attempt to resolve a both-modified item by merging individual
    children. If the only divergence is in ``<traces>`` we can union by
    (target, ref); other divergent children fall through to a body
    conflict.

    Returns True iff every divergent child was resolvable without a
    residual conflict.
    """
    # Compare each child by tag.
    base_children = list(base_item)
    ours_children = list(ours_item)
    theirs_children = list(theirs_item)

    # Build per-tag groups (most payloads have unique child tags).
    def _by_tag(children: list) -> dict[str, list]:
        out: dict[str, list] = {}
        for c in children:
            out.setdefault(c.tag, []).append(c)
        return out

    base_t = _by_tag(base_children)
    ours_t = _by_tag(ours_children)
    theirs_t = _by_tag(theirs_children)

    all_tags = set(base_t) | set(ours_t) | set(theirs_t)
    body_conflict_tags: list[str] = []
    trace_conflict = False

    for tag in all_tags:
        b = base_t.get(tag, [])
        o = ours_t.get(tag, [])
        t = theirs_t.get(tag, [])
        if [etree.tostring(x) for x in o] == [etree.tostring(x) for x in t]:
            continue
        if [etree.tostring(x) for x in o] == [etree.tostring(x) for x in b]:
            # Replace ours' children of this tag with theirs.
            for x in o:
                ours_item.remove(x)
            for x in t:
                ours_item.append(deepcopy(x))
            auto.inc()
            continue
        if [etree.tostring(x) for x in t] == [etree.tostring(x) for x in b]:
            continue  # ours wins
        if tag == "traces" and o and t:
            # Union by (target, ref).
            unioned = _union_traces(b[0] if b else None, o[0], t[0])
            ours_item.remove(o[0])
            ours_item.append(unioned)
            auto.inc()
            continue
        body_conflict_tags.append(tag)

    # Attribute divergence (e.g. @name on hlr): treat as body conflict.
    base_attrs = dict(base_item.attrib)
    ours_attrs = dict(ours_item.attrib)
    theirs_attrs = dict(theirs_item.attrib)
    for ak in set(base_attrs) | set(ours_attrs) | set(theirs_attrs):
        ov = ours_attrs.get(ak)
        tv = theirs_attrs.get(ak)
        bv = base_attrs.get(ak)
        if ov == tv:
            continue
        if ov == bv:
            if tv is None:
                ours_item.attrib.pop(ak, None)
            else:
                ours_item.set(ak, tv)
            auto.inc()
            continue
        if tv == bv:
            continue
        body_conflict_tags.append(f"@{ak}")

    if not body_conflict_tags:
        return True

    # Caller is responsible for appending the body conflict so we
    # don't double-count when only one container path is in play.
    return False


def _union_traces(
    base: "etree._Element | None",
    ours: "etree._Element",
    theirs: "etree._Element",
) -> "etree._Element":
    """Return a new ``<traces>`` element containing the union of
    ``<trace>`` rows from ours and theirs (and any base rows still
    present on either side). Rows are keyed by ``(target, ref)``.
    """
    seen: dict[tuple[str, str], "etree._Element"] = {}
    base_keys = set()
    if base is not None:
        for t in base:
            if t.tag == "trace":
                base_keys.add((t.get("target", ""), t.get("ref", "")))
    for source in (ours, theirs):
        for trace in source:
            if trace.tag != "trace":
                continue
            k = (trace.get("target", ""), trace.get("ref", ""))
            if k in seen:
                continue
            seen[k] = deepcopy(trace)
    # Deletions: a row that was in base but is missing on BOTH ours and
    # theirs is a deliberate deletion. We keep rows that survived on at
    # least one side, which is the union semantics requested.
    merged = etree.Element("traces")
    for k, trace in seen.items():
        merged.append(trace)
    # Preserve original tail of ours' first child for whitespace continuity.
    return merged


# --------------------------------------------------------------------- #
# Helpers                                                               #
# --------------------------------------------------------------------- #

def _index(parent: "etree._Element | None", tag: str, key_attr: str) -> dict[str, "etree._Element"]:
    out: dict[str, "etree._Element"] = {}
    if parent is None:
        return out
    for child in parent:
        if child.tag != tag:
            continue
        k = child.get(key_attr)
        if k is None:
            continue
        out[k] = child
    return out


def _q(value: str) -> str:
    """Quote an attribute value for an xpath predicate."""
    if "'" in value:
        return f'"{value}"'
    return f"'{value}'"


def _container_for(container_path: str) -> tuple[str | None, str | None]:
    """Return (item_tag, key_attr) for a given container path, or
    (None, None) if the path is not a registered list container."""
    # container_path looks like '/hlrs/section[@number=...]' or '/sdd/modules'
    for parent_xp, child_tag, key_attr in _CONTAINERS:
        # Match if container_path ends with the parent_xp head or a
        # group-keyed instance of it.
        head = parent_xp.split("/")[0]      # e.g. "hlrs"
        tail = parent_xp.split("/")[-1]     # e.g. "section"
        if (
            container_path == f"/{parent_xp}"
            or container_path.startswith(f"/{head}/{tail}[")
            or container_path.startswith(f"/{head}/{tail}/")
            or container_path == f"/{head}"  # singleton wrappers
        ):
            return child_tag, key_attr
    return None, None


def _allocate_free_id(existing: str, in_use: set[str]) -> str:
    """Allocate the next free id of the same shape as ``existing``.

    Supports two shapes used by the Project.xml id contract:

    * ``HLR-NNN``     (HLR-005)
    * ``LLR-XXX-NN``  (LLR-005)

    For other patterns we append ``-1``, ``-2`` … until a free name
    appears.
    """
    import re
    m = re.match(r"^(HLR-)(\d+)$", existing)
    if m:
        prefix = m.group(1)
        n = int(m.group(2))
        while True:
            n += 1
            cand = f"{prefix}{n:03d}"
            if cand not in in_use:
                return cand
    m = re.match(r"^(LLR-[^-]+-)(\d+)$", existing)
    if m:
        prefix = m.group(1)
        n = int(m.group(2))
        while True:
            n += 1
            cand = f"{prefix}{n:02d}"
            if cand not in in_use:
                return cand
    n = 1
    while f"{existing}-{n}" in in_use:
        n += 1
    return f"{existing}-{n}"


def _replace_in_place(
    parent: "etree._Element",
    old: "etree._Element",
    new: "etree._Element",
) -> None:
    idx = list(parent).index(old)
    parent.remove(old)
    parent.insert(idx, new)


def _recompute_counts(root: "etree._Element") -> None:
    counts = root.find("metadata/counts")
    if counts is None:
        return
    mapping = {
        "hlrs":     len(root.findall("hlrs/section/hlr")),
        "llrs":     len(root.findall("llrs/function/llr")),
        "tests":    len(root.findall("tests/file/test")),
        "modules":  len(root.findall("sdd/modules/module")),
        "fixtures": len(root.findall("stp/fixture")),
    }
    for child in counts:
        tag = child.tag
        if tag in mapping:
            child.text = str(mapping[tag])


# --------------------------------------------------------------------- #
# Conflict resolution helpers (Stage B feed)                            #
# --------------------------------------------------------------------- #

def apply_resolution(
    merged_xml: str | bytes,
    conflict: Conflict | dict[str, Any],
    resolution: dict[str, Any],
) -> str:
    """Substitute a Stage-B (AI or manual) resolution into the merged
    XML and return the new merged XML string.

    ``resolution`` shapes (per intent):

    * ``merge.body``      → ``{"merged_xml": "<hlr ...>...</hlr>"}``
                            (single complete payload element)
    * ``merge.trace``     → ``{"traces": [{"target", "ref", "name?"}, ...]}``
    * ``merge.rename``    → ``{"final_id": "HLR-NNN"}`` (rename the
                            ``rename_to`` placeholder back to a chosen id)
    * ``merge.schema_bump`` → ``{"schema_version": "1.6"}``
    """
    if isinstance(conflict, dict):
        kind = conflict.get("kind")
        container = conflict.get("container", "")
        key = conflict.get("key", "")
        rename_to = conflict.get("rename_to")
    else:
        kind = conflict.kind
        container = conflict.container
        key = conflict.key
        rename_to = conflict.rename_to

    parser = etree.XMLParser(
        remove_blank_text=False,
        remove_comments=False,
        strip_cdata=False,
        resolve_entities=False,
    )
    root = etree.fromstring(
        merged_xml.encode("utf-8") if isinstance(merged_xml, str) else merged_xml,
        parser,
    )

    if kind == "schema_bump":
        new_v = (resolution.get("schema_version") or "").strip()
        if new_v:
            root.set("schema_version", new_v)
    elif kind == "body":
        target = root.find(container.lstrip("/"))
        if target is None:
            raise MergeError(f"apply_resolution: cannot locate {container!r}")
        new_body = resolution.get("merged_xml") or ""
        new_elem = etree.fromstring(new_body.encode("utf-8"), parser)
        parent = target.getparent()
        idx = list(parent).index(target)
        parent.remove(target)
        parent.insert(idx, new_elem)
    elif kind == "trace":
        target = root.find(container.lstrip("/"))
        if target is None:
            raise MergeError(f"apply_resolution: cannot locate {container!r}")
        new_traces = etree.SubElement(target, "_tmp_traces")
        for t in resolution.get("traces") or []:
            tr = etree.SubElement(new_traces, "trace")
            tr.set("target", str(t.get("target", "")))
            tr.set("ref", str(t.get("ref", "")))
            if t.get("name"):
                tr.set("name", str(t["name"]))
        existing = target.find("traces")
        if existing is not None:
            target.remove(existing)
        new_traces.tag = "traces"
    elif kind == "rename":
        if rename_to:
            for elem in root.iter():
                for ak, av in list(elem.attrib.items()):
                    if av == rename_to:
                        final = (resolution.get("final_id") or "").strip()
                        if final:
                            elem.set(ak, final)
    elif kind == "id_collision":
        # The renamed theirs-side item already lives in the tree under
        # rename_to; the resolution lets the user pick a final id.
        final = (resolution.get("final_id") or "").strip()
        if final and rename_to:
            for elem in root.iter():
                if elem.get("id") == rename_to or elem.get("name") == rename_to:
                    for ak in ("id", "name"):
                        if elem.get(ak) == rename_to:
                            elem.set(ak, final)
    elif kind == "modify_delete":
        # Default policy: keep ours (modify wins). The TS layer can
        # also pass a "delete": True flag to remove the item.
        if resolution.get("delete"):
            target = root.find(container.lstrip("/"))
            if target is not None:
                target.getparent().remove(target)
    else:
        raise MergeError(f"apply_resolution: unsupported conflict kind {kind!r}")

    return etree.tostring(
        root, xml_declaration=True, encoding="UTF-8",
    ).decode("utf-8")


__all__ = [
    "Conflict",
    "MergeResult",
    "MergeError",
    "merge_three_way",
    "apply_resolution",
]
