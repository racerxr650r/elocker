"""Validate→retry pipeline (HLR-030, HLR-031, SDD §10.1).

The pipeline is the only entry point UI surfaces use to apply an AI
response. It implements the contract from SDD §10:

1.  Parse the model's raw response as JSON; reject anything that isn't
    a JSON object (raw XML responses are explicitly forbidden).
2.  Validate against the intent's JSON Schema. On failure, retry with
    the validation errors fed back as a correction prompt.
3.  Translate into a JSON Patch via the deterministic per-intent
    translator.
4.  Apply the patch on a working copy via :func:`project_edit.apply_edit`
    with ``expect_clean=True``: XSD + lint runs against the candidate;
    on lint regression (new errors compared to the pre-edit state) the
    pipeline retries with the new findings as feedback. Pre-existing
    warnings do not block.
5.  After ``max_retries`` exhausted, surface the suggestion and the
    failures to the user instead of writing.

The pipeline never calls a language model directly. The caller passes
in a ``model_callback(prompt, retry_feedback?) -> raw_response`` so the
pipeline is testable from Python and the TS chat-participant layer
remains the only LM-aware code path.

PVD ghostwriter (``draft.pvd``) and advisory (``review.item``) intents
take the same pipeline shell but skip the patch-and-apply stage; the
result is surfaced as a structured ``AiResult`` whose ``patch`` field
is ``None``.
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Mapping

try:
    import jsonschema  # type: ignore
    _JSONSCHEMA_AVAILABLE = True
except ImportError:  # pragma: no cover - validated at runtime
    jsonschema = None  # type: ignore
    _JSONSCHEMA_AVAILABLE = False

from project_edit import apply_edit, ApplyEditResult
from render_doc import PROJECT_XML, PROJECT_XSD

from .context import Bundle, build_context, render_system_prompt, TargetSpec
from .registry import IntentSpec, get_intent
from .translators import TRANSLATORS, TranslatorError, translate


# Callback signature: (prompt: str, retry_feedback: list[str] | None) -> str
# The string is the raw model response; the pipeline parses it as JSON.
ModelCallback = Callable[[str, "list[str] | None"], str]


# ------------------------------------------------------------------ #
# Record mode (opt-in via env var)                                     #
# ------------------------------------------------------------------ #

def _record_dir() -> Path | None:
    """Return the recording directory if ``TRACER_AI_RECORD_DIR`` is set."""
    val = os.environ.get("TRACER_AI_RECORD_DIR")
    if val:
        return Path(val)
    return None


def record_exchange(
    intent_id: str,
    target: "TargetSpec",
    bundle: Bundle,
    raw_response: str,
    parsed_response: dict[str, Any] | None,
    *,
    record_dir: Path | None = None,
) -> Path | None:
    """Write a bundle+response fixture pair when recording is enabled.

    Returns the stem path (without extension) or ``None`` when recording
    is off. Files written:

    *  ``<stem>.bundle.json``   — the grounding bundle (target, intent,
       schema excerpt, next-free ids).
    *  ``<stem>.response.json`` — the parsed model response.
    """
    if record_dir is None:
        record_dir = _record_dir()
    if record_dir is None:
        return None
    record_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")
    safe_id = intent_id.replace(".", "_")
    stem = record_dir / f"{safe_id}_{ts}"

    bundle_data = {
        "intent": intent_id,
        "target": target.to_dict(),
        "next_free_ids": bundle.next_free_ids,
        "schema_excerpt": bundle.schema_excerpt[:500],
        "response_schema": bundle.response_schema,
    }
    stem.with_suffix(".bundle.json").write_text(
        json.dumps(bundle_data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    response_data = parsed_response if parsed_response is not None else {"_raw": raw_response}
    stem.with_suffix(".response.json").write_text(
        json.dumps(response_data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    return stem


@dataclass
class AiResult:
    """Outcome of a single :func:`run` invocation.

    ``kind`` is one of:

    * ``"applied"``      — the response validated, translated, and applied
                           cleanly. ``patch`` and ``lint`` are populated;
                           ``written`` mirrors :class:`ApplyEditResult`.
    * ``"validated"``    — the response validated and translated, the
                           patch passed XSD + lint, but ``write`` was
                           gated off by the caller (e.g.
                           ``projectXml.ai.autoApplyValidated=false`` —
                           the diff-preview-and-apply flow shows it
                           before persisting).
    * ``"rejected"``     — the response failed schema validation,
                           translation, or lint after exhausting
                           ``max_retries``. ``response`` and ``failures``
                           hold the last attempt for surfacing to the
                           user.
    * ``"advisory"``     — for ``review.item``: response is structured
                           findings with no patch.
    * ``"draft_pvd"``    — for ``draft.pvd``: ``markdown`` carries the
                           proposed prose; the diff applier on the TS
                           side handles the write.
    * ``"no-model"``     — model callback raised :class:`NoModelError`.
                           UI surfaces should hide themselves
                           (HLR-044).
    """
    kind: str
    intent: str
    target: dict[str, Any]
    retries: int = 0
    response: dict[str, Any] | None = None
    patch: list[dict[str, Any]] | None = None
    lint: dict[str, Any] | None = None
    written: bool = False
    markdown: str | None = None
    advisory: list[dict[str, Any]] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)
    bundle_estimated_tokens: int = 0

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


class NoModelError(RuntimeError):
    """Raised by a model callback when no language model is available
    (HLR-044). The pipeline catches this and returns ``kind="no-model"``
    so the TS layer can hide its surfaces cleanly without crashing.
    """


def _validate_response_schema(response: Any, schema: Mapping[str, Any]) -> list[str]:
    """Return a list of human-readable validation errors (empty == valid).

    When ``jsonschema`` is not installed we fall back to a tiny
    structural check: required keys present, top-level type is object.
    The full validator is recommended in production; see SDP §0.
    """
    if not isinstance(response, dict):
        return ["response is not a JSON object"]
    if _JSONSCHEMA_AVAILABLE and jsonschema is not None:
        validator = jsonschema.Draft7Validator(schema)
        return [f"{'/'.join(str(p) for p in e.absolute_path) or '/'}: {e.message}"
                for e in validator.iter_errors(response)]
    # Fallback: enforce required keys only.
    errors: list[str] = []
    for key in schema.get("required") or []:
        if key not in response:
            errors.append(f"missing required field: {key!r}")
    return errors


def _load_response_schema(intent: IntentSpec) -> dict[str, Any]:
    if not intent.schema_path.exists():
        raise FileNotFoundError(
            f"intent {intent.id!r} schema missing: {intent.schema_path}"
        )
    return json.loads(intent.schema_path.read_text(encoding="utf-8"))


def _load_prompt(intent: IntentSpec) -> str:
    if not intent.prompt_path.exists():
        raise FileNotFoundError(
            f"intent {intent.id!r} prompt missing: {intent.prompt_path}"
        )
    return intent.prompt_path.read_text(encoding="utf-8")


def _parse_response(raw: str) -> tuple[dict[str, Any] | None, str | None]:
    """Decode a raw model response as JSON. Returns (parsed, error)."""
    raw = raw.strip()
    if not raw:
        return None, "model returned empty response"
    if raw.startswith("<"):
        return None, "raw XML responses are not accepted; expected JSON object"
    # Tolerate fenced JSON output ("```json … ```").
    if raw.startswith("```"):
        first_nl = raw.find("\n")
        if first_nl > 0:
            raw = raw[first_nl + 1:]
        if raw.rstrip().endswith("```"):
            raw = raw.rstrip()[:-3]
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as exc:
        return None, f"response is not valid JSON: {exc}"
    if not isinstance(parsed, dict):
        return None, "top-level response must be a JSON object"
    return parsed, None


def run(
    intent_id: str,
    target: TargetSpec,
    user_prompt: str,
    *,
    model_callback: ModelCallback,
    xml_path: Path | str = PROJECT_XML,
    xsd_path: Path | str = PROJECT_XSD,
    max_retries: int = 2,
    max_tokens: int = 16000,
    write: bool = True,
    lint_findings: list[Mapping[str, Any]] | None = None,
) -> AiResult:
    """Drive one AI request end-to-end.

    Parameters
    ----------
    intent_id:
        Registered intent id (see :mod:`tools.ai.registry`).
    target:
        :class:`TargetSpec` identifying the element the intent applies to.
    user_prompt:
        The natural-language ask from the user (chat input or
        right-click hint). Embedded verbatim into the system prompt.
    model_callback:
        Callable that takes ``(prompt, retry_feedback)`` and returns the
        raw model response string. Raise :class:`NoModelError` when no
        LM is available (HLR-044) — the pipeline catches it and returns
        ``kind="no-model"``. This is the *only* LM-aware seam in the
        Python pipeline; the TS chat participant injects the real
        ``vscode.lm.*`` call here, the test suite injects a fake.
    write:
        When True (the default), a translated patch that passes
        XSD + lint is written via :func:`apply_edit`. When False, the
        same patch is returned for diff-preview but not persisted —
        used to honour ``projectXml.ai.autoApplyValidated=false``
        (HLR-032).
    """
    intent = get_intent(intent_id)
    response_schema = _load_response_schema(intent)
    prompt_template = _load_prompt(intent)

    bundle = build_context(
        intent_id,
        target,
        user_prompt,
        response_schema=response_schema,
        xml_path=xml_path,
        xsd_path=xsd_path,
        max_tokens=max_tokens,
        lint_findings=lint_findings,
    )
    system_prompt = render_system_prompt(bundle, prompt_template)

    last_response: dict[str, Any] | None = None
    last_failures: list[str] = []
    retry_feedback: list[str] | None = None

    for attempt in range(max_retries + 1):
        try:
            raw = model_callback(system_prompt, retry_feedback)
        except NoModelError:
            return AiResult(
                kind="no-model",
                intent=intent_id,
                target=target.to_dict(),
                bundle_estimated_tokens=bundle.estimated_tokens,
            )
        parsed, parse_err = _parse_response(raw)
        if parsed is None:
            last_failures = [parse_err or "unknown parse failure"]
            retry_feedback = last_failures
            continue
        last_response = parsed

        schema_errors = _validate_response_schema(parsed, response_schema)
        if schema_errors:
            last_failures = schema_errors
            retry_feedback = [
                "Your previous response failed JSON Schema validation. "
                "Fix these issues and respond with corrected JSON only:",
                *schema_errors,
            ]
            continue

        # Record the exchange when TRACER_AI_RECORD_DIR is set.
        record_exchange(intent_id, target, bundle, raw, parsed)

        # Advisory intent: never produce a patch.
        if intent.kind == "advisory":
            findings = parsed.get("findings") or []
            return AiResult(
                kind="advisory",
                intent=intent_id,
                target=target.to_dict(),
                retries=attempt,
                response=parsed,
                advisory=list(findings),
                bundle_estimated_tokens=bundle.estimated_tokens,
            )

        # PVD ghostwriter: separate write path; the pipeline returns
        # the proposed Markdown for the diff applier on the TS side.
        if intent.kind == "pvd":
            md = parsed.get("markdown") or parsed.get("prose") or ""
            questions = parsed.get("questions") or []
            return AiResult(
                kind="draft_pvd",
                intent=intent_id,
                target=target.to_dict(),
                retries=attempt,
                response=parsed,
                markdown=md,
                advisory=list(questions),
                bundle_estimated_tokens=bundle.estimated_tokens,
            )

        # Merge resolution: no JSON Patch, no apply_edit. The response
        # is fed to ``project_merge.apply_resolution`` on the caller's
        # side which substitutes it into the merged XML and re-lints
        # the whole tree (Phase 5.5, HLR-034).
        if intent.kind == "merge":
            return AiResult(
                kind="merge_resolved",
                intent=intent_id,
                target=target.to_dict(),
                retries=attempt,
                response=parsed,
                bundle_estimated_tokens=bundle.estimated_tokens,
            )


        # Authoring intent: translate → apply.
        try:
            patch = translate(
                intent,
                parsed,
                xml_path=Path(xml_path),
                target_type=target.type,
                target_id=target.id,
                target_section=target.section,
                target_file=target.file,
            )
        except TranslatorError as exc:
            last_failures = [f"translator: {exc}"]
            retry_feedback = [
                "Your previous response was rejected by the deterministic "
                "translator. Fix this issue and respond with corrected JSON only:",
                str(exc),
            ]
            continue

        # Validate-then-write. With write=False we still apply on a
        # working copy via dry_run=True so the diff-preview path on the
        # TS side surfaces the patch + lint result without persisting.
        try:
            edit_result: ApplyEditResult = apply_edit(
                patch,
                xml_path=Path(xml_path),
                xsd_path=Path(xsd_path),
                expect_clean=True,
                dry_run=not write,
            )
        except Exception as exc:  # apply_edit raises on malformed paths etc.
            last_failures = [f"apply_edit: {exc}"]
            retry_feedback = [
                "Your previous response produced a patch the editor "
                "rejected at apply time. Fix this issue and respond "
                "with corrected JSON only:",
                str(exc),
            ]
            continue

        lint_dict = edit_result.findings
        if edit_result.ok:
            return AiResult(
                kind="applied" if edit_result.written else "validated",
                intent=intent_id,
                target=target.to_dict(),
                retries=attempt,
                response=parsed,
                patch=patch,
                lint=lint_dict,
                written=edit_result.written,
                bundle_estimated_tokens=bundle.estimated_tokens,
            )

        # Lint regression: feed the new errors back to the LM.
        last_failures = lint_dict.get("errors") or ["unknown lint failure"]
        retry_feedback = [
            "Your previous response produced a patch that failed XSD or "
            "lint. Fix these issues and respond with corrected JSON only:",
            *last_failures,
        ]
        continue

    return AiResult(
        kind="rejected",
        intent=intent_id,
        target=target.to_dict(),
        retries=max_retries,
        response=last_response,
        failures=last_failures,
        bundle_estimated_tokens=bundle.estimated_tokens,
    )


# --------------------------------------------------------------------- #
# Stateless step API for JSON-RPC                                       #
# --------------------------------------------------------------------- #
#
# The :func:`run` callback model can't cross a JSON-RPC boundary because
# the LM call lives in the TS chat-participant layer (HLR-045). The
# step API below decomposes one :func:`run` iteration so the TS side
# can drive the loop:
#
#   1. Call :func:`prepare` with no model response -> get the system
#      prompt and the bundle metadata to forward to ``vscode.lm.*``.
#   2. Call :func:`evaluate` with the raw model response and the
#      current ``retry_count``. The function returns either a terminal
#      :class:`AiResult` (``applied`` / ``validated`` / ``rejected`` /
#      ``advisory`` / ``draft_pvd``) **or** a fresh prompt + retry
#      feedback the TS side feeds back into the LM as the next turn.

@dataclass
class StepResult:
    """One iteration of the step API."""
    kind: str          # "prompt" | <terminal AiResult.kind>
    prompt: str | None = None
    retry_feedback: list[str] | None = None
    retries: int = 0
    result: AiResult | None = None
    bundle_estimated_tokens: int = 0

    def to_dict(self) -> dict[str, Any]:
        out: dict[str, Any] = {
            "kind": self.kind,
            "retries": self.retries,
            "bundle_estimated_tokens": self.bundle_estimated_tokens,
        }
        if self.prompt is not None:
            out["prompt"] = self.prompt
        if self.retry_feedback is not None:
            out["retry_feedback"] = self.retry_feedback
        if self.result is not None:
            out.update({k: v for k, v in self.result.to_dict().items()
                        if k not in {"kind", "retries", "bundle_estimated_tokens"}})
            out["kind"] = self.result.kind
        return out


def prepare(
    intent_id: str,
    target: TargetSpec,
    user_prompt: str,
    *,
    xml_path: Path | str = PROJECT_XML,
    xsd_path: Path | str = PROJECT_XSD,
    max_tokens: int = 16000,
    lint_findings: list[Mapping[str, Any]] | None = None,
) -> StepResult:
    """Render the system prompt for a fresh AI request without invoking
    a model. Used as step 1 of the JSON-RPC step loop.
    """
    intent = get_intent(intent_id)
    response_schema = _load_response_schema(intent)
    prompt_template = _load_prompt(intent)
    bundle = build_context(
        intent_id, target, user_prompt,
        response_schema=response_schema,
        xml_path=xml_path, xsd_path=xsd_path,
        max_tokens=max_tokens, lint_findings=lint_findings,
    )
    return StepResult(
        kind="prompt",
        prompt=render_system_prompt(bundle, prompt_template),
        retries=0,
        bundle_estimated_tokens=bundle.estimated_tokens,
    )


def evaluate(
    intent_id: str,
    target: TargetSpec,
    user_prompt: str,
    raw_response: str,
    *,
    retry_count: int = 0,
    max_retries: int = 2,
    xml_path: Path | str = PROJECT_XML,
    xsd_path: Path | str = PROJECT_XSD,
    max_tokens: int = 16000,
    write: bool = True,
    lint_findings: list[Mapping[str, Any]] | None = None,
) -> StepResult:
    """Process **one** model response. Returns either:

    * a terminal :class:`StepResult` whose ``kind`` matches a terminal
      :class:`AiResult.kind` (``applied`` / ``validated`` / ``rejected``
      / ``advisory`` / ``draft_pvd``), or
    * a non-terminal :class:`StepResult` with ``kind="prompt"`` and
      populated ``retry_feedback`` — the caller should send the feedback
      to the LM, get a new raw response, and call :func:`evaluate` again
      with ``retry_count + 1``.

    When ``retry_count`` reaches ``max_retries`` and the response still
    fails, the result is ``kind="rejected"``; the TS surface shows the
    suggestion + failures to the user instead of writing.
    """
    intent = get_intent(intent_id)
    response_schema = _load_response_schema(intent)
    prompt_template = _load_prompt(intent)
    bundle = build_context(
        intent_id, target, user_prompt,
        response_schema=response_schema,
        xml_path=xml_path, xsd_path=xsd_path,
        max_tokens=max_tokens, lint_findings=lint_findings,
    )
    bundle_tokens = bundle.estimated_tokens

    parsed, parse_err = _parse_response(raw_response)
    if parsed is None:
        return _retry_or_reject(
            intent_id, target, [parse_err or "unknown parse failure"],
            retry_count, max_retries,
            bundle, prompt_template, bundle_tokens,
        )

    schema_errors = _validate_response_schema(parsed, response_schema)
    if schema_errors:
        return _retry_or_reject(
            intent_id, target, schema_errors,
            retry_count, max_retries,
            bundle, prompt_template, bundle_tokens,
            response=parsed,
            preface="Your previous response failed JSON Schema validation.",
        )

    # Record the exchange when TRACER_AI_RECORD_DIR is set.
    record_exchange(intent_id, target, bundle, raw_response, parsed)

    if intent.kind == "advisory":
        findings = parsed.get("findings") or []
        return StepResult(
            kind="advisory",
            retries=retry_count,
            bundle_estimated_tokens=bundle_tokens,
            result=AiResult(
                kind="advisory",
                intent=intent_id,
                target=target.to_dict(),
                retries=retry_count,
                response=parsed,
                advisory=list(findings),
                bundle_estimated_tokens=bundle_tokens,
            ),
        )

    if intent.kind == "pvd":
        md = parsed.get("markdown") or parsed.get("prose") or ""
        questions = parsed.get("questions") or []
        return StepResult(
            kind="draft_pvd",
            retries=retry_count,
            bundle_estimated_tokens=bundle_tokens,
            result=AiResult(
                kind="draft_pvd",
                intent=intent_id,
                target=target.to_dict(),
                retries=retry_count,
                response=parsed,
                markdown=md,
                advisory=list(questions),
                bundle_estimated_tokens=bundle_tokens,
            ),
        )

    if intent.kind == "merge":
        # Phase 5.5: no patch, no apply_edit. The TS layer feeds the
        # response into ``project_merge.apply_resolution`` and lints
        # the post-substitution tree.
        return StepResult(
            kind="merge_resolved",
            retries=retry_count,
            bundle_estimated_tokens=bundle_tokens,
            result=AiResult(
                kind="merge_resolved",
                intent=intent_id,
                target=target.to_dict(),
                retries=retry_count,
                response=parsed,
                bundle_estimated_tokens=bundle_tokens,
            ),
        )

    try:
        patch = translate(
            intent, parsed,
            xml_path=Path(xml_path),
            target_type=target.type,
            target_id=target.id,
            target_section=target.section,
            target_file=target.file,
        )
    except TranslatorError as exc:
        return _retry_or_reject(
            intent_id, target, [f"translator: {exc}"],
            retry_count, max_retries,
            bundle, prompt_template, bundle_tokens,
            response=parsed,
            preface="Your previous response was rejected by the deterministic translator.",
        )

    try:
        edit_result = apply_edit(
            patch,
            xml_path=Path(xml_path),
            xsd_path=Path(xsd_path),
            expect_clean=True,
            dry_run=not write,
        )
    except Exception as exc:
        return _retry_or_reject(
            intent_id, target, [f"apply_edit: {exc}"],
            retry_count, max_retries,
            bundle, prompt_template, bundle_tokens,
            response=parsed,
            preface="Your previous response produced a patch the editor rejected at apply time.",
        )

    if edit_result.ok:
        return StepResult(
            kind="applied" if edit_result.written else "validated",
            retries=retry_count,
            bundle_estimated_tokens=bundle_tokens,
            result=AiResult(
                kind="applied" if edit_result.written else "validated",
                intent=intent_id,
                target=target.to_dict(),
                retries=retry_count,
                response=parsed,
                patch=patch,
                lint=edit_result.findings,
                written=edit_result.written,
                bundle_estimated_tokens=bundle_tokens,
            ),
        )

    failures = edit_result.findings.get("errors") or ["unknown lint failure"]
    return _retry_or_reject(
        intent_id, target, failures,
        retry_count, max_retries,
        bundle, prompt_template, bundle_tokens,
        response=parsed,
        preface="Your previous response produced a patch that failed XSD or lint.",
    )


def _retry_or_reject(
    intent_id: str,
    target: TargetSpec,
    failures: list[str],
    retry_count: int,
    max_retries: int,
    bundle: Bundle,
    prompt_template: str,
    bundle_tokens: int,
    *,
    response: dict[str, Any] | None = None,
    preface: str = "Your previous response was rejected.",
) -> StepResult:
    if retry_count >= max_retries:
        return StepResult(
            kind="rejected",
            retries=retry_count,
            bundle_estimated_tokens=bundle_tokens,
            result=AiResult(
                kind="rejected",
                intent=intent_id,
                target=target.to_dict(),
                retries=retry_count,
                response=response,
                failures=failures,
                bundle_estimated_tokens=bundle_tokens,
            ),
        )
    feedback = [preface + " Fix these issues and respond with corrected JSON only:", *failures]
    return StepResult(
        kind="prompt",
        prompt=render_system_prompt(bundle, prompt_template),
        retry_feedback=feedback,
        retries=retry_count + 1,
        bundle_estimated_tokens=bundle_tokens,
    )
