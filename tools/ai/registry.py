"""Intent registry for the inline-AI authoring layer (HLR-048..HLR-053).

Single source of truth for every named intent the AI pipeline knows
about. The chat participant, the tree context-menu builder, the Quick
Fix layer, and the sidecar's ``ai_*`` JSON-RPC methods all consume
this registry rather than hard-coding intent names.

Each intent owns:

* ``id``        — dotted intent name (e.g. ``"draft.hlr"``).
* ``label``     — human-readable label for menus and chat.
* ``kind``      — one of ``"authoring"`` | ``"pvd"`` | ``"advisory"`` |
                  ``"merge"``.
                  ``advisory`` intents (``review.item``) never produce
                  a JSON Patch; ``pvd`` intents (``draft.pvd``) target
                  ``doc/PVD.md`` instead of ``Project.xml``; ``merge``
                  intents (``merge.*``) target a residual conflict
                  from ``project_merge.merge_three_way`` and return
                  resolution payloads consumed by
                  ``project_merge.apply_resolution`` (Phase 5.5,
                  HLR-034).
* ``targets``   — ``ui:treeNode`` payload kinds this intent applies to
                  (e.g. ``("Hlr",)``). Empty tuple means "global".
* ``slash``     — slash command for the chat participant.
* ``schema``    — file name of the JSON Schema for the typed response,
                  resolved under :data:`SCHEMAS_DIR`.
* ``prompt``    — file name of the system prompt, resolved under
                  :data:`PROMPTS_DIR`.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

PACKAGE_DIR = Path(__file__).resolve().parent
SCHEMAS_DIR = PACKAGE_DIR / "schemas"
PROMPTS_DIR = PACKAGE_DIR / "intents"


@dataclass(frozen=True)
class IntentSpec:
    """Metadata for one named AI intent."""
    id: str
    label: str
    kind: str            # "authoring" | "pvd" | "advisory"
    targets: tuple[str, ...]  # complex-type names this intent fits
    slash: str
    schema: str
    prompt: str

    @property
    def schema_path(self) -> Path:
        return SCHEMAS_DIR / self.schema

    @property
    def prompt_path(self) -> Path:
        return PROMPTS_DIR / self.prompt

    def to_dict(self) -> dict[str, object]:
        return {
            "id":      self.id,
            "label":   self.label,
            "kind":    self.kind,
            "targets": list(self.targets),
            "slash":   self.slash,
            "schema":  self.schema,
            "prompt":  self.prompt,
        }


_REGISTRY: tuple[IntentSpec, ...] = (
    IntentSpec(
        id="draft.module",
        label="Draft SDD module with AI",
        kind="authoring",
        targets=("SddModule",),
        slash="draft-module",
        schema="draft.module.schema.json",
        prompt="draft.module.md",
    ),
    IntentSpec(
        id="draft.hlr",
        label="Draft HLR with AI",
        kind="authoring",
        targets=("Hlr",),
        slash="draft-hlr",
        schema="draft.hlr.schema.json",
        prompt="draft.hlr.md",
    ),
    IntentSpec(
        id="draft.llr",
        label="Draft LLR with AI",
        kind="authoring",
        targets=("Llr",),
        slash="draft-llr",
        schema="draft.llr.schema.json",
        prompt="draft.llr.md",
    ),
    IntentSpec(
        id="draft.test",
        label="Draft test with AI",
        kind="authoring",
        targets=("Test",),
        slash="draft-test",
        schema="draft.test.schema.json",
        prompt="draft.test.md",
    ),
    IntentSpec(
        id="draft.pvd",
        label="Draft PVD section with AI",
        kind="pvd",
        targets=(),
        slash="draft-pvd",
        schema="draft.pvd.schema.json",
        prompt="draft.pvd.md",
    ),
    IntentSpec(
        id="expand.hlr_to_llrs",
        label="Expand HLR to LLRs with AI",
        kind="authoring",
        targets=("Hlr",),
        slash="expand",
        schema="expand.hlr_to_llrs.schema.json",
        prompt="expand.hlr_to_llrs.md",
    ),
    IntentSpec(
        id="expand.llr_to_tests",
        label="Expand LLR to tests with AI",
        kind="authoring",
        targets=("Llr",),
        slash="expand",
        schema="expand.llr_to_tests.schema.json",
        prompt="expand.llr_to_tests.md",
    ),
    IntentSpec(
        id="review.item",
        label="Review with AI",
        kind="advisory",
        targets=("Hlr", "Llr", "Test", "SddModule"),
        slash="review",
        schema="review.item.schema.json",
        prompt="review.item.md",
    ),
    IntentSpec(
        id="suggest.traces",
        label="Suggest traces with AI",
        kind="authoring",
        targets=("Hlr", "Llr", "Test"),
        slash="suggest-traces",
        schema="suggest.traces.schema.json",
        prompt="suggest.traces.md",
    ),
    IntentSpec(
        id="gap.fix",
        label="Fix coverage gap with AI",
        kind="authoring",
        targets=("Hlr", "Llr"),
        slash="gap-fill",
        schema="gap.fix.schema.json",
        prompt="gap.fix.md",
    ),
    # Phase 5.5 — AI-assisted merge conflict resolution (HLR-034).
    # Each ``merge.*`` intent runs through the same validate→retry
    # pipeline as the authoring intents but does not produce a JSON
    # Patch: the resolution payload is substituted into the merged
    # XML by ``project_merge.apply_resolution`` on the TS side.
    IntentSpec(
        id="merge.body",
        label="Resolve merge body conflict with AI",
        kind="merge",
        targets=("Hlr", "Llr", "Test", "SddModule"),
        slash="resolve-conflicts",
        schema="merge.body.schema.json",
        prompt="merge.body.md",
    ),
    IntentSpec(
        id="merge.trace",
        label="Resolve merge trace conflict with AI",
        kind="merge",
        targets=("Hlr", "Llr", "Test"),
        slash="resolve-conflicts",
        schema="merge.trace.schema.json",
        prompt="merge.trace.md",
    ),
    IntentSpec(
        id="merge.rename",
        label="Resolve merge id collision with AI",
        kind="merge",
        targets=("Hlr", "Llr", "Test"),
        slash="resolve-conflicts",
        schema="merge.rename.schema.json",
        prompt="merge.rename.md",
    ),
    IntentSpec(
        id="merge.schema_bump",
        label="Resolve schema_version conflict with AI",
        kind="merge",
        targets=("Project",),
        slash="resolve-conflicts",
        schema="merge.schema_bump.schema.json",
        prompt="merge.schema_bump.md",
    ),
)


INTENTS: Mapping[str, IntentSpec] = {spec.id: spec for spec in _REGISTRY}


def list_intents() -> list[IntentSpec]:
    """Return every registered intent in registration order."""
    return list(_REGISTRY)


def get_intent(intent_id: str) -> IntentSpec:
    """Look up an intent by id; raise KeyError on miss."""
    spec = INTENTS.get(intent_id)
    if spec is None:
        raise KeyError(f"unknown AI intent: {intent_id}")
    return spec


# AI actions matrix: which intents apply to a given complex type. Used
# by the sidecar to project an `aiActions` field onto each ui_hints_index
# entry so the VS Code tree provider can render the right context-menu
# entries without naming intents per type (HLR-053).
def ai_actions_for_type(type_name: str) -> list[str]:
    """Return the intent ids that target a given complex-type name."""
    return [spec.id for spec in _REGISTRY if type_name in spec.targets]
