# TraceR Developer's Guide

This guide is for two audiences:

1.  **Template authors** — anyone adding a brand-new generated
    document to a TraceR project, or modifying one of the existing
    Jinja2 templates under [templates/](templates/). Start with
    [§13 Adding a New Generated Document](#13-adding-a-new-generated-document)
    and [§12 Writing Templates](#12-writing-templates).
2.  **Repository maintainers and contributors** — anyone extending
    the schema, the renderer, the linter, or the VS Code extension.
    [§11 Editing and Authoring Notes](#11-editing-and-authoring-notes),
    [§16 Linter Contract](#16-linter-contract-finding-findingsitems-code-values),
    [§17 UI Hint Vocabulary](#17-ui-hint-vocabulary), and
    [§18 AI Pipeline Recording Mode](#18-ai-pipeline-recording-mode)
    cover the day-to-day contracts; the underlying `Project.xml`
    schema and renderer data surface live in
    [Appendix A: Schema Reference for `Project.xml`](#appendix-a-schema-reference-for-projectxml).

`doc/Project.xml` is the **single source of truth** for the project's
specification, design, and verification artefacts. The five
generated specification documents in [../doc/](../doc/) are produced
by [render_doc.py](render_doc.py) from this single file via the
Jinja2 templates in [templates/](templates/).

The companion document for AI agents is
[.github/skills/project-xml/SKILL.md](../.github/skills/project-xml/SKILL.md).
Day-to-day usage instructions for the VS Code extension and the CLI
live in the [User Manual](User_Manual.md).

## Role

`Project.xml` exists for two reasons:

1.  **To document** the project. The five spec markdown documents in
    [doc/](../doc/) are *generated* from `Project.xml` via Jinja2
    templates in [tools/templates/](../tools/templates/) and the renderer
    [render_doc.py](../tools/render_doc.py).
2.  **To measure traceability** across the four layers of the stack —
    SDD → HLRs → LLRs → Tests. Every relation lives in a `<traces>`
    block on the originating element, so a renderer can compose a
    forward or reverse traceability matrix without any other input.

The five generated documents are:

*   [doc/SDD.md](../doc/SDD.md) — Software Design Document
*   [doc/HLRs.md](../doc/HLRs.md) — High-Level Requirements
*   [doc/LLRs.md](../doc/LLRs.md) — Low-Level Requirements
*   [doc/STP.md](../doc/STP.md) — Software Test Plan
*   [doc/Traceability.md](../doc/Traceability.md) — Traceability Matrix

Together with the per-test annotations under [test/](../test/), the
file holds enough structured information to regenerate any of the five
generated documents from a single edit point.

## 11. Editing and Authoring Notes

*   **CDATA content is markdown.** Templates that emit markdown pass it
    through verbatim. Templates that emit other formats (HTML, PDF,
    DOCX) should run the CDATA through a markdown renderer.
*   **Use CDATA defensively.** Any body containing backticks, angle
    brackets, ampersands, or square brackets that *might* look like
    XML or entity markup should be wrapped in `<![CDATA[...]]>`.
*   **Indentation is meaningful** in the markdown payloads of `<text>`
    and `<intro>` only insofar as markdown itself uses indentation
    (for sub-bullets, code blocks, etc.). The renderer strips the
    four-space requirement-list indentation that `HLRs.md` and
    `LLRs.md` use, so a template can re-add that indentation when
    emitting bullet items.
*   **IDs are stable.** `HLR-NNN` and `LLR-XXX-NN` identifiers are
    contracts; do not renumber them when re-rendering.
*   **Order is preserved.** `<section>` / `<hlr>` / `<llr>` / `<test>`
    children appear in the same order as their source files. Templates
    iterate in document order.
*   **Headings come from the template, never the data.** Do not put
    section numbers, `##` headings, or "Document Overview" content into
    the `<sdd>` or `<stp>` payloads. Adding either to the data will
    produce duplicate or mis-numbered headings in the rendered
    document.
*   **Anchors come from the template, never the data.** The HLRs,
    LLRs, and STP templates emit `<a id="...">` for every requirement
    and test name. The Traceability template links to those anchors;
    do not remove them when editing the templates.
*   **Bump `schema_version`** on the `<project>` root whenever you
    change the structure of `Project.xml` in a way the existing
    templates and `tools/render_doc.py` could not consume unchanged.

## 12. Writing Templates

A template is a [Jinja2](https://jinja.palletsprojects.com/) file
under [tools/templates/](../tools/templates/) whose only inputs are the
`project` namespace described in §9 and the
[`gh_slug`](#9-renderer-data-surface) filter. Everything else —
titles, section numbering, table headers, anchor `<a id>` tags,
lead-in sentences — lives in the template, *not* in the data.

### 12.1 Anatomy of a template

Look at [templates/SDD.md.j2](../tools/templates/SDD.md.j2) and
[templates/HLRs.md.j2](../tools/templates/HLRs.md.j2) for working examples.
A typical template has four parts, in order:

1.  **A leading `{# ... #}` comment block** documenting which
    `project.*` fields the template reads, the standard headings
    it owns, and any non-obvious editing tips. The renderer
    strips this from the output but it is the contract for future
    template maintainers.
2.  **Helper macros** (`{% macro foo(...) %}`) for any markdown
    fragment used more than once — typically the per-bullet
    rendering of a complex element (e.g. `fn_block(fn)` in
    SDD.md.j2). Macros use `| indent(N)` so multi-line bullet
    bodies render as one bullet item rather than fragmenting.
3.  **The document title and metadata block**. Read project name,
    version, date, and author from `project.metadata.*`. The
    document `title` and `id` come from the
    `<metadata><document id="...">` entry chosen by the second
    CLI argument to `render_doc.py`.
4.  **The body**, which iterates over the relevant payload(s)
    (`project.sdd`, `project.hlrs`, `project.flat_llrs`,
    `project.tests_by_hlr`, etc.) and emits Markdown.

### 12.2 Conventions enforced by every existing template

*   **Section numbering lives in the template.** Use a `{% set %}`
    counter or `loop.index + N` rather than reading a number from
    the data. The data carries content, not structure.
*   **Anchors come from the template.** Every HLR, LLR, test, and
    SDD section heading the Traceability Matrix links to must be
    preceded by an `<a id="..."></a>` tag emitted by the
    template. Inspect the existing four templates before adding a
    new one to see the exact anchor format.
*   **Use the precomputed indexes.** Never recompute relations in
    Jinja. `project.tests_by_llr[lid]`, `project.llrs_by_hlr[hid]`,
    `project.hlr_by_id[hid]`, and friends exist for this. If your
    template needs a relation that does not yet exist on the
    `project` namespace, add a `build_*` function in
    [render_doc.py](../tools/render_doc.py) and expose it; do not loop in
    Jinja.
*   **Wrap relative paths against `../`.** Every link to a source
    file uses `../<path>` because the rendered document lives
    under `doc/` while the source files live above it. Existing
    templates use the form
    `[{{ m.path }}](../{{ m.path }})`.
*   **Trim with care.** Jinja's `{%- ... -%}` whitespace markers
    are essential; without them the output gets stray blank lines
    around `{% for %}` and `{% if %}` blocks. Match the trim
    style of the existing templates; in particular, keep blank
    lines *between* logical paragraphs (Markdown needs them) and
    suppress whitespace *inside* control structures.
*   **Treat optional fields as optional.** Use
    `{% if project.sdd.scope %}...{% endif %}` rather than
    indexing unconditionally. The XSD makes most fields optional;
    real-world payloads frequently omit them.
*   **CDATA bodies are markdown.** Markdown-emitting templates
    pass them through verbatim; non-markdown emitters (HTML, PDF)
    must run the body through a markdown renderer.

### 12.3 The `gh_slug` filter

The Traceability template links to SDD section headings via
GitHub's heading-anchor convention. Use the `gh_slug` filter
rather than reinventing the rule:

```jinja
[§{{ num }} {{ title }}](SDD.md#{{ (num ~ '-' ~ title) | gh_slug }})
```

### 12.4 Local development loop

```sh
# Render one template; the second positional argument selects the
# <metadata><document id="..."> entry that populates project.metadata.
python3 tools/render_doc.py tools/templates/MyDoc.md.j2 MyDoc --out doc/MyDoc.md

# Or render to stdout for quick inspection:
python3 tools/render_doc.py tools/templates/MyDoc.md.j2 MyDoc | less
```

If rendering raises a Jinja `UndefinedError`, the template referenced
a field that the renderer did not expose. Either fix the template, or
add the field to the relevant `build_*` function in
[render_doc.py](../tools/render_doc.py).

## 13. Adding a New Generated Document

A new document is added by editing data and templates only. From
Phase 2.5 onwards, the VS Code
extension picks up the new tree node, render command, and Markdown
preview automatically; no TypeScript change is required.

The canonical recipe is the four steps below. After each step,
run `make validate-xml` (or `python3 tools/lint_project.py`) before
moving on.

### Step 1 — Decide whether you need a new payload

Review the existing `<project>` payloads (`<sdd>`, `<stp>`,
`<hlrs>`, `<llrs>`, `<tests>`) and the renderer indexes (§9). Two
outcomes:

*   **Reusing existing data** (e.g. a new view over HLRs by SDD
    section, or a per-author test summary) — skip Step 2
    entirely. Your template will read the existing
    `project.flat_hlrs` / `project.tests_by_llr` / etc.
*   **Brand-new content** (e.g. a `<plan>` payload listing
    delivery phases) — you need a new payload. Continue with
    Step 2.

### Step 2 — (If new payload) extend the schema and renderer

1.  **Edit [tools/project.xsd](../tools/project.xsd):**
    *   Define the new complex types (mirror the style of the
        existing `Sdd*` and `Stp*` types; use `mixed="true"` for
        markdown-bearing leaves).
    *   Add the new top-level element to the `<xs:element
        name="project">` `xs:all` block (`minOccurs="0"`).
2.  **Bump `schema_version`** on the `<project>` root in
    [doc/Project.xml](../doc/Project.xml) (current value is
    `1.5`; bump to `1.6` for the next change).
3.  **Edit [render_doc.py](../tools/render_doc.py):**
    *   Add a `build_<payload>(elem)` function returning a
        `SimpleNamespace` shaped exactly the way you want
        templates to read it. Mirror the existing `build_sdd` /
        `build_stp` patterns.
    *   In the renderer's main `parse_project_to_dict` /
        equivalent, call `build_<payload>(root.find("<payload>"))`
        and attach the result as `project.<payload>`.
    *   If the new payload introduces cross-references that other
        documents will need, also build the inverse indexes here
        and expose them on the `project` namespace (e.g.
        `project.<payload>_by_hlr[hid]`).
4.  **Document the new element** in this file under a new
    `## N. <payload>` section that mirrors the style of §3 and
    §4 (XML skeleton, child-element table, render mapping).
5.  **Populate the payload** in [doc/Project.xml](../doc/Project.xml).

### Step 3 — Add the document to `<metadata>`

Every generated document needs a matching `<document>` entry under
`<metadata>`. Without it, the renderer's `project.metadata` lookup
fails, the linter flags the absence, and (from Phase 2.5) the
VS Code extension does not contribute a render command for the
new document.

```xml
<metadata>
  ...existing <document> entries...
  <document id="MyDoc"
            title="My New Document"
            source="doc/MyDoc.md"
            version="0.1"
            date="YYYY-MM-DD"
            author="Your Name"/>
</metadata>
```

*   `id` must be unique within `<metadata>`. The renderer's second
    CLI argument matches against this id. Stick to PascalCase
    (matching the existing `SDD`, `HLRs`, `LLRs`, `STP`,
    `Traceability`).
*   `source` is the workspace-relative path the rendered output
    will be written to.

### Step 4 — Author the template

Create [`tools/templates/<DocId>.md.j2`](../tools/templates/) following the
guidelines in §12. The minimum viable template is:

```jinja
{#- MyDoc.md.j2 - Renders <metadata><document id="MyDoc"> -#}
# {{ project.metadata.title }}: {{ project.name }} ({{ project.short_name }})

**Version:** {{ project.metadata.version }}
**Date:** {{ project.metadata.date }}
**Author(s):** {{ project.metadata.author }}

{# ... iterate project.<payload> here ... #}
```

Render once to verify:

```sh
python3 tools/render_doc.py tools/templates/MyDoc.md.j2 MyDoc --out doc/MyDoc.md
python3 tools/lint_project.py
```

A clean lint result and a non-empty `doc/MyDoc.md` with the right
headings means the document is in. Re-render the Traceability
Matrix afterwards if your new document or payload introduced any
`<traces>` blocks.

### Step 5 (optional) — Wire it into automated regeneration

If the project has a `Makefile` target that batch-regenerates all
documents, add the new render line there alongside the existing
five. From Phase 2.5 onwards the VS Code extension's
`Render All` command enumerates `<metadata><document>` entries
automatically; you do not need to teach it about the new id.

### What you do *not* need to change

*   Not [tools/lint_project.py](../tools/lint_project.py): its
    required-document set is derived from the
    `<metadata><document>` declarations of the project being
    linted (Phase 2.5). The linter warns when a declared
    document's `template` (explicit attribute or the conventional
    `tools/templates/<id>.md.j2` path) is missing from disk; it
    no longer carries a hard-coded list of “standard” ids.
*   Not the VS Code extension's TypeScript code: the schema-driven
    surfaces (Phase 2.5) discover new documents and payloads
    via `<metadata><document>` and `xs:appinfo` UI hints in the
    XSD. The only TypeScript change ever needed is for **bespoke
    visualisations** that go beyond a generic tree node, form, or
    code lens.

## 14. Agents and Prompts

TraceR ships reusable VS Code agent and prompt definitions under
`.github/agents/` and `.github/prompts/`. These are consumed by AI
coding assistants (such as GitHub Copilot) that support agent and
prompt discovery.

### Agents

Agent files (`.agent.md`) define interactive assistants that ask
questions, gather context, and perform tasks. Each has a YAML
frontmatter header specifying its description, tools, and
optional sub-agent dependencies.

| File | Scope | Description |
| ---- | ----- | ----------- |
| `ci.agent.md` | Generic | GitHub Actions workflow generator. Presents a menu of common workflow categories and generates YAML. |
| `makefile.agent.md` | Generic | Makefile generator. Prompts for targets and implements them with the `awk`-based self-documenting help hack. |
| `TracerDevelop.agent.md` | Project-specific | Expert developer agent. Understands the TraceR architecture: Python sidecar, XSD schema, VS Code extension, AI pipeline, and the schema-driven surfaces contract. |
| `build.agent.md` | Project-specific | Builds the VS Code extension (`npm run build`). |
| `package.agent.md` | Project-specific | Packages the extension into a `.vsix` (delegates to `build`, then runs `vsce package`). |

### Prompts

Prompt files (`.prompt.md`) define automated multi-step workflows
with user approval gates. They execute a fixed sequence of
operations.

| File | Scope | Description |
| ---- | ----- | ----------- |
| `UpdateDocs.prompt.md` | Generic | Scans the current branch's changes and updates spec documents (SDD, HLRs, LLRs, Tests in Project.xml; SDP, SAR, User Manual, Developers Guide) to match the implemented work. |
| `PR.prompt.md` | Generic | Updates the SDP status, generates a release-note-quality commit message, commits, pushes, and opens a pull request. |
| `PrepRelease.prompt.md` | Generic | Prepares a release branch: bumps VERSION, triages Dependabot alerts, updates the Vulnerability Report, commits, pushes, opens a release PR. |
| `Release.prompt.md` | Generic | Creates a GitHub Release from the VERSION file with auto-generated categorised release notes. |

### Adding a new agent or prompt

1.  Create a `.agent.md` or `.prompt.md` file under
    `.github/agents/` or `.github/prompts/`.
2.  Add a YAML frontmatter header with `description`, `mode`
    (for prompts), and `tools`.
3.  Write the body as markdown instructions. Agents should
    describe their interaction flow and constraints. Prompts
    should describe numbered steps with approval gates.
4.  Mark the agent or prompt as **Generic** (works in any TraceR
    repository) or **Project-specific** (references this
    project's specific files/architecture).

Generic agents and prompts discover paths dynamically (tools
directory, test directory, doc directory) and check for the
existence of optional files (SDP.md, SAR.md, VR.md) before
attempting to update them.

## 15. `<plan>` — Phase 2.5 Retrofit Demo Payload

The `<plan>` element is the canonical example of a **payload that the
toolchain learns about purely through `<metadata><document>` and the
XSD**, with no edits to `render_doc.py`, `lint_project.py`, or the
VS Code extension's TypeScript code. It exists to keep the
schema-driven contract honest: if a future contributor accidentally
adds a hard-coded reference to one of the five "standard" payloads
(`<sdd>`, `<stp>`, `<hlrs>`, `<llrs>`, `<tests>`), the `<plan>` proof
in [test/doc/Project.xml](../test/doc/Project.xml) is what fails first.

### 14.1 XML skeleton

```xml
<plan version="0.1">
  Free-form markdown introduction text. Mixed content is
  allowed so a project can ship a hand-authored plan
  without committing to a fixed structure.

  <item id="P1" status="done">
    Markdown body for this plan item, including links,
    backticks, lists, etc.
  </item>
  <item id="P2" status="in-progress">
    ...
  </item>
</plan>
```

### 14.2 Element reference

| Element / attribute | Required | Notes |
|---------------------|----------|-------|
| `<plan>`            | optional on `<project>` | Mixed content; carries an optional `version` attribute. |
| `<plan>/@version`   | optional | Free-form string. Convention: bump alongside `Project.xml`'s `schema_version` only when the plan's schema-relevant shape changes. |
| `<plan>/<item>`     | optional, repeatable | Each item is a discrete plan entry. Mixed content (markdown). |
| `<item>/@id`        | optional | Stable identifier for the item. Convention: short uppercase prefix (`P1`, `M3`). |
| `<item>/@status`    | optional | Free-form, but the demo template recognises `done`, `in-progress`, `blocked`, and `planned`. |

### 14.3 What the toolchain does *not* know about `<plan>`

*   **`render_doc.py` has no `build_plan` function.** The renderer's
    `parse_project_to_dict` walks every direct child of `<project>`
    that it knows about (`<sdd>`, `<stp>`, `<hlrs>`, `<llrs>`,
    `<tests>`); `<plan>` is read by the template directly as raw
    markdown via the standard `xml_path` parser surface. New
    payloads with structured shape *do* need a `build_*` entry
    (§13 Step 2).
*   **The linter has no `<plan>`-specific rule.** The XSD validates
    its structural shape; semantic rules are absent because the
    payload is intentionally free-form.
*   **The VS Code extension contributes nothing `<plan>`-specific.**
    It picks up the document via `<metadata><document id="Plan">`
    and its `template=`/`output=` attributes, registers a
    `projectXml.render.Plan` command at runtime, and renders the
    template through the same code path used for the five standard
    documents.

### 14.4 Adopting `<plan>` (or removing it) for a real project

`<plan>` ships in [test/doc/Project.xml](../test/doc/Project.xml) as the
schema-driven retrofit's regression test, **not** in the canonical
[doc/Project.xml](../doc/Project.xml). A project that wants a planning
document can either:

*   **Use `<plan>` as-is.** Copy the `<metadata><document>` entry,
    the [tools/templates/Plan.md.j2](../tools/templates/Plan.md.j2)
    template, and a `<plan>` payload into the project's
    `Project.xml`. Lint and render work immediately.
*   **Define a richer payload.** Follow §13 Steps 2-4: extend the
    XSD, add a `build_<payload>` to the renderer, and author a
    template. The `<plan>` proof remains in the test fixture so
    the schema-driven contract continues to be exercised.

## 16. Linter Contract (`Finding`, `Findings.items[]`, `code` values)

The linter exposes two complementary surfaces. Earlier phases
contracted only the human-readable string lists; Phase 2.5 adds a
**structured** surface so downstream consumers (the VS Code
extension's Quick-Fix layer, AI surfaces, Marketplace dashboards)
can dispatch on a stable identifier rather than parsing localised
message text.

### 15.1 `Finding` and `Findings.to_dict()`

Each lint finding is a `Finding` record:

| Field      | Type                                      | Notes |
|------------|-------------------------------------------|-------|
| `severity` | `"error"` &#124; `"warning"` &#124; `"note"` | Determines how the CLI report and the VS Code Problems panel categorise the entry. |
| `message`  | `str`                                     | Canonical user-facing text. The CLI report and the legacy `errors` / `warnings` / `notes` lists hold this verbatim — pinned by HLR-043 cross-surface equivalence. |
| `code`     | `str` &#124; `None`                       | Stable machine identifier. Optional today; populated for the rules listed in §16.3. |

`Findings.to_dict()` is the JSON-RPC `lint` method's return shape:

```python
{
  "errors":   ["error message 1", "error message 2", ...],   # message-only, ordered
  "warnings": ["warning message 1", ...],                    # message-only, ordered
  "notes":    ["note message 1", ...],                       # message-only, ordered
  "items": [
    {"severity": "error",   "message": "...", "code": "broken-trace"},
    {"severity": "warning", "message": "...", "code": "no-test"},
    {"severity": "warning", "message": "...", "code": null},
    ...
  ],
}
```

The flat `errors` / `warnings` / `notes` lists remain byte-identical
to the pre-Phase-2.5 surface so cross-surface equivalence holds; the
new `items` array carries the structured records in declaration
order.

### 15.2 `LintFinding` (TypeScript mirror)

The VS Code extension declares the same shape as
`LintFinding` in [tools/vscode-project-xml/src/sidecar.ts](../tools/vscode-project-xml/src/sidecar.ts):

```ts
interface LintFinding {
  severity: 'error' | 'warning' | 'note';
  message: string;
  code: string | null;
}
```

The diagnostics provider keys its `vscode.Diagnostic.code` off
`LintFinding.code` so a Quick-Fix can match on the stable token
rather than the user-facing message; the badge index built by
[util/badges.ts](../tools/vscode-project-xml/src/util/badges.ts)
walks the same `items[]` array.

### 15.3 `code` values currently emitted

| `code`              | Severity   | Raised when |
|---------------------|------------|-------------|
| `broken-trace`      | `error`    | A `<traces>/<trace>` resolves to an id that does not exist (HLR / LLR / SDD §). |
| `id-format`         | `error`    | An `id="..."` attribute does not match the `HLR-NNN` / `LLR-XXX-NN` pattern, or two payloads share the same id. |
| `missing-template`  | `warning`  | A declared `<metadata><document>` entry's `template=` (explicit or convention) does not exist on disk. |
| `no-test`           | `warning`  | An HLR or LLR has no test that traces back to it (directly or via the LLR-fan-out). |

Findings raised before the `code` field was introduced (e.g.
`<project> is missing required @schema_version`) carry `code: null`.
New rules added in future phases will document their `code` here;
existing values are stable and may be relied on by external tooling.

## 17. UI Hint Vocabulary

[tools/project.xsd](../tools/project.xsd) publishes a small UI-hint
vocabulary in the namespace `urn:tracer:ui:v1` (prefix `ui:`). The
vocabulary is purely declarative — it is documented inside
`<xs:annotation><xs:appinfo>` blocks on the renderable complex types
and is **passed through unchanged by every XSD validator**. Phase
2.5b consumers (the JSON-RPC sidecar in
[tools/project_io.py](../tools/project_io.py) and the VS Code
extension) read these blocks to drive tree nodes, code lenses, and
form panels with no per-payload code. Adding a new payload to the
schema therefore requires only an XSD change and a Jinja2 template
— not a TypeScript edit.

The same namespace is also used by **per-element attributes**
(`ui:icon`, `ui:color`, `ui:group`) that a project author may attach
to individual `<hlr>` / `<llr>` / `<test>` / `<module>` elements via
the `xs:anyAttribute` declarations on those types. Those attributes
are decoration applied to one specific element; the appinfo
vocabulary documented below decorates the *complex type* and
applies uniformly to every instance.

### 16.1 Vocabulary

| Element | Where it appears | Purpose |
| ------- | ---------------- | ------- |
| `<ui:treeNode label="..." idAttr="..." group="..."/>` | At most once on a renderable complex type. | Declares that elements of this type appear in the Project Spec tree. `label` is a tiny expression of attribute / child references (`@id` for an attribute value, `@id — @name` for a templated label, a literal string for a fixed label, `name1\|name2` to fall back from one to another). `idAttr` names the attribute used to identify the element for reveal-in-XML. `group` is a slash-separated path under which siblings cluster (e.g. `requirements/hlrs`). |
| `<ui:form>...</ui:form>` | Up to once per renderable type. Wraps any number of `<ui:field>` children. | Declares the editable surface — what a Phase 3 form panel renders. |
| `<ui:field attr="..." kind="..." required="..."/>` | One per editable attribute, inside `<ui:form>`. | Describes a single attribute on the parent type. `kind` is one of `text` \| `textarea` \| `enum` \| `ref:HLR` \| `ref:LLR` \| `ref:SDD` \| `cdata`. `required="true"` marks the field as required in the form. |
| `<ui:field child="..." kind="..." required="..."/>` | One per editable child element, inside `<ui:form>`. | Same as the `attr=` variant but targets a named child element rather than an attribute. `kind="cdata"` is the canonical choice for markdown bodies. |
| `<ui:lens kind="..."/>` | Zero or more per renderable type. | Attaches a code lens above each instance of this type in `Project.xml`. Built-in `kind` values are `coverage` (HLR/LLR — counts downstream LLRs / tests) and `tracesCount` (HLR / Test — counts incoming `<traces>`). New kinds are added to the lens provider in lockstep with the schema. |
| `<ui:document/>` | On the `Document` complex type only. | Marks `<metadata><document>` entries as discoverable rendering targets. The `id`, `template`, and `output` attributes documented in [§2 — `<metadata>`](#2-metadata) are the contract; the `<ui:document/>` marker is just the affordance that the discovery surface walks. |

### 16.2 Example

The annotation on the `Hlr` complex type:

```xml
<xs:complexType name="Hlr">
  <xs:annotation>
    <xs:appinfo>
      <ui:treeNode label="@id — @name" idAttr="id" group="hlrs"/>
      <ui:form>
        <ui:field attr="id"      kind="text"    required="true"/>
        <ui:field attr="name"    kind="text"    required="true"/>
        <ui:field child="text"   kind="cdata"/>
        <ui:field child="traces" kind="ref:SDD"/>
      </ui:form>
      <ui:lens kind="coverage"/>
      <ui:lens kind="tracesCount"/>
    </xs:appinfo>
  </xs:annotation>
  <xs:sequence>
    <xs:element name="text"   type="MdText" minOccurs="0"/>
    <xs:element name="traces" type="Traces" minOccurs="0"/>
  </xs:sequence>
  <xs:attribute name="id"   type="HlrId"          use="required"/>
  <xs:attribute name="name" type="NonEmptyString" use="required"/>
  <xs:anyAttribute namespace="urn:tracer:ui:v1" processContents="skip"/>
</xs:complexType>
```

declares that every `<hlr>`:

* Appears in the Project Spec tree under the `hlrs` group, labelled
  `HLR-001 — name of the requirement`, identified by its `id`
  attribute for reveal-in-XML.
* Edits as a four-field form: text id, text name, CDATA-wrapped
  markdown body, and a list of `<traces><trace target="SDD" .../>`
  references.
* Carries two code lenses — one summarising downstream LLR / test
  coverage, one counting incoming traces.

### 16.3 Currently annotated types

The vocabulary is published on every renderable type in
[tools/project.xsd](../tools/project.xsd):

| Type      | `ui:treeNode` group | Lenses                           |
| --------- | ------------------- | -------------------------------- |
| `Document` | (no tree node — `ui:document` marker only) | — |
| `SddModule` | `sdd`              | —                                |
| `Hlr`     | `hlrs`              | `coverage`, `tracesCount`        |
| `Llr`     | `llrs`              | `coverage`                       |
| `Test`    | `tests`             | `tracesCount`                    |
| `StpFixture` | (form only — no tree node)                        | — |
| `TestFile`   | (form only — no tree node)                        | — |
| `Plan`    | `plan`              | —                                |
| `Plan/item` | `plan/items`      | —                                |

Phase 4 added `<ui:form>` annotations to `StpFixture` and `TestFile`
so the form panel's add commands (`Project Spec: Add STP Fixture`,
`Project Spec: Add Test File`) can derive a JSON Schema for them.
Neither type carries a `<ui:treeNode>` annotation today; STP
fixtures and test files surface in the tree only as the parents of
their children (artefacts and tests respectively), so they have no
standalone tree leaf to decorate.

### 16.4 Stability

The vocabulary's element and attribute names listed in §17.1 are
**stable** for any consumer reading the XSD. New `ui:lens` kinds and
new `ui:field` `kind=` values may be added; existing values will not
change meaning.

### 16.5 Consumer status (Phase 2.5b)

The VS Code extension consumes the vocabulary at four seams:

| Seam | Hint key | Slice |
|---|---|---|
| `ProjectSpecProvider.buildGenericPayloadNodes` — auto-projects any uncovered `ui:treeNode`-bearing payload | `tree_node` | E |
| `ProjectSpecProvider.buildHlrsNode` / `buildLlrsNode` / `buildTestsNode` / `buildSddNode` — leaf locators read `(tag, idAttr)` from the schema | `tree_node.id_attr`, `element` | H |
| `CoverageCodeLensProvider.getLensTargets` — inline coverage / tracesCount lenses scan whichever elements declare a supported lens | `lenses[].kind`, `tree_node.id_attr`, `element` | F |
| `LintDiagnosticsProvider` (via `buildIdScanRegistryFromHints`) — diagnostic ranges resolve any payload's id tokens, not only `HLR-NNN` / `LLR-XXX-NN` | `tree_node.id_attr`, `element` | G |

Adding a new payload with a `<ui:treeNode/>` annotation in
`tools/project.xsd` therefore surfaces in the tree, lens, and
diagnostic surfaces with **no TypeScript edits**. Adding a `ui:lens
kind="coverage"` annotation (and registering the per-element related-
items handler in `RELATED_DISPATCH`) lights up inline coverage lenses
on the new payload too.

# Appendix A: Schema Reference for `Project.xml`

## 18. AI Pipeline Recording Mode

The AI pipeline supports an **opt-in recording mode** for capturing
model exchanges as fixture files that can be replayed deterministically
without a live model.

### Activation

Set the `TRACER_AI_RECORD_DIR` environment variable to a directory path
before running the pipeline:

```bash
export TRACER_AI_RECORD_DIR=test/fixtures/ai_recordings
```

When set, every schema-validated model response writes a paired fixture:

*   `<intent>_<timestamp>.bundle.json` — the grounding bundle (intent id,
    target spec, next-free IDs, schema excerpt, response schema).
*   `<intent>_<timestamp>.response.json` — the parsed model response.

When the variable is unset (the default), recording is a no-op.

### Fixture format

**Bundle** (`*.bundle.json`):
```json
{
  "intent": "draft.hlr",
  "target": { "type": "Hlr", "section": "1" },
  "next_free_ids": { "hlr": "HLR-042" },
  "schema_excerpt": "<section number=\"1\" .../>",
  "response_schema": {}
}
```

**Response** (`*.response.json`):
```json
{
  "name": "Widget Configuration",
  "text": "The system SHALL allow users to configure widget parameters.",
  "traces": [{ "target": "SDD", "ref": "3.1" }]
}
```

### Replay testing

`test/test_ai_integration.py` loads each curated fixture pair from
`test/fixtures/ai_recordings/`, calls the translator with the recorded
response, applies the resulting operations via `apply_edit`, and asserts
lint-clean output with expected elements and resolved placeholder
references.  No network access or model dependency is required.

The fixture directory ships with at least one pair per intent:
`draft.hlr`, `draft.llr`, `draft.test`, `draft.module`,
`expand.hlr_to_llrs`, `expand.llr_to_tests`, `suggest.traces`,
`review.item`, `gap.fix` (simple LLR, simple test, and full cascade).

# Appendix A: Schema Reference for `Project.xml`

This appendix is the canonical structural reference for
`doc/Project.xml`: every element, attribute, cardinality, and
renderer-side projection. The body of this guide (§11–§18) covers
authoring rules, the linter contract, the UI hint vocabulary, and
the AI recording mode that build on top of the schema. Any change to
[project.xsd](project.xsd) or [render_doc.py](render_doc.py) MUST be
mirrored in this appendix in the same commit.

## 1. Root Element

```xml
<project name="Valgrind Parser" short_name="vgp" schema_version="1.5">
  <metadata>...</metadata>
  <sdd>...</sdd>
  <stp>...</stp>
  <hlrs>...</hlrs>
  <llrs>...</llrs>
  <tests>...</tests>
</project>
```

| Attribute | Description |
| --------- | ----------- |
| `name` | Full project name. |
| `short_name` | Binary / package name. |
| `schema_version` | Version of *this* schema. Bump when the structure changes incompatibly. The current schema is `1.5`. |

The XSD's own `<xs:schema>` root carries a matching `version="1.5"`
attribute so the Phase 6 packaging script
([tools/vscode-project-xml/scripts/prepackage.js](../tools/vscode-project-xml/scripts/prepackage.js))
can pin the bundled tree's `dist/python/.bundle_version` file from a
single source of truth — keep the two in lockstep when bumping
either one.

The XSD root reserves the namespace prefix `ui` (`urn:tracer:ui:v1`)
for optional UI-only hints (icon, group, color) that consumers such
as the VS Code extension may attach to payload elements via
`ui:*` attributes. The core renderer ignores these attributes; they
are reserved so that a future hint registry can be added without
breaking existing files. The `urn:tracer:ui:v1` namespace is also
used by `<xs:appinfo>` blocks inside
[tools/project.xsd](../tools/project.xsd) to publish a per-element
UI hint vocabulary (`ui:treeNode`, `ui:form`, `ui:lens`,
`ui:document`); see [§17. UI Hint Vocabulary](#17-ui-hint-vocabulary).

Children may appear in any order; the renderer looks them up by tag.
The XSD declares `<project>`'s children with `xs:all`, so an
XSD-aware editor accepts any ordering at the top level. Inside a
payload (`<sdd>`, `<stp>`, etc.) the same `xs:all` rule applies to
direct children, but **list-bearing wrappers** like
`<sdd>/<modules>`, `<sdd>/<architecture>`, and `<sdd>/<modules>/<module>`
use `xs:sequence` and require their own children in the order shown
in this document. The most common cause of an XSD failure is
putting an SDD module's `<error_handling>` block *before* its
`<dependencies>` block (see §3.8).

## 2. `<metadata>`

Document-level metadata for each generated spec, plus derived counts.

```xml
<metadata>
  <document id="SDD"          title="..." source="doc/SDD.md"          version="..." date="..." author="..."/>
  <document id="HLRs"         title="..." source="doc/HLRs.md"         version="..." date="..." author="..."/>
  <document id="LLRs"         title="..." source="doc/LLRs.md"         version="..." date="..." author="..."/>
  <document id="STP"          title="..." source="doc/STP.md"          version="..." date="..." author="..."/>
  <document id="Traceability" title="..." source="doc/Traceability.md" version="..." date="..." author="..."/>
  <counts>
    <count name="hlrs"       value="45"/>
    <count name="llrs"       value="124"/>
    <count name="tests"      value="120"/>
    <count name="test_files" value="6"/>
  </counts>
</metadata>
```

The renderer selects which `<document>` block populates
`project.metadata` based on its command-line `METADATA_ID` argument
(`SDD`, `HLRs`, `LLRs`, `STP`, or `Traceability`). Every generated
document must have a matching `<document id="...">` entry.

### Optional `template` and `output` attributes (schema_version `1.3`+)

A `<document>` may carry two optional attributes that decouple the
document id from its rendering location:

| Attribute | Default (by convention) | Purpose |
| --------- | ----------------------- | ------- |
| `template` | `tools/templates/<id>.md.j2` | Repo-relative path to the Jinja2 template that renders this document. |
| `output`   | the `source` attribute       | Repo-relative path to the rendered Markdown file. |

When both are omitted, the conventional paths above apply, so
existing files do not need to be edited. When present, they let a
project:

*   Ship a generated document whose template lives outside
    `tools/templates/` (for example, in a subfolder per audience).
*   Render the same template to a different output path than the
    `source` attribute (for example, a per-version snapshot).
*   Add a brand-new generated document with **no Python or
    TypeScript edits**: declare it in `<metadata>`, drop a template
    at the path it points to, and the renderer, the linter, the
    VS Code tree, the per-document render commands, and the
    Markdown preview all pick it up automatically.

The sidecar's `list_documents` JSON-RPC method (see
[`tools/project_io.py`](../tools/project_io.py)) enumerates these
entries with the resolved `template` and `output` paths so consumers
can discover them at runtime.

`<count>` values are derived (informational); the canonical counts come
from counting the corresponding child elements at render time.

## 3. `<sdd>` — Software Design Document Payload

The `<sdd>` element is a **data-only payload** consumed by
[templates/SDD.md.j2](../tools/templates/SDD.md.j2). All standard SDD scaffolding
(section numbers, standard headings such as "Purpose of the Document",
boilerplate lead-in sentences, and the auto-generated "Document
Overview") lives in the template, *not* in the data. This lets the same
template render an SDD for any project that supplies a similarly-shaped
payload.

```xml
<sdd>
  <kind>command-line application</kind>
  <audience>developers, testers, and maintainers</audience>
  <scope>...</scope>
  <overview>...</overview>
  <definitions>...</definitions>
  <references>...</references>
  <architecture>...</architecture>
  <design_goals>...</design_goals>
  <modules>...</modules>
  <data_dictionary>...</data_dictionary>
  <traceability>...</traceability>
</sdd>
```

All children are optional unless noted. Every text body marked as
**markdown** below is emitted verbatim by the template; wrap it in
`<![CDATA[...]]>` whenever it contains backticks, angle brackets, or
ampersands.

### 3.1 `<kind>` and `<audience>`

Plain-text strings used in the §1.1 "Purpose of the Document" sentence
("This document provides a detailed design for the … *kind*. It is
intended for *audience* of the … software.").

### 3.2 `<scope>` — §1.2

```xml
<scope>
  <intro>describes the design of the source modules ...</intro>
  <file path="src/main.c">Application entry point ...</file>
  <file path="src/vgp.c">Streaming parser ...</file>
  <outro><![CDATA[It does not exhaustively describe ...]]></outro>
</scope>
```

| Element / Attribute | Description |
| ------------------- | ----------- |
| `<intro>` | Lead-in sentence ("This document `<intro>`:"). |
| `<file path>` | One bullet per in-scope file. Body is markdown. |
| `<outro>` | Optional trailing paragraph after the file list. |

### 3.3 `<overview>` — §1.3

```xml
<overview>
  <para><![CDATA[Markdown paragraph...]]></para>
  <para><![CDATA[Another paragraph...]]></para>
</overview>
```

### 3.4 `<definitions>` — §1.4

```xml
<definitions>
  <term name="SDD">Software Design Document</term>
  <term name="ctags"><![CDATA[Universal Ctags — used at runtime ...]]></term>
</definitions>
```

### 3.5 `<references>` — §1.5

```xml
<references>
  <ref><![CDATA[Valgrind User Manual: <https://...>]]></ref>
</references>
```

Each `<ref>` body is one markdown bullet.

### 3.6 `<architecture>` — §2.1

```xml
<architecture>
  <intro><![CDATA[`vgp` is a single executable composed of ...]]></intro>
  <component path="src/main.c"><![CDATA[Acts as the controller ...]]></component>
  <component path="src/vgp.c"><![CDATA[Implements the streaming parser ...]]></component>
  <flow>
    <intro>The runtime data flow is:</intro>
    <step><![CDATA[`main()` parses argv ...]]></step>
    <step><![CDATA[...]]></step>
  </flow>
</architecture>
```

| Element | Description |
| ------- | ----------- |
| `<intro>` | Optional lead-in for §2.1. |
| `<component path>` | One bulleted component description. Body is markdown. |
| `<flow>` | Optional ordered numbered list. Contains `<intro>` and one or more `<step>` children. |

### 3.7 `<design_goals>` — §2.2

```xml
<design_goals>
  <goal name="Streaming"><![CDATA[Process logs line-by-line ...]]></goal>
</design_goals>
```

### 3.8 `<modules>` — §3..N

One `<module>` per design unit. Each renders as its own top-level
section ("Detailed Design for *path-or-title*"). The XSD requires
the child elements **in the order shown below**:

1.  `<purpose>`
2.  `<responsibility>` (repeatable)
3.  `<interfaces>`
4.  `<data_structures>`
5.  `<functions>`
6.  `<algorithm>`
7.  `<dependencies>`
8.  `<error_handling>`

A module that swaps `<dependencies>` and `<error_handling>` is
the single most common XSD violation when authoring SDDs by hand.

```xml
<modules>
  <module path="src/main.c">
    <purpose><![CDATA[is the entry point for the `vgp` executable. ...]]></purpose>
    <responsibility><![CDATA[Define `main()`.]]></responsibility>
    <responsibility>...</responsibility>
    <interfaces title_suffix="(optional appended title text)">
      <prose><![CDATA[Optional free-form markdown above the interface list.]]></prose>
      <interface title="Command-Line Arguments"><![CDATA[markdown body]]></interface>
      <interface title="File System">...</interface>
    </interfaces>
    <data_structures><![CDATA[markdown body]]></data_structures>
    <functions>
      <intro>Optional lead-in.</intro>
      <function signature="void parse_command_line(int argc, char *argv[])"
                summary="optional one-line summary attribute">
        <purpose>Walk argv and populate the global app_config.</purpose>
        <pre>...</pre>
        <post>...</post>
        <returns>...</returns>
        <logic>
          <step><![CDATA[Iterate `argv[1..argc-1]`.]]></step>
        </logic>
        <notes>...</notes>
      </function>
      <group name="Helpers">
        <function signature="...">...</function>
      </group>
    </functions>
    <algorithm><![CDATA[markdown body]]></algorithm>
    <dependencies>
      <dep>...markdown bullet...</dep>
    </dependencies>
    <error_handling>
      <case name="Unknown option"><![CDATA[markdown body]]></case>
    </error_handling>
  </module>
</modules>
```

| `<module>` child | Renders as |
| ---------------- | ---------- |
| `<purpose>` | Lead sentence in §N.1 ("`<module>` *purpose-text*"). |
| `<responsibility>` (repeatable) | Bullet in §N.1. |
| `<interfaces>` | §N.2 "External Interfaces". Optional `title_suffix` attribute appends to the heading. Optional `<prose>` precedes the per-interface subsections. Each `<interface title>` becomes §N.2.k with the given title and verbatim markdown body. |
| `<data_structures>` | §N.3.1 "Key Data Structures". Body is markdown. |
| `<functions>` | §N.3.2 "Key Functions". Use bare `<function>` children for ungrouped entries; use `<group name="...">` to introduce a labelled subsection of related functions. |
| `<function>` verbose form | When `<purpose>` is present, the template emits a nested *Purpose / Pre-condition / Post-condition / Return Value / Logic (numbered) / Notes* bullet block. |
| `<function>` compact form | When `<purpose>` is absent, the template emits one bullet: "**`signature`** — *summary*", optional inline numbered logic steps, optional notes paragraph. The `summary` may be supplied as either an attribute or a `<summary>` child element. |
| `<algorithm>` | §N.3.3 "Parsing Strategy / Algorithm". |
| `<dependencies>` | §N.4 "Dependencies". Each `<dep>` child is one markdown bullet. |
| `<error_handling>` | §N.5 "Error Handling and Logging". Each `<case name>` becomes one bullet. |

A `<module>` with a `path` attribute renders its heading as
"Detailed Design for [path](../relpath)". A `<module>` with only a
`title` attribute renders as "Detailed Design for *title*".

### 3.9 `<data_dictionary>` — §N+1

```xml
<data_dictionary>
  <type name="AppConfig"
        header="inc/vgp.h"
        instance="app_config"
        instance_in="src/vgp.c"
        summary="Application-wide configuration ...">
    <field name="verbose"      type="bool" desc="-v flag"/>
    <field name="print_source" type="bool" desc="-s flag"/>
  </type>
  <constants header="inc/vgp.h">
    <constant name="MAX_LINE_LENGTH" value="4096" purpose="Bounded line buffer ..."/>
  </constants>
  <other><![CDATA[Optional trailing markdown for additional dictionary content.]]></other>
</data_dictionary>
```

`instance` and `instance_in` are optional; when present, the template
adds "instantiated as the global `<instance>` in [`<instance_in>`]".

The XSD permits multiple `<constants>` blocks (each optionally
labelled with its own `header` attribute) so that a project can group
constants by their owning header file. The current renderer and SDD
template only consume the **first** `<constants>` block; until the
renderer is extended to handle the list, keep all constants in one
block (or split per-header projects across types instead).

### 3.10 `<traceability>` — §N+2

```xml
<traceability>
  <theme name="CLI parsing"               sections="§3.2.1, §3.3.2"/>
  <theme name="Streaming parser dispatch" sections="§4.4"/>
</traceability>
```

This drives the SDD's own narrative traceability table. The full
matrix used by [doc/Traceability.md](../doc/Traceability.md) is built
from the `<traces>` blocks on each `<hlr>`, `<llr>`, and `<test>`
element (see §8).

## 4. `<stp>` — Software Test Plan Payload

The `<stp>` element is the data-only payload consumed by
[templates/STP.md.j2](../tools/templates/STP.md.j2). It supplies the prose for
STP §1, §2, §5, §6, and §7. STP §3 (Test Catalogue) and §4 (LLR
Coverage Matrix) are computed by the template directly from `<tests>`
and `<llrs>`, not from `<stp>`.

```xml
<stp>
  <introduction>
    <purpose><![CDATA[markdown]]></purpose>
    <scope><![CDATA[markdown]]></scope>
    <related>
      <doc><![CDATA[[SDD.md](SDD.md) — design]]></doc>
      <doc>...</doc>
    </related>
  </introduction>

  <strategy>
    <levels>
      <level name="Unit"
             source="[test/unit/](../test/unit/)"
             driver="cmocka group runners"
             style="White-box, per-function"/>
      <level name="Integration"
             source="..."
             driver="..."
             style="..."/>
    </levels>
    <framework><![CDATA[markdown]]></framework>
    <build_execution>
      <intro><![CDATA[All tests are built and executed by `make test`. The target:]]></intro>
      <step><![CDATA[Step 1 markdown.]]></step>
      <step><![CDATA[Step 2 markdown.]]></step>
      <outro><![CDATA[Optional trailing paragraph.]]></outro>
    </build_execution>
    <pass_fail>
      <criterion><![CDATA[markdown bullet 1]]></criterion>
      <criterion><![CDATA[markdown bullet 2]]></criterion>
    </pass_fail>
    <traceability_convention><![CDATA[markdown]]></traceability_convention>
  </strategy>

  <integration_environment>
    <intro><![CDATA[markdown]]></intro>
    <fixture name="`c_error_generator`"
             source="[test/integration/c_error_generator.c](../test/integration/c_error_generator.c)">
      <artefact key="valgrind_log" label="Generated Valgrind Log"
                path="`build/.../valgrind.log`"/>
      <artefact key="vgp_output"   label="Generated vgp Output"
                path="`build/.../vgp_output.log`"/>
    </fixture>
    <fixture name="..." source="...">
      <artefact key="..." label="..." path="..."/>*
    </fixture>
    <outro><![CDATA[Optional trailing paragraph.]]></outro>
  </integration_environment>

  <tooling>
    <tool name="`gcc`"
          required_for="Build of `vgp` and unit tests"
          notes="C99, with -fanalyzer, ..."/>
    <tool name="..." required_for="..." notes="..."/>
  </tooling>

  <maintenance><![CDATA[markdown]]></maintenance>
</stp>
```

| Element | Renders as |
| ------- | ---------- |
| `<introduction>` | §1 — `<purpose>` (§1.1), `<scope>` (§1.2), `<related>` (§1.3 list of `<doc>` markdown links). |
| `<strategy>` | §2 — `<levels>` table, `<framework>` paragraph, `<build_execution>` numbered list (`<intro>` + `<step>`* + optional `<outro>`), `<pass_fail>` bulleted list, and `<traceability_convention>` paragraph. |
| `<integration_environment>` | §5 — `<intro>`, fixture table, `<outro>`. |
| `<tooling>` | §6 — one row per `<tool>`. |
| `<maintenance>` | §7 — verbatim markdown. |

### 4.1 Fixture Artefacts (schema 1.1+)

Each `<fixture>` carries a `name` and `source` plus zero or more
`<artefact key="..." label="..." path="..."/>` children. The renderer
collects the **union of artefact keys across all fixtures** in
first-seen order; the STP template emits one column per key with the
header taken from the first fixture that defined that key's `label`.
A fixture missing a given key renders an em-dash in that column.

This makes the fixture table extensible per-project: add new `key`s
freely without touching the template or renderer.

## 5. `<hlrs>` — High-Level Requirements

```xml
<hlrs>
  <section number="2" title="Command-Line Interface and Application Lifecycle">
    <intro><![CDATA[Optional intro paragraph that appears before the first HLR in the section.]]></intro>
    <hlr id="HLR-002" name="Argument Parsing and Validation">
      <text><![CDATA[The application shall parse `argv` and shall: ...]]></text>
      <traces>
        <trace target="SDD" ref="3.2.1"/>
        <trace target="SDD" ref="3.3.2"/>
      </traces>
    </hlr>
  </section>
</hlrs>
```

| Element / Attribute | Description |
| ------------------- | ----------- |
| `<section number title>` | One per H2 in `HLRs.md`. |
| `<intro>` | Optional markdown paragraph between the section heading and its first HLR. |
| `<hlr id name>` | One per HLR. `id` is `HLR-NNN`; `name` is the short title. |
| `<text>` | The HLR `shall` clause, as markdown. |
| `<trace target="SDD" ref="3.2.1"/>` | Each link from this HLR to an SDD section (dotted id). Repeatable. |

The HLRs template emits `<a id="HLR-NNN"></a>` immediately before the
bullet for every HLR so other documents (notably
[doc/Traceability.md](../doc/Traceability.md)) can link to it.

## 6. `<llrs>` — Low-Level Requirements

```xml
<llrs>
  <function number="4"
            title="`parse_command_line` ([src/main.c](../src/main.c))"
            name="parse_command_line"
            source="src/main.c">
    <intro><![CDATA[Optional intro paragraph.]]></intro>
    <llr id="LLR-PCL-04">
      <text><![CDATA[`parse_command_line` shall handle the `-h` flag ...]]></text>
      <traces>
        <trace target="HLR" ref="HLR-003" name="Usage and Help Information Display"/>
        <trace target="HLR" ref="HLR-009" name="Application Exit Status"/>
      </traces>
    </llr>
  </function>
</llrs>
```

| Element / Attribute | Description |
| ------------------- | ----------- |
| `<function number title name source>` | One per H2 in `LLRs.md`. `name` is the bare function name; `source` is the file the function lives in; `title` may be markdown (function name + linked source path). |
| `<intro>` | Optional markdown paragraph before the first LLR in the function's section. |
| `<llr id>` | One per LLR. `id` is `LLR-XXX-NN` (function prefix + 2-digit sequence). The XXX mnemonic style is a project convention, not a renderer requirement. |
| `<text>` | Body of the LLR `shall` clause. |
| `<trace target="HLR" ref="HLR-NNN" name="..."/>` | Each link to an HLR; `name` is the HLR's short name copied through for convenience. |

The LLRs template emits `<a id="LLR-XXX-NN"></a>` immediately before
the bullet for every LLR.

## 7. `<tests>` — Test Sources

```xml
<tests>
  <file path="test/unit/vgp_core.c" role="unit" count="79">
    <header><![CDATA[Optional file-level comment block, verbatim.]]></header>
    <test name="test_initialize_parse_state_normal">
      <purpose><![CDATA[Verifies LLR-IPS-03: every ParseState field is reset...]]></purpose>
      <traces>
        <trace target="LLR" ref="LLR-IPS-03"/>
        <trace target="HLR" ref="HLR-038"/>
      </traces>
    </test>
  </file>
</tests>
```

| Element / Attribute | Description |
| ------------------- | ----------- |
| `<file path role count>` | One per `*.c` file under `test/`. `role` is a **project-defined free-form string** (e.g. `unit`, `runner+integration`, `integration-fixture`); the renderer treats it as opaque metadata. `count` is the number of `<test>` children — informational. |
| `<header>` | Optional file-level doc-comment, verbatim. |
| `<test name>` | One per `static void test_*(void **state)` in the file. |
| `<purpose>` | Doc-comment block immediately above the test, normalised to plain text. |
| `<trace target="LLR"\|"HLR" ref="..."/>` | Each link the test claims, taken from the IDs cited in its doc-comment. |

The STP template emits `<a id="<test_name>"></a>` in the catalogue row
for every test so other documents can link to individual tests.

## 8. Traceability Discovery

A renderer building the traceability matrix does **not** need any
input besides the `<traces>` blocks:

*   **SDD → HLR** — for each `<hlr>`, its `<trace target="SDD">`
    children name the SDD sections it implements.
*   **HLR → LLR** — for each `<llr>`, its `<trace target="HLR">`
    children name the HLRs it implements.
*   **LLR → Test** — for each `<test>`, its `<trace target="LLR">`
    children name the LLRs it verifies.
*   **HLR → Test (direct)** — `<test>`'s `<trace target="HLR">`
    children are direct HLR claims (used when a test verifies an HLR
    that has no specific bound LLR).

`render_doc.py` builds these relations once at load time and exposes
them on the `project` namespace so templates do not have to recompute
them. See **§9 Renderer Data Surface** below.

A complete forward matrix is built by composing the four relations. A
reverse matrix is built by inverting them. Coverage gaps (LLRs / HLRs
without any verifying test) are inferred from the same data and
reported in
[doc/Traceability.md §6](../doc/Traceability.md#6-coverage-gaps).

## 9. Renderer Data Surface

Templates receive a single `project` namespace. The values below are
all populated by [render_doc.py](../tools/render_doc.py); use them directly
rather than recomputing relations in Jinja.

| Name | Description |
| ---- | ----------- |
| `project.name`, `project.short_name`, `project.schema_version` | From the `<project>` root element. |
| `project.metadata` | The `<document>` block selected by the CLI `METADATA_ID` argument. |
| `project.counts` | Dict of `<count name=…>` values from `<metadata>`. |
| `project.sdd` | `<sdd>` payload as a nested namespace. |
| `project.stp` | `<stp>` payload as a nested namespace. |
| `project.hlrs` | List of `<section>` namespaces, each with `.hlrs[]`. |
| `project.llrs` | List of `<function>` group namespaces, each with `.llrs[]`. |
| `project.tests` | List of `<file>` namespaces, each with `.tests[]`. |
| `project.flat_hlrs` | Every HLR in declaration order, annotated with `section_number` and `section_title`. |
| `project.flat_llrs` | Every LLR in declaration order, annotated with `function_name` and `function_number`. |
| `project.flat_tests` | Every test, sorted by (file, name), each with `.file`, `.name`, `.purpose`, `.traces`. |
| `project.hlr_by_id`, `project.llr_by_id` | ID → namespace lookup. |
| `project.file_of_test` | Test name → owning source-file path. |
| `project.sdd_titles` | SDD section number → heading title; mirrors the SDD template's numbering scheme (used for SDD-anchor links). |
| `project.hlrs_by_sdd[ref]` | List of HLR ids that cite the SDD section `ref`. |
| `project.llrs_by_hlr[hid]` | List of LLR ids that implement HLR `hid`. |
| `project.tests_by_llr[lid]` | List of test names that verify LLR `lid`. |
| `project.tests_by_hlr[hid]` | List of test names that verify HLR `hid` directly. |
| `project.llrs_no_test` | LLRs with no `<test>` citing them. |
| `project.hlrs_no_test` | HLRs with neither a direct test nor any LLR-bound test in their chain. |

Custom Jinja filters registered by the renderer:

| Filter | Purpose |
| ------ | ------- |
| `gh_slug` | Approximates GitHub's heading-anchor slug rule (lowercase, strip punctuation, spaces → hyphens). Used by the Traceability template to link to SDD section headings. |

When extending the schema with a new payload root, add a corresponding
`build_*` function in `render_doc.py` and expose it on the returned
`SimpleNamespace` so templates can reach it by attribute access.

### 9.1 UI Hints Index (Phase 2.5b)

Out-of-band of the `project.*` namespace consumed by Jinja templates,
[tools/render_doc.py](../tools/render_doc.py) also exposes the
`<xs:appinfo>` UI vocabulary documented in [§17. UI Hint
Vocabulary](#16-ui-hint-vocabulary) as a JSON-serialisable index:

```python
from render_doc import parse_ui_hints_index
index = parse_ui_hints_index()  # reads tools/project.xsd by default
```

The index is keyed by complex-type name (e.g. `Hlr`, `Llr`, `Test`,
`SddModule`, `Document`, `Plan`, `Plan/item`). Each entry has the
shape:

```json
{
  "tree_node": {"label": "@id — @name",
                "id_attr": "id",
                "group":   "hlrs"} | null,
  "form":      [{"target": "id",
                 "kind":   "attr|child",
                 "field":  "text|textarea|enum|ref:HLR|ref:LLR|cdata",
                 "required": true|false}, ...],
  "lenses":    [{"kind": "coverage|tracesCount|..."}, ...],
  "document":  true|false,
  "element":   "hlr" | null
}
```

The `element` field (Slice D) records the lowercase XML element name
the type is bound to via `<xs:element name="X" type="Y">`. For inline
nested types like `Plan/item` the field carries the inline element
name (`"item"`). It lets consumers iterate the parsed tree without
hard-coding per-payload tag names.

The same index is exposed over the JSON-RPC sidecar:

* `ui_hints_index({xsd_path?})` — returns `{"ui_hints_index": {...}}`.
* `parse_to_json({...})` — embeds the same dict under the
  `_ui_hints_index` top-level key alongside the parsed project, so
  the VS Code extension fetches the parsed tree and the hint
  vocabulary in a single round trip. Slice D additionally embeds a
  `_nodes` top-level key — a `Record<typeName, ParsedNode[]>` mirror
  of the index that lists every element in `Project.xml` bound to a
  type carrying a `ui:treeNode` hint. Each `ParsedNode` is
  `{tag, attrs, ui, text}` (see [src/sidecar.ts](../tools/vscode-project-xml/src/sidecar.ts)
  for the TS shape). Inline types are scoped to children of their
  parent element so unrelated tags with the same local name are not
  pulled in.

Templates do **not** consume this index — it is dedicated to
non-Jinja consumers (the VS Code tree provider, lens provider,
locator, and Phase 3 form panels). Adding a new `<ui:lens>` `kind=`
or a new `<ui:field>` `kind=` requires a matching change in the
consumer (and a §17 update); the index walker itself accepts any
attribute set without further code changes.

## 10. Regeneration

The XML is the canonical source. After modifying any input, regenerate
the dependent markdown documents:

```bash
python3 tools/render_doc.py tools/templates/SDD.md.j2          SDD          --out doc/SDD.md
python3 tools/render_doc.py tools/templates/HLRs.md.j2         HLRs         --out doc/HLRs.md
python3 tools/render_doc.py tools/templates/LLRs.md.j2         LLRs         --out doc/LLRs.md
python3 tools/render_doc.py tools/templates/STP.md.j2          STP          --out doc/STP.md
python3 tools/render_doc.py tools/templates/Traceability.md.j2 Traceability --out doc/Traceability.md
```

Run `python3 tools/render_doc.py --help` for the full command-line
reference.

### 10.1 Bootstrapping a new project

For a brand-new project the renderer can create the initial
`doc/Project.xml` skeleton:

```bash
python3 tools/render_doc.py --init \
    --name "My Product" --short-name myprod --author "Jane Doe"
```

This writes `doc/Project.xml` (a valid schema-1.1 skeleton with empty
`<sdd>`, `<stp>`, `<hlrs>`, `<llrs>`, `<tests>` payloads and a fully
populated `<metadata>` block). The file is refused if it already
exists; pass `--force` to overwrite. From there the workflow is to
start populating `doc/Project.xml` and regenerating the five spec
documents above.

### 10.2 Validation

Two validators ship alongside the schema:

*   **[tools/project.xsd](../tools/project.xsd)** — XML Schema describing the
    structural shape (element nesting, attribute names and required-ness,
    `HLR-NNN` / `LLR-XXX-NN` id formats, trace target enum). The
    canonical `doc/Project.xml` references it via
    `xsi:noNamespaceSchemaLocation="../tools/project.xsd"`, so any
    XSD-aware editor (VS Code's Red Hat XML extension, IntelliJ,
    oXygen) provides autocomplete and inline error squigglies for free.
*   **[tools/lint_project.py](../tools/lint_project.py)** — semantic checker
    that resolves every `<trace>` against the actual HLR/LLR/test ids,
    flags duplicates, warns about coverage gaps (LLRs/HLRs with no
    verifying test), and validates against the XSD when `lxml` or
    `xmllint` is available.

Run both via the Make target:

```bash
make validate-xml
```

Or directly:

```bash
python3 tools/lint_project.py                # all checks, warnings on
python3 tools/lint_project.py --no-warnings  # errors only (for CI)
```

Exit status is non-zero only on **errors** (broken trace refs, bad id
formats, duplicate ids, malformed XML, XSD violations). Coverage gaps
are warnings. The simplest round-trip parse check remains:

```python
import xml.etree.ElementTree as ET
ET.parse("doc/Project.xml")  # raises on malformed XML
```
