---
id: 1V4F
type: work
status: planned
rank: zzzzzzzzzzzzzzzi
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# codex bugs

## Issue
codex bug, it couldnt see previous conversation thread when i swtiched to it mid convo.

example: 798d406f5d60481a89cd76b2c0ac7b0c

▸ gpt-5.6-terra
What would you like me to continue with? I don’t have the preceding task details in this chat.


it also couldnt access tools:

The Switchboard board_* tools were not available in this session, so I could not append commit links or move #VWSD to needs-verification.

session: bb2d13c855b94b2c87c91ced1e437da6

## Plan
### Goal

A pane switched onto a guest (Codex/Claude Code) mid-conversation must give that guest the conversation so far, so its first turn answers what the user asked instead of replying "I don't have the preceding task details in this chat". Relay's own transcript stays exactly what the user and the models said — the handover is context on the guest's prompt, not a fake message in the pane.

### Findings

- `backend/relay_core/guest_harness_provider.py:522` — `HarnessProvider.complete()` calls `last_user_message(messages)` (`:962`) and sends **only that prompt** through `harness.send()`. That is right while the guest ran the whole conversation (it holds its own session); it is wrong for a guest that has seen none of it.
- `backend/relay_core/session_protocol.py:286` — `SessionCommands._set_model()`, guest branch (`:317`): when the pane was on another model or another guest, it starts a **fresh** harness through `guest_harness_provider.start_provider` (`:383`). `start_provider` passes no conversation, and the contract's `Harness.start()` (`backend/relay_core/guest_harness.py`) takes only `cwd/model/resume/fork/permissions/effort` — there is no way to hand a harness a transcript.
- `resume`/`fork` come only from the request's `guest` block: the model box stages a model and nothing else (`src/Pane.h:10676`), while a Sessions-pane guest *row* stages `resume`/`fork` (`src/Pane.h:10732` `guestResumeFrom`, `:10764`). So a mid-conversation model switch always lands on an empty guest session while the pane shows the full history — the owner's example (session `798d406f5d60481a89cd76b2c0ac7b0c`, a bare prompt answered by `gpt-5.6-terra` with "I don't have the preceding task details in this chat").
- `docs/AGENT-SESSIONS-PROTOCOL.md:4869-4876` states today's rule ("takes the last user message"; "the Relay conversation is kept; the guest's context is not"), so the fix is a §29.3 change and the doc moves with it.
- Relay already has the convention for machine-written text on a prompt: `agent.CONTEXT_OPEN`/`CONTEXT_CLOSE` (`backend/relay_core/agent.py:201`), which `conv_index.py:101` knows to ignore. The brief is prepended inside `complete()`, so it never enters `agent.messages` and the index and the transcript stay clean.

### Steps

1. **Renderer.** Add `carry_over(messages, limit=...)` to `backend/relay_core/guest_harness_provider.py`, beside `last_user_message` (`:962`): the earlier turns as `You:` / `Assistant:` lines, each tool call as one line (Relay's own label from `tool_labels`, no tool output), wrapped in `agent.CONTEXT_OPEN` … `agent.CONTEXT_CLOSE`, opened by one imperative line ("You are taking over a conversation that ran on another model. Continue it; do not summarise it back."). Caps: last 20 turns, at most 48 KiB, per-message truncation, and an explicit `… (earlier turns omitted)` marker. Returns `""` when there is no earlier turn.
2. **State.** `HarnessProvider` (`:440`) gets `self.briefed: bool`; `start_provider` (`:383`) sets it `False` for a fresh harness and `True` when the request carried `guest.resume`/`guest.fork` (the user asked for that guest's own session, 29.4). Reuse of a live harness (`switch_model`, `:1177`) leaves it as it is.
3. **The handover.** In `complete()` (`:522`): when `not self.briefed` and `messages` holds an earlier turn, `prompt = carry_over(messages) + prompt`; emit one `status` event naming the guest, the turns handed over and `truncated` when the cap bit; set `self.briefed = True` immediately before `harness.send()`, so a failed first turn is not briefed twice on a live harness and a restarted harness is briefed again. Attachments and every other path are unchanged; a pane that is not on a guest is untouched.
4. **Tests.** `tests/test_guest_harness_provider.py`, on `tests/guest_harness_fake.py` (`sent[0]["prompt"]` is the surface, no test starts a real guest): a fresh provider over a multi-turn transcript briefs the first prompt (earlier user/assistant text present, the user's own prompt last) and sends the second turn bare; a provider that ran the conversation from its first turn never briefs; a harness started with `guest.resume` never briefs; the cap and the `truncated` marker; exactly one `status` event. Plus one case in `tests/test_session_protocol.py` (guest class near `:564`): a mid-conversation `set_model` onto `guest:codex` leaves the guest's first prompt carrying the transcript.
5. **Docs.** Rewrite the §29.3 paragraph at `docs/AGENT-SESSIONS-PROTOCOL.md:4869-4876` and the module docstring of `guest_harness_provider.py`: a guest that has not seen this conversation is given it once, as a Relay context block, and the status line says so.
6. **Only if the owner answers the question below the other way:** when a switch returns a pane to a guest whose own session this conversation already ran on, start the harness with that id as `resume` (`guest_harness_provider.session_guest(agent.session_data())`) rather than fresh.

### Risks

- The brief spends the guest's own context window; capped at 20 turns / 48 KiB and reported in the status line. A recap on the chores role (`suggestions.recap`) is the follow-up if that is ever too much, not part of this fix.
- A guest that ran part of the conversation, was left and is switched back to, is briefed on the *whole* transcript and may see its own earlier turns twice. Accepted for a uniform rule ("a harness that has not been given this conversation is briefed on it"); step 6 is the alternative.
- Wording matters: a brief that reads like a task list gets summarised back. One imperative line, asserted in the tests.
- Existing tests that assert an exact `sent[0]["prompt"]` over a multi-turn transcript will need updating; single-message ones are unaffected.
- **Question for the owner (1):** when a pane switches back onto a guest whose own session this conversation already ran on (codex → GLM → codex), should Relay resume the guest's own session — its own memory, cheaper, but it misses the turns the other model ran — or start fresh and brief the whole transcript? *Recommendation: brief a fresh one (step 6 stays out of this card); resume-on-return is a separate improvement.*

### Verify

- `python3 -m pytest tests/test_guest_harness_provider.py tests/test_session_protocol.py -x` — targeted, and no test starts a real guest (29.5).
- `scripts/relay-build` first (never `cmake --build` by hand).
- Manual, one Codex turn, which spends the owner's subscription: under Xvfb with an isolated `XDG_CONFIG_HOME`, hold a two-turn conversation on a hosted model, switch the model to Codex, ask "continue" — the reply is informed and a status line says the conversation was handed over.
- Evidence under `docs/qa_evidence/YYYY-MM-DD-guest-carries-conversation/`, with the QA checklist in this card.
