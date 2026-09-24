---
id: EFRT
type: work
status: needs-qa-llm
labels: [bug]
component: [gui, providers]
milestone: beta
workstream: providers
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-18
rank: hf
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: 'owner, 2026-09-18: "i noticed an inconsistency where, in the models options page, there was low, medium, high, max reasoning. but in the model roles, there were only 3 options"'
links: {plans: [], commits: [b63f8d2], evidence: ['docs/qa_evidence/2026-09-18-reasoning-levels-and-model-roles/'], related: [RM1N], github: null}
---
# Every reasoning-effort picker offers the same levels, and never drops "high"

## Issue

> i noticed an inconsistency where, in the models options page, there was low, medium, high, max
> reasoning. but in the model roles, there were only 3 options

## Behaviour as implemented

A picker offers the levels the pane's (or the tier's) **provider** can actually be asked for, and
every picker in the app derives that list from the same place, so two of them can no longer disagree:

- `presets.effort_levels(style)` replaces `distinct_efforts`. Levels that send the same request are
  still one entry, but the entry now keeps the level whose own name is the value sent, not the first
  of the group. Kimi and GLM offer `low, high, max` (medium is the same request as high there),
  Gemini `low, medium, high`, Relay Free `low, medium` (13.9's cap, unchanged), OpenAI and
  OpenRouter all four, and Anthropic and MiniMax none at all.
- Every preset also carries `effort_note`: one line saying what happens to the levels it left out
  ("medium is sent as high."). It is shown beside the pickers rather than left for the user to work out.
- `Pane::offeredEfforts()` / `Pane::effortNote()` / `Pane::nearestEffort()` are the GUI's side of it.
  `Pane::efforts()` still returns Relay's four and is still what *stored* values are validated
  against: a pane, a tier or a role keeps the level it was set to across a provider switch.
- Following it now: the pane's effort box, Alt+. / Alt+, (`effortStep`), `/effort`, the palette's
  Reasoning effort submenu, the model chip's tooltip, Options › Models › Default reasoning effort,
  and the effort boxes in the Model roles modal.
- A stored level the provider does not have is displayed as the level it is sent as — the chip
  tooltip reads "Reasoning effort: medium (sent as high)" — and is left stored as it is.
- On a provider with no effort knob the pickers say so ("This model has no reasoning setting")
  instead of offering choices that send an identical request.

## Why

The two pickers were written against different lists: Options offered Relay's four levels whatever
the provider, the roles modal offered `distinct_efforts`, which collapsed levels mapping to the same
provider value. The collapse kept the *first* level of each group, so on Kimi and GLM — the owner's
providers — it offered "medium" and dropped "high", which is Relay's own default effort and the level
every other surface shows. The roles modal could therefore not display a tier set to "high" at all:
`findData("high")` missed and the box fell back to "Model default". Relay Free's deliberate
Low/Medium cap (13.9) is the same rule read the other way and is preserved exactly.

## Checks

- [ ] Options › Models › "Default reasoning effort" on a GLM or Kimi key lists low, high, max and
      the row says "medium is sent as high."; on OpenRouter it lists all four and says nothing.
- [ ] Model roles on the same provider: every effort box lists the same three levels, and a tier
      set to "high" shows "high" rather than "Model default".
- [ ] Alt+. and Alt+, step through those three levels; the model chip's tooltip agrees.
- [ ] `/effort medium` on GLM keeps the pane's level at medium and says what it is sent as.
- [ ] Switch the pane to Anthropic: the pickers say the model has no reasoning setting; switching
      back to GLM shows the level the pane had.
- [ ] `PYTHONPATH=backend python3 -m unittest discover -s tests` passes, including
      `test_presets.py` and `test_session_protocol.py`.
