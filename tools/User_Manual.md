# TraceR User Manual

## Overview

TraceR helps you keep your project's design, requirements, and tests
honest — and traceable to each other — without juggling a stack of
hand-edited Word documents.

You write your project once, in a single file (`doc/Project.xml`),
and TraceR turns it into a fully linked set of specification
documents:

* **Software Design Document** (`doc/SDD.md`)
* **High-Level Requirements** (`doc/HLRs.md`)
* **Low-Level Requirements** (`doc/LLRs.md`)
* **Software Test Plan** (`doc/STP.md`)
* **Traceability Matrix** (`doc/Traceability.md`) — shows which
  requirement maps to which test, in both directions.

You get TraceR in two flavors, and both work from the same project
file:

1.  **A VS Code extension.** This is the day-to-day experience: a
    side panel that lists your requirements and tests, instant
    error checking, click-through links between everything,
    fill-in-the-blank forms for adding new items, and (optionally)
    AI assistance for drafting and reviewing.
2.  **A small set of command-line tools.** The same engine,
    runnable from a terminal or a build script. Useful for
    continuous-integration checks, one-off renders, or just
    working without an editor open.

If your project lints clean from the command line, it lints clean
in the editor — and vice versa.

## Installation

### What you need

* **Python 3.10 or newer**, with the `Jinja2` package installed.
* *Optional but recommended:* the `lxml` Python package (or the
  `xmllint` tool from `libxml2-utils`). These give you stricter
  validation. TraceR still works without them.
* For the VS Code extension: **VS Code 1.90 or newer** and **Node.js
  20 LTS**.

On most systems:

```bash
# Python packages
pip install jinja2 lxml

# (Optional) xmllint on Debian/Ubuntu
sudo apt install libxml2-utils
```

### Installing the VS Code extension

The shipped `.vsix` file already includes a copy of TraceR's Python
tools, so the extension works in any folder as long as Python 3.10+
is on your PATH.

1.  Install the prerequisite XML extension from Red Hat:

    ```bash
    code --install-extension redhat.vscode-xml
    ```

2.  Install TraceR from the `.vsix`:

    ```bash
    code --install-extension tracer-project-xml-<version>.vsix
    ```

The first time you open an empty folder, the extension can scaffold
the `tools/` directory for you (see *Getting Started* below), so
your teammates and your CI server can use the command-line tools
even if they don't have the extension installed.

### Installing the command-line tools by themselves

If you only want the command-line tools (for example, on a build
server), clone the repository and use a Python virtual environment:

```bash
git clone https://github.com/racerxr650r/TraceR.git
cd TraceR
python3 -m venv .venv && source .venv/bin/activate
pip install jinja2 lxml
```

You don't need the extension for the CLI, and you don't need the
CLI for the extension.

## Getting Started

### Starting a brand-new project

The easiest path is the **Get Started with Project Spec** walkthrough
inside VS Code:

1.  Open an empty folder in VS Code.
2.  Pick **Help → Get Started → Project Spec**, or press
    `Ctrl/Cmd+Shift+P` and run **Project Spec: Show Walkthrough**.
3.  Follow the seven cards in order. They will:
    1.  Drop the `tools/` directory into your folder.
    2.  Create a starter `doc/Project.xml`.
    3.  Walk you through adding your first high-level requirement.
    4.  …and your first low-level requirement.
    5.  …and your first test.
    6.  Run the lint check.
    7.  Render all five spec documents.

By the end you have a working project and all five spec documents
under `doc/`.

If you'd rather do it from the command line:

```bash
python3 tools/render_doc.py --init \
    --name "MyProject" \
    --short-name MP \
    --author "Me" \
    --xml doc/Project.xml

python3 tools/render_doc.py --all   # renders all five spec docs
python3 tools/lint_project.py        # checks for problems
```

### Editing an existing project

Open the folder in VS Code. The **TraceR** icon appears in
the activity bar (the strip on the far left). Click it to open the
tree view. From there you can:

* **Click any item** in the tree to open the edit form for that
  item, with its current values pre-populated.
* **Right-click a category** (HLRs, LLRs, Tests, …) and pick
  **Add HLR**, **Add LLR**, **Add Test**, etc. A form appears;
  fill it in and submit.
* **Right-click any item** and pick **Reveal in Project.xml** to
  jump to its source, or **Edit in Form…** to open the form.
* Use the **dropdown menu** (⋯ in the view title bar) to Refresh,
  Run Linter, Render All Documents, or Resolve Merge Conflicts.
* **Right-click any item** and use the AI menu (when AI is
  available) to draft, expand, review, or fix coverage gaps.
* **Save** the file. TraceR re-checks for problems and updates the
  side preview.

### What lives where

```
doc/
  Project.xml         <- the one file you actually edit
  SDD.md              <- generated, do not edit
  HLRs.md             <- generated, do not edit
  LLRs.md             <- generated, do not edit
  STP.md              <- generated, do not edit
  Traceability.md     <- generated, do not edit
tools/
  User_Manual.md      <- this document
  Developers_Guide.md <- for template authors and contributors
  …command-line tools and templates…
```

The five generated `*.md` files are rebuilt from `Project.xml`
every time you render. If you edit them by hand your changes will
be overwritten — change `Project.xml` instead.

### Using Copilot Chat to manage your project

When **GitHub Copilot Chat** is active in VS Code, you can describe
what you want in plain English and Copilot will handle the
`Project.xml` edits, traceability links, and document regeneration
for you.

The key is the TraceR skill file installed at
`.github/skills/tracer/SKILL.md` during project initialization. That
file teaches Copilot the TraceR rules — what is generated vs.
hand-authored, how IDs work, how traces connect requirements to tests,
and which documents to regenerate after each edit. If your project
was initialized before this file existed, create it by running
**Project Spec: Initialize Project.xml** again (it will not overwrite
your existing `Project.xml` or `PVD.md`).

Open the Chat panel (`Ctrl/Cmd+Alt+I`) and try prompts like the ones
below.

#### Drafting and scaffolding

```
Read doc/PVD.md and suggest HLRs for any sections that have no
requirements yet.
```

```
Generate a starter SDD section for the <module name> module.
Trace it to the most relevant HLRs.
```

```
Draft a Product Vision Document for a <short description>.
The target users are <who> and the key goal is <what>.
```

#### Adding requirements

```
Add an HLR for <feature>. Trace it to SDD section <N.N>.
```

```
Add an HLR for <feature> and suggest two or three LLRs that
implement it. Include traces for all of them.
```

```
Add three LLRs under HLR-<NNN> covering <topic 1>, <topic 2>,
and <topic 3>.
```

```
HLR-<NNN> is too vague to test. Suggest improved wording that is
specific and traceable.
```

#### Adding tests and closing coverage gaps

```
Add a test entry for <function_name>() in test/<file>.c.
It should trace to LLR-<XXX-NN>.
```

```
Show me all LLRs that have no test. For each one, suggest a test
name and a one-line purpose.
```

```
HLR-<NNN> is showing as untested in Traceability.md. Walk me
through what is missing and how to fix it.
```

```
Check my test coverage and list every requirement that has a gap.
```

#### Generating and updating documents

```
Regenerate all five spec documents from the current Project.xml.
```

```
Update the Traceability matrix after I added two new tests.
```

```
Preview the HLRs document without writing it to disk.
```

#### Review and consistency

```
Review Project.xml for broken traces or ID format problems.
```

```
Are there any LLRs that trace to an HLR outside their expected
section? Flag anything that looks misplaced.
```

```
The linter is reporting a warning on HLR-<NNN>. What does it
mean and how do I fix it?
```

```
Trace HLR-<NNN> all the way to its tests and show me the full
chain.
```

> **Tip:** The more specific you are, the better the result. Mentioning
> the exact HLR or LLR ID, the function name, or the test file path
> helps Copilot locate the right node in `Project.xml` and produce a
> clean, ready-to-paste edit rather than a generic suggestion.

## VS Code Extension

When the extension is active, you get the following surfaces. They
all stay in sync with `doc/Project.xml`.

### The TraceR tree

The **TraceR** icon in the activity bar opens a structural tree view
of your project, grouped by category:

* **HLRs** — High-Level Requirements, grouped by section.
* **LLRs** — Low-Level Requirements, grouped by function or module.
* **Tests** — your tests, grouped by source file.
* **SDD** — design document modules.
* **STP** — Software Test Plan fixtures.

Each group shows a parenthetical count (e.g. "HLRs (12)") and each
item shows its ID and name. A small badge appears on items with
problems:

* ❌ — at least one error refers to this item.
* ⚠ — at least one warning refers to this item.

Badges propagate upward: if any child item has a badge, its parent
group node also shows the worst-severity badge so you can spot
problems without expanding every group.

![Tree view with all groups collapsed](../images/screenshots/tree-view-collapsed.png)

Click any group to expand it and reveal its children:

![Tree view with HLRs group expanded](../images/screenshots/tree-view-expanded.png)

#### Dropdown menu

The view title bar includes a **Refresh** icon button and a **⋯**
overflow menu with five commands:

| Command | What it does |
|---------|-------------|
| **Refresh** | Reload the tree from `Project.xml` |
| **Run Linter** | Lint and update diagnostics |
| **Render All Documents** | Regenerate all five spec documents to disk |
| **Resolve Merge Conflicts** | Launch the structural merge resolver |
| **Initialize Project.xml** | Bootstrap a new project from scratch |

These are the same commands available in the Command Palette under
the **Project Spec:** prefix, but accessible directly from the tree
view without leaving the side panel.

#### Context menus — leaf items

Right-clicking any leaf item (an HLR, LLR, test, or module) shows
context-sensitive commands:

* **Project Spec: Reveal in Project.xml** — opens `Project.xml` in the
  editor and selects the element.
* **Project Spec: Edit in Form…** — opens the schema-driven edit form
  (see [Forms](#forms) below).

![Right-click context menu on an HLR leaf](../images/screenshots/context-menu-leaf.png)

#### Context menus — group nodes

Right-clicking a group node (HLRs, LLRs, Tests, SDD) shows an
**Add…** command that opens a pre-populated form for adding a new item
to that category:

![Right-click context menu on the HLRs group](../images/screenshots/context-menu-group.png)

Click any item to open it in the edit form, or right-click it and pick
**Reveal in Project.xml** to jump to its XML source:

![Project.xml with HLR element selected after Reveal](../images/screenshots/reveal-in-xml.png)

### Problems and the status bar

TraceR re-checks `Project.xml` every time you save. Anything it
doesn't like shows up in the **Problems** panel, anchored to the
exact line. Common problems include:

* A trace that points at a requirement or section ID that doesn't
  exist.
* An ID that doesn't follow the expected pattern (for example,
  `HLR-1` instead of `HLR-001`).
* A requirement that has no test.
* A document declared in the metadata but missing its template
  file.

The status bar at the bottom of the window shows a live **TraceR**
lint summary (e.g. "TraceR: 0 errors / 3 warnings"). The status bar
item is colour-coded:

* **Red background** with the ✖ icon when there are errors.
* **Yellow background** with the ⚠ icon when there are warnings
  but no errors.
* **Green ✓** when everything is clean.

Click the status bar item to jump to the Problems panel.

![Status bar showing TraceR lint summary](../images/screenshots/status-bar.png)

![Problems panel with lint diagnostics](../images/screenshots/problems-panel.png)

### Quick Fixes

When the cursor is on a problem, a lightbulb appears. Quick Fixes
include:

| Problem                | What the Quick Fix does                          |
| ---------------------- | ------------------------------------------------ |
| Broken trace           | Replace the bad ID using a picker.               |
| Wrong ID format        | Renumber as the next free ID.                    |
| Missing template file  | Stub a starter template for you.                 |
| Requirement with no test | Insert a starter `<test>` block.               |

### Inline coverage hints

With `doc/Project.xml` open in the editor, look just above each
`<hlr>`, `<llr>`, and `<test>` element. TraceR draws a row of small
clickable hints — things like *"2 LLRs / 4 tests"* over a
high-level requirement, or *"Traced from HLR-001"* over a test.

![Coverage code lenses on HLR elements](../images/screenshots/code-lenses.png)

To use them:

1.  Open `doc/Project.xml`.
2.  Scroll to any HLR, LLR, or test. The hints appear as a thin
    grey line above the element.
3.  **Click a hint** to jump to the related items. If there's only
    one related item, the cursor jumps straight there. If there
    are several, a quick-pick list opens — pick one to jump.

What each hint means:

* **Coverage** (on `<hlr>` and `<llr>`) — counts how many
  downstream items trace back to this requirement (LLRs and tests
  for an HLR; tests for an LLR). Click to pick which downstream
  item to open.
* **Traces count** (on `<hlr>` and `<test>`) — counts incoming
  traces from the items above. Click to pick which upstream item
  to open.

The hints update automatically as you save. If you don't see them,
check that **Editor: Code Lens** is enabled in VS Code Settings
(`editor.codeLens` = `true`) — inline hints are implemented as
VS Code code lenses.

### Render and preview

TraceR ships two rendering commands. Both live under the
**Project Spec:** prefix in the Command Palette
(`Ctrl/Cmd+Shift+P`).

#### Preview a single document (no files written)

Use this while you're iterating — nothing on disk changes.

1.  Make sure you have a workspace open with `doc/Project.xml` in
    it. (Either of the two surfaces below also works without
    `Project.xml` open in the editor, but the workspace itself
    must contain it.)
2.  Open the Command Palette: **View → Command Palette…**, or
    `Ctrl+Shift+P` (Linux/Windows) / `Cmd+Shift+P` (macOS).
3.  Type **`Project Spec: Render & Preview`** and press Enter.
4.  A picker drops down titled *"Project Spec: render & preview"*
    with the placeholder *"Choose a generated document to
    preview"*. It lists every generated document declared in
    `Project.xml`, showing:
    * the document id on the left (e.g. `SDD`, `HLRs`, `LLRs`,
      `STP`, `Traceability`),
    * the document title in the middle,
    * the on-disk path it would write to on the right.
5.  Pick one and press Enter.
6.  TraceR renders the document in memory and opens it in VS
    Code's built-in Markdown preview, in a pane to the right of
    the editor. The address bar of that preview shows a
    `tracer-preview:` URL — that's how you can tell it's the
    in-memory render, not the file on disk.
7.  **Tip:** with **`projectXml.previewOnSave`** turned on (the
    default), every time you save `doc/Project.xml` the open
    preview re-renders against the new content. Just keep the
    preview open in a side pane while you edit.

If you want to refresh the preview manually without saving, click
the small refresh icon (↻) at the top of the preview tab, or
re-run **Render & Preview** for the same document.

If a render fails (for example, a template has an error), TraceR
shows the error in a notification toast and writes the details to
the **Project Spec** output channel — see *Where to look when
things go wrong* below.

#### Write all five documents to disk

Use this before you commit, so the rendered Markdown files in your
diff match the current `Project.xml`.

1.  Open the Command Palette (`Ctrl/Cmd+Shift+P`).
2.  Type **`Project Spec: Render All Documents`** and press Enter.
3.  A progress notification appears in the bottom-right of the
    window saying *"Project Spec: rendering all documents"*. It
    cycles through each document id as it renders.
4.  When the render finishes:
    * On success, you'll see *"Project Spec: rendered N documents."*
      The five files under `doc/` (`SDD.md`, `HLRs.md`, `LLRs.md`,
      `STP.md`, `Traceability.md`) are now refreshed on disk.
    * If anything failed, you'll see an error notification listing
      which document ids failed. Click the **Project Spec** entry
      in the *Output* panel (**View → Output**, then pick
      *Project Spec* from the dropdown) for the line-by-line log.
5.  Open the Source Control view to see the rendered files in your
    diff alongside `Project.xml` and commit them together.

There is no menu shortcut for these commands by default. If you
use them often, bind them to a keyboard shortcut via **File →
Preferences → Keyboard Shortcuts** (search for
`projectXml.renderAndPreview` or `projectXml.renderAll`).

#### Where to look when things go wrong

* **Notification toasts** appear in the bottom-right corner. Click
  one to see the full message.
* The **Project Spec** output channel logs every render
  attempt — open it via **View → Output**, then pick
  *Project Spec* from the dropdown on the right side of the panel.
* The **Problems panel** (**View → Problems**, or
  `Ctrl/Cmd+Shift+M`) shows lint findings against `Project.xml`
  itself; if a render fails because the project file is invalid,
  the underlying problem is usually listed there.

### Forms

Forms are the easiest way to add or edit items. Each form is
generated from the schema, so the fields you see always match what
the project file expects.

![HLR edit form with coverage hints](../images/screenshots/edit-form.png)

**To add a new item:**

1.  Open the **TraceR** view in the activity bar.
2.  Right-click the appropriate category in the tree and pick the
    matching **Add…** command. The available commands are:

    | Right-click on…   | Pick…                  | Adds                                  |
    | ----------------- | ---------------------- | ------------------------------------- |
    | **HLRs**          | **Add HLR…**           | A new high-level requirement          |
    | **LLRs**          | **Add LLR…**           | A new low-level requirement           |
    | **SDD**           | **Add SDD Module…**    | A new design module                   |
    | **STP**           | **Add STP Fixture…**   | A new test fixture                    |
    | **Tests**         | **Add Test File…**     | A new source file containing tests    |
    | a test file       | **Add Test…**          | A new test inside that file           |

    The same commands are also available from the Command Palette
    under their **Project Spec:** prefix, or via the tree view's
    dropdown menu.

3.  A form panel opens beside the editor. The first ID field is
    pre-filled with the next free ID (e.g. `HLR-007`,
    `LLR-MOD-03`).
4.  Fill in the fields. Required fields are marked. Markdown is
    supported in description fields.
5.  Click **Submit**. TraceR validates the change against the
    schema and runs the linter:
    * If everything's clean, the change is written to
      `Project.xml`, comments and ordering are preserved, and the
      tree refreshes.
    * If the result has errors, nothing is written. The form shows
      what went wrong so you can correct it.

If `Project.xml` is open with unsaved changes when you submit,
TraceR will ask you to save or discard those changes first —
adding a new item only works against a clean copy on disk.

**To edit an existing item with a form:**

1.  Click the item in the tree (single-click), or right-click it
    and pick **Edit in Form…**.
2.  The form opens, pre-populated with the current values.

The edit form includes these additional features:

* **Traces** — the item's existing `<traces>` are pre-populated in
  the form as editable rows. Each row shows the target type (HLR,
  LLR, SDD) and ref. Click the **Add Trace** button at the bottom
  of the traces section to add a new trace reference.
* **Coverage hints** — below the form fields, a coverage sidebar
  shows related items (e.g. downstream LLRs and tests for an HLR,
  or upstream HLRs for an LLR). Test entries include a sublabel
  showing the parent test file path. Click any coverage item to
  reveal it in `Project.xml`.

Submit to apply the change (validated and lint-checked just like
an add).

**Renaming an item's ID:** When you change an item's `@id` field in
the edit form, TraceR automatically updates every trace reference
that pointed to the old ID. You don't need to hunt for stale
cross-references — they cascade automatically.

### AI assistance (optional)

When a language model is available in VS Code and AI is enabled in
settings, you also get:

* The **`@projectspec`** chat participant, with slash commands like
  `/draft-hlr`, `/draft-llr`, `/draft-test`, `/expand`, `/review`,
  `/suggest-traces`, and `/gap-fill`.
* **`/gap-fill` cascading creation** — when a gap requires not just a
  test but also an upstream LLR, HLR, or module, the AI creates the
  full chain in one action. You review and accept a single diff that
  adds all the needed elements with correct cross-references.
* **AI items** in the right-click menu of every Project Spec tree
  node, so you can draft or expand from the tree itself.
* A diff-preview-and-apply step on every AI suggestion: nothing
  ever lands in your file without you accepting the diff first.
  Each accepted change is also backed up to `.edit_doc/backups/`
  for safety.

If no model is available — or if you turn AI off — every AI
surface disappears cleanly and the rest of the extension keeps
working exactly as before.

### Resolving merge conflicts

If two branches edit `Project.xml` and Git can't merge them on its
own, run **Project Spec: Resolve Merge Conflicts**. TraceR will:

1.  Auto-merge the disjoint changes (different sections, different
    requirements added on each side, different traces, …).
2.  Open VS Code's three-way merge editor on whatever's left.
3.  When AI is on, badge per-region suggestions with a ✨.

TraceR never writes the merged file automatically — you commit
the result from the merge editor like any other merge.

### Useful settings

These live under **Settings → Extensions → Project Spec**:

| Setting                         | Default            | What it does                                                                 |
| ------------------------------- | ------------------ | ---------------------------------------------------------------------------- |
| `projectXml.xmlPath`            | `doc/Project.xml`  | Path to the project file.                                                    |
| `projectXml.autoLintOnChange`   | `true`             | Re-check on save.                                                            |
| `projectXml.warningsAsErrors`   | `false`            | Treat warnings as errors in Problems and the status bar.                     |
| `projectXml.previewOnSave`      | `true`             | Refresh the side preview when the file is saved.                             |
| `projectXml.showCoverageBadges` | `true`             | Show ❌ / ⚠ badges on tree items.                                            |
| `projectXml.ai.enabled`         | `true`             | Turn AI surfaces on or off.                                                  |
| `projectXml.ai.modelFamily`     | _(empty)_          | Optional preferred model family (e.g. `gpt-4o`).                             |
| `projectXml.ai.autoApplyValidated` | `false`         | Skip the diff-preview step for AI patches that already passed validation.    |
| `projectXml.merge.enabled`      | `true`             | Enable structural merge for `Project.xml`.                                   |
| `projectXml.merge.aiResidualResolution` | `true`     | Use AI to suggest resolutions for residual merge conflicts.                  |

## Command Line Tools

The tools live in the `tools/` directory. Every script accepts
`--help`. Here are the ones you'll use most often.

### `render_doc.py` — generate the spec documents

```bash
# Regenerate every spec document.
python3 tools/render_doc.py --all

# Regenerate just one document by name.
python3 tools/render_doc.py tools/templates/HLRs.md.j2 HLRs --out doc/HLRs.md

# Bootstrap a brand-new project (creates Project.xml, PVD, SAR, VR, SDP).
python3 tools/render_doc.py --init \
    --name "MyProject" --short-name MP --author "Me" \
    --xml doc/Project.xml

# Generate a single hand-authored document from its template.
python3 tools/render_doc.py --generate-doc SAR \
    --name "MyProject" --short-name MP

# Generate all hand-authored document templates.
python3 tools/render_doc.py --generate-doc all \
    --name "MyProject" --short-name MP
```

The `--generate-doc` command generates hand-authored documents
(PVD, SAR, VR, SDP) from templates under `tools/templates/`. If a
target file already exists, it prompts before overwriting (use
`--force` to skip the prompt). The `--init` command generates all
four automatically alongside `Project.xml`.

### `lint_project.py` — check for problems

```bash
python3 tools/lint_project.py
```

Exits with status `0` if everything is clean, non-zero if there
are any errors. Warnings are reported but don't fail the run
unless you pass `--warnings-as-errors`.

Notable lint warnings include:

* **`mixed-prefix`** — a function's LLRs use two different id
  prefixes (e.g. both `LLR-PPD-*` and `LLR-PRJP-*`). This usually
  means AI-generated ids didn't adopt the established naming
  convention. Rename the outliers to match.

### `Makefile` — common tasks

The `tools/Makefile` ties the most common operations together:

```bash
make -C tools render        # re-render and check for drift
make -C tools lint          # run the linter
make -C tools validate-xml  # validate against the schema only
make -C tools test          # run the test suite
make -C tools ci            # render + lint + validate + test
```

This is what you'd normally wire into a continuous-integration
workflow.

## Example Workflow

Here's what a typical day with TraceR looks like once a project is
already set up.

1.  **Pull and open the project.**

    ```bash
    git pull
    code .
    ```

    The **TraceR** icon appears in the activity bar.

2.  **Add a new requirement.** Right-click **HLRs** in the tree →
    **Add HLR**. The form pre-fills the next free ID. Fill in the
    name and description, then submit. TraceR validates the change
    and saves the file.

3.  **Break it down into low-level requirements.** Right-click the
    new HLR. If AI is available, pick **Expand with AI** — TraceR
    drafts a few candidate LLRs, with traces back to the parent
    HLR pre-filled. Review the diff and accept what looks right.
    Otherwise, pick **Add LLR** and fill the form yourself.

4.  **Add tests.** Right-click an LLR → **Add Test**, or ask the
    AI for `@projectspec /draft-test`. Tests inherit the LLR's
    traces automatically.

5.  **Render and review.** Save the file. The Problems panel
    should show `0 errors / 0 warnings`. The side preview shows
    the freshly rendered HLRs document. From a terminal you can
    also run:

    ```bash
    python3 tools/lint_project.py
    make -C tools render
    ```

6.  **Commit.** The five generated `*.md` files appear in the
    diff alongside `Project.xml`. Commit them together so reviewers
    see the rendered result.

7.  **Handle a merge conflict.** If a teammate edits
    `Project.xml` at the same time, run **Project Spec: Resolve
    Merge Conflicts** when Git complains. TraceR auto-merges the
    disjoint changes; whatever's left lands in the merge editor
    for you to decide.

That's the loop. For deeper details — the schema, the linter's
problem codes, how to add a new kind of generated document —
see the [Developer's Guide](Developers_Guide.md).

## Agents and Prompts

TraceR ships reusable VS Code agent and prompt files under
`.github/agents/` and `.github/prompts/`. These work with AI
coding assistants (such as GitHub Copilot) that support agent
and prompt discovery.

### Agents (`.github/agents/`)

Agents are interactive — they ask questions and guide you
through a task.

| Agent | Purpose |
| ----- | ------- |
| **ci.agent.md** | Create and maintain GitHub Actions workflows. Presents a menu of common workflow categories (CI, code quality, releases, PR automation, etc.) and generates the YAML. |
| **makefile.agent.md** | Create and maintain Makefiles. Prompts for targets, implements them with the self-documenting help hack (`make help` lists all targets). |
| **TracerDevelop.agent.md** | Project-specific development agent. Expert in the TraceR architecture (Python tools, VS Code extension, schema, sidecar, AI pipeline). Use for implementing features. |
| **build.agent.md** | Build the VS Code extension (`npm run build`). |
| **package.agent.md** | Package the extension into a `.vsix` (delegates to build, then runs `vsce package`). |

### Prompts (`.github/prompts/`)

Prompts are automated workflows — they execute a fixed sequence of
steps with approval gates.

| Prompt | Purpose |
| ------ | ------- |
| **UpdateDocs.prompt.md** | Scan the current branch's changes and update spec documents (SDD, HLRs, LLRs, Tests in Project.xml; SDP, SAR, User Manual, Developers Guide) to match the work done. |
| **PR.prompt.md** | Update the SDP status, generate a release-note-quality commit message, commit, push, and open a pull request. |
| **PrepRelease.prompt.md** | Prepare a release: bump VERSION, triage Dependabot alerts (dismiss with justification where possible), update the Vulnerability Report, commit, push, and open a release PR. |
| **Release.prompt.md** | Create a GitHub Release using the version from VERSION, with auto-generated categorised release notes. |

### Using agents and prompts

In VS Code with GitHub Copilot, agents and prompts appear in the
Chat panel. Type `@` to see available agents, or use the prompt
picker to select a prompt. They can also be invoked from the
command palette.

All prompts are generic — they discover project paths dynamically
and work in any repository that uses TraceR to maintain a
`Project.xml`.

## Appendix A: AI Skill Reference (SKILL.md)

When you initialise a new project with **Project Spec: Initialise
Project.xml…**, the extension automatically copies a
**SKILL.md** file into `.github/skills/tracer/SKILL.md` in your
workspace. This file is an AI-agent skill definition — AI coding
assistants (such as GitHub Copilot) that support skill discovery
will read it automatically and learn how to work with your
TraceR project.

### What the skill file contains

The SKILL.md teaches AI agents:

* **What TraceR is** and how the traceability chain works
  (SDD → HLR → LLR → Test).
* **Key files** and their purposes (`doc/Project.xml`,
  `tools/render_doc.py`, `tools/lint_project.py`, etc.).
* **Hard rules** the agent must follow — never edit generated `.md`
  files, IDs are stable contracts, always use `<![CDATA[…]]>` for
  markdown content, render and lint after every edit.
* **Decision flow** — a lookup table mapping common tasks ("Add an
  HLR", "Fix a coverage gap", "Update a spec section") to the
  correct sequence of actions.
* **CLI commands** — the `make -C tools` targets for rendering,
  linting, testing, and building.
* **VS Code extension features** — tree view, form panels, context
  menus, dropdown menu, code lenses, and the `@projectspec` AI
  chat participant.
* **Common pitfalls** — the same mistakes humans make (editing
  generated files, forgetting traces, not rendering after edits),
  written so the AI agent avoids them too.

### When to use it

You don't need to do anything — the skill file works passively. As
long as it's at `.github/skills/tracer/SKILL.md`, any AI agent
that supports VS Code skills will discover it and apply the
guidance when you ask it to work on your TraceR project.

If you're using a project that was created before this feature
existed, you can copy the file manually from the extension:

```
~/.vscode/extensions/tracer.vscode-project-xml-<version>/
    media/skills/tracer/SKILL.md
```

…into your workspace at `.github/skills/tracer/SKILL.md`.

### Customising the skill

The SKILL.md is a plain markdown file with a YAML frontmatter
header. You can edit it freely — add project-specific conventions,
rename sections, or extend the decision flow with entries specific
to your domain. The extension will not overwrite it if it already
exists.
