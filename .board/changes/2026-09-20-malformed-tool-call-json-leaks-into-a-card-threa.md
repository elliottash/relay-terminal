---
id: VN69
type: work
status: needs-verification
labels: [bug, switchboard, agent]
implemented_by: glm/glm-5.3-flash
rank: zzzzzzzzzzzzzzz
created: '2026-09-20'
source: pane 2, 2026-09-20 (noticed on the way)
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-strip-tool-json-fragments/], related: [], github: null}
---
# Malformed tool-call JSON leaks into a card thread's agent reply

## Issue
Found while diagnosing the page-agent question flow: a card Discuss turn's reply text, written into the card's thread, contained leaked tool-call JSON fragments instead of clean prose.

## Evidence
Measured 2026-09-20T04:06Z, glm-5.3, board worker turn `6f0f9a44…/601fb424…` (Discuss on #Y2JW).

`issues/threads/Y2JW.md`, entry `20260920T040604Z-st` (`kind=comment mode=discuss`), begins:

```
The owner answered all three. Recording their decisions on the card:


The Decisions text got truncated by a stray quote — fixing the section:_resolver: fe"tool_use_id": "toolu_bdrk_01FnV1wBKtgZ6ggKyUqNuqbB"
```

followed later in the same entry by `Malformed calls on my own — redoing the section fix
properly:` and then the clean reply. The `_resolver: fe` / `"tool_use_id": …` fragments are
pieces of a malformed `board_update_card` tool call, not prose: the model broke a JSON argument
mid-string and the turn's accumulated answer text (what `_card_answer` writes to the thread)
carried the salvage as text. Worker log shows the turn's `board_update_card` calls all `ok=True`
around it (04:05:31–04:05:55Z), so this is the text path, not a failed tool.

Suspect area: the answer accumulation for card turns in `backend/relay_core/board_turns.py` /
`board_protocol.py` (`_card_answer`) and whatever salvages a malformed tool call into text in the
agent executor — the fragment should be suppressed or kept out of the card-visible reply.

## Plan
Diagnosis (2026-09-20): a malformed call that *stays* a call is a tool-result error and never text (`agent.py`); the leak rode the **content** path. The glm-5.3 gateway broke a `board_update_card` call mid-string and streamed the rest — an Anthropic-style `{"type": "tool_use", "id": "toolu_bdrk_…"}` block — as content deltas. `provider._stream` concatenates content verbatim; `_tidy_local`/`recover_tool_calls` cover local endpoints only; `board_turns._observe` joins every delta into the answer `_card_answer` writes to the thread. So the fragment reached the thread unexamined.

Fix, three layers:
1. `localtext.strip_tool_fragments(content)` — new: cuts tool-call JSON that was streamed into prose, anchored on shapes only a call takes (`"type": "tool_use"`, `"tool_use_id"`, `"id": "toolu_…"`, `"function": {`), line-wise until prose resumes. Fenced blocks stay (may be shown on purpose); a broken string's tail before the anchor survives (one stray half-word beats guessing backwards and eating prose). The opposite bet from `recover_tool_calls`: that one must see the whole message before it dares run a call; this one only deletes, so it works inside prose.
2. `provider._stream` applies it to the stored message at end of stream (hosted and local; after `_tidy_local`, so whole-message recovery still wins), and `_partial` applies it to what a cut-off step keeps. Deltas already shown stay as they streamed — this cleans what is stored and replayed.
3. `board_turns._observe` strips the joined answer before `on_answer`, so a card thread can never record fragments even if a future provider leaks some other way.

Tests: `tests/test_localtext.py` (measured #Y2JW fragment, whole-message leak, OpenAI envelope, prose-mentions-JSON untouched, fenced block stays), `tests/test_provider.py` (stream stores clean content, real tool calls survive; cut-off partial carries no leak), `tests/test_board_turns.py` (a leaked delta never reaches the card thread).

## QA checklist
- [ ] `strip_tool_fragments` cuts the measured #Y2JW fragment (Anthropic-style block, truncated mid-string) and keeps the prose on both sides — `tests/test_localtext.py:StripTests`.
- [ ] A whole message of leaked call JSON empties to `''`; an OpenAI-envelope leak cuts; prose that merely mentions JSON is untouched; a fenced ```json block stays.
- [ ] A hosted stream that leaks stores clean content and keeps its real `tool_calls` — `tests/test_provider.py:StreamTests`.
- [ ] A cut-off step's partial carries no leaked JSON (and a leak-only partial is dropped).
- [ ] A card turn whose deltas contain the leak writes a clean thread entry — `tests/test_board_turns.py`.
- [ ] Live pane display: deltas still show what the provider streamed, as documented in the Plan — accepted, not a defect.
- [ ] Regression: `tests.test_localtext tests.test_board_turns tests.test_provider` (87) green; `test_plan_turns` (13) and `test_provider_local` (22) green; `test_board_protocol` has 2 failures that also fail on the pre-change snapshot (card stage moves, unrelated — another session's work).
