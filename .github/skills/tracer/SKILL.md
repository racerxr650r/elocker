---
name: tracer
description: "Use when working with this project's specification, requirements, design, tests, or traceability. This project is managed by TraceR — doc/Project.xml is the SINGLE SOURCE OF TRUTH for design, requirements, and verification. USE FOR: reading, editing, creating, generating, updating, or regenerating any spec document — SDD (Software Design Document), HLRs (High-Level Requirements), LLRs (Low-Level Requirements), STP (Software Test Plan), Traceability matrix, or PVD (Product Vision Document); any change to doc/Project.xml or doc/PVD.md; adding, removing, or editing requirements (HLRs or LLRs); adding tests that trace or link to requirements; generating doc/SDD.md, doc/HLRs.md, doc/LLRs.md, doc/STP.md, or doc/Traceability.md; checking, fixing, or understanding test coverage; tracing or linking a requirement to a test; finding untested or uncovered requirements; explaining why a requirement or test is missing from a document; linting or validating the project spec. DO NOT USE FOR: routine source-code changes that do not alter a documented behaviour or requirement."
---

# TraceR — AI Working Rules

This project is managed by [TraceR](https://github.com/racerxr650r/TraceR).
`doc/Project.xml` is the **single source of truth** for the project's
specification, design, and verification artefacts.

## Document Map

| File | How it is maintained | Rule |
| ---- | -------------------- | ---- |
| `doc/Project.xml` | **Hand-edited** | The only spec file you change directly |
| `doc/PVD.md` | **Hand-authored** | Product Vision Document — not generated |
| `doc/SDP.md` | **Hand-authored** | Software Development Plan — not generated |
| `doc/notes.md` | **Hand-authored** | Working notes; binds nothing, traces nothing |
| `doc/SDD.md` | **Generated** | Never edit — regenerate from `Project.xml` |
| `doc/HLRs.md` | **Generated** | Never edit — regenerate from `Project.xml` |
| `doc/LLRs.md` | **Generated** | Never edit — regenerate from `Project.xml` |
| `doc/STP.md` | **Generated** | Never edit — regenerate from `Project.xml` |
| `doc/Traceability.md` | **Generated** | Never edit — regenerate from `Project.xml` |

## Hard Rules

1. **Never edit `doc/SDD.md`, `doc/HLRs.md`, `doc/LLRs.md`, `doc/STP.md`,
   or `doc/Traceability.md` directly.** They are generated from
   `doc/Project.xml`. Any hand edit is overwritten on the next render.

2. **IDs are stable contracts.** `HLR-NNN` and `LLR-XXX-NN` identifiers
   may never be renumbered or reused once allocated. Always allocate the
   next free number when adding a new item.

3. **Every `<hlr>` must carry at least one `<traces target="SDD" ref="N.N">`.
   Every `<llr>` must carry at least one `<traces target="HLR" ref="HLR-NNN">`.
   Every `<test>` must carry at least one `<traces target="LLR" ref="LLR-XXX-NN">`
   or `<traces target="HLR" ref="HLR-NNN">`.** Missing traces produce
   coverage-gap rows in `doc/Traceability.md` and linter warnings.

4. **Wrap special characters in CDATA.** Any `<text>`, `<purpose>`,
   `<intro>`, or other body element whose content contains backticks,
   angle brackets (`<`, `>`), ampersands (`&`), or Markdown link syntax
   (`[text](url)`) must be wrapped in `<![CDATA[...]]>`. Bare `<` or `&`
   silently corrupt the file on the next parse.

5. **Headings and section numbers belong in templates, not in data.**
   Do not put `##` headings, section numbers, or "Document Overview"
   prose inside `<sdd>` or `<stp>` payloads — the renderer adds all
   document scaffolding.

6. **Anchors are owned by the templates.** The generated HLRs.md,
   LLRs.md, and STP.md emit `<a id="...">` for every requirement and
   test name so Traceability.md can link to them. Do not add or remove
   these anchors manually.

## Counts and Document Versions

Two things in `<metadata>` are hand-maintained and silently drift if
forgotten:

*   **`<counts>`** must match reality. After adding or removing any `<hlr>`,
    `<llr>`, `<test>`, or `<tests>/<file>`, update the corresponding
    `<count name="…" value="…"/>`. Nothing enforces this, and a stale count
    misreports the snapshot line in `Traceability.md`.
*   **`<document version="…" date="…">`** should be bumped for each document
    whose content you changed, so a reader can tell a regenerated file from a
    revised one.

Verify counts against the file itself rather than trusting the attribute:

```sh
python3 -c "
import xml.etree.ElementTree as ET
r = ET.parse('doc/Project.xml').getroot()
print('hlrs', len(list(r.iter('hlr'))), '| llrs', len(list(r.find('llrs').iter('llr'))))"
```

## Recording Tests as They Are Written

A requirement is only *verified* when a catalogued test traces to it. Writing
the test is not enough — an uncatalogued test leaves the requirement in the
coverage-gap list forever. Every test written during implementation therefore
needs an entry:

```xml
<tests>
  <file path="test/unit/analyze.c" role="unit" count="12">
    <test name="merge_nested_spans">
      <purpose><![CDATA[Verifies that overlapping and nested comment spans are
        coalesced before their lines are excluded from ELOC.]]></purpose>
      <traces>
        <trace target="LLR" ref="LLR-MRG-02"/>
      </traces>
    </test>
  </file>
</tests>
```

Unit tests trace to **LLRs**; integration, fixture, and instrumented tests
trace to **HLRs**. The test's doc comment in source and its `<traces>` block
here must name the same identifiers.

## Adding Requirements Discovered During Implementation

Implementation surfaces behaviour nobody wrote down. When it does:

1.  Allocate the **next free ID** — never reuse a retired one (`HLR-042` is
    retired and must stay so) and never renumber an existing one.
2.  Place it in the section whose subject it belongs to, in document order.
3.  Give it at least one `<traces>` link, and write the LLR beneath it in the
    same change — a requirement with no LLR is a gap by construction.
4.  Update `<counts>`, re-render, and confirm `lint_project.py` reports 0
    errors.

## Traceability Chain

```
SDD sections  ←── HLR  (traces target="SDD" ref="N.N")
HLR items     ←── LLR  (traces target="HLR" ref="HLR-NNN")
LLR items     ←── Test (traces target="LLR" ref="LLR-XXX-NN")
```

Coverage gaps — items with no downstream trace — appear as warning rows
in `doc/Traceability.md` and in the VS Code Problems panel.

## Decision Flow

| Intent | Action |
| ------ | ------ |
| Update a section of the SDD | Edit the matching node inside `<sdd>` in `Project.xml`, then render `SDD`. |
| Add an HLR | Append `<hlr id="HLR-NNN" name="...">` in the correct `<section>`; add `<traces target="SDD" ref="N.N">` for every SDD section it implements. Render `HLRs` and `Traceability`. |
| Add an LLR | Append `<llr id="LLR-XXX-NN">` in the correct `<function>`; add `<traces target="HLR" ref="HLR-NNN">` for every HLR it implements. Render `LLRs` and `Traceability`. |
| Add a test | Add the test function in source with a comment citing the LLR(s)/HLR(s). Add a matching `<test name="...">` with `<purpose>` and `<traces>` inside the correct `<tests>/<file>` node. Render `STP` and `Traceability`. |
| Close a coverage gap (requirement has no test) | Check whether a downstream LLR traces to the requirement and whether that LLR has a test tracing to it. Add the missing trace link or create the missing test. |
| Wrong ID format (linter warning) | Use the Quick Fix lightbulb in VS Code, or manually allocate the next free ID — never reuse an existing number. |
| Broken trace (linter error) | The `ref` attribute points to an ID that does not exist. Use the Quick Fix lightbulb to pick a valid replacement, or correct it by hand. |

## Rendering

After editing `doc/Project.xml`, regenerate the affected documents.

**Via the CLI — use this when working in a terminal or as an agent.** The
VS Code commands below are unavailable outside the editor.

```sh
python3 tools/lint_project.py                    # validate first; 0 errors required
python3 tools/render_doc.py tools/templates/SDD.md.j2          SDD          --out doc/SDD.md
python3 tools/render_doc.py tools/templates/HLRs.md.j2         HLRs         --out doc/HLRs.md
python3 tools/render_doc.py tools/templates/LLRs.md.j2         LLRs         --out doc/LLRs.md
python3 tools/render_doc.py tools/templates/STP.md.j2          STP          --out doc/STP.md
python3 tools/render_doc.py tools/templates/Traceability.md.j2 Traceability --out doc/Traceability.md
```

Render **every** document whose payload you touched, plus `Traceability`
whenever any `<traces>` block changed — which is nearly always.

**Via VS Code:**

- **Command Palette** (`Ctrl+Shift+P`) → **Project Spec: Render All Documents** —
  regenerates all five spec documents at once.
- **Project Spec: Render & Preview** — renders one document in memory for
  review without writing files to disk.

**Via the linter:**

The linter re-checks `Project.xml` on every save. Run it manually via
**Project Spec: Run Linter** in the Command Palette, or look at the
**Problems** panel (`Ctrl+Shift+M`). Common problems include:

- A trace pointing at a requirement ID that does not exist.
- An ID that does not follow the required pattern (`HLR-NNN` or `LLR-XXX-NN`).
- A requirement that has no downstream test.

## Product Vision Document (PVD)

`doc/PVD.md` is the **hand-authored** Product Vision Document. It defines
*why* the product exists, *who* it is for, and *how success is measured*.
It is **not** generated from `Project.xml` — never overwrite it from a
template.

**AI role:** the developer is the author; the AI is the ghostwriter.

When asked to work on the PVD:

1. Read `doc/PVD.md` first to see what sections exist and how complete
   each one is.
2. Identify thin or missing sections (placeholders, one-line summaries
   where a paragraph is expected, TODO markers, single-item lists).
3. Ask the developer targeted questions to elicit the missing content
   before drafting it. Prefer a small number of specific, answerable
   questions over open-ended prompts.
4. Draft the content once answered. Match the existing tone, heading
   depth, and table style of the document.
5. Do not invent product direction — if a question is genuinely open
   (scope, target users, success metrics, roadmap), ask rather than guess.

Expected PVD sections (in roughly this order): Vision Statement,
Problem Statement, Target Users (with personas), Value Proposition,
Product Principles, Scope (in / out / non-goals), Success Metrics,
Roadmap Themes, Relationship to the spec stack (PVD → HLRs → LLRs →
SDD → STP → Traceability).
