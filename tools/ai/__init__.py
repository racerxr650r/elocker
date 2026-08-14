"""Phase 5 inline-AI authoring layer for TraceR Project.xml.

Pure-Python: this package never calls a language model directly. The
TypeScript chat-participant layer in ``tools/vscode-project-xml/src/ai/``
is the only LM-aware code; it routes raw model responses back through
this package's :mod:`pipeline` for typed-schema validation and
deterministic JSON-Patch translation.

Public surface
--------------

* :mod:`tools.ai.context` — :func:`build_context` assembles a grounding
  bundle (PVD/SDD/upstream payloads/XSD/lint state) with deterministic
  truncation. The schema and the user intent are never dropped.

* :mod:`tools.ai.pipeline` — :func:`validate_response` runs a candidate
  model response through its intent's JSON Schema, translates it to a
  JSON Patch, applies the patch on a working copy, and runs XSD + lint.
  On failure returns a structured retry feedback packet.

* :mod:`tools.ai.translators` — per-intent deterministic translators
  from the typed-JSON response shape into a list of operations
  consumable by :func:`project_edit.apply_edit`.

* :mod:`tools.ai.provenance` — append-only ``.edit_doc/ai_history.jsonl``
  writer (HLR-033).

* :data:`INTENTS` — registry of every named intent: schema path,
  prompt path, kind (`authoring` | `pvd` | `advisory`), human label.
"""
from __future__ import annotations

from .registry import INTENTS, IntentSpec, get_intent, list_intents

__all__ = [
    "INTENTS",
    "IntentSpec",
    "get_intent",
    "list_intents",
]
