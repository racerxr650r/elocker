#!/usr/bin/env python3
"""
Render a Markdown document from a Jinja2 template plus the data in
doc/Project.xml.

Usage:
    python3 tools/render_doc.py tools/templates/SDD.md.j2 SDD > out/SDD.md

The second argument is the document id from <metadata>/<document id="...">
(SDD, HLRs, LLRs, or STP); it picks which document's metadata gets
exposed to the template as `project.metadata`. The template controls
everything else.
"""
from __future__ import annotations

import argparse
import os
import sys
import defusedxml.ElementTree as ET
from pathlib import Path
from types import SimpleNamespace
from typing import Any

import jinja2

PROJECT_XML = Path(__file__).resolve().parent.parent / "doc" / "Project.xml"
PROJECT_XSD = Path(__file__).resolve().parent / "project.xsd"


class ProjectXmlError(Exception):
    """Raised by the library API when Project.xml cannot be loaded
    or rendered. The CLI catches this and converts it into a non-zero
    exit with a stderr message; library callers (project_io.py, the
    test suite) can catch it directly without involving SystemExit.
    """


def _attrs(elem: ET.Element) -> dict[str, str]:
    return dict(elem.attrib)


# ---------------------------------------------------------------------------
# UI hint vocabulary (urn:tracer:ui:v1)
#
# Phase 2.5b lets payload-bearing elements (HLRs, LLRs, tests, SDD modules)
# carry optional `ui:icon`, `ui:color`, `ui:group` attributes. The XSD
# declares them via `<xs:anyAttribute namespace="urn:tracer:ui:v1"
# processContents="skip"/>` so unknown ui:* attributes are ignored rather
# than rejected. ElementTree exposes namespaced attribute names as
# Clark-notation ("{ns}local") keys; we project the recognised subset
# onto a flat dict and surface it as `.ui` on the SimpleNamespace nodes
# so both the Jinja templates (which currently ignore it) and the
# JSON-RPC `parse_to_json` consumers (the VS Code tree) see the same
# shape. Returns None when no recognised ui:* attribute is present, so
# absent hints serialise as null rather than empty objects.
# ---------------------------------------------------------------------------

UI_NAMESPACE = "urn:tracer:ui:v1"
_UI_HINT_KEYS = ("icon", "color", "group")


def _ui_hints(elem: ET.Element) -> dict[str, str] | None:
    hints: dict[str, str] = {}
    for key in _UI_HINT_KEYS:
        value = elem.get(f"{{{UI_NAMESPACE}}}{key}")
        if value is not None and value != "":
            hints[key] = value
    return hints or None


# ---------------------------------------------------------------------------
# UI hint vocabulary distilled from <xs:appinfo> in tools/project.xsd
# (Phase 2.5b).
#
# Every renderable complex type in the XSD carries an
# <xs:annotation><xs:appinfo> block declaring how the schema element
# should be projected to a UI surface — `ui:treeNode`, `ui:form`,
# `ui:lens`, `ui:document`. parse_ui_hints_index walks the XSD once
# and returns a single dict keyed by complex-type name. Consumers
# (the VS Code tree provider, lens provider, locator, and Phase 3
# form panels) read this index instead of re-parsing the XSD or
# special-casing per-payload element names.
#
# Shape:
#
#   {
#     "Hlr": {
#       "tree_node": {"label": "@id — @name",
#                     "id_attr": "id",
#                     "group":   "hlrs"},
#       "form":      [{"target": "id",
#                      "kind":   "attr",
#                      "field":  "text",
#                      "required": True}, ...],
#       "lenses":    [{"kind": "coverage"}, {"kind": "tracesCount"}],
#       "document":  False,
#     },
#     ...
#   }
#
# Types without an <xs:appinfo> block are absent from the index;
# missing sub-fields default to None (tree_node) or [] (form, lenses).
# The index is JSON-serialisable.
# ---------------------------------------------------------------------------

_XS_NAMESPACE = "http://www.w3.org/2001/XMLSchema"


def _local(tag: str) -> str:
    return tag.split("}", 1)[1] if tag.startswith("{") else tag


def _ui_local(tag: str) -> str | None:
    if not tag.startswith("{"):
        return None
    ns, local = tag[1:].split("}", 1)
    return local if ns == UI_NAMESPACE else None


def _parse_ui_field(elem: ET.Element) -> dict[str, Any]:
    attrs = elem.attrib
    if "attr" in attrs:
        target, kind = attrs["attr"], "attr"
    elif "child" in attrs:
        target, kind = attrs["child"], "child"
    else:
        target, kind = "", "attr"
    return {
        "target":   target,
        "kind":     kind,
        "field":    attrs.get("kind", "text"),
        "required": attrs.get("required", "").lower() == "true",
    }


def _parse_appinfo(appinfo: ET.Element) -> dict[str, Any]:
    tree_node: dict[str, str] | None = None
    form: list[dict[str, Any]] = []
    lenses: list[dict[str, str]] = []
    document = False
    for child in appinfo:
        local = _ui_local(child.tag)
        if local is None:
            continue
        if local == "treeNode":
            tree_node = {
                "label":   child.get("label", ""),
                "id_attr": child.get("idAttr", ""),
                "group":   child.get("group", ""),
            }
        elif local == "form":
            for field_elem in child:
                if _ui_local(field_elem.tag) == "field":
                    form.append(_parse_ui_field(field_elem))
        elif local == "lens":
            lenses.append({"kind": child.get("kind", "")})
        elif local == "document":
            document = True
    return {
        "tree_node": tree_node,
        "form":      form,
        "lenses":    lenses,
        "document":  document,
        # `element` (the lowercase XML tag bound to this complex type)
        # is filled in by parse_ui_hints_index after a second pass
        # over the XSD's <xs:element type="..."> declarations.
        "element":   None,
    }


def parse_ui_hints_index(
    xsd_path: Path | str = PROJECT_XSD,
) -> dict[str, dict[str, Any]]:
    """Distil the per-complex-type UI hint vocabulary from
    ``tools/project.xsd``.

    Walks every ``xs:complexType`` in the schema, reads any
    ``xs:annotation/xs:appinfo`` block under it, and projects the
    ``ui:treeNode`` / ``ui:form`` / ``ui:lens`` / ``ui:document``
    children into a JSON-serialisable index keyed by the type's
    ``@name``. Anonymous inline complex types (those declared inside
    an ``xs:element``) are walked too and keyed under the parent
    element's local name (e.g. ``Plan/item``).

    Pinned by Developers_Guide.md §17 and consumed by
    [tools/project_io.py](project_io.py)'s ``ui_hints_index`` and
    ``parse_to_json`` JSON-RPC methods (Phase 2.5b).
    """
    xsd_path = Path(xsd_path)
    try:
        root = ET.parse(xsd_path).getroot()
    except ET.ParseError as exc:
        raise ProjectXmlError(
            f"{xsd_path}: malformed XSD: {exc}"
        ) from exc
    except FileNotFoundError as exc:
        raise ProjectXmlError(f"{xsd_path}: file not found") from exc

    appinfo_tag    = f"{{{_XS_NAMESPACE}}}appinfo"
    annotation_tag = f"{{{_XS_NAMESPACE}}}annotation"
    complextype_tag = f"{{{_XS_NAMESPACE}}}complexType"
    element_tag    = f"{{{_XS_NAMESPACE}}}element"

    index: dict[str, dict[str, Any]] = {}

    def _walk_complextype(node: ET.Element, key: str) -> None:
        annotation = node.find(annotation_tag)
        if annotation is None:
            return
        appinfo = annotation.find(appinfo_tag)
        if appinfo is None:
            return
        index[key] = _parse_appinfo(appinfo)

    # Top-level named complex types.
    for ct in root.findall(complextype_tag):
        name = ct.get("name")
        if not name:
            continue
        _walk_complextype(ct, name)
        # Inline nested complex types under named elements (e.g. Plan/item).
        for elem in ct.iter(element_tag):
            inline = elem.find(complextype_tag)
            if inline is None:
                continue
            local = elem.get("name")
            if not local:
                continue
            _walk_complextype(inline, f"{name}/{local}")
            # Inline complex types are bound to their own element name.
            if f"{name}/{local}" in index:
                index[f"{name}/{local}"]["element"] = local

    # Second pass: bind top-level named types to the lowercase XML
    # element name(s) they appear under (xs:element name="X" type="Y").
    # If a type is bound to multiple element names we keep the first;
    # all currently-annotated types are 1:1.
    for elem in root.iter(element_tag):
        type_attr = elem.get("type")
        local = elem.get("name")
        if not type_attr or not local:
            continue
        if type_attr in index and index[type_attr]["element"] is None:
            index[type_attr]["element"] = local

    # Third pass: project the per-type AI action list. The registry
    # in :mod:`tools.ai.registry` is the single source of truth for
    # which intents apply to which complex type (HLR-053).  Imported
    # lazily so render_doc.py keeps working when the optional ai
    # package is absent in some downstream consumer.
    try:
        from ai.registry import ai_actions_for_type as _ai_actions_for_type
    except Exception:  # pragma: no cover - defensive
        _ai_actions_for_type = lambda _name: []  # noqa: E731
    for type_name, entry in index.items():
        entry["ai_actions"] = _ai_actions_for_type(type_name)

    return index


def collect_nodes_by_type(
    xml_path: Path | str = PROJECT_XML,
    hints_index: dict[str, dict[str, Any]] | None = None,
) -> dict[str, list[dict[str, Any]]]:
    """Walk Project.xml and collect every element bound to a complex
    type that carries a ``ui:treeNode`` hint, keyed by the same
    complex-type name used by :func:`parse_ui_hints_index`.

    Each emitted node is a JSON-serialisable dict::

        {
            "tag":   "hlr",                     # actual XML local name
            "attrs": {"id": "HLR-001", ...},     # non-namespaced attrs
            "ui":    {"icon": "star", ...} | None,  # urn:tracer:ui:v1 attrs
            "text":  "..." | None,                # stripped element.text
        }

    Inline nested types (e.g. ``Plan/item``) are scoped to children of
    their parent element so unrelated ``<item>`` elements elsewhere in
    the tree are not pulled in.

    Types whose ``tree_node`` hint is None are skipped; the index is
    only useful to consumers that want to render those nodes (the
    VS Code Project Spec tree provider, Phase 2.5b Slice E+).
    """
    xml_path = Path(xml_path)
    if hints_index is None:
        hints_index = parse_ui_hints_index()
    try:
        root = ET.parse(xml_path).getroot()
    except ET.ParseError as exc:
        raise ProjectXmlError(
            f"{xml_path}: malformed XML: {exc}"
        ) from exc
    except FileNotFoundError as exc:
        raise ProjectXmlError(f"{xml_path}: file not found") from exc

    def _node_payload(elem: ET.Element) -> dict[str, Any]:
        attrs: dict[str, str] = {}
        ui: dict[str, str] = {}
        for k, v in elem.attrib.items():
            if k.startswith("{"):
                ns, local = k[1:].split("}", 1)
                if ns == UI_NAMESPACE:
                    ui[local] = v
                # Other foreign-namespace attrs are dropped.
            else:
                attrs[k] = v
        text = (elem.text or "").strip() or None
        return {
            "tag":   elem.tag,
            "attrs": attrs,
            "ui":    ui or None,
            "text":  text,
        }

    nodes: dict[str, list[dict[str, Any]]] = {}

    for key, entry in hints_index.items():
        if entry.get("tree_node") is None:
            continue
        element = entry.get("element")
        if not element:
            continue
        if "/" in key:
            # Inline type: scope to the parent type's element.
            parent_key = key.split("/", 1)[0]
            parent_element = (hints_index.get(parent_key) or {}).get("element")
            if not parent_element:
                continue
            collected: list[dict[str, Any]] = []
            for parent in root.iter(parent_element):
                for child in parent.iter(element):
                    if child is parent:
                        continue
                    collected.append(_node_payload(child))
            nodes[key] = collected
        else:
            nodes[key] = [_node_payload(e) for e in root.iter(element)]

    return nodes



def _text(elem: ET.Element | None) -> str:
    if elem is None or elem.text is None:
        return ""
    return elem.text


def _gh_slug(text: str) -> str:
    """Approximate GitHub's heading-anchor slug rule.

    GitHub lowercases the heading, strips characters other than letters,
    digits, spaces and hyphens, then replaces spaces with hyphens. Used
    by the Traceability template to link to SDD section headings.
    """
    import re
    s = text.strip().lower()
    s = re.sub(r"[^\w\s-]", "", s, flags=re.UNICODE)
    s = re.sub(r"\s+", "-", s)
    return s


# --------------------------------------------------------------------- #
# Convert each branch of the XML tree into SimpleNamespace objects so   #
# templates can use attribute access (section.title) instead of XML     #
# method calls. Templates stay readable for non-Python authors.          #
# --------------------------------------------------------------------- #
def _children_text(elem: ET.Element, tag: str) -> list[str]:
    """Collect the .text of every direct child with the given tag."""
    return [_text(c) for c in elem.findall(tag)]


def build_function(elem: ET.Element) -> SimpleNamespace:
    """A <function signature [summary]> with optional verbose children."""
    logic_elem = elem.find("logic")
    return SimpleNamespace(
        signature=elem.get("signature", ""),
        summary=elem.get("summary", "") or _text(elem.find("summary")),
        purpose=_text(elem.find("purpose")),
        pre=_text(elem.find("pre")),
        post=_text(elem.find("post")),
        returns=_text(elem.find("returns")),
        logic=_children_text(logic_elem, "step") if logic_elem is not None else [],
        notes=_text(elem.find("notes")),
    )


def build_functions(elem: ET.Element | None) -> SimpleNamespace | None:
    if elem is None:
        return None
    flat = [build_function(f) for f in elem.findall("function")]
    groups = [
        SimpleNamespace(
            name=g.get("name", ""),
            functions=[build_function(f) for f in g.findall("function")],
        )
        for g in elem.findall("group")
    ]
    return SimpleNamespace(
        intro=_text(elem.find("intro")),
        flat=flat,
        groups=groups,
    )


def build_interfaces(elem: ET.Element | None) -> SimpleNamespace | None:
    if elem is None:
        return None
    return SimpleNamespace(
        title_suffix=elem.get("title_suffix", ""),
        intro=_text(elem.find("intro")),
        prose=_text(elem.find("prose")),
        items=[
            SimpleNamespace(title=i.get("title", ""), body=_text(i))
            for i in elem.findall("interface")
        ],
    )


def build_named_body_list(parent: ET.Element | None, child_tag: str) -> list[SimpleNamespace]:
    if parent is None:
        return []
    return [
        SimpleNamespace(name=c.get("name", ""), body=_text(c))
        for c in parent.findall(child_tag)
    ]


def build_module(elem: ET.Element) -> SimpleNamespace:
    return SimpleNamespace(
        path=elem.get("path", ""),
        title=elem.get("title", ""),
        purpose=_text(elem.find("purpose")),
        responsibilities=_children_text(elem, "responsibility"),
        interfaces=build_interfaces(elem.find("interfaces")),
        data_structures=_text(elem.find("data_structures")),
        functions=build_functions(elem.find("functions")),
        algorithm=_text(elem.find("algorithm")),
        dependencies=[
            _text(d) for d in (elem.find("dependencies") or [])
        ] if elem.find("dependencies") is not None else [],
        error_handling=build_named_body_list(elem.find("error_handling"), "case"),
        ui=_ui_hints(elem),
    )


def build_data_dictionary(elem: ET.Element | None) -> SimpleNamespace | None:
    if elem is None:
        return None
    types = []
    for t in elem.findall("type"):
        types.append(SimpleNamespace(
            name=t.get("name", ""),
            header=t.get("header", ""),
            instance=t.get("instance", ""),
            instance_in=t.get("instance_in", ""),
            summary=t.get("summary", ""),
            fields=[
                SimpleNamespace(
                    name=f.get("name", ""),
                    type=f.get("type", ""),
                    desc=f.get("desc", ""),
                )
                for f in t.findall("field")
            ],
        ))
    consts_elem = elem.find("constants")
    constants = None
    if consts_elem is not None:
        constants = SimpleNamespace(
            header=consts_elem.get("header", ""),
            items=[
                SimpleNamespace(
                    name=c.get("name", ""),
                    value=c.get("value", ""),
                    purpose=c.get("purpose", ""),
                )
                for c in consts_elem.findall("constant")
            ],
        )
    return SimpleNamespace(
        types=types,
        constants=constants,
        other=_text(elem.find("other")),
    )


def build_sdd(elem: ET.Element | None) -> SimpleNamespace | None:
    if elem is None:
        return None
    scope_elem = elem.find("scope")
    scope = None
    if scope_elem is not None:
        scope = SimpleNamespace(
            intro=_text(scope_elem.find("intro")),
            files=[
                SimpleNamespace(path=f.get("path", ""), body=_text(f))
                for f in scope_elem.findall("file")
            ],
            outro=_text(scope_elem.find("outro")),
        )
    overview = [_text(p) for p in elem.findall("overview/para")]
    definitions = [
        SimpleNamespace(name=t.get("name", ""), body=_text(t))
        for t in elem.findall("definitions/term")
    ]
    references = [_text(r) for r in elem.findall("references/ref")]
    arch_elem = elem.find("architecture")
    architecture = None
    if arch_elem is not None:
        flow_elem = arch_elem.find("flow")
        flow = None
        if flow_elem is not None:
            flow = SimpleNamespace(
                intro=_text(flow_elem.find("intro")),
                steps=_children_text(flow_elem, "step"),
            )
        architecture = SimpleNamespace(
            intro=_text(arch_elem.find("intro")),
            components=[
                SimpleNamespace(path=c.get("path", ""), body=_text(c))
                for c in arch_elem.findall("component")
            ],
            flow=flow,
        )
    design_goals = [
        SimpleNamespace(name=g.get("name", ""), body=_text(g))
        for g in elem.findall("design_goals/goal")
    ]
    modules = [build_module(m) for m in elem.findall("modules/module")]
    return SimpleNamespace(
        kind=_text(elem.find("kind")),
        audience=_text(elem.find("audience")),
        scope=scope,
        overview=overview,
        definitions=definitions,
        references=references,
        architecture=architecture,
        design_goals=design_goals,
        modules=modules,
        data_dictionary=build_data_dictionary(elem.find("data_dictionary")),
        traceability=[
            SimpleNamespace(name=t.get("name", ""), sections=t.get("sections", ""))
            for t in elem.findall("traceability/theme")
        ],
    )


def build_trace(elem: ET.Element) -> SimpleNamespace:
    return SimpleNamespace(**_attrs(elem))


def build_traces(elem: ET.Element | None) -> list[SimpleNamespace]:
    if elem is None:
        return []
    return [build_trace(t) for t in elem.findall("trace")]


def build_hlr(elem: ET.Element) -> SimpleNamespace:
    return SimpleNamespace(
        id=elem.get("id", ""),
        name=elem.get("name", ""),
        text=_text(elem.find("text")),
        traces=build_traces(elem.find("traces")),
        ui=_ui_hints(elem),
    )


def build_hlr_section(elem: ET.Element) -> SimpleNamespace:
    return SimpleNamespace(
        number=elem.get("number", ""),
        title=elem.get("title", ""),
        intro=_text(elem.find("intro")),
        hlrs=[build_hlr(h) for h in elem.findall("hlr")],
    )


def build_llr(elem: ET.Element) -> SimpleNamespace:
    return SimpleNamespace(
        id=elem.get("id", ""),
        text=_text(elem.find("text")),
        traces=build_traces(elem.find("traces")),
        ui=_ui_hints(elem),
    )


def build_llr_group(elem: ET.Element) -> SimpleNamespace:
    """An <llrs>/<function> element groups LLRs by the function they cover."""
    return SimpleNamespace(
        number=elem.get("number", ""),
        title=elem.get("title", ""),
        name=elem.get("name", ""),
        source=elem.get("source", ""),
        intro=_text(elem.find("intro")),
        llrs=[build_llr(l) for l in elem.findall("llr")],
    )


def build_test(elem: ET.Element) -> SimpleNamespace:
    return SimpleNamespace(
        name=elem.get("name", ""),
        purpose=_text(elem.find("purpose")),
        traces=build_traces(elem.find("traces")),
        ui=_ui_hints(elem),
    )


def build_test_file(elem: ET.Element) -> SimpleNamespace:
    return SimpleNamespace(
        path=elem.get("path", ""),
        role=elem.get("role", ""),
        count=int(elem.get("count", "0")),
        header=_text(elem.find("header")),
        tests=[build_test(t) for t in elem.findall("test")],
    )


def build_stp(elem: ET.Element | None) -> SimpleNamespace | None:
    if elem is None:
        return None

    intro_elem = elem.find("introduction")
    introduction = None
    if intro_elem is not None:
        related_elem = intro_elem.find("related")
        introduction = SimpleNamespace(
            purpose=_text(intro_elem.find("purpose")),
            scope=_text(intro_elem.find("scope")),
            related=_children_text(related_elem, "doc") if related_elem is not None else [],
        )

    strat_elem = elem.find("strategy")
    strategy = None
    if strat_elem is not None:
        levels_elem = strat_elem.find("levels")
        levels = []
        if levels_elem is not None:
            levels = [
                SimpleNamespace(
                    name=l.get("name", ""),
                    source=l.get("source", ""),
                    driver=l.get("driver", ""),
                    style=l.get("style", ""),
                )
                for l in levels_elem.findall("level")
            ]
        be_elem = strat_elem.find("build_execution")
        build_execution = None
        if be_elem is not None:
            build_execution = SimpleNamespace(
                intro=_text(be_elem.find("intro")),
                steps=_children_text(be_elem, "step"),
                outro=_text(be_elem.find("outro")),
            )
        pf_elem = strat_elem.find("pass_fail")
        pass_fail = _children_text(pf_elem, "criterion") if pf_elem is not None else []
        strategy = SimpleNamespace(
            levels=levels,
            framework=_text(strat_elem.find("framework")),
            build_execution=build_execution,
            pass_fail=pass_fail,
            traceability_convention=_text(strat_elem.find("traceability_convention")),
        )

    ie_elem = elem.find("integration_environment")
    integration_environment = None
    if ie_elem is not None:
        fixtures = []
        # Collect the union of artefact keys across fixtures (in
        # first-seen order) so the STP template can emit a stable
        # column header per artefact column.
        artefact_keys: list[str] = []
        for f in ie_elem.findall("fixture"):
            artefacts = {}
            for a in f.findall("artefact"):
                key = a.get("key", "")
                if key and key not in artefact_keys:
                    artefact_keys.append(key)
                artefacts[key] = SimpleNamespace(
                    key=key,
                    label=a.get("label", key),
                    path=a.get("path", ""),
                )
            fixtures.append(SimpleNamespace(
                name=f.get("name", ""),
                source=f.get("source", ""),
                artefacts=artefacts,
            ))
        # Resolve a per-key label using the first fixture that defines
        # it (so the column header matches the author's chosen label).
        artefact_labels: dict[str, str] = {}
        for f in fixtures:
            for k, a in f.artefacts.items():
                artefact_labels.setdefault(k, a.label)
        integration_environment = SimpleNamespace(
            intro=_text(ie_elem.find("intro")),
            fixtures=fixtures,
            artefact_keys=artefact_keys,
            artefact_labels=artefact_labels,
            outro=_text(ie_elem.find("outro")),
        )

    tooling_elem = elem.find("tooling")
    tooling = []
    if tooling_elem is not None:
        tooling = [
            SimpleNamespace(
                name=t.get("name", ""),
                required_for=t.get("required_for", ""),
                notes=t.get("notes", ""),
            )
            for t in tooling_elem.findall("tool")
        ]

    return SimpleNamespace(
        introduction=introduction,
        strategy=strategy,
        integration_environment=integration_environment,
        tooling=tooling,
        maintenance=_text(elem.find("maintenance")),
    )


def load_project(xml_path: Path, metadata_for: str) -> SimpleNamespace:
    try:
        root = ET.parse(xml_path).getroot()
    except ET.ParseError as exc:
        raise ProjectXmlError(f"{xml_path}: malformed XML: {exc}") from exc
    except FileNotFoundError as exc:
        raise ProjectXmlError(f"{xml_path}: file not found") from exc
    if root.tag != "project":
        raise ProjectXmlError(
            f"Root element is <{root.tag}>, expected <project>"
        )

    # Pull the document metadata block requested by the caller.
    metadata = None
    for d in root.findall("metadata/document"):
        if d.get("id") == metadata_for:
            metadata = SimpleNamespace(**_attrs(d))
            break
    if metadata is None:
        raise ProjectXmlError(
            f"<document id={metadata_for!r}> not found in metadata block"
        )

    sdd = build_sdd(root.find("sdd"))
    stp = build_stp(root.find("stp"))
    hlrs = [build_hlr_section(s) for s in root.findall("hlrs/section")]
    llrs = [build_llr_group(f) for f in root.findall("llrs/function")]
    tests = [build_test_file(f) for f in root.findall("tests/file")]

    # Counts pass-through so templates can show "X tests across Y files".
    counts = {c.get("name"): c.get("value")
              for c in root.findall("metadata/counts/count")}

    # Cross-reference maps consumed by the LLR coverage matrix in the
    # STP template. Each maps an upstream identifier to the list of
    # test names that carry a <trace target="..."> citing it.
    tests_by_llr: dict[str, list[str]] = {}
    tests_by_hlr: dict[str, list[str]] = {}
    for tf in tests:
        for t in tf.tests:
            for tr in t.traces:
                target = getattr(tr, "target", "")
                ref = getattr(tr, "ref", "")
                if not ref:
                    continue
                if target == "LLR":
                    tests_by_llr.setdefault(ref, []).append(t.name)
                elif target == "HLR":
                    tests_by_hlr.setdefault(ref, []).append(t.name)

    # Flat list of every LLR (in declaration order) annotated with the
    # owning function group, for the coverage-matrix loop.
    flat_llrs: list[SimpleNamespace] = []
    for grp in llrs:
        for llr in grp.llrs:
            flat_llrs.append(SimpleNamespace(
                id=llr.id,
                text=llr.text,
                traces=llr.traces,
                ui=getattr(llr, "ui", None),
                function_name=grp.name or grp.title,
                function_number=grp.number,
            ))

    # Flat list of every HLR (in declaration order) annotated with its
    # owning section number/title. Used by the Traceability template.
    flat_hlrs: list[SimpleNamespace] = []
    for sec in hlrs:
        for hlr in sec.hlrs:
            flat_hlrs.append(SimpleNamespace(
                id=hlr.id,
                name=hlr.name,
                text=hlr.text,
                traces=hlr.traces,
                ui=getattr(hlr, "ui", None),
                section_number=sec.number,
                section_title=sec.title,
            ))

    # ID lookup tables.
    hlr_by_id = {h.id: h for h in flat_hlrs}
    llr_by_id = {l.id: l for l in flat_llrs}

    # LLRs grouped by HLR they implement (from each LLR's <traces>).
    llrs_by_hlr: dict[str, list[str]] = {}
    for llr in flat_llrs:
        for tr in llr.traces:
            if getattr(tr, "target", "") == "HLR" and tr.ref:
                llrs_by_hlr.setdefault(tr.ref, []).append(llr.id)

    # HLRs grouped by SDD section they implement.
    hlrs_by_sdd: dict[str, list[str]] = {}
    for hlr in flat_hlrs:
        for tr in hlr.traces:
            if getattr(tr, "target", "") == "SDD" and tr.ref:
                hlrs_by_sdd.setdefault(tr.ref, []).append(hlr.id)

    # File-of-test lookup so the test-side matrix can show source paths.
    file_of_test: dict[str, str] = {}
    for tf in tests:
        for t in tf.tests:
            file_of_test[t.name] = tf.path

    # Flat alphabetical test list for the §5 reverse matrix.
    flat_tests: list[SimpleNamespace] = []
    for tf in tests:
        for t in tf.tests:
            flat_tests.append(SimpleNamespace(
                name=t.name,
                purpose=t.purpose,
                traces=t.traces,
                ui=getattr(t, "ui", None),
                file=tf.path,
            ))
    flat_tests.sort(key=lambda t: (t.file, t.name))

    # IDs that have no direct binding in the verification chain.
    llrs_no_test = [l.id for l in flat_llrs if l.id not in tests_by_llr]
    hlrs_no_test = [
        h.id for h in flat_hlrs
        if h.id not in tests_by_hlr
        and not any(lid in tests_by_llr for lid in llrs_by_hlr.get(h.id, []))
    ]

    # SDD section number -> heading title. Mirrors the numbering scheme
    # used by SDD.md.j2 so the Traceability template can resolve refs
    # like "3.2.1" or "4.3.2" to a human-readable section name.
    sdd_titles: dict[str, str] = {}
    if sdd is not None:
        sdd_titles.update({
            "1": "Introduction",
            "1.1": "Purpose of the Document",
            "1.2": "Scope of the Document",
            "1.3": "Project Overview",
            "1.4": "Definitions, Acronyms, and Abbreviations",
            "1.5": "References",
            "1.6": "Document Overview",
            "2": "System Overview",
            "2.1": "System Architecture",
            "2.2": "Design Goals and Constraints",
        })
        for i, m in enumerate(sdd.modules):
            sec = i + 3
            mod_label = m.path or m.title
            sdd_titles[f"{sec}"] = f"Detailed Design ({mod_label})"
            sdd_titles[f"{sec}.1"] = f"Purpose and Responsibilities ({mod_label})"
            if m.interfaces is not None:
                base = "External Interfaces"
                if getattr(m.interfaces, "title_suffix", ""):
                    base = f"{base} {m.interfaces.title_suffix}"
                sdd_titles[f"{sec}.2"] = f"{base} ({mod_label})"
                for j, iface in enumerate(m.interfaces.items, start=1):
                    sdd_titles[f"{sec}.2.{j}"] = iface.title
            sdd_titles[f"{sec}.3"] = f"Internal Structure ({mod_label})"
            sub_n = 0
            if m.data_structures:
                sub_n += 1
                sdd_titles[f"{sec}.3.{sub_n}"] = "Key Data Structures"
            if m.functions is not None:
                sub_n += 1
                sdd_titles[f"{sec}.3.{sub_n}"] = f"Key Functions ({mod_label})"
            if m.algorithm:
                sub_n += 1
                sdd_titles[f"{sec}.3.{sub_n}"] = "Parsing Strategy / Algorithm"
            if m.dependencies:
                sdd_titles[f"{sec}.4"] = f"Dependencies ({mod_label})"
            if m.error_handling:
                sdd_titles[f"{sec}.5"] = f"Error Handling and Logging ({mod_label})"
        dd_sec = len(sdd.modules) + 3
        sdd_titles[f"{dd_sec}"] = "Data Dictionary"

    return SimpleNamespace(
        name=root.get("name", ""),
        short_name=root.get("short_name", ""),
        schema_version=root.get("schema_version", ""),
        metadata=metadata,
        counts=counts,
        sdd=sdd,
        sdd_titles=sdd_titles,
        stp=stp,
        hlrs=hlrs,
        llrs=llrs,
        flat_hlrs=flat_hlrs,
        flat_llrs=flat_llrs,
        hlr_by_id=hlr_by_id,
        llr_by_id=llr_by_id,
        llrs_by_hlr=llrs_by_hlr,
        hlrs_by_sdd=hlrs_by_sdd,
        tests=tests,
        flat_tests=flat_tests,
        file_of_test=file_of_test,
        tests_by_llr=tests_by_llr,
        tests_by_hlr=tests_by_hlr,
        llrs_no_test=llrs_no_test,
        hlrs_no_test=hlrs_no_test,
    )


PVD_TEMPLATE = Path(__file__).resolve().parent / "templates" / "PVD.md.template"
SAR_TEMPLATE = Path(__file__).resolve().parent / "templates" / "SAR.md.template"
VR_TEMPLATE = Path(__file__).resolve().parent / "templates" / "VR.md.template"
SDP_TEMPLATE = Path(__file__).resolve().parent / "templates" / "SDP.md.template"
TRACER_SKILL_TEMPLATE = Path(__file__).resolve().parent / "templates" / "tracer.skill.md"

# All hand-authored document templates (not Jinja2-rendered from Project.xml)
AUTHORED_TEMPLATES = {
    "PVD": {"template": PVD_TEMPLATE, "output": "PVD.md"},
    "SAR": {"template": SAR_TEMPLATE, "output": "SAR.md"},
    "VR": {"template": VR_TEMPLATE, "output": "VR.md"},
    "SDP": {"template": SDP_TEMPLATE, "output": "SDP.md"},
}


def _ns_to_jsonable(value: Any) -> Any:
    """Recursively convert SimpleNamespace / dict / list trees into a
    JSON-serialisable structure. Used by parse_project_to_dict so that
    the same data the templates see can be exposed verbatim over the
    project_io.py JSON-RPC surface.
    """
    if isinstance(value, SimpleNamespace):
        return {k: _ns_to_jsonable(v) for k, v in vars(value).items()}
    if isinstance(value, dict):
        return {str(k): _ns_to_jsonable(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_ns_to_jsonable(v) for v in value]
    return value


def parse_project_to_dict(
    xml_path: Path | str = PROJECT_XML,
    metadata_for: str | None = None,
) -> dict[str, Any]:
    """Library entry point: load Project.xml and return the parsed
    project as a JSON-serialisable dict matching the SimpleNamespace
    tree the templates consume.

    If `metadata_for` is None, the first <document id="..."> entry in
    <metadata> is used so callers that just want the structural data
    do not need to know which spec doc to ask for.
    """
    xml_path = Path(xml_path)
    if metadata_for is None:
        try:
            root = ET.parse(xml_path).getroot()
        except ET.ParseError as exc:
            raise ProjectXmlError(
                f"{xml_path}: malformed XML: {exc}"
            ) from exc
        except FileNotFoundError as exc:
            raise ProjectXmlError(f"{xml_path}: file not found") from exc
        first = root.find("metadata/document")
        if first is None or not first.get("id"):
            raise ProjectXmlError(
                f"{xml_path}: <metadata> has no <document id=\"...\"> "
                f"entries; pass metadata_for explicitly"
            )
        metadata_for = first.get("id", "")
    project = load_project(xml_path, metadata_for)
    return _ns_to_jsonable(project)


def render_document(
    template_path: Path | str,
    metadata_id: str,
    xml_path: Path | str = PROJECT_XML,
) -> str:
    """Library entry point: load Project.xml and render the named
    template against it. Returns the rendered markdown as a string,
    normalised to exactly one trailing newline (matching the CLI).
    """
    template_path = Path(template_path)
    xml_path = Path(xml_path)
    if not template_path.exists():
        raise ProjectXmlError(f"{template_path}: template not found")
    project = load_project(xml_path, metadata_id)
    output = render(template_path, project)
    return output.rstrip("\n") + "\n"


TEMPLATES_DIR = Path(__file__).resolve().parent / "templates"


def list_documents(
    xml_path: Path | str = PROJECT_XML,
) -> list[dict[str, str]]:
    """Library entry point: enumerate ``<metadata><document>`` entries
    so the VS Code extension (and any other consumer) can register
    one render command and one preview target per discovered document
    without naming the spec stack in source.

    Each returned dict carries ``id``, ``title``, ``source``,
    ``version``, ``date``, ``author``, ``template``, and ``output``.
    The ``template`` and ``output`` fields are filled in from the
    optional XSD attributes when present; otherwise they fall back to
    the project convention:

      * ``template = tools/templates/<id>.md.j2`` (relative to the
        repository root)
      * ``output   = <source>``

    Phase 2.5 of the schema-driven retrofit pins this method as the
    single source of truth for the document set the extension
    discovers; the extension and the linter no longer hard-code which
    documents exist.
    """
    xml_path = Path(xml_path)
    try:
        root = ET.parse(xml_path).getroot()
    except ET.ParseError as exc:
        raise ProjectXmlError(f"{xml_path}: malformed XML: {exc}") from exc
    except FileNotFoundError as exc:
        raise ProjectXmlError(f"{xml_path}: file not found") from exc
    if root.tag != "project":
        raise ProjectXmlError(
            f"Root element is <{root.tag}>, expected <project>"
        )

    documents: list[dict[str, str]] = []
    for d in root.findall("metadata/document"):
        doc_id = d.get("id", "")
        if not doc_id:
            continue
        source = d.get("source", "")
        template = d.get("template") or f"tools/templates/{doc_id}.md.j2"
        output = d.get("output") or source
        documents.append({
            "id": doc_id,
            "title": d.get("title", ""),
            "source": source,
            "version": d.get("version", ""),
            "date": d.get("date", ""),
            "author": d.get("author", ""),
            "template": template,
            "output": output,
        })
    return documents


SKELETON_PROJECT_XML = """\
<?xml version="1.0" encoding="UTF-8"?>
<!--
  Project.xml — single source of truth for this project's spec stack.
  See tools/Developers_Guide.md for the schema reference.

  This file was created by `render_doc.py` in init mode. Fill in the
  payload sections (sdd, stp, hlrs, llrs, tests) as the project takes
  shape, then regenerate the markdown specs with `render_doc.py`.
-->
<project name="{name}" short_name="{short_name}" schema_version="1.3"
         xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:noNamespaceSchemaLocation="{schema_location}">
  <metadata>
    <document id="SDD"          title="Software Design Document"     source="doc/SDD.md"          version="0.1" date="{date}" author="{author}"/>
    <document id="HLRs"         title="High-Level Requirements"      source="doc/HLRs.md"         version="0.1" date="{date}" author="{author}"/>
    <document id="LLRs"         title="Low-Level Requirements"       source="doc/LLRs.md"         version="0.1" date="{date}" author="{author}"/>
    <document id="STP"          title="Software Test Plan"           source="doc/STP.md"          version="0.1" date="{date}" author="{author}"/>
    <document id="Traceability" title="Traceability Matrix"          source="doc/Traceability.md" version="0.1" date="{date}" author="{author}"/>
    <counts>
      <count name="hlrs"       value="0"/>
      <count name="llrs"       value="0"/>
      <count name="tests"      value="0"/>
      <count name="test_files" value="0"/>
    </counts>
  </metadata>

  <!-- Software Design Document payload (see tools/Developers_Guide.md §3). -->
  <sdd>
  </sdd>

  <!-- Software Test Plan payload (see tools/Developers_Guide.md §4). -->
  <stp>
  </stp>

  <!-- High-Level Requirements (see tools/Developers_Guide.md §5). -->
  <hlrs>
  </hlrs>

  <!-- Low-Level Requirements (see tools/Developers_Guide.md §6). -->
  <llrs>
  </llrs>

  <!-- Test sources (see tools/Developers_Guide.md §7). -->
  <tests>
  </tests>
</project>
"""


def init_project(
    *,
    name: str,
    short_name: str,
    author: str = "TBD",
    xml_path: Path | str = PROJECT_XML,
    pvd_path: Path | str | None = None,
    pvd_template: Path | str = PVD_TEMPLATE,
    schema_location: str | None = None,
    force: bool = False,
) -> dict[str, Any]:
    """Bootstrap a new project: write skeleton Project.xml and PVD.md.

    Returns a result dict with keys:
      * ``xml_path``  — absolute path of the written Project.xml
      * ``pvd_path``  — absolute path of the written PVD.md
      * ``existing``  — list of files that already existed (only when
        ``force=True`` and the call overwrote them; empty otherwise).

    ``schema_location`` is written verbatim into the skeleton's
    ``xsi:noNamespaceSchemaLocation`` attribute so XSD-aware editors
    can resolve ``tools/project.xsd``. When ``None`` (the default), a
    relative path from ``xml_path``'s parent directory to the canonical
    ``tools/project.xsd`` is computed automatically.

    Raises ``ProjectXmlError`` if ``force`` is False and either target
    file exists, or if the PVD template is missing. Does not write to
    stderr/stdout.
    """
    from datetime import date as _date

    xml_path = Path(xml_path)
    pvd_template = Path(pvd_template)
    if pvd_path is None:
        pvd_path = Path(__file__).resolve().parent.parent / "doc" / "PVD.md"
    pvd_path = Path(pvd_path)

    if schema_location is None:
        schema_location = os.path.relpath(
            PROJECT_XSD, start=xml_path.resolve().parent
        )

    today = _date.today().isoformat()

    targets = [xml_path, pvd_path]
    existing = [p for p in targets if p.exists()]
    if xml_path.exists() and not force:
        raise ProjectXmlError(
            "refusing to overwrite existing file(s): "
            + str(xml_path)
            + " (pass force=True to overwrite)"
        )

    if not pvd_template.exists():
        raise ProjectXmlError(
            f"PVD template not found: {pvd_template}"
        )

    xml_path.parent.mkdir(parents=True, exist_ok=True)
    pvd_path.parent.mkdir(parents=True, exist_ok=True)

    xml_path.write_text(
        SKELETON_PROJECT_XML.format(
            name=name,
            short_name=short_name,
            date=today,
            author=author,
            schema_location=schema_location,
        )
    )

    # Substitute the obvious header placeholders in the PVD template.
    # Body placeholders (e.g. <Persona 1>, <Capability 1>) are left for
    # the human author to fill in.  Skip writing if PVD already exists
    # (preserve any existing hand-authored content).
    pvd_text = pvd_template.read_text()
    pvd_text = pvd_text.replace("<Product Name>", name)
    pvd_text = pvd_text.replace("<short_name>", short_name)
    pvd_text = pvd_text.replace("<YYYY-MM-DD>", today)
    pvd_text = pvd_text.replace("<Your name(s)>", author)
    if not pvd_path.exists():
        pvd_path.write_text(pvd_text)

    # Install the TraceR AI skill file into the project's .github/ directory
    # so Copilot loads the TraceR working rules for this repository.
    # The project root is assumed to be two levels above xml_path (i.e.,
    # <root>/doc/Project.xml → <root>).
    skill_path = xml_path.resolve().parent.parent / ".github" / "skills" / "tracer" / "SKILL.md"
    if not skill_path.exists() and TRACER_SKILL_TEMPLATE.exists():
        skill_path.parent.mkdir(parents=True, exist_ok=True)
        skill_path.write_text(TRACER_SKILL_TEMPLATE.read_text())

    return {
        "xml_path": str(xml_path),
        "pvd_path": str(pvd_path),
        "skill_path": str(skill_path),
        "existing": [str(p) for p in existing] if force else [],
    }


def generate_doc(
    *,
    doc_id: str,
    name: str = "<Product Name>",
    short_name: str = "<short_name>",
    author: str = "TBD",
    output_dir: Path | str | None = None,
    force: bool = False,
    interactive: bool = False,
) -> dict[str, Any]:
    """Generate a hand-authored document from its template.

    ``doc_id`` must be one of the keys in ``AUTHORED_TEMPLATES``
    (PVD, SAR, VR, SDP).

    When ``interactive=True`` and the target file exists, the caller is
    expected to have already prompted the user — this function does not
    perform I/O prompts itself. Use ``_generate_doc_cli`` for the
    interactive CLI path.

    Returns a result dict with keys:
      * ``output_path`` — absolute path of the written file
      * ``skipped``     — True if the file existed and was not overwritten
    """
    from datetime import date as _date

    if doc_id not in AUTHORED_TEMPLATES:
        raise ProjectXmlError(
            f"Unknown document id '{doc_id}'. "
            f"Valid ids: {', '.join(sorted(AUTHORED_TEMPLATES))}"
        )

    entry = AUTHORED_TEMPLATES[doc_id]
    template_path = Path(entry["template"])
    if not template_path.exists():
        raise ProjectXmlError(
            f"{doc_id} template not found: {template_path}"
        )

    if output_dir is None:
        output_dir = Path(__file__).resolve().parent.parent / "doc"
    output_dir = Path(output_dir)
    output_path = output_dir / entry["output"]

    if output_path.exists() and not force:
        return {"output_path": str(output_path), "skipped": True}

    today = _date.today().isoformat()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    text = template_path.read_text()
    text = text.replace("<Product Name>", name)
    text = text.replace("<short_name>", short_name)
    text = text.replace("<YYYY-MM-DD>", today)
    text = text.replace("<Your name(s)>", author)
    output_path.write_text(text)

    return {"output_path": str(output_path), "skipped": False}


def generate_all_docs(
    *,
    name: str = "<Product Name>",
    short_name: str = "<short_name>",
    author: str = "TBD",
    output_dir: Path | str | None = None,
    force: bool = False,
) -> list[dict[str, Any]]:
    """Generate all hand-authored document templates.

    Returns a list of result dicts (one per document) from generate_doc.
    """
    results = []
    for doc_id in AUTHORED_TEMPLATES:
        result = generate_doc(
            doc_id=doc_id,
            name=name,
            short_name=short_name,
            author=author,
            output_dir=output_dir,
            force=force,
        )
        results.append({"doc_id": doc_id, **result})
    return results


def _generate_doc_cli(
    *,
    doc_ids: list[str],
    name: str,
    short_name: str,
    author: str,
    output_dir: Path,
    force: bool,
) -> int:
    """CLI wrapper for generate_doc: prompts on existing files."""
    from datetime import date as _date

    had_error = False
    for doc_id in doc_ids:
        if doc_id not in AUTHORED_TEMPLATES:
            sys.stderr.write(
                f"render_doc.py --generate-doc: unknown document id "
                f"'{doc_id}'. Valid ids: "
                f"{', '.join(sorted(AUTHORED_TEMPLATES))}\n"
            )
            had_error = True
            continue

        entry = AUTHORED_TEMPLATES[doc_id]
        output_path = output_dir / entry["output"]

        do_write = force
        if output_path.exists() and not force:
            # Interactive prompt
            try:
                answer = input(
                    f"{output_path} already exists. Replace? [y/N] "
                )
            except (EOFError, KeyboardInterrupt):
                sys.stderr.write("\nAborted.\n")
                return 1
            do_write = answer.strip().lower() in ("y", "yes")
            if not do_write:
                sys.stderr.write(f"  Skipped {output_path}\n")
                continue

        try:
            result = generate_doc(
                doc_id=doc_id,
                name=name,
                short_name=short_name,
                author=author,
                output_dir=output_dir,
                force=True,  # We already prompted
            )
        except ProjectXmlError as exc:
            sys.stderr.write(f"render_doc.py --generate-doc: {exc}\n")
            had_error = True
            continue

        if result["skipped"]:
            sys.stderr.write(f"  Skipped {result['output_path']}\n")
        else:
            sys.stderr.write(f"  Wrote {result['output_path']}\n")

    return 1 if had_error else 0


def _init_project_cli(
    *,
    name: str,
    short_name: str,
    author: str,
    xml_path: Path,
    pvd_path: Path,
    pvd_template: Path,
    schema_location: str | None,
    force: bool,
) -> int:
    """Delegate project bootstrap to `make project-init`."""
    import subprocess

    tools_dir = Path(__file__).resolve().parent
    cmd = [
        "make", "-C", str(tools_dir), "project-init",
        f"NAME={name}",
        f"SHORT_NAME={short_name}",
        f"AUTHOR={author}",
        f"XML_OUT={xml_path}",
        f"PVD_OUT={pvd_path}",
    ]
    if force:
        cmd.append("FORCE=1")
    result = subprocess.run(cmd)
    return result.returncode


def render(template_path: Path, project: SimpleNamespace) -> str:
    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(template_path.parent),
        autoescape=False,  # nosec B701 — output is Markdown, not browser-served HTML
        trim_blocks=True,
        lstrip_blocks=False,
        keep_trailing_newline=True,
        undefined=jinja2.StrictUndefined,
    )
    env.filters["gh_slug"] = _gh_slug
    template = env.get_template(template_path.name)
    return template.render(project=project)


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="render_doc.py",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=(
            "Render a Markdown specification document from a Jinja2 "
            "template combined with the data in doc/Project.xml.\n"
            "\n"
            "The renderer loads Project.xml, builds a `project` namespace "
            "exposing the parsed payload (sdd, stp, hlrs, llrs, tests) "
            "plus cross-reference indexes (tests_by_llr, tests_by_hlr, "
            "llrs_by_hlr, hlrs_by_sdd, etc.), then renders the named "
            "template against it. The output is written to stdout by "
            "default, or to the path given by --out."
        ),
        epilog=(
            "EXAMPLES\n"
            "  Bootstrap a new project (delegates to make project-init;\n"
            "  writes doc/Project.xml, PVD.md, SAR.md, VR.md, SDP.md):\n"
            "    python3 tools/render_doc.py --init \\\n"
            "        --name \"My Product\" --short-name myprod \\\n"
            "        --author \"Jane Doe\"\n"
            "\n"
            "  Or equivalently via make:\n"
            "    make -C tools project-init NAME=\"My Product\" \\\n"
            "        SHORT_NAME=\"myprod\" AUTHOR=\"Jane Doe\"\n"
            "\n"
            "  Generate a single hand-authored document:\n"
            "    python3 tools/render_doc.py --generate-doc SAR \\\n"
            "        --name \"My Product\" --short-name myprod\n"
            "\n"
            "  Generate all hand-authored documents:\n"
            "    python3 tools/render_doc.py --generate-doc all \\\n"
            "        --name \"My Product\" --short-name myprod\n"
            "\n"
            "  Render the SDD to stdout:\n"
            "    python3 tools/render_doc.py tools/templates/SDD.md.j2 SDD\n"
            "\n"
            "  Render the HLRs document to a file:\n"
            "    python3 tools/render_doc.py tools/templates/HLRs.md.j2 \\\n"
            "        HLRs --out doc/HLRs.md\n"
            "\n"
            "  Use a Project.xml from a different location:\n"
            "    python3 tools/render_doc.py tools/templates/STP.md.j2 \\\n"
            "        STP --xml /path/to/Project.xml --out doc/STP.md\n"
            "\n"
            "  Regenerate every spec document in this project:\n"
            "    for d in SDD HLRs LLRs STP Traceability; do \\\n"
            "      python3 tools/render_doc.py \\\n"
            "        tools/templates/$d.md.j2 $d --out doc/$d.md; \\\n"
            "    done\n"
            "\n"
            "EXIT STATUS\n"
            "  0  Render (or --init) succeeded.\n"
            "  1  --init refused to overwrite an existing file (use --force).\n"
            "  2  Argument parsing failed (argparse default).\n"
            "  Other non-zero values are raised by the underlying XML "
            "parser, Jinja2, or filesystem operations."
        ),
    )
    parser.add_argument(
        "template",
        type=Path,
        nargs="?",
        metavar="TEMPLATE",
        help=(
            "Path to the Jinja2 template to render (e.g. "
            "tools/templates/SDD.md.j2). The template's parent directory "
            "becomes the loader root, so {%% include %%} / {%% import %%} "
            "directives may reference sibling templates by relative path. "
            "Required unless --init is given."
        ),
    )
    parser.add_argument(
        "metadata_id",
        nargs="?",
        metavar="METADATA_ID",
        help=(
            "Identifier of the <metadata>/<document id=\"...\"> block in "
            "Project.xml whose title/version/date/author values should be "
            "exposed to the template as `project.metadata`. Must match "
            "exactly one <document id=\"...\"> entry. Standard values in "
            "this project: SDD, HLRs, LLRs, STP, Traceability. "
            "Required unless --init is given."
        ),
    )
    parser.add_argument(
        "--xml",
        type=Path,
        default=PROJECT_XML,
        metavar="PATH",
        help=(
            "Path to the Project.xml file to load as the data source. "
            "Defaults to %(default)s (relative to the repository root)."
        ),
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        metavar="PATH",
        help=(
            "Write the rendered document to this file instead of stdout. "
            "Output is normalised to exactly one trailing newline so "
            "round-tripping a document does not accumulate blank lines."
        ),
    )
    init_group = parser.add_argument_group(
        "project bootstrap (--init)",
        "Bootstrap a new project by delegating to `make project-init`. "
        "Creates a skeleton Project.xml plus hand-authored document "
        "templates (PVD, SAR, VR, SDP). When --init is given, TEMPLATE "
        "and METADATA_ID are not required.",
    )
    init_group.add_argument(
        "--init",
        action="store_true",
        help=(
            "Bootstrap a new project by invoking `make project-init`. "
            "Creates a skeleton Project.xml at --xml (default "
            "doc/Project.xml) and hand-authored documents (PVD, SAR, VR, "
            "SDP). Refuses to overwrite existing files unless --force is "
            "also given."
        ),
    )
    init_group.add_argument(
        "--name",
        default=None,
        metavar="NAME",
        help="Full project name (used as Project.xml @name and in PVD title).",
    )
    init_group.add_argument(
        "--short-name",
        default=None,
        metavar="SHORT",
        help="Short / package name (used as Project.xml @short_name).",
    )
    init_group.add_argument(
        "--author",
        default="TBD",
        metavar="AUTHOR",
        help="Author string for metadata blocks and PVD header. Default: TBD.",
    )
    init_group.add_argument(
        "--pvd-out",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "doc" / "PVD.md",
        metavar="PATH",
        help="Output path for the generated PVD. Default: %(default)s.",
    )
    init_group.add_argument(
        "--pvd-template",
        type=Path,
        default=PVD_TEMPLATE,
        metavar="PATH",
        help="Source PVD template. Default: %(default)s.",
    )
    init_group.add_argument(
        "--force",
        action="store_true",
        help=(
            "With --init or --generate-doc, overwrite existing files "
            "without prompting."
        ),
    )
    init_group.add_argument(
        "--schema-location",
        default=None,
        metavar="PATH",
        help=(
            "Value to write into the skeleton's "
            "xsi:noNamespaceSchemaLocation attribute so XSD-aware editors "
            "can resolve tools/project.xsd. By default, a relative path "
            "from --xml's parent directory to tools/project.xsd is "
            "computed automatically."
        ),
    )
    gen_group = parser.add_argument_group(
        "document generation (--generate-doc)",
        "Generate hand-authored document(s) from their templates. "
        "Prompts before overwriting existing files unless --force is given. "
        "Valid document ids: " + ", ".join(sorted(AUTHORED_TEMPLATES)) + ".",
    )
    gen_group.add_argument(
        "--generate-doc",
        nargs="+",
        metavar="DOC_ID",
        help=(
            "Generate one or more hand-authored documents from templates. "
            "Use 'all' to generate all available templates. "
            "Valid ids: " + ", ".join(sorted(AUTHORED_TEMPLATES)) + "."
        ),
    )
    gen_group.add_argument(
        "--doc-out-dir",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "doc",
        metavar="PATH",
        help="Output directory for generated documents. Default: %(default)s.",
    )
    args = parser.parse_args()

    if args.generate_doc:
        doc_ids = []
        for d in args.generate_doc:
            if d.lower() == "all":
                doc_ids = list(AUTHORED_TEMPLATES.keys())
                break
            doc_ids.append(d.upper() if d.upper() in AUTHORED_TEMPLATES else d)
        name = args.name or "<Product Name>"
        short_name = args.short_name or "<short_name>"
        return _generate_doc_cli(
            doc_ids=doc_ids,
            name=name,
            short_name=short_name,
            author=args.author,
            output_dir=args.doc_out_dir,
            force=args.force,
        )

    if args.init:
        if not args.name or not args.short_name:
            parser.error("--init requires --name and --short-name")
        return _init_project_cli(
            name=args.name,
            short_name=args.short_name,
            author=args.author,
            xml_path=args.xml,
            pvd_path=args.pvd_out,
            pvd_template=args.pvd_template,
            schema_location=args.schema_location,
            force=args.force,
        )

    if args.template is None or args.metadata_id is None:
        parser.error("TEMPLATE and METADATA_ID are required unless --init is given")

    try:
        project = load_project(args.xml, args.metadata_id)
    except ProjectXmlError as exc:
        sys.stderr.write(f"{exc}\n")
        return 1
    output = render(args.template, project)
    # Normalize to exactly one trailing newline so round-tripping a doc
    # does not accumulate blank lines at EOF.
    output = output.rstrip("\n") + "\n"

    if args.out:
        args.out.write_text(output)
    else:
        sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
