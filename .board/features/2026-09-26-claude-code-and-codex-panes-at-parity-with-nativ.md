---
id: VDWA
type: work
status: planning
labels: [feature, guests, models, routing]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [probe], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'Relay pane cae84571, 2026-09-26 (from #D49C restore discussion)'
links: {plans: [], commits: [], evidence: [], related: [D49C, GT7X, G19V, KZHX], github: null}
---
# Claude Code and Codex panes at parity with native models: lossless handover, recorded reasoning, side jobs and compaction

## Issue
Make guest-harness panes (Claude Code, Codex) equivalent to native-model panes, so drawing or switching between them never loses context or features. Covers the four gaps found on 2026-09-26: the capped handover to a fresh guest, guest tool output recorded into Relay's transcript, guest reasoning not recorded, and guest panes getting no titles/summaries/recaps or compaction unless a separate job model is set.

> file a card with a plan to go through itesm 1-4 and implement parity
> — elliott · [session:258ff0e9de5540e09f638ed1541c9168](relay://session/258ff0e9de5540e09f638ed1541c9168) · 2026-09-26

## Planning notes
What exists (2026-09-26, `backend/relay_core/guest_harness_provider.py`): guest turns write their tool calls/results into `agent.messages` (#1V4F, `record_guest_tools`), a fresh guest mid-conversation gets `handover_brief`, and a pane returning to a guest it ran resumes that session with only the catch-up (#Q8TM, `_guest_cursors`). The four gaps, measured:

1. **Handover to a fresh guest is capped** — `HANDOVER_MAX_CHARS = 160_000` (~40K tokens) and `HANDOVER_TOOL_RESULT_CHARS = 4_000`, while a native model reads the whole `agent.messages` up to its window (then compacts).
2. **Recorded guest tool output** — cut at `RECORDED_TOOL_RESULT_CHARS = 32_768`. This equals native `tools.MAX_OUTPUT` (32768), so a model switched in later sees what it would have seen natively. Already at parity; the work is a test that pins the two constants together, plus checking Codex/Claude structured results (diffs, images) survive `_recorded`.
3. **Guest reasoning is not recorded** — `_on_thinking` emits `thinking_delta` to the UI and counts chars; the text never enters `agent.messages`. Native Kimi/GLM keep it in `reasoning_content` (agent.py ~5535).
4. **Side jobs and compaction** — `serves_side_calls = False`: titles, summaries, recaps and route assist go to the job's own role, and when that role follows main (the default) they get nothing. `_maybe_compact` skips on a guest unless `summaries` has its own model; `_compact_after_turn` never runs on a guest. So a guest pane has no titles/recaps by default, and its Relay transcript grows without bound until a native model is switched in.

## Plan
**Goal.** A pane's conversation is the same whichever model serves it: switching or re-drawing between a guest and a native model loses nothing a native-to-native switch would keep, and a guest pane gets the same side jobs.

**Step 1 — Side jobs on a guest pane (item 4).** In `Agent.side_provider`, when the main provider declines side calls and the job's role follows main, resolve the job's tier instead (summaries/chores → Flash, drawn at rank 1) with guest rows excluded; never return the empty answer. Same rule for `_maybe_compact`'s `summaries` check. Owns `backend/relay_core/agent.py` (side_provider, `_guest_harness` callers) and `roles.py` if a guest-free tier draw needs a helper. Tests: a guest pane with default roles gets a title and a recap from a native model; a guest never serves a side call.

**Step 2 — Compaction on a guest pane (item 4).** The guest keeps its own context, so Relay compacts only its transcript: when `agent.messages` passes the window of the *next* model likely to serve it (Main rank 1's smallest window), compact with the Step 1 summaries provider, marking the compaction so the guest's `_guest_cursors` catch-up still counts messages correctly (the cursor's `messages` index must be remapped or the catch-up falls back to a full brief). Tests: cursor survives a compaction; a switch to native after a long guest run starts under the window.

**Step 3 — Record guest reasoning (item 3).** Accumulate `thinking_delta` text per assistant step in the turn and attach it to the recorded assistant message as `reasoning` (and `reasoning_content` where the next provider needs it, via the existing normalizer at agent.py ~5535). Bound it like native reasoning. Check what each harness actually emits: Claude Code gives thinking blocks, Codex gives reasoning summaries; record what arrives and say which in the doc. Tests with `FakeHarness` thinking events.

**Step 4 — Handover sized to the guest's window (item 1).** Replace the fixed 160K-char / 4K-per-tool caps with a budget from the guest model's context window (the catalog knows it) minus the guest's own system/prompt overhead: send the whole transcript with tool results at `MAX_OUTPUT` when it fits; otherwise the compaction summary (Step 2) plus the most recent messages in full. Tests: small transcript goes verbatim; oversize one gets summary + tail; tokens stay under the window.

**Step 5 — Pin item 2.** Test that `RECORDED_TOOL_RESULT_CHARS == tools.MAX_OUTPUT` and that structured guest results (diff, image reference) round-trip through `_recorded` into a form a native model reads.

**Spike, not in scope unless it proves cheap:** synthesising a native Claude Code / Codex session file from Relay's transcript and `--resume`-ing it, which would make native → guest lossless without a brief. Undocumented formats; record findings only.

**Order.** 1 → 2 (2 needs 1's provider) → 4 (needs 2's summary); 3 and 5 are independent. Update protocol 29.3 in `docs/AGENT-SESSIONS-PROTOCOL.md` with each step. Fix #G19V/#KZHX's red parity test first or alongside, since this work runs that suite.

## Done means
- A guest pane with default job rules gets pane titles, summaries and recaps from a native model drawn from the job's tier; no side call is ever sent to, or silently dropped by, a guest.
- A long guest session's Relay transcript is compacted by Relay; switching that pane to a native model starts under the native model's window, and switching back resumes the guest session with a correct catch-up.
- Guest reasoning text is kept on the recorded assistant messages, so a native model switched in sees it as it would its own.
- A fresh guest started mid-conversation receives the whole transcript when it fits the guest's window, else the compaction summary plus the recent messages in full; no fixed 160K-char / 4K-per-tool cap remains.
- Tests pin recorded guest tool output to native `MAX_OUTPUT`, and protocol 29.3 describes all of the above.
- Failure looks like: a guest pane with no title after its first turn; a native model failing a switch-in on an over-window transcript; a resumed guest re-reading the whole conversation after a compaction.

## Tasks

- [ ] Step 1: side jobs on guest panes resolve the job's tier (guests excluded), never nothing <!-- t:kn -->
- [ ] Step 2: Relay-side compaction of a guest pane's transcript, with the resume cursor remapped <!-- t:rf blocked_by=kn -->
- [ ] Step 3: record guest reasoning on assistant messages <!-- t:f1 -->
- [ ] Step 4: handover to a fresh guest sized to its window (full, or summary + tail) <!-- t:9y blocked_by=rf -->
- [ ] Step 5: pin recorded guest tool output to native MAX_OUTPUT; structured results round-trip <!-- t:fn -->
- [ ] Protocol 29.3 updated; parity tests green (#G19V, #KZHX) <!-- t:qh -->
