---
id: WFJM
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui, worker, providers]
milestone: desktop-alpha
workstream: providers
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: '"Ask the agent" on a Switchboard card reaches the same provider a pane reaches, whatever the settings say after a model switch; a 401 anywhere names the model, the host and the preset and says the key was rejected; `tests/test_configure_provider.py` (13) is green and fails on the old code; `ctest` and `./scripts/test.sh` pass'
source: 'owner, feature intake 2026-09-18: "-- ''ask the agent'' didnt work. it said provider HTTP 401."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-the-board-asks-on-the-panes-provider/'], related: [], github: null}
---
# The Switchboard asks on the pane's provider, not on a settings file half a model behind

## Issue

> "ask the agent" didnt work. it said provider HTTP 401.

Ordinary pane agents worked on the same machine at the same moment, so the board was reaching a
different endpoint — or reaching the right one with the wrong key.

## Findings

**The key and the URL came from settings that drift apart.** `RelayWindow::startBoardWorker`
(`src/main.cpp`) built the board's `configure` out of four independent QSettings values:

| Setting | Written by |
|---|---|
| `provider/preset` | **every** model switch — the model chip, `/model`, the palette (`Pane::selectModel`, and the `model_changed` handler) |
| `provider/base`, `provider/model`, `provider/extra` | only a full re-configure (`Pane::configurePreset`, the provider dialog) |

A model switch between turns sends the new preset's whole endpoint to the pane's own worker over
`set_model` and then writes **only** `provider/preset`. From that moment the four disagree: the
preset says Z.AI Coding Plan, the base URL still says Moonshot. A pane never notices, because a
pane sends the preset's own endpoint on the wire. The board did notice: it named one preset and
carried the other provider's URL, and `session_protocol.provider_config` looks the stored key up
**by the preset name** and then posts it to the **request's** base URL. So the Z.AI key was
presented to `api.moonshot.ai`, which answered — correctly — `HTTP 401`.

`docs/qa_evidence/.../before.log` shows exactly that against a stand-in: `{'endpoint':
'/moonshot', 'presented': '/zai', 'accepted': False}`.

The owner's settings today are consistent again (`preset=glm-coding`, `base=…/coding/paas/v4`),
which is why the board works when checked later: the next launch runs `configurePreset`, which
rewrites all four together. The window in which it fails is "any session after the first model
switch", and the owner's hint counters record three uses of the model chip.

**And the failure said nothing.** `Provider HTTP 401. Check endpoint, model access, key, quota,
and parameters.` does not say which of the user's providers refused, that the answer is a key
rather than a retry, or — the whole point here — that the endpoint was not the one they picked.
The precedent for fixing that is the 2026-09-17 suggestions card, which attributed a failed side
call to its model.

Checked and **not** the cause: the `switchboard` role (it is tiered `main`, so it follows the pane's
model and resolves to the same preset), the tier fall-through, `board.configure`, the board tools,
`RELAY_PANE_ID`, and the keyring itself — `test_key` on `glm-coding` passes in 1.5 s.

## Change

**A named preset is an endpoint** (`docs/AGENT-SESSIONS-PROTOCOL.md` section 1). In `configure` and
`set_model`, `base_url`, `model` and `extra` are now optional when `preset` names a built-in preset:
the preset fills in each one that is missing. This is the rule a `roles` entry has always followed
(13.4); it is now the same rule one level up, so a caller with only "which provider" to say can say
only that, and the key and the URL can no longer come from two places.

- `backend/relay_core/session_protocol.py`, `provider_config`: resolves `base_url`/`model`/`extra`
  from the named preset when they are absent, then looks the key up as before. An endpoint that
  **is** given still wins, so the provider dialog — where the user may point a preset at a proxy by
  hand — is unchanged, as is a custom endpoint with no preset.
- `src/main.cpp`, `startBoardWorker`: when `provider/preset` names a preset, the configure carries
  the preset and no endpoint of its own. `provider/base|model|extra` are still sent for `custom`
  (or an empty preset), which has no preset to resolve. Nothing else about the message changed.
- `backend/relay_core/provider.py`, new `ChatProvider.http_message`: an HTTP failure names the model
  and the host, and matches the endpoint to a preset label when there is one. 401 and 403 get their
  own sentence — "the API key was rejected for glm-5.3 at api.z.ai (Z.AI · GLM-5.3 · Coding Plan).
  The stored key is missing, wrong, or belongs to a different endpoint of the same provider — open
  Settings › Models › API keys… to check it." The response body is still never included: providers
  quote the request they were sent.
- `session_protocol.provider_name`, used by the "no stored key" error, which now says *which*
  provider has no key.

New test file `tests/test_configure_provider.py` (13 tests, no network, no keyring): a preset alone
resolves that preset's endpoint and key; the shape the board sends now cannot pair a key with a
foreign endpoint for **any** preset; the shape it used to send is kept as a description of the bug;
an explicit endpoint still wins; the 401/403 wording; and that no status message can carry the key.
Ten of the thirteen fail against `backend/` at HEAD.

## Not done here, and why

`Pane::selectModel` and the `model_changed` handler still write `provider/preset` without
`provider/base|model|extra`, so those settings stay stale until the next full configure. Nothing
reads them inconsistently any more after this change, but the provider dialog still *shows* the
stale base URL and model as its defaults, which will read as wrong to anyone who opens it after
switching models. That is a two-line fix in `Pane::selectModel` and belongs to whoever owns that
function; this session was scoped to `startBoardWorker`.

## QA checklist

1. **The bug, end to end.** `docs/qa_evidence/2026-09-18-the-board-asks-on-the-panes-provider/run.sh
   before` must end in `Provider HTTP 401` with the stand-in reporting `endpoint: /moonshot,
   presented: /zai, accepted: False`. `run.sh after` must end in `done` with the agent's answer
   appended to the card, and the stand-in must only ever see `/zai` keys at `/zai`. Neither needs a
   key or the network.
2. **In the app.** Put a desynced `[provider]` block in an isolated `XDG_CONFIG_HOME`
   (`preset=glm-coding`, `base=https://api.moonshot.ai/v1`, `model=kimi-k3`), open Relay,
   Ctrl+Shift+S, and confirm the board's `configure` carries `preset` and no `base_url`/`model`
   (`gui-configure.txt` is one such run, recorded from the real GUI).
3. **The real thing.** With a real stored key, switch the pane's model with the chip, then open the
   Switchboard and ask a card a question. It must answer, on the provider the chip now shows.
4. **The 401 reads like a key problem.** `card-401-before.png` and `card-401-after.png` are the same
   app, same card, same refused key. Check the new line names the model, the host and the preset,
   and never contains the key. Check a non-401 status (say a stand-in returning 500) still carries
   "Check endpoint, model access, key, quota, and parameters."
5. **Nothing regressed for panes.** A pane configured from the model list still works; the provider
   dialog with a hand-edited base URL under a named preset still reaches that URL (not the preset's);
   a `custom` preset with only a base URL and model still finds the key of the matching endpoint.
6. **No stored key.** Remove a preset's key, select it, and confirm the error names the provider
   ("No stored key for OpenAI · GPT-6 Astra (openai). Import from Warp or enter a key.") rather than
   "this provider".
7. **Tests.** `./scripts/test.sh` and `ctest --test-dir build`. At the time of writing
   `test_board_protocol.AskTests`, `test_board_tools` and `test_remote_wire` fail from other
   sessions' in-flight work (board cleanup tools, the `## Request` → `## Issue` rename, secret
   input); they fail identically with this change reverted.

## Known gaps

- A preset pointed at a proxy by hand in the provider dialog applies to panes only; the Switchboard
  follows the preset's own base URL. Naming a preset and meaning a different endpoint is exactly the
  ambiguity that caused this bug, so the board takes the unambiguous reading.
- `http_message` matches the endpoint to a preset by URL, so a 401 caused by *this* mismatch names
  the provider the request actually went to, not the one the user thinks they are on. That is the
  useful half of the answer ("you are talking to Moonshot") but it is not spelled out as a mismatch.
- The board still has no way to say "no provider is configured" before the first question; a keyless
  window opens and browses, and only the ask reports it.

## Follow-up: the stale provider settings (2026-09-18, card 3ES1)

The "Not done here" item above is done in card 3ES1's commit. `Pane::rememberPreset` (new, next to
`Pane::selectModel`) writes `provider/preset` together with that preset's own `provider/base`,
`provider/model` and `provider/extra`; `selectModel` and the `model_changed` handler call it instead
of writing the preset alone. After `/kimi` on a glm-coding pane the settings read
`base=https://api.moonshot.ai/v1`, `model=kimi-k3`, `preset=kimi`
(`docs/qa_evidence/2026-09-18-model-switch-mid-turn/implementer-kimi-provider-settings.txt`), so the
provider dialog opens on the model the chip shows. A Main ↔ Flash role switch still leaves them on
the main preset, which is right: the role is not the pane's provider.
