---
id: 308N
type: work
status: needs-qa-llm
labels: [bug]
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zz31
created: '2026-09-17'
acceptance: a suggestion appears after a finished command and can be accepted with Tab, with a test covering the path
source: '`issues/feature_intake.txt`, 2026-09-17: "suggested next command / prompt isnt working yet"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Next command and next prompt suggestions never appear

The suggestion feature (ghost text in an empty prompt box after a command, `requestSuggestion("next_command", …)`)
does not produce anything in normal use. Find out where it stops: the setting, the side call, the model role
(suggestions now run on the Flash tier), the event, or the ghost-text path in the composer. Add a test that
would have caught a silent failure, and a status line when a suggestion call errors.

## Cause

The path itself is sound end to end — it was driven for real against a stand-in model and produced ghost
text (see Evidence). What is broken is what happens when the side call **fails**, and that is the state a
user lands in, because the suggestion does not run on the pane's own model.

Two things stack up:

1. **Suggestions run on the Flash tier, not the pane's model.** Since the roles merge,
   `SessionCommands._suggest` asks for `agent.side_provider(cheap=True, role="suggestions")`, and
   `ROLE_TIERS["suggestions"] == "flash"`, so the call goes to `TIER_DEFAULTS[<main preset>]["flash"]` — for
   a pane on `kimi-k3` that is `kimi-k2.7-code-highspeed`, a **different model** from the one the pane is
   configured with and the one whose access the user has checked. Any key, quota, model-access or endpoint
   problem specific to that model breaks suggestions while everything else in the pane keeps working. (This
   was reproduced against a stand-in endpoint: the call really is made on the Flash model.)

2. **A failed suggestion was invisible.** `_background("suggest", …)` with no `on_error` emitted a bare
   `{"event": "error", "id": "suggest-N", "source": "suggest", "text": …}`. The GUI has no branch for that
   id, so it fell through to the generic error handler, which

   - printed only the provider's own line — "Provider HTTP 401. Check endpoint, model access, key, quota,
     and parameters." — with nothing saying a *suggestion* had failed or on which model, and
   - ran the turn-failure bookkeeping on the way: `m_agentBusy = event.value("agent_busy").toBool(false)`
     forced the pane's busy state to false (the background error carries no `agent_busy`), `m_configuring`
     was cleared, and a pending submit could be dropped.

   So the honest description of the report is: the ghost text never appears, and the only trace is a naked
   provider line that reads like an agent problem.

Everything else was checked and is fine: the trigger (`shell_state` `loaded` → `ready` sets
`m_commandLoaded`, then the 250 ms `requestSuggestion`), the opt-in settings (`suggestions/next_command` and
`suggestions/next_prompt`, both **off by default** under Settings › Privacy), the protocol round trip, the
role resolution, `suggestions.py`, and the ghost path (`handleSessionEvent` → `m_aiGhost` → `updateGhost()`
→ `RichEditor::setGhost` → the paint in `RichEditor::paintEvent`, Tab accepting it).

## Implemented

- `backend/relay_core/session_protocol.py`, `_suggest`: a failed suggestion is reported **as a suggestion** —
  `{"event": "suggestion", "kind", "id", "text": "", "error": <provider message>, "model": <the model the
  suggestions role resolved to>}` — instead of a bare protocol error. Nothing about it can be mistaken for
  the agent turn failing.
- `src/main.cpp`, the `suggestion` handler: an event carrying `error` for the pending suggestion clears the
  pending id and shows an attributed status line —
  `Next-command suggestion failed (kimi-k2.7-code-highspeed): Provider HTTP 401. Check endpoint, model
  access, key, quota, and parameters.` — and returns, so none of the turn-failure bookkeeping runs.
- `docs/AGENT-SESSIONS-PROTOCOL.md` section 9 documents the error shape.
- `tests/test_session_protocol.py`, `test_a_failed_suggestion_is_reported_as_a_suggestion`: a provider that
  refuses no-tools calls must produce a `suggestion` event with the error and the model, and **no** `error`
  event for that id. Checked against the old code: it fails there with `- error / + suggestion`.

Build: `cmake --build build` with no new warnings. `ctest --test-dir build` 16/16; `./scripts/test.sh` 511
tests pass (one pre-existing flake,
`tests/test_subagents.py::HandoffTests::test_cancelled_main_turn_does_not_wake_and_keeps_result`, which
fails at the same rate on an unmodified backend).

## Evidence

`docs/qa_evidence/2026-09-17-bugfix-batch1/`, harness `suggestions.sh [ok|fail]`. The whole path runs for
real — the GUI, the real worker, the real role resolution and the real protocol — against a local
OpenAI-compatible endpoint standing in for the model, so no key leaves the machine and no provider is
called. The stand-in is wired in by pointing the `kimi` preset at 127.0.0.1 from a shim in `RELAY_DATA_DIR`
and putting a dummy value in `RELAY_KIMI_API_KEY`, which is what the keystore reads first.

- `model-calls-ok.log`: `{"model": "kimi-k2.7-code-highspeed", "system": "You suggest the next shell command
  …"}` — the side call is made, on the Flash tier, which is the first half of the cause.
- `suggestions-ok-02-after-the-command.png`: after `echo hello`, `git status --short` stands in the empty
  prompt box as grey ghost text.
- `suggestions-ok-03-after-tab.png`: Tab accepted it — the command is real, highlighted text in the box.
- `suggestions-fail-02-after-the-command.png`: with the stand-in refusing only the suggestion call (401,
  the way a provider refuses a Flash model a key has no access to), the status bar now reads
  "Next-command suggestion failed (kimi-k2.7-code-highspeed): Provider HTTP 401 …". Before the fix the same
  run showed only "Provider HTTP 401. Check endpoint, model access, key, quota, and parameters."

## QA checklist

1. Settings › Privacy: both "AI next-command suggestions" and "Suggested next prompts" are **off** by
   default. Turn the first on, run a command from the prompt box (`ls`), and wait: a grey suggestion must
   appear in the empty prompt box within a second or two. Tab accepts it; → accepts one word; typing
   anything replaces it; Esc and switching panes clear it.
2. Turn it off again and run another command: no suggestion, and no model call.
3. Turn on "Suggested next prompts", finish an agent turn, and leave the prompt box empty: a suggested
   prompt appears. In plan mode it must not (the worker answers `reason: "plan_mode"`).
4. In Terminal mode (Ctrl+I to "terminal") a next-prompt suggestion must not be shown, and in Agent mode a
   next-command one must not be.
5. Check the model it runs on: Actions › model roles (the gear on the model chip) › Advanced ›
   "Next-command and next-prompt suggestions" follows the Flash tier. Note which model that is for your
   provider and confirm your key can use it.
6. Make it fail on purpose — set the Suggestions role to a provider you have no key for, or a model name
   that does not exist — and run a command: the status line must name the call and the model
   ("Next-command suggestion failed (…): …"), the prompt box must stay usable, and an agent turn running at
   the same time must **not** be marked as finished or errored by it.
7. Start an agent turn, then make a suggestion fail during it: the turn must keep running and the pane must
   still show as busy.
8. With suggestions on, check that nothing is sent when the prompt box is not empty (type something before
   the command finishes): no ghost text replaces what you typed.
9. On the owner's own provider and key, repeat 1 and 3 on the KDE desktop.
