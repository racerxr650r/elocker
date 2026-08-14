"""Append-only provenance log for AI suggestions (HLR-033).

Every accepted or rejected AI suggestion becomes one JSONL row in
``.edit_doc/ai_history.jsonl`` (relative to the workspace root). The
record carries enough metadata for an audit ("which sentences in the
spec came from a model, on what prompt, with what validator state") to
satisfy the [PVD §6 #7](../../doc/PVD.md) "AI as a co-author, not an
oracle" principle.

Schema (one JSON object per line):

    {
      "ts":        "2026-04-26T12:34:56Z",
      "intent":    "draft.hlr",
      "model":     "claude-sonnet-4.7" | null,
      "prompt_sha256": "...",
      "outcome":   "accepted" | "rejected" | "auto_rejected",
      "retries":   2,
      "validator": {"errors": 0, "warnings": 1, "ok": true},
      "patch":     [...] | null,
      "target":    {"type": "Hlr", "id": "HLR-061", ...},
      "notes":     "validator-blocked: broken-trace HLR-999"
    }

The log is created on first write. The directory is created when missing
(``.edit_doc`` is project-local; the recommendation is to commit the log
in regulated environments and gitignore it elsewhere — see SDP §9).
"""
from __future__ import annotations

import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Mapping


PROVENANCE_DIRNAME = ".edit_doc"
PROVENANCE_FILENAME = "ai_history.jsonl"


def prompt_sha256(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def _now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def append_record(
    *,
    workspace_root: Path | str,
    intent: str,
    outcome: str,
    prompt: str,
    model: str | None = None,
    retries: int = 0,
    validator: Mapping[str, Any] | None = None,
    patch: list[Mapping[str, Any]] | None = None,
    target: Mapping[str, Any] | None = None,
    notes: str = "",
    enabled: bool = True,
) -> Path | None:
    """Append a single provenance record. No-ops when ``enabled`` is False
    (mapped from ``projectXml.ai.logHistory`` on the TS side). Returns the
    log path on write, or ``None`` when disabled.

    The record is written atomically per-line: an OS write of a single
    ``\\n``-terminated JSON object is atomic on POSIX up to PIPE_BUF and the
    JSON serialisation we emit is well under that ceiling. Multi-process
    appending is therefore safe without locking, matching the convention
    used by container log streams.
    """
    if not enabled:
        return None
    root = Path(workspace_root)
    log_dir = root / PROVENANCE_DIRNAME
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / PROVENANCE_FILENAME

    record: dict[str, Any] = {
        "ts":            _now_iso(),
        "intent":        intent,
        "model":         model,
        "prompt_sha256": prompt_sha256(prompt),
        "outcome":       outcome,
        "retries":       int(retries),
        "validator":     dict(validator) if validator else None,
        "patch":         list(patch) if patch is not None else None,
        "target":        dict(target) if target else None,
        "notes":         notes,
    }

    line = json.dumps(record, separators=(",", ":"), sort_keys=True) + "\n"
    # Open in append mode, line-buffered. POSIX guarantees atomicity for
    # writes <= PIPE_BUF (4096 bytes); our records are well below that.
    with log_path.open("a", encoding="utf-8") as fh:
        fh.write(line)
        fh.flush()
        try:
            os.fsync(fh.fileno())
        except OSError:
            # fsync can fail on non-syncable filesystems (some CI mounts);
            # the write itself has already gone through the kernel cache.
            pass
    return log_path


def read_records(workspace_root: Path | str) -> list[dict[str, Any]]:
    """Read every JSONL record from the provenance log. Used by tests and
    by the (future) "show provenance" UI command. Returns an empty list
    when the log does not exist.
    """
    log_path = Path(workspace_root) / PROVENANCE_DIRNAME / PROVENANCE_FILENAME
    if not log_path.exists():
        return []
    out: list[dict[str, Any]] = []
    for line in log_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            out.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return out
