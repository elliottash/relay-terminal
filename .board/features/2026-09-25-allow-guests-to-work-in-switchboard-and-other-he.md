---
id: E34S
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
session: 43e383b8-1d85-4df5-a410-a559dfc39565
rank: zzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'remote: iOS Safari'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-e34s-guest-helper/], related: [], github: null}
---
# Allow guests to work in switchboard and other helper agents

## Issue
For some reason that’s not allowed. We should make it work, potentially with exceptions

## Done means
With the window's model on a guest preset (`guest:claude` / `guest:codex`), the Switchboard helper and the other helper consoles (Options, Actions, Sessions) run on that guest: they keep their helper brief, take board/app/activity tools through the `relay_board` bridge, and never show the "helper never starts a guest" refusal (#GH5T). Picking a guest preset in a helper's model box sticks instead of being refused. One deliberate exception: a per-card conversation (card console) still cannot be a second agent on the guest's harness — it opens on the Models priority list, or shows today's refusal sentence when that list is empty. Failure would be recognised as the helper console printing the old refusal text, or the helper answering but its `board_*`/`app_*` calls failing as out-of-scope.

## Plan
**Goal.** Let a helper agent (the Switchboard/Board helper and the Options, Actions, Sessions helper consoles — one lazy worker per tab, `src/RelayWindow.h:4932` `helperWorker`) run on a guest harness (`guest:claude`, `guest:codex`) exactly as a pane agent does, instead of being diverted away from it. The card's "potentially with exceptions" becomes one kept exception: card consoles.

**Findings.** The ban is card #GH5T and predates the `relay_board` MCP bridge, so its stated reason ("the helper's whole job is Relay's own `board_*` and `app_*` tools, which a guest does not take") is obsolete — guests now take board/app/activity/exec tools through the bridge (`backend/relay_core/guest_board_bridge.py`, wired per start in `backend/relay_core/guest_harness_provider.py:818-882`, scope-checked on dispatch). The refusal lives in four places:

- `backend/worker.py:361-412` — `configure`: a helper whose resolved config is a guest never starts one; `resolver.leave_guest(...)` walks the Models priority list, and with no spare an `UnavailableProvider` stand-in shows `helper_refusal` text with `agent_role` forced to `"main"`.
- `backend/relay_core/roles.py:1265` (HELPER_ROLE docs) and `RoleResolver.leave_guest` — the helper is moved off a guest onto the fallback list.
- `backend/relay_core/board_protocol.py:665` `refuse_model_selection` — a helper model-box pick naming a guest is refused (`model_switch_refused`, reason `helper_refusal`).
- `backend/relay_core/board_protocol.py:703` `_usable_config` / `_build_card_console` — a card console raises `ValueError(GHP.helper_refusal(...))` when the Main config is a guest: a guest harness is one process = one agent ("not an endpoint"), so a second Relay agent cannot be built from it. This one is mechanical, not policy.

**Steps.**

1. `backend/worker.py` (configure, ~355-412): when the resolved helper config is a guest, run the pane path — `guest_harness_provider.start_provider(...)` then `attach(agent, provider)` — keeping `agent_role="helper"`. Delete the `resolver.leave_guest` diversion and the stand-in for this case; keep `UnavailableProvider` for genuine start failures. The helper gets its own `relay_board` bridge with the helper's scope, as `start_provider` builds one per harness.
2. `backend/relay_core/roles.py`: `leave_guest` no longer special-cases `agent_role == "helper"`; update the HELPER_ROLE docstring and the `#GH5T` comments.
3. `backend/relay_core/board_protocol.py:665` `refuse_model_selection`: drop the helper-on-guest refusal so a guest pick in a helper's model box sticks (other refusals unchanged).
4. Card console exception — `board_protocol.py:703` `_usable_config` + `_build_card_console`: instead of raising, resolve a guest Main through `resolver.leave_guest`'s priority list (the same walk the helper used); when that list is empty, surface today's `helper_refusal` sentence in the card conversation.
5. `backend/relay_core/guest_harness_provider.py:181` `helper_refusal`: reword to say only card conversations cannot run on the guest (still used by step 4's empty case); leave `UnavailableProvider` as is.
6. Comments that state the old rule: `src/AgentContext.h:29` ("a guest harness cannot run Relay's tools (#GH5T)"), `backend/relay_core/guest_instructions.py:14-17`, the `worker.py` block comment.
7. Tests: rewrite `tests/test_guest_harness_provider.py:1475+` (helper worker now starts the guest, keeps role `helper`, gets a bound bridge), `tests/test_roles.py:933+` (leave_guest helper case goes; keep the fallback-walk tests for the card-console path), `tests/test_card_model_selection.py:38-59` (guest pick in a helper box allowed; card console falls back / refuses only on an empty list). Add one `configure` test that a helper on a guest harness serves a board tool call through the bridge.

**Risks.**

- **Card-console fallback is a choice.** Fallback-to-priority-list is recommended (it is what the helper did today); the alternative is keeping the hard refusal there. Owner can flip this in review.
- **Concurrency limits.** Each tab's helper is lazy (starts at the first ask, `src/RelayWindow.h:4988`) but a guest helper is one more live claude/codex session per tab; subscription session limits may refuse, which shows in the helper console rather than silently degrading.
- **Guest asks and diffs.** A guest helper raises approvals/diff-asks (26.5) on the helper console's ask row; verify the shape there, since guests ask more often than native helpers.
- **Side calls.** A guest harness serves no side calls (as on panes today), so helper-conversation titles/summaries degrade the same way.

**Verify.** `pytest tests/test_guest_harness_provider.py tests/test_roles.py tests/test_card_model_selection.py` plus the worker configure tests, then by hand: set the window's preset to `guest:claude`, open Switchboard, ask its helper "list my cards" — it must answer through the bridge (no refusal sentence); open a card conversation — it must open on the fallback preset; pick `guest:codex` in a helper's model box — it must stick.

## Execution Summary
Landed in `98823eb` (9 paths; the verify slot built the exact tree before the swap). A stray one-path landing of the `agent.py` comment (`d87c23c0`) was taken back by `repair` (`c0fcb08`) and its hunk rides in `98823eb`.

- `worker.py` — the `leave_guest` diversion, the spare and the `UnavailableProvider` stand-in are gone. A helper following a guest Main starts that guest as its own agent (`agent_role` reports `"main"`, the same as a following-Main helper today); a helper's own guest pick (the worker reconfigures on its box pick's preset) starts that guest; a pinned non-guest role still wins with no start; a helper guest that cannot start is one `error` on configure, exactly a pane's.
- `roles.py` — guest entries resolve for `switchboard` (picks and candidates; the tier-skip was the one `switchboard`-specific refusal left). `leave_guest` is now a non-mutating question — "where does a card console go?" — no rebase, and `main_note` went with it. `HELPER_ROLE` docs say the new rule.
- `board_protocol.py` — `refuse_model_selection` keeps only the #BMS1 mid-turn refusal, so a guest pick in a helper's model box sticks. `_usable_config` became `_console_model(main)`: a card console on a guest helper takes the priority list's first usable entry (config, preset and effort together, so `_build_card_console` and `_sync_card_model` both move cleanly); an empty list raises the sentence, which the card conversation shows as before.
- `guest_harness_provider.py` — `helper_refusal` reworded for the one case left (a card console with nothing on the list); `UnavailableProvider` removed with its reason.
- Comments restated the old rule in `src/AgentContext.h`, `agent.py` and the `worker.py` block comment; all rewritten.

The plan's step 1 said "keeping `agent_role=\"helper\"`" — the landed code reports `"main"`, which is what a helper following Main already reported before this card (`state[\"agent_role\"]` describes the model, not the brief; the helper brief rides on the context). No behavior hangs on it.

## Tests
All on a clean export of `98823eb` (not the working tree): `PYTHONPATH=backend:tests python3.12 -m unittest tests.test_guest_harness_provider tests.test_roles tests.test_card_model_selection` — **170 tests, OK**. In the checkout on the same bytes: `tests.test_configure_provider tests.test_board_protocol` — 198 tests, OK. Evidence: `docs/qa_evidence/2026-09-25-e34s-guest-helper/`.

- `test_guest_harness_provider.WorkerProtocolTests` — a helper worker on a guest preset starts the guest, hands it the board bridge, and its roles summary follows the guest; the helper's own guest pick starts that guest; a `board_list` call over the helper's wiring answers from the workspace's `.board`; a helper guest that cannot start is one `error`, no stand-in; the pane cases and the #MH7P stand-in rule (re-stated with a local stub) unchanged.
- `test_roles.LeaveGuestTests` — `leave_guest` answers with the first usable entry and never rebases; guest rows / keyless rows skipped in order; Relay Free only when named and working; nothing usable → `None`; a helper pick naming a guest resolves; the helper's candidate list may choose a guest.
- `test_card_model_selection` — a guest pick is allowed when the board is idle and still refused mid-turn; `_console_model` takes the priority list; the empty list raises the sentence; a cached card moves to the spare model and keeps its history.

## Try it
Stage and open (stage.sh materializes the project and the profile; run.sh opens the window and prints its display):

```
docs/qa_evidence/2026-09-25-e34s-guest-helper/scenario/stage.sh /tmp/rl-e34s
RELAY_BIN=$PWD/build/relay /tmp/rl-e34s/home/run.sh
```

The window opens on the staged project with **Claude Code as the Main model** and `relay-free` on the Options › Models priority list; the project's Board holds three cards (#W1A2 Water the office plants, #W3B4 Sharpen the pencils, #W5C6 File the receipts). The agent's pass (`scenario/me-pass.sh`, captures in `../captures/`) already confirmed: the window configures on the guest, the Board opens with the three cards listed, and the Board agent's console sits on `claude-opus-5.5` with no refusal anywhere.

**Your task** (Ctrl+Shift+A opens the Board if it is not open): in the Board agent's box at the bottom, ask **"list my cards"** and press Enter.

**Question:** does the guest helper answer with the three staged cards — through the bridge, as its own agent now — and is this a helper you would use with Claude Code as your Main? (Before this card the same ask answered with the refusal; the sealed expectation is `scenario/expected.md`, and the agent's pass findings are noted under it.)
