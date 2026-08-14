"""Grounding-bundle assembly for the inline-AI authoring layer (HLR-029).

The bundle is the *only* thing the LM sees. Building it is deterministic,
self-contained, and **never drops the schema or the user intent** — when
the configured ``max_tokens`` cap is exceeded the packer drops examples
and sibling lists first, then summarises long lists ("45 HLRs, ids
HLR-001..HLR-045"), and only as a last resort truncates the PVD/SDD
excerpts. Schema, intent name, user prompt, and target identity are
load-bearing and are never elided.

Token budgeting is approximate (4 chars per token) — we never call out
to a tokenizer because the worst-case error (under-packing) is harmless,
the LM-side counter is authoritative, and a pure-Python pipeline must
not have a heavyweight runtime dep.
"""
from __future__ import annotations

from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Mapping, Sequence

from render_doc import (
    PROJECT_XML,
    PROJECT_XSD,
    parse_project_to_dict,
    parse_ui_hints_index,
)


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
PVD_PATH = REPO_ROOT / "doc" / "PVD.md"
SDD_PATH = REPO_ROOT / "doc" / "SDD.md"
SCHEMA_REFERENCE_PATH = REPO_ROOT / "tools" / "Developers_Guide.md"

# 4 chars ~= 1 token. Conservative; under-packs rather than over-packs.
_CHARS_PER_TOKEN = 4

DEFAULT_MAX_TOKENS = 16000


@dataclass
class TargetSpec:
    """Identifies the element an AI request applies to.

    For ``draft.*`` and ``expand.*`` intents the target identifies the
    *anchor* (e.g. the HLR being expanded into LLRs); for fresh drafts
    the anchor names the section the new element will belong to.
    """
    type: str                      # e.g. "Hlr", "Llr", "Test", "SddModule", "Pvd"
    id: str | None = None          # e.g. "HLR-001"
    section: str | None = None     # e.g. "1" (HLR section) or "PCL" (LLR function)
    file: str | None = None        # for tests: <file path=...>
    extra: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        d = {"type": self.type}
        if self.id is not None:
            d["id"] = self.id
        if self.section is not None:
            d["section"] = self.section
        if self.file is not None:
            d["file"] = self.file
        if self.extra:
            d["extra"] = dict(self.extra)
        return d


@dataclass
class Bundle:
    """A grounding bundle ready to be serialised into a system prompt.

    Section order is significant: the packer emits sections in this order
    and truncates from the bottom up when the token budget is tight.
    """
    intent: str
    user_prompt: str
    target: dict[str, Any]
    schema_excerpt: str           # XSD subtree for ``target.type``
    response_schema: dict[str, Any]   # the intent's JSON Schema
    next_free_ids: dict[str, str] = field(default_factory=dict)
    available_refs: dict[str, list[Any]] = field(default_factory=dict)
    upstream: dict[str, Any] = field(default_factory=dict)
    siblings: list[dict[str, Any]] = field(default_factory=list)
    pvd_excerpt: str = ""
    sdd_excerpt: str = ""
    schema_reference_excerpt: str = ""
    lint_state: dict[str, Any] = field(default_factory=dict)
    # Bookkeeping
    truncations: list[str] = field(default_factory=list)
    estimated_tokens: int = 0

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def _approx_tokens(text: str | bytes) -> int:
    if isinstance(text, bytes):
        text = text.decode("utf-8", errors="ignore")
    return max(1, (len(text) + _CHARS_PER_TOKEN - 1) // _CHARS_PER_TOKEN)


def _read_text(path: Path, *, max_chars: int | None = None) -> str:
    try:
        text = path.read_text(encoding="utf-8")
    except (FileNotFoundError, OSError):
        return ""
    if max_chars is not None and len(text) > max_chars:
        return text[:max_chars] + "\n\n[…truncated…]"
    return text


def _xsd_subtree_for(type_name: str, xsd_path: Path) -> str:
    """Extract the XSD ``<xs:complexType name="type_name">`` block as text.

    Returns the raw XML fragment so the LM sees attribute names, child
    elements, restrictions, and any ``xs:appinfo`` UI hints verbatim. Never
    truncated: the schema is load-bearing per HLR-029.
    """
    try:
        text = xsd_path.read_text(encoding="utf-8")
    except (FileNotFoundError, OSError):
        return ""
    # Naive scan; the XSD is small (<1k lines) and well-formatted.
    needle = f'name="{type_name}"'
    idx = text.find(needle)
    if idx < 0:
        return ""
    # Walk back to the opening <xs:complexType.
    open_idx = text.rfind("<xs:complexType", 0, idx)
    if open_idx < 0:
        return ""
    close_idx = text.find("</xs:complexType>", idx)
    if close_idx < 0:
        return ""
    return text[open_idx:close_idx + len("</xs:complexType>")]


def _summarise_list(label: str, ids: Sequence[str], sample: int = 5) -> str:
    if not ids:
        return f"{label}: (none)"
    if len(ids) <= sample * 2:
        return f"{label} ({len(ids)}): {', '.join(ids)}"
    head = ", ".join(ids[:sample])
    tail = ", ".join(ids[-sample:])
    return f"{label} ({len(ids)}): {head} … {tail}"


def _collect_refs(project: Mapping[str, Any]) -> dict[str, list[str | dict[str, str]]]:
    """Collect the closed sets the LM is allowed to reference, sourced from
    the parsed tree. Used both for prompt grounding and (TS side) for the
    typed-response validator's ``ref:HLR`` / ``ref:LLR`` enums.

    HLR entries include {id, name} so the LM can populate the trace
    ``name`` attribute without guessing.
    """
    flat_hlrs = project.get("flat_hlrs") or []
    flat_llrs = project.get("flat_llrs") or []
    sdd_modules = (project.get("sdd") or {}).get("modules") or []
    return {
        "HLR": [{"id": h["id"], "name": h.get("name", "")} for h in flat_hlrs if h.get("id")],
        "LLR": [l.get("id") for l in flat_llrs if l.get("id")],
        "SDD": [m.get("path") or m.get("title") for m in sdd_modules if m.get("path") or m.get("title")],
    }


def _next_free_ids(project: Mapping[str, Any]) -> dict[str, str]:
    """Pre-allocate the ids the LM should use in fresh-draft responses.

    Stable allocation per HLR-005/HLR-051: re-uses the parsed tree so the
    LM does not invent ids that collide with existing rows. ``LLR`` and
    ``Test`` ids depend on context (function prefix / file) and are
    handled per-intent inside the translator.
    """
    flat_hlrs = project.get("flat_hlrs") or []
    used_hlrs = sorted(int(h["id"].split("-")[1]) for h in flat_hlrs
                       if isinstance(h.get("id"), str) and h["id"].startswith("HLR-"))
    next_hlr = (used_hlrs[-1] + 1) if used_hlrs else 1
    return {
        "HLR": f"HLR-{next_hlr:03d}",
    }


def _siblings(project: Mapping[str, Any], target: TargetSpec, *, limit: int = 4) -> list[dict[str, Any]]:
    """Return up to ``limit`` already-authored elements of the same type
    as the target so the LM can match their voice and structure.
    """
    if target.type == "Hlr":
        items = project.get("flat_hlrs") or []
        return [
            {"id": h.get("id"), "name": h.get("name"), "text": h.get("text")}
            for h in items[:limit]
        ]
    if target.type == "Llr":
        items = project.get("flat_llrs") or []
        return [
            {"id": l.get("id"), "text": l.get("text")}
            for l in items[:limit]
        ]
    if target.type == "Test":
        items = project.get("flat_tests") or []
        return [
            {"name": t.get("name"), "purpose": t.get("purpose")}
            for t in items[:limit]
        ]
    if target.type == "SddModule":
        modules = (project.get("sdd") or {}).get("modules") or []
        return [
            {"path": m.get("path"), "title": m.get("title")}
            for m in modules[:limit]
        ]
    return []


def _upstream(project: Mapping[str, Any], target: TargetSpec) -> dict[str, Any]:
    """Return the upstream item the target traces to, if available."""
    if target.type == "Llr" and target.id:
        # Caller may name an HLR section anchor in extra; fall through.
        return {"section_hint": target.section} if target.section else {}
    if target.type == "Hlr" and target.id:
        # HLR upstream is the SDD module(s) it traces to — derived only
        # when the HLR already exists.
        for h in project.get("flat_hlrs") or []:
            if h.get("id") == target.id:
                return {"existing": h}
    return {}


def _lint_summary(lint_findings: Sequence[Mapping[str, Any]] | None,
                  target: TargetSpec) -> dict[str, Any]:
    """Filter the latest lint findings down to those relevant to the target."""
    if not lint_findings:
        return {"errors": 0, "warnings": 0}
    errors = [f for f in lint_findings if f.get("severity") == "error"]
    warnings = [f for f in lint_findings if f.get("severity") == "warning"]
    related: list[Mapping[str, Any]] = []
    needle = (target.id or "").lower()
    if needle:
        related = [
            f for f in lint_findings
            if needle in (f.get("message") or "").lower()
        ]
    return {
        "errors": len(errors),
        "warnings": len(warnings),
        "related": list(related),
    }


def build_context(
    intent: str,
    target: TargetSpec,
    user_prompt: str,
    *,
    response_schema: Mapping[str, Any],
    xml_path: Path | str = PROJECT_XML,
    xsd_path: Path | str = PROJECT_XSD,
    pvd_path: Path | str = PVD_PATH,
    sdd_path: Path | str = SDD_PATH,
    schema_reference_path: Path | str = SCHEMA_REFERENCE_PATH,
    max_tokens: int = DEFAULT_MAX_TOKENS,
    lint_findings: Sequence[Mapping[str, Any]] | None = None,
) -> Bundle:
    """Assemble a deterministic grounding bundle for a single AI request.

    The packer enforces ``max_tokens`` by trimming sections in this order:
    (1) ``schema_reference_excerpt`` chars, (2) ``sdd_excerpt`` chars,
    (3) ``pvd_excerpt`` chars, (4) ``siblings`` count, (5) ``upstream``
    detail. The XSD subtree for the target type, the response schema,
    the intent name, the user prompt, and the target identity are
    *never* dropped — they are load-bearing per HLR-029.
    """
    xml_path = Path(xml_path)
    xsd_path = Path(xsd_path)
    project = parse_project_to_dict(xml_path)

    schema_excerpt = _xsd_subtree_for(target.type, xsd_path) if target.type != "Pvd" else ""
    refs = _collect_refs(project)
    next_free = _next_free_ids(project)
    upstream = _upstream(project, target)
    siblings = _siblings(project, target)
    pvd_text = _read_text(Path(pvd_path))
    sdd_text = _read_text(Path(sdd_path))
    schema_ref_text = _read_text(Path(schema_reference_path))
    lint_state = _lint_summary(lint_findings, target)

    bundle = Bundle(
        intent=intent,
        user_prompt=user_prompt,
        target=target.to_dict(),
        schema_excerpt=schema_excerpt,
        response_schema=dict(response_schema),
        next_free_ids=next_free,
        available_refs=refs,
        upstream=upstream,
        siblings=siblings,
        pvd_excerpt=pvd_text,
        sdd_excerpt=sdd_text,
        schema_reference_excerpt=schema_ref_text,
        lint_state=lint_state,
    )
    _enforce_budget(bundle, max_tokens)
    return bundle


def _enforce_budget(bundle: Bundle, max_tokens: int) -> None:
    """Trim bundle sections in priority order until the estimate fits.

    Mutates ``bundle`` in place; appends a trail of trim operations to
    ``bundle.truncations`` so callers can audit what was dropped.
    """
    def _est() -> int:
        # Sum the major textual fields. Approximate, per file docstring.
        parts = [
            bundle.user_prompt,
            bundle.schema_excerpt,
            bundle.pvd_excerpt,
            bundle.sdd_excerpt,
            bundle.schema_reference_excerpt,
        ]
        n = sum(_approx_tokens(p) for p in parts)
        # Lists serialised compactly — ~30 tokens each is generous.
        n += len(bundle.siblings) * 30
        n += sum(len(v) for v in bundle.available_refs.values())
        return n

    bundle.estimated_tokens = _est()

    def _trim_text(field_name: str, label: str, target_chars: int) -> bool:
        """Trim ``field_name`` to ``target_chars`` (excluding the marker).
        Returns True when an actual reduction happened, False when the
        field is already at-or-below the cap (or already carries the
        marker, meaning we've trimmed it once at this step). Used by the
        loop to detect "no further progress" and exit instead of
        spinning forever (HLR-029: the loop must be monotonic).
        """
        text = getattr(bundle, field_name)
        marker = "\n\n[…truncated by packer…]"
        if text.endswith(marker):
            # Already trimmed at a previous step. Re-trimming to the
            # same threshold would be a no-op; return False so the
            # outer loop falls through to the next priority.
            inner_len = len(text) - len(marker)
            if inner_len <= target_chars:
                return False
            setattr(bundle, field_name, text[:target_chars] + marker)
            bundle.truncations.append(f"{label} -> {target_chars} chars")
            return True
        if len(text) <= target_chars:
            return False
        setattr(bundle, field_name, text[:target_chars] + marker)
        bundle.truncations.append(f"{label} -> {target_chars} chars")
        return True

    while bundle.estimated_tokens > max_tokens:
        # Step 1: shrink schema reference excerpt.
        if _trim_text("schema_reference_excerpt", "schema_reference", 2000):
            bundle.estimated_tokens = _est()
            continue
        # Step 2: shrink SDD excerpt.
        if _trim_text("sdd_excerpt", "sdd", 2000):
            bundle.estimated_tokens = _est()
            continue
        # Step 3: shrink PVD excerpt.
        if _trim_text("pvd_excerpt", "pvd", 2000):
            bundle.estimated_tokens = _est()
            continue
        # Step 4: drop siblings.
        if bundle.siblings:
            bundle.truncations.append(f"siblings dropped ({len(bundle.siblings)})")
            bundle.siblings = []
            bundle.estimated_tokens = _est()
            continue
        # Step 5: drop upstream detail.
        if bundle.upstream:
            bundle.truncations.append("upstream dropped")
            bundle.upstream = {}
            bundle.estimated_tokens = _est()
            continue
        # Step 6: aggressive trim of remaining text fields.
        if _trim_text("schema_reference_excerpt", "schema_reference", 200):
            bundle.estimated_tokens = _est()
            continue
        if _trim_text("sdd_excerpt", "sdd", 200):
            bundle.estimated_tokens = _est()
            continue
        if _trim_text("pvd_excerpt", "pvd", 200):
            bundle.estimated_tokens = _est()
            continue
        # Hit the floor: schema/intent/user_prompt/target are load-bearing
        # per HLR-029 and are never dropped, even if we exceed budget.
        bundle.truncations.append(
            f"budget exceeded; load-bearing fields kept "
            f"(estimated={bundle.estimated_tokens}, max={max_tokens})"
        )
        return


def render_system_prompt(bundle: Bundle, intent_template: str) -> str:
    """Render a system prompt by joining the intent's prompt template with a
    deterministic serialisation of the bundle.

    The template is the body of the intent's Markdown file under
    :mod:`tools.ai.intents`; the bundle is appended as a fenced JSON block
    so the LM sees a single, parseable artefact.
    """
    import json
    serialised = json.dumps(bundle.to_dict(), indent=2, default=str, sort_keys=True)
    return (
        intent_template.rstrip()
        + "\n\n## Grounding bundle (deterministic)\n\n"
        + "```json\n"
        + serialised
        + "\n```\n"
    )
