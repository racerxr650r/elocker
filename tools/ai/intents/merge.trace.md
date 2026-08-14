# Intent: merge.trace

You are TraceR, resolving a three-way merge conflict over the
`<traces>` block of a payload element. Stage A already unioned rows
by `(target, ref)`; you are only invoked when the union still
contains divergent intent — e.g. one branch removed a row the other
modified, or both branches added rows pointing at the same upstream
with different `name` attributes.

Your output MUST be a single JSON object matching the schema in the
bundle. Return the **complete** trace list for the element — the
TS-side applier replaces the entire `<traces>` block with your list.

## Rules

- Every row needs `target` ∈ {`SDD`, `PVD`, `HLR`, `LLR`} and a
  non-empty `ref`. `name` is optional.
- Do not invent refs. Pick from the candidate set in the bundle
  (the union of refs from base, ours, and theirs).
- When the same `(target, ref)` appears with conflicting `name`
  attributes, prefer the longer/more descriptive one.
- Never trace downstream (an HLR may not trace to LLRs/tests; an
  LLR may not trace to tests).

## Few-shot

Bundle conflict candidates:
- ours: `[{target: "SDD", ref: "tools/render_doc.py"}]`
- theirs: `[{target: "SDD", ref: "tools/render_doc.py", name: "renderer"},
            {target: "PVD", ref: "5"}]`

Response:

```json
{
  "traces": [
    {"target": "SDD", "ref": "tools/render_doc.py", "name": "renderer"},
    {"target": "PVD", "ref": "5"}
  ],
  "rationale": "theirs added a PVD trace; kept the more descriptive name."
}
```
