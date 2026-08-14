"""Phase 3 write surface: ``apply_edit`` and the JSON-Schema deriver.

Two things live here:

1.  :func:`apply_edit` — the authoritative write-back primitive for
    ``doc/Project.xml``.  It accepts a list of small JSON-Patch-like
    operations, applies them on a working copy of the parsed tree, runs
    XSD validation + the project linter, and **only on a clean result**
    rewrites the file via lxml so comments, CDATA, attribute order, and
    whitespace are preserved (HLR-018, HLR-019).  On validation failure
    the on-disk file is byte-identical to its pre-call state and the
    findings are returned to the caller.

2.  :func:`derive_form_schema` — a payload-agnostic helper that walks
    the XSD complex type bound to a given UI tree node and projects it
    plus the Phase 2.5b ``ui:form`` field hints into a single
    ``(jsonSchema, uiSchema)`` pair the VS Code form webview drives
    react-jsonschema-form with.

The patch path syntax is intentionally tiny — exactly what the form
panel and the Phase 2.5c structural Quick Fixes need:

* ``/hlrs/section[number=1]/hlr[id=HLR-001]/@name`` — set an attribute.
* ``/hlrs/section[number=1]/hlr[id=HLR-001]/text`` — set the text body
  of a child element (CDATA-wrapped automatically when the value
  contains markup-significant characters).
* ``/hlrs/section[number=1]/hlr[id=HLR-001]/traces`` — replace the
  ``<traces>`` subtree with ``{"trace": [{"@target": "...", ...}]}``.
* ``/hlrs/section[number=1]/hlr/-`` — append a new child described by
  the value dict (with ``@attr`` keys for attributes and bare keys for
  child elements).
* ``/hlrs/section[number=1]/hlr[id=HLR-001]`` — ``op=remove`` deletes
  the element entirely.

The selector grammar is documented in :func:`_resolve` below.
"""
from __future__ import annotations

import os
import re
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from render_doc import (
    PROJECT_XML,
    PROJECT_XSD,
    ProjectXmlError,
    parse_ui_hints_index,
)
from lint_project import lint as _lint, Findings  # noqa: F401 (re-export shape)

try:  # lxml is optional for the renderer but required for apply_edit.
    from lxml import etree  # type: ignore
except ImportError as exc:  # pragma: no cover — surfaced as ProjectXmlError below
    etree = None  # type: ignore
    _LXML_IMPORT_ERROR: ImportError | None = exc
else:
    _LXML_IMPORT_ERROR = None


# Characters that force a CDATA wrap when written back to the XML.
_CDATA_TRIGGERS = re.compile(r"[<>&]|\]\]>|\[[^\]]*\]\([^)]*\)|`")


def _beautify(xml_bytes: bytes) -> bytes:
    """Normalize indentation of serialized Project.xml.

    Re-parses with ``remove_blank_text=True`` and applies
    ``etree.indent`` so every element sits on its own line with
    consistent 2-space indentation.  CDATA sections and comments are
    preserved.
    """
    parser = etree.XMLParser(
        remove_blank_text=True,
        strip_cdata=False,
        remove_comments=False,
    )
    tree = etree.fromstring(xml_bytes, parser).getroottree()
    etree.indent(tree.getroot(), space="  ")
    return etree.tostring(tree, xml_declaration=True, encoding="UTF-8")


# --------------------------------------------------------------------- #
# JSON-Schema deriver                                                   #
# --------------------------------------------------------------------- #

# RJSF widget hints keyed on the ``ui:form/@kind`` value that
# ``parse_ui_hints_index`` projects onto each form field.
_WIDGET_BY_KIND = {
    "text":     {"ui:widget": "text"},
    "textarea": {"ui:widget": "textarea"},
    "cdata":    {"ui:widget": "textarea"},
    "enum":     {"ui:widget": "select"},
}


def derive_form_schema(
    type_name: str,
    *,
    xsd_path: Path | str = PROJECT_XSD,
    refs: dict[str, list[str]] | None = None,
) -> dict[str, Any]:
    """Derive a JSON Schema + uiSchema from the XSD subtree bound to a
    UI tree node, plus the matching ``ui:form`` field hints.

    Parameters
    ----------
    type_name:
        Complex-type name as it appears in the index returned by
        :func:`render_doc.parse_ui_hints_index` (e.g. ``"Hlr"`` or
        ``"Llr"``).
    xsd_path:
        Path to ``project.xsd``.  Defaults to the project XSD next to
        :mod:`render_doc`.
    refs:
        Optional snapshot of the ids the form should populate
        ``ref:HLR`` / ``ref:LLR`` / ``ref:SDD`` selectors with, sourced
        by the caller from the parsed tree.  When omitted the
        corresponding fields surface as free-text.

    The returned dict has shape::

        {
            "schema":   { ... JSON Schema ... },
            "uiSchema": { ... RJSF uiSchema ... },
            "fields":   [ ... raw form-hint entries from the XSD ... ],
            "type":     "Hlr",
        }

    The schema is intentionally minimal — strings, arrays of objects
    for ``Traces``, with ``required`` lifted from the XSD ``use`` flag
    and the ``ui:form/@required`` mirror.  More elaborate types
    (numbers, dates, enumerations beyond TraceTarget) join later as
    the schema grows; today the surface is exactly enough for HLRs and
    LLRs.
    """
    refs = dict(refs or {})
    index = parse_ui_hints_index(xsd_path)
    entry = index.get(type_name)
    if entry is None:
        raise ProjectXmlError(
            f"unknown UI hint entry '{type_name}'; "
            f"available: {sorted(index)}"
        )
    fields = list(entry.get("form") or [])
    if not fields:
        raise ProjectXmlError(
            f"complex type '{type_name}' has no <ui:form> hints in "
            f"{xsd_path}; cannot derive a form schema"
        )

    # Walk the XSD once so attribute use="required" can refine the
    # `required` field on each <ui:field>.
    xsd_use = _xsd_attribute_use(xsd_path, type_name)

    schema: dict[str, Any] = {
        "type": "object",
        "title": type_name,
        "properties": {},
        "required": [],
    }
    ui_schema: dict[str, Any] = {}

    for field in fields:
        target = field["target"]
        kind = field["field"]
        required = bool(field.get("required")) or xsd_use.get(target, False)

        property_schema: dict[str, Any]
        widget = _WIDGET_BY_KIND.get(kind, {}).copy()

        if kind.startswith("ref:"):
            target_kind = kind.split(":", 1)[1]
            # The Traces subtree is exposed as an array of {target, ref, name?}
            # objects; the field's `kind` only fixes the *primary* target the
            # picker pre-fills with, but every row stays freely editable.
            property_schema = _trace_array_schema(target_kind, refs)
            widget = {
                "items": {
                    "ref": {"ui:widget": "select"} if refs else {},
                },
            }
        elif kind == "cdata" or kind == "textarea":
            property_schema = {"type": "string", "title": target}
            widget.setdefault("ui:widget", "textarea")
            widget.setdefault("ui:options", {"rows": 8})
        else:
            property_schema = {"type": "string", "title": target}

        schema["properties"][target] = property_schema
        if widget:
            ui_schema[target] = widget
        if required:
            schema["required"].append(target)

    # Nudge RJSF towards a sane ordering: attributes first, then text,
    # then traces. The hint list is already authored in that order so
    # we just propagate it.
    ui_schema["ui:order"] = [field["target"] for field in fields]

    return {
        "schema": schema,
        "uiSchema": ui_schema,
        "fields": fields,
        "type": type_name,
    }


def _trace_array_schema(
    primary_target: str,
    refs: dict[str, list[str]],
) -> dict[str, Any]:
    """JSON Schema for a `<traces>` subtree, projected as an array of
    `{target, ref, name?}` objects so RJSF renders one editable row
    per trace with add/remove buttons.

    The ``ref`` enum contains the *union* of all known ids across every
    target kind so that:
    1. A Test tracing directly to an HLR still displays the ref value.
    2. The RJSF ``select`` widget always has a populated option list.
    """
    target_enum = sorted({primary_target, "SDD", "HLR", "LLR"})
    ref_schema: dict[str, Any] = {"type": "string", "title": "ref"}

    # Merge all ref-id pools into a single enum for the select widget.
    all_refs: list[str] = []
    for ids in refs.values():
        all_refs.extend(ids)
    if all_refs:
        # Deduplicate, keep stable order (sorted).
        ref_schema["enum"] = sorted(set(all_refs))

    return {
        "type": "array",
        "title": "traces",
        "items": {
            "type": "object",
            "properties": {
                "target": {
                    "type": "string",
                    "title": "target",
                    "enum": target_enum,
                    "default": primary_target,
                },
                "ref": ref_schema,
                "name": {"type": "string", "title": "name (optional)"},
            },
            "required": ["target", "ref"],
        },
    }


_XS_NAMESPACE = "http://www.w3.org/2001/XMLSchema"


def _xsd_attribute_use(
    xsd_path: Path | str,
    type_name: str,
) -> dict[str, bool]:
    """Return ``{attribute_name: required}`` for the named complex type."""
    import defusedxml.ElementTree as ET

    out: dict[str, bool] = {}
    try:
        root = ET.parse(xsd_path).getroot()
    except (ET.ParseError, FileNotFoundError):
        return out
    for ct in root.iter(f"{{{_XS_NAMESPACE}}}complexType"):
        if ct.get("name") != type_name:
            continue
        for attr in ct.findall(f"{{{_XS_NAMESPACE}}}attribute"):
            name = attr.get("name") or ""
            out[name] = attr.get("use", "optional") == "required"
        break
    return out


# --------------------------------------------------------------------- #
# apply_edit                                                            #
# --------------------------------------------------------------------- #

@dataclass
class ApplyEditResult:
    ok: bool
    written: bool
    findings: dict[str, Any]
    operations_applied: int

    def to_dict(self) -> dict[str, Any]:
        return {
            "ok": self.ok,
            "written": self.written,
            "findings": self.findings,
            "operations_applied": self.operations_applied,
        }


def apply_edit(
    operations: list[dict[str, Any]],
    *,
    xml_path: Path | str = PROJECT_XML,
    xsd_path: Path | str = PROJECT_XSD,
    expect_clean: bool = True,
    dry_run: bool = False,
) -> ApplyEditResult:
    """Apply a list of JSON-Patch-like operations to ``Project.xml``.

    The on-disk contract:

    * Read the current bytes of ``xml_path``.
    * Parse with lxml (preserving comments, CDATA, processing
      instructions, attribute order, whitespace).
    * Apply each ``operations[i]`` in order on the in-memory tree.
    * Serialise back to bytes; validate against the XSD (lxml or
      xmllint) and run :func:`lint_project.lint` on a temporary file
      holding the candidate output.
    * If ``expect_clean`` is True and validation produced any
      ``error``-severity finding (or XSD validation failed), the
      original file is **left untouched** and ``ApplyEditResult`` is
      returned with ``written=False`` and the structured findings.
    * Otherwise the file is replaced atomically (write to ``.tmp`` +
      ``os.replace``) so partial writes are impossible.

    Parameters
    ----------
    operations:
        List of dicts each shaped like ``{"op", "path", "value"}``.
        Supported ``op`` values are ``replace``, ``add``, and
        ``remove``.  See module docstring for the path syntax.
    expect_clean:
        When True (the default), the write only happens if the post-
        edit candidate produces zero ``error`` findings.  When False
        the result is written regardless and findings are returned
        for advisory display — used only by future AI-suggest flows
        that intentionally introduce a transient problem.
    dry_run:
        When True, the candidate is parsed, applied, and validated but
        the on-disk file is **never** written, even when validation
        passes. Used by :mod:`tools.ai.pipeline` to honour
        ``projectXml.ai.autoApplyValidated=false`` (HLR-032): the same
        validate path runs so the diff-preview surface gets a real
        lint result, then the user explicitly accepts before any write.
    """
    if etree is None:
        raise ProjectXmlError(
            f"apply_edit requires lxml: {_LXML_IMPORT_ERROR}"
        )
    xml_path = Path(xml_path)
    xsd_path = Path(xsd_path)
    if not xml_path.exists():
        raise ProjectXmlError(f"file not found: {xml_path}")
    if not xsd_path.exists():
        raise ProjectXmlError(f"file not found: {xsd_path}")
    if not isinstance(operations, list):
        raise ValueError("operations must be a list")

    original_bytes = xml_path.read_bytes()
    parser = etree.XMLParser(
        remove_blank_text=False,
        remove_comments=False,
        strip_cdata=False,
        resolve_entities=False,
    )
    try:
        tree = etree.fromstring(original_bytes, parser).getroottree()
    except etree.XMLSyntaxError as exc:  # pragma: no cover (would fail lint)
        raise ProjectXmlError(f"{xml_path}: malformed XML: {exc}") from exc

    applied = 0
    for op in operations:
        if not isinstance(op, dict):
            raise ValueError(f"operations[{applied}] must be an object")
        _apply_operation(tree.getroot(), op)
        applied += 1

    candidate = etree.tostring(
        tree,
        xml_declaration=True,
        encoding="UTF-8",
    )

    # Beautify: normalize indentation so AI-inserted elements match the
    # hand-authored style (2-space indent, one element per line).
    candidate = _beautify(candidate)

    # Validate the candidate without touching the original file.
    fd, tmp_name = tempfile.mkstemp(
        prefix=".project_io_apply_edit.",
        suffix=".xml",
        dir=str(xml_path.parent),
    )
    tmp_path = Path(tmp_name)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(candidate)
        findings = _lint(tmp_path, xsd_path)
        findings_dict = findings.to_dict()
        # Lint paths quote the file name verbatim; rewrite tmp_path back
        # to xml_path so error messages reference the user-visible file.
        findings_dict = _rewrite_path_in_findings(
            findings_dict, tmp_path, xml_path,
        )
        ok = not findings.errors
        if expect_clean and not ok:
            # Sanity check: original on-disk bytes must be untouched.
            assert xml_path.read_bytes() == original_bytes
            return ApplyEditResult(
                ok=False,
                written=False,
                findings=findings_dict,
                operations_applied=applied,
            )
        if dry_run:
            # Validation passed (or expect_clean=False) but the caller
            # asked us not to write — the diff-preview surface owns
            # the eventual write. Sanity check: file untouched.
            assert xml_path.read_bytes() == original_bytes
            return ApplyEditResult(
                ok=ok,
                written=False,
                findings=findings_dict,
                operations_applied=applied,
            )
        # Atomic replace.
        os.replace(tmp_path, xml_path)
        tmp_path = None  # don't unlink in finally
    finally:
        if tmp_path is not None and tmp_path.exists():
            try:
                tmp_path.unlink()
            except OSError:  # pragma: no cover
                pass

    return ApplyEditResult(
        ok=ok,
        written=True,
        findings=findings_dict,
        operations_applied=applied,
    )


def _rewrite_path_in_findings(
    findings: dict[str, Any],
    tmp_path: Path,
    real_path: Path,
) -> dict[str, Any]:
    needle = str(tmp_path)
    repl = str(real_path)
    out = {
        "errors": [m.replace(needle, repl) for m in findings.get("errors", [])],
        "warnings": [m.replace(needle, repl) for m in findings.get("warnings", [])],
        "notes": [m.replace(needle, repl) for m in findings.get("notes", [])],
        "items": [
            {
                **item,
                "message": item.get("message", "").replace(needle, repl),
            }
            for item in findings.get("items", [])
        ],
    }
    out["ok"] = not out["errors"]
    return out


# --------------------------------------------------------------------- #
# Patch operation execution                                             #
# --------------------------------------------------------------------- #

# Path step grammar: ``tag`` | ``tag[k=v,k2=v2]`` | ``tag[N]`` |
# ``tag/-`` (append slot) | ``@attr`` | ``-`` (only as final step).
_STEP_RE = re.compile(
    r"""^
    (?P<tag>(?:@[A-Za-z_][\w-]*) | (?:[A-Za-z_][\w-]*) | -)
    (?:\[(?P<predicate>[^\]]*)\])?
    $""",
    re.VERBOSE,
)


def _split_path(path: str) -> list[str]:
    if not path or not path.startswith("/"):
        raise ValueError(f"path must start with '/': {path!r}")
    # Split on '/' but not inside bracket predicates (e.g.
    # file[path=test/foo.py] must remain a single step).
    steps: list[str] = []
    depth = 0
    current: list[str] = []
    for ch in path[1:]:  # skip leading '/'
        if ch == "[":
            depth += 1
            current.append(ch)
        elif ch == "]":
            depth -= 1
            current.append(ch)
        elif ch == "/" and depth == 0:
            segment = "".join(current)
            if segment:
                steps.append(segment)
            current = []
        else:
            current.append(ch)
    segment = "".join(current)
    if segment:
        steps.append(segment)
    return steps


def _parse_predicate(pred: str) -> dict[str, str] | int | None:
    pred = pred.strip()
    if not pred:
        return None
    if pred.isdigit():
        return int(pred)
    out: dict[str, str] = {}
    for term in pred.split(","):
        term = term.strip()
        if not term:
            continue
        if "=" not in term:
            raise ValueError(f"bad predicate term: {term!r}")
        k, v = term.split("=", 1)
        out[k.strip()] = v.strip()
    return out


def _resolve_to_element(
    root: "etree._Element",
    steps: list[str],
) -> "etree._Element | None":
    """Walk ``steps`` and return the deepest matching element (or None).

    Unlike :func:`_resolve`, every step is consumed — used for paths
    whose final addressing happens by the caller (e.g. the ``tag/-``
    append form, which needs the element that owns the append slot).
    """
    current = root
    for step in steps:
        match = _STEP_RE.match(step)
        if not match:
            raise ValueError(f"bad path step: {step!r}")
        tag = match.group("tag")
        if tag.startswith("@") or tag == "-":
            raise ValueError(f"unexpected step in element path: {step!r}")
        pred = _parse_predicate(match.group("predicate") or "")
        nxt = _select_child(current, tag, pred)
        if nxt is None:
            return None
        current = nxt
    return current


def _resolve(
    root: "etree._Element",
    steps: list[str],
) -> tuple["etree._Element | None", str]:
    """Walk ``steps`` from ``root`` and return (final_element, final_step).

    The returned ``final_element`` is the *parent* of the last
    addressed thing; the second value is the unparsed final step,
    which the caller interprets as ``@attr``, ``tag[...]`` (existing
    child), ``tag/-`` (append slot), or ``-`` (sole append slot).
    """
    if not steps:
        return root, ""
    parent = root
    for i, step in enumerate(steps[:-1]):
        match = _STEP_RE.match(step)
        if not match:
            raise ValueError(f"bad path step: {step!r}")
        tag = match.group("tag")
        if tag.startswith("@"):
            raise ValueError("attribute step only allowed as the last segment")
        pred = _parse_predicate(match.group("predicate") or "")
        nxt = _select_child(parent, tag, pred)
        if nxt is None:
            raise ValueError(
                f"path segment not found: /{'/'.join(steps[:i + 1])}"
            )
        parent = nxt
    return parent, steps[-1]


def _select_child(
    parent: "etree._Element",
    tag: str,
    predicate: dict[str, str] | int | None,
) -> "etree._Element | None":
    children = [c for c in parent if isinstance(c.tag, str) and c.tag == tag]
    if predicate is None:
        if len(children) == 1:
            return children[0]
        if not children:
            return None
        raise ValueError(
            f"ambiguous selector '{tag}' under <{parent.tag}>: "
            f"{len(children)} children — supply a predicate"
        )
    if isinstance(predicate, int):
        if 0 <= predicate < len(children):
            return children[predicate]
        return None
    for c in children:
        if all(c.get(k) == v for k, v in predicate.items()):
            return c
    return None


def _cascade_id_rename(
    root: "etree._Element",
    parent_tag: str,
    old_id: str,
    new_id: str,
) -> None:
    """Update all <trace> refs that point to the renamed element."""
    # Determine the target value used in <trace target="..." ref="...">
    target_map = {"hlr": "HLR", "llr": "LLR"}
    trace_target = target_map.get(parent_tag)
    if not trace_target:
        return
    for trace_el in root.iter("trace"):
        if trace_el.get("target") == trace_target and trace_el.get("ref") == old_id:
            trace_el.set("ref", new_id)


def _apply_operation(root: "etree._Element", op: dict[str, Any]) -> None:
    op_kind = op.get("op")
    path = op.get("path")
    value = op.get("value")
    if op_kind not in {"replace", "add", "remove"}:
        raise ValueError(f"unsupported op {op_kind!r}; "
                         f"use 'replace', 'add', or 'remove'")
    if not isinstance(path, str):
        raise ValueError("op.path must be a string")
    steps = _split_path(path)
    if not steps:
        raise ValueError("path must address something below the root")

    # `tag/-` is the append form. Detect it up front so the rest of
    # the function reasons about a clean (parent, last_step) pair.
    if steps[-1] == "-":
        if len(steps) < 2:
            raise ValueError("append path must be of the form '.../tag/-'")
        tag_step = steps[-2]
        match = _STEP_RE.match(tag_step)
        if not match:
            raise ValueError(f"bad path step: {tag_step!r}")
        tag = match.group("tag")
        if tag.startswith("@") or tag == "-":
            raise ValueError(f"append target must be an element tag, got {tag_step!r}")
        if op_kind != "add":
            raise ValueError("only 'add' may target an append slot")
        if not isinstance(value, dict):
            raise ValueError("add requires an object value describing the new element")
        # Walk down to the element that owns the append slot. The
        # tag step (`steps[-2]`) is the *new* element's name; its
        # owner is the element addressed by `steps[:-2]`.
        owner = _resolve_to_element(root, steps[:-2])
        if owner is None:
            raise ValueError(f"append parent not found: {path}")
        new_elem = _build_element(
            tag, value,
            owner.nsmap if hasattr(owner, "nsmap") else None,
        )
        owner.append(new_elem)
        return

    parent, last = _resolve(root, steps)
    match = _STEP_RE.match(last)
    if not match:
        raise ValueError(f"bad final path step: {last!r}")
    tag = match.group("tag")
    pred = _parse_predicate(match.group("predicate") or "")

    if tag.startswith("@"):
        attr = tag[1:]
        if op_kind == "remove":
            if attr in parent.attrib:
                del parent.attrib[attr]
            return
        if value is None:
            raise ValueError(f"replace/add @{attr} requires a value")
        old_value = parent.get(attr)
        parent.set(attr, str(value))
        # Cascade: when renaming an id on an hlr/llr element, update
        # all <trace> elements that reference the old id.
        if attr == "id" and old_value and old_value != str(value):
            _cascade_id_rename(root, parent.tag, old_value, str(value))
        return

    if tag == "-":
        # ``/path/-`` shorthand isn't currently used; require the
        # ``tag/-`` form so the appended element type is explicit.
        raise ValueError("bare '-' append is not supported; use 'tag/-'")

    if pred is not None and op_kind == "add":
        raise ValueError(
            "add cannot target an existing predicate — use 'tag/-' to append"
        )

    if op_kind == "add":
        if not isinstance(value, dict):
            raise ValueError("add requires an object value describing the new element")
        new_elem = _build_element(tag, value, parent.nsmap if hasattr(parent, "nsmap") else None)
        parent.append(new_elem)
        return

    # replace / remove on an element (or its text body).
    target = parent if pred is None and tag == "" else _select_child(parent, tag, pred)
    if target is None:
        raise ValueError(f"target not found: {path}")

    if op_kind == "remove":
        parent.remove(target)
        return

    # replace
    if isinstance(value, dict):
        # Replace the element's children + attributes (except the id-
        # like attributes used in the predicate, which stay as the
        # element's identity).
        kept_attrs = {} if pred is None else dict(pred)
        target.attrib.clear()
        for k, v in kept_attrs.items():
            target.set(k, v)
        # Drop existing children, then rebuild from the value dict.
        for child in list(target):
            target.remove(child)
        target.text = None
        _populate_element(target, value)
    else:
        # Treat as text content. CDATA-wrap when the body contains
        # markup-significant characters so HLR-018 round-trip stays
        # intact when round-trips happen later.
        text = "" if value is None else str(value)
        for child in list(target):
            target.remove(child)
        if text and _CDATA_TRIGGERS.search(text):
            target.text = etree.CDATA(text)
        else:
            target.text = text or None


def _build_element(
    tag: str,
    spec: dict[str, Any],
    nsmap: Any | None,
) -> "etree._Element":
    elem = etree.SubElement(etree.Element("__discard__", nsmap=nsmap), tag)
    elem.getparent().remove(elem)
    _populate_element(elem, spec)
    return elem


def _populate_element(elem: "etree._Element", spec: dict[str, Any]) -> None:
    """Apply ``spec`` to ``elem`` using the convention:

      * ``"@name"`` keys become attributes.
      * String/numeric values become element text (CDATA-wrapped when
        the value contains markup characters).
      * Dict values become a single child element.
      * List values become repeated child elements (one per list item).
    """
    for key, value in spec.items():
        if key.startswith("@"):
            elem.set(key[1:], str(value))
            continue
        if isinstance(value, list):
            for item in value:
                child = etree.SubElement(elem, key)
                if isinstance(item, dict):
                    _populate_element(child, item)
                else:
                    text = str(item)
                    if text and _CDATA_TRIGGERS.search(text):
                        child.text = etree.CDATA(text)
                    else:
                        child.text = text
            continue
        if isinstance(value, dict):
            child = etree.SubElement(elem, key)
            _populate_element(child, value)
            continue
        # Scalar: child element with text content.
        child = etree.SubElement(elem, key)
        text = "" if value is None else str(value)
        if text and _CDATA_TRIGGERS.search(text):
            child.text = etree.CDATA(text)
        elif text:
            child.text = text


# --------------------------------------------------------------------- #
# Next-free id allocation                                               #
# --------------------------------------------------------------------- #

def next_free_hlr_id(xml_path: Path | str = PROJECT_XML) -> str:
    """Return the next free ``HLR-NNN`` id (zero-padded to the widest
    existing id; default width 3).
    """
    import defusedxml.ElementTree as ET

    root = ET.parse(xml_path).getroot()
    ids = [
        h.get("id", "")
        for h in root.findall("hlrs/section/hlr")
    ]
    return _next_id_for_pattern(ids, prefix="HLR-", default_width=3)


def next_free_llr_id(
    function_prefix: str,
    xml_path: Path | str = PROJECT_XML,
) -> str:
    """Return the next free ``LLR-<PREFIX>-NN`` id.

    Discovers the established prefix by inspecting existing LLRs under
    the named ``<function>``. Falls back to a sanitized derivation of
    the function name only when no LLRs exist yet in that function.
    """
    import re
    import defusedxml.ElementTree as ET

    root = ET.parse(xml_path).getroot()

    # Find the <function name="..."> element matching function_prefix.
    func_el = None
    for f in root.findall("llrs/function"):
        if f.get("name") == function_prefix:
            func_el = f
            break

    # Collect IDs from that specific function (for prefix discovery).
    func_ids = [
        l.get("id", "")
        for l in (func_el.findall("llr") if func_el is not None else [])
    ]

    # Discover the established prefix from existing LLRs in this function.
    # Pattern: LLR-<PREFIX>-<NN>
    established_prefix: str | None = None
    llr_id_re = re.compile(r"^LLR-([A-Z0-9]+)-\d+$")
    for fid in func_ids:
        m = llr_id_re.match(fid)
        if m:
            established_prefix = m.group(1)
            break

    if established_prefix:
        upper = established_prefix
    else:
        # No existing LLRs — derive a short prefix from the function name.
        upper = re.sub(r"[^A-Z0-9]", "", function_prefix.upper())
        if not upper:
            upper = "GEN"
        # Limit to a reasonable length (3-4 chars typical).
        if len(upper) > 4:
            upper = upper[:4]

    # Scan ALL LLR ids (not just this function) to avoid collisions.
    all_ids = [
        l.get("id", "")
        for l in root.findall("llrs/function/llr")
    ]
    return _next_id_for_pattern(
        all_ids, prefix=f"LLR-{upper}-", default_width=2,
    )


def _next_id_for_pattern(
    existing: Iterable[str], *, prefix: str, default_width: int,
) -> str:
    max_n = 0
    width = default_width
    for value in existing:
        if not value.startswith(prefix):
            continue
        suffix = value[len(prefix):]
        if not suffix.isdigit():
            continue
        n = int(suffix)
        if n > max_n:
            max_n = n
        if len(suffix) > width:
            width = len(suffix)
    return f"{prefix}{max_n + 1:0{width}d}"
