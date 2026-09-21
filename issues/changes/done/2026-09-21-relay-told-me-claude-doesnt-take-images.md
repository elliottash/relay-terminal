---
id: P1CS
type: work
status: done
labels: [bug, models, guest]
assignee: claude-code
rank: m
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A guest pane refused an image because "sonnet" is not a model id

## Issue
bug: relay told me claude doesnt take images

## Execution Summary
A pane on the Claude Code guest harness (protocol 29.3) carries
`base_url: harness://claude` and, as its model, the family name the CLI reports — `opus`,
`sonnet`, `haiku`, `fable` (`guest_harness_claude.MODEL_ALIASES`, written into `config.model` by
`guest_harness_provider.start_provider`). None of those match a prefix in
`presets.VISION_MODELS`, which is keyed on API ids (`claude-`), so `model_supports_vision` read
the pane as text-only and `Agent._begin_vision_turn` refused the turn before it was sent —
"sonnet cannot read images and no vision model is set" — for a guest that does take images:
`guest_harness_claude._user_message` and `guest_harness_codex._build_input` both put image blocks
on the wire, and `guest_harness_provider.last_user_message` already hands them over.

`_begin_vision_turn` now returns early on a guest pane (`Agent._on_a_guest_harness`, the
`harness://` base URL that `guest_harness_provider.config_guest_id` tests). That also stops a
*pinned* vision model taking a guest pane's image turn: the swap would hand the picture to a model
the guest never sees, on a turn the guest's own session and transcript are holding, and the guest
would answer about a picture it was not shown. Same shape as #W56B, one layer up: there a real
model id was missing from the list; here the pane's "model" is not an id at all.

## Tests
`PYTHONPATH=$PWD/backend python3 -m unittest tests.test_images tests.test_presets tests.test_roles
tests.test_guest_harness_provider tests.test_plan_turns` — 227 tests, OK. New:
`test_a_guest_harness_pane_takes_the_image_turn_itself` and
`test_a_guest_pane_is_not_routed_off_its_harness_for_an_image` in `tests/test_images.py`; both
fail on the code as it was (the first with the refusal event, the second serving `gpt-6-astra`).

## Resolution
2026-09-21: fixed and landed with its tests.
