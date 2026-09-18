---
id: 3ES1
type: work
status: needs-qa-llm
labels: [bug]
component: [worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: zzzzzr
created: '2026-09-18'
source: issues/bug_intake.txt, 2026-09-18
acceptance: 'A model switch (chip, /model, /glm, /kimi, the palette, Alt+F / /flash / /main) is accepted while a turn runs; the request in flight finishes on the old model; the next request of the same turn goes to the new model with the whole conversation; the transcript marks where it took effect; `tests/test_model_switch.py` passes; live glm-5.3 -> glm-5.3-flash and glm-5.3 -> kimi-k3 both finish the turn; the context bar measures against the new model from the moment of the switch; the phone shows the switch lines and its model indicator follows; a smaller window compacts before the switch (old model summarising) and a switch that cannot fit is refused with the pane left on the current model'
links: {plans: [], commits: [f83d73d], evidence: ['docs/qa_evidence/2026-09-18-model-switch-mid-turn/'], related: [WFJM, EM1E], github: null}
---
# Changing the model is refused while the agent is working

## Issue
i couldnt change the model while the agent was working. that should be allowed and it should cross over just like warp.

Note from filing: the protocol refuses `set_model` while a turn runs (`agent_busy`, docs/AGENT-SESSIONS-PROTOCOL.md section 2), so this is a designed refusal the owner wants lifted: the switch should be accepted mid-turn and take effect as the turn carries on.

## Findings: what the backend could already do

- **Conversation across providers: already converted.** Every preset speaks the OpenAI chat format.
  `agent.adapt_history` (run by `Agent.set_model`) copies reasoning between `reasoning_content`
  (Kimi, GLM; Kimi rejects an assistant tool call without it) and `reasoning` (OpenRouter). Tool-call
  ids are opaque strings each provider accepts from another (a GLM `call_…` id went to Moonshot
  in the live run without complaint). Effort is re-mapped per provider by `set_model` too.
- **A smaller window: already handled by auto-compaction**, which runs at every step boundary. The
  switch only had to land *before* that check.
- **A mid-step switch mechanism already existed for subagents** (`agent_set_model`: a running
  subagent switches before its next model call, via `_SubInbox.drain`). The main agent had none;
  `SessionCommands._set_model` refused with "Stop the active agent turn before switching model",
  and the GUI refused even earlier (`Pane::selectModel`, `/glm`, `/kimi`, `Pane::setAgentRole`).
- **Image turns** (EM1E) already swap the provider for one turn; a switch must not land inside one.

## Change

**Worker.** `set_model` is accepted while a turn runs:

- `TurnSupervisor.now_or_later(now, later)` decides "idle or not" under the supervisor's lock, so a
  turn can neither start nor finish between the check and the switch. Idle: applied at once, as
  before (`model_changed {applies: "now"}`).
- Busy: `Agent.defer_model` stores the switch (`model_changed {applies: "next_step",
  in_flight_model}`). Two switches before the next request: the last one wins. A switch back to the
  model in force drops the pending one.
- `Agent.apply_pending_model` lands it at the top of the next step of the tool loop, after every tool
  result is in and **before** the auto-compaction check and the request, emitting `model_applied
  {at: "step", step, model, from_model, history_converted?, compacts?}` then `context`. The worker's
  follow-ups (subagents that inherit the main model, role defaults) run at that moment.
- A turn that ends without another request: the supervisor applies it right after
  `agent_finished` (`model_applied {at: "turn_end"}`), so it is in force for the next turn and for
  side calls (recaps, suggestions) in between; `done` stays the turn's last event.
- An image turn stays on its vision model to the end; a switch during one says `applies: "turn_end"`.
- A missing stored key is still refused at once, mid-turn or not, with nothing left pending.
- `set_agent_role` (Main ↔ Flash: Alt+F, `/flash`, `/main`) goes through the same path, with its
  own follow-up (it sets the pane's role; it does not rebase the roles as `set_model` does).

**GUI** (`src/main.cpp`). `Pane::selectModel`, `Pane::setAgentRole` and `/glm` / `/kimi` no longer
refuse while busy (a full `configure`, which starts a new conversation, still does). The chip moves
at once. The `model_changed` handler prints `↻ kimi-k3 takes over at the next step · glm-5.3 is not
interrupted` in the transcript (the running clock owns the status line) and keeps the context bar on
the old window until the switch lands; a new `model_applied` handler prints `→ now on kimi-k3 ·
conversation converted from glm-5.3` where it took effect (`· from the next turn` for a turn-end
landing, `· its window is smaller, compacting first` when it triggers compaction).

**WFJM leftover.** `Pane::rememberPreset` writes `provider/preset` **and** the preset's own
`provider/base|model|extra`; `selectModel` and the `model_changed` handler use it, so the provider
dialog no longer shows the first preset's endpoint after a switch (evidence:
`implementer-kimi-provider-settings.txt`).

**Remote.** `model_applied` is classified as forwarded in `remote/wire.py`, like `model_changed`.

### The three gaps, closed (2026-09-18, second pass)

1. **The context bar agrees with the chip from the moment of the switch.** While a switch waits,
   the worker's `context` event carries `next {model, window, limit_tokens, used_tokens, percent,
   will_compact, in_flight_model}` (`Agent.context_event`, from `Agent.switch_fit`), and `set_model`
   / `set_agent_role` are followed by `context` whatever the outcome. The pane
   (`Pane::updateContextLabel`) shows `N% left ↻` against the new window; the tooltip says it is
   measured against the new model's window, that the request in flight is still on the old one (and
   its window) until the switch lands, and, when the conversation is already over the new limit, that
   the switch compacts first — the bar turns amber / reads `0.0% left ↻` at once. `model_applied`
   and `model_switch_refused` clear it.
2. **The phone shows the switch.** `app/app.js` renders `model_changed` (deferred), `model_applied`
   and `model_switch_refused` with the desktop's own wording (`↻ X takes over at the next step · Y is
   not interrupted`, `→ now on X · …`, `✗ …`) in its transcript and prompt-box note, and keeps a
   per-pane model indicator under the pane title (`small · big finishing the current step` while a
   switch waits, accent colour; back to the current model on a refusal). The phone had no model
   indicator before; it now has one, fed by these events. `model_switch_refused` is forwarded
   (`remote/wire.py`).
3. **A smaller window compacts before the switch; a switch that cannot fit is refused.**
   `Agent.apply_pending_model` checks the conversation against the new window before switching.
   Over its limit (`min(auto-compaction limit, window − min(max_tokens, window/4))`): the existing
   compactor (`Agent.compact`, now with `target_window` / `target_max_tokens` / `target_model` /
   `keep_turns`) runs with the **model still in force summarising** and the new window's limit and
   carried-block budgets as the target (`compaction_started/compacted {reason: "model_switch",
   for_model}`), with one more pass keeping only the last turn if a previous turn kept whole is
   still too much; then the switch lands (`model_applied {compacted: true}`). Still over the ceiling
   (one long current turn), or the compaction fails or is stopped: `model_switch_refused {at, model,
   current_model, preset, agent_role?, reason}`, the pane stays on the current model, the turn
   carries on (a stop still stops it). A window that cannot hold the system prompt and tools with
   room for a reply is refused when the switch is asked for, before anything changes
   (`at: "request"`, no `model_changed`). `model_changed` says `will_compact: true` when it can tell
   at the switch. An idle switch that must compact runs as an exclusive task off the protocol
   thread (`applies: "after_compaction"`, `model_applied {at: "now"}`), and so does a turn-end
   landing that must compact, before the next queued turn starts (`TurnSupervisor.
   start_exclusive_locked`). One entry point for both switches: `Agent.request_model` (set_model and
   `set_agent_role`). The GUI prints `✗ <reason>` in the transcript and puts the chip, the role and
   the provider settings back; the phone does the same.

Also closed: `model_applied {at: "turn_end"}` now carries the `turn_id` of the turn it waited for.

**Docs.** `docs/AGENT-SESSIONS-PROTOCOL.md` section 2 (the whole contract) and 12.8 (event order).

**Tests.** `tests/test_model_switch.py` (9, offline): the next step runs on the new model and the
request in flight on the old; last switch wins; switching back cancels; a turn that ends first hands
it to the next turn, after `agent_finished`; a smaller window compacts right after the switch; a
missing key is refused mid-turn; a role switch carries its own follow-up; the idle path is unchanged;
and, against two local HTTP endpoints, the next request goes to the *other* provider with the
OpenRouter-style `reasoning` converted to Kimi's `reasoning_content`. The busy-refusal test in
`tests/test_session_protocol.py` no longer lists `set_model`.

## What cannot cross, and what happens

- **Nothing in the message format was found that cannot cross** between the presets Relay ships;
  conversion is by `adapt_history`. A provider that needs something Relay does not keep (say, signed
  thinking blocks) would fail its first request after the switch with the provider's own error, as
  it would after an idle switch today.
- **A conversation that does not fit the new window even after compaction** is no longer sent to
  fail: the switch is refused (`model_switch_refused`) and the pane stays on the model it was on.
- **The request in flight is never moved.** A switch while the model is streaming its answer lands
  only if the turn makes another request; otherwise it applies from the next turn.

## QA checklist

1. **Same provider, mid-turn.** Configure glm-coding. Ask for two sequential `run_command` calls,
   the first `sleep 25; echo alpha`. While it sleeps press Alt+F. The chip must say `Flash agent ·
   glm-5.3-flash` at once, `↻ glm-5.3-flash takes over at the next step · glm-5.3 is not interrupted`
   must appear, then `→ now on glm-5.3-flash` right after `exit 0`, and the turn must finish. The
   worker log (`~/.local/share/relay/logs/worker.log`) must show `model_applied … at=step step=2
   from_model=glm-5.3 to_model=glm-5.3-flash`. `implementer-driver.sh flash` does all of this.
2. **Cross provider.** Same, but type `/kimi` + Enter while it sleeps. Expect `→ now on kimi-k3 ·
   conversation converted from glm-5.3`, `host=api.moonshot.ai` on the `model_applied` log line, and
   a normal answer (no HTTP 400 about `reasoning_content`). `implementer-driver.sh kimi`.
3. **Turn ends first.** Ask something answered without tools and switch while it is thinking. Expect
   `→ now on … · from the next turn` after the answer, and the next prompt served by the new model.
4. **Last one wins.** Switch twice during the sleep (e.g. `/kimi`, then `/glm`): only one `→ now on`
   line, for the last model. Switch away and back: no `→ now on` line at all.
5. **Nothing else loosened.** The provider dialog (a full configure), rewind, fork, resume and
   `/compact` are still refused while a turn runs. Esc still stops a turn mid-switch.
6. **Image turn.** Attach an image with a vision model configured, switch during it: the line says
   `after this turn`, the image turn finishes on the vision model, then `→ now on … · from the next turn`.
7. **Provider dialog (WFJM).** After `/kimi`, open the provider dialog: base URL and model are the
   Moonshot ones, not Z.AI's.
8. **Tests.** `./scripts/test.sh` and `ctest`. `tests/test_model_switch.py` fails on the old code
   (the switch was refused).
9. **Context bar at the switch (gap 1).** `gaps-driver.sh compact` (offline: `fake-provider.py` as
   three local endpoints, no key). Right after `/model small` mid-turn the chip says `small · local`
   and the bar reads `0.0% left ↻`, amber; hovering it says `Measured against small's window, which
   serves the next request`, `Over it: the switch compacts the conversation first` and `The request
   in flight is still on big (131,072-token window)`. After `→ now on small` the ↻ is gone.
10. **Compact before switching (gap 3).** Same run: `↻ small takes over at the next step · big is not
    interrupted · will compact to fit`, then after `exit 0` two `Conversation compacted (to fit
    small's window)` lines, `→ now on small · compacted to fit its window`, and the answer from small.
    `gaps-compact-requests.jsonl`: the summary calls (`tools: false`) went to `big`, and small's first
    request is a few thousand characters, not the 65k the conversation was.
11. **Refused (gap 3).** `gaps-driver.sh refuse`: idle `/model micro` prints `✗ micro cannot take
    over: its 2,048-token window does not hold the system prompt and tools …` and the chip stays
    `big · local`. Then a turn with one 60k-character answer: `/model small` during its command is
    refused after `exit 0` (`✗ small cannot take over: even compacted …`), the chip goes back to
    `big · local`, and the turn finishes `Done on big`. No request ever went to small.
12. **Phone (gap 2).** `tests/test_remote_browser.py` `test_a_model_switch_mid_turn_shows_where_it_lands`,
    and `phone-shots.py` for screenshots at a phone's size: the three lines, the indicator `small ·
    big finishing the current step` (accent) and then `small`, and a refusal in red with the
    indicator on the model kept. With a real desktop (screen stream) the lines arrive in the terminal
    as the desktop prints them; the indicator and note come from the events.

## Known gaps

None of the three listed on the first pass is left (see "The three gaps, closed" above).

- The phone's model indicator appears with the first model event of a session it watches; the pane
  list it gets on connecting carries no model. Adding one means a `model` field in the pane list the
  desktop publishes (`src/RemoteShare.*`, `remote/gui_host.py`, `remote/panes.py`), which are another
  session's files with uncommitted work in them today; left for that session or a follow-up card.
