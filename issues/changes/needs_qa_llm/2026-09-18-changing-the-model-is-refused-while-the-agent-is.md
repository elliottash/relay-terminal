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
acceptance: 'A model switch (chip, /model, /glm, /kimi, the palette, Alt+F / /flash / /main) is accepted while a turn runs; the request in flight finishes on the old model; the next request of the same turn goes to the new model with the whole conversation; the transcript marks where it took effect; `tests/test_model_switch.py` passes; live glm-5.3 -> glm-5.3-flash and glm-5.3 -> kimi-k3 both finish the turn'
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
- **A conversation that does not fit the new window even after compaction** fails that request like
  any over-long conversation. Compaction trims tool outputs and summarizes older turns first.
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

## Known gaps

- The context bar shows the old model's window until the switch lands; the chip shows the new model
  at once. That is deliberate (the window shown is the one in force) but could read as a mismatch.
- `model_applied` for a turn-end landing has no `turn_id` (the supervisor applies it between turns).
- The phone client (`app/app.js`) receives `model_applied` but does not render it yet.
