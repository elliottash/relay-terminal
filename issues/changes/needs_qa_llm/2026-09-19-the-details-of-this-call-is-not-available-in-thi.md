---
id: EC58
type: work
status: needs-qa-llm
rank: zzzzzt
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [TK9C], github: null}
assignee: agent
implemented_by: Claude Fable 5.1 (batch review), 2026-09-19
---
# "The details of this call is not available in this call any more" bug.

## Issue
when i click on tooltipps it sometimes says: "The details of this call is not available in this call any more" 

seemsl 9ike a bug and should be fixed

## Plan
**Goal.** Clicking a tool-call line (fold or open-call) must not answer "The detail of this call is not available in this pane any more." while the worker still has the output. The click should fetch and show the call's detail even for old or restored lines.

**Findings.**
- The message the user sees (they paraphrased it as "The details of this call is not available in this call any more") is one of two status lines in `src/Pane.h`: `foldRequested()` at ~3318 and `openCallTarget()` at ~3391.
- Both look the clicked anchor up in `m_calls` (`QHash<QString, CallRecord>`, Pane.h:11659). That map is **bounded to 400 entries** (`rememberCall()`, Pane.h:3257–3261) and is **in-memory only**: a pane restored from saved scrollback (`WindowState` scrollback, Pane.h:431) shows the old `relay://call/…` / `relay://open-call/…` lines but `m_calls` is empty. So every old call line, and every line in a restored pane, hits the "not available any more" branch — hence "sometimes".
- The miss is largely unnecessary: the anchor URI itself is `relay://call/<pane>/<turn>/<call>` (CallLines.h, `kOpenPrefix` at :36, parse at Pane.h:1164), i.e. it already carries `turn_id` and `call_id`. `m_calls` is only really needed for the rich label, merged-run members and stored diffs.
- Worker side: `Agent.tool_output()` (`backend/relay_core/agent.py:918`) raises "Unknown turn_id (only the last 50 turns are kept)"; the GUI already names that case separately (Pane.h:2832), so the worker bound is *not* the message the user hits. The conversation index (`backend/relay_core/conv_index.py:140`) also stores `tool_call`/`tool_output` content and could be a deeper fallback for turns older than 50.

**Steps.**
1. In `foldRequested()` and `openCallTarget()` (`src/Pane.h` ~3311, ~3352): when `m_calls` has no record, parse the anchor URI (`calllines` ref) and, if it yields a valid `turn` and a call id, synthesize a minimal `CallRecord` (turnId + callIds from the URI, no label/diff/members) and proceed to send `tool_output_get` as today. Show the returned output with a generic summary ("tool call · <call-id>") instead of the rich label. Keep the "not available" message only when the URI itself doesn't parse or `m_workerReady` is false.
2. Raise the `m_calls` bound from 400 to something a long session won't hit (e.g. 5000) in `rememberCall()` — cheap insurance so labels/merged runs keep working further back.
3. Merged runs and diff opens still need the real record: if the record is missing and the line was a merged run or diff, fall back to the single-call fetch of step 1 using the URI's call id rather than failing outright.
4. (Optional, ask owner first) Worker fallback in `Agent.tool_output()`: when `turn_log` no longer holds the turn, look the call up in the conversation index (`conv_index.py`) and answer from there instead of raising. This extends detail access past 50 turns; the index caps content, so the reply may be truncated.

**Risks.**
- A synthesized record has no `label`: fold rendering and the details pane must tolerate an invalid/empty label (check `foldOptions()` and the `fold-` reply handler at Pane.h:3327–3348).
- Requiring the URI's call id assumes old/restored anchors always carry it — verify against restored scrollback in a live session before relying on it.
- Question for the owner: is step 4 (conversation-index fallback for turns older than 50) wanted, or is fixing the GUI-side miss (steps 1–3) enough for this card? Recommendation: do steps 1–3 now, file step 4 separately if it still bites.

**Verify.**
- `./scripts/test.sh` and `ctest --test-dir build` (FoldLayer tests live in `tests/FoldLayerTest.cpp`; add coverage for the URI-fallback path, ideally as a unit test on the record-lookup helper).
- Live under Xvfb with isolated `XDG_CONFIG_HOME`: (a) run an agent session past 400 tool calls, scroll up, click an early call line — its detail opens; (b) quit and restore a pane with call lines, click one — its detail opens from the worker instead of the error status.
- Update `docs/AGENT-SESSIONS-PROTOCOL.md` §23.6 only if the `open` behaviour changes; otherwise no protocol change (no new messages are added by steps 1–3).

## Implemented (steps 1–3), 2026-09-19

`src/Pane.h`:

- `foldRequested()` and `openCallTarget()` now take the call id from the anchor URI
  (`relay://call/<pane>/<turn>/<call>`, which a merged run spells with its *first* member's id)
  whenever `m_calls` has no record. Only a URI that names no call, an empty turn, or a worker that
  is not ready still shows "not available in this pane any more". A record that is there is still
  preferred: it carries the rich label, the merged run's members and the stored diff.
- `rememberCall()`'s bound went from 400 to 5,000. 400 is a day's work, and every line past it lost
  its label and its diff pane; a record is a label and a few strings.
- `handleFoldError()`: when the fetch was made from the anchor alone, the worker's own "Unknown
  turn_id (only the last 50 turns are kept)" is only half the reason — a pane restored from saved
  scrollback shows lines from before this worker existed, and no number of turns brings those back.
  The fold now says both.

`tests/calllines_test.cpp::everyAnchorCarriesEnoughToRefetchTheCall` pins what the fallback rests
on: every anchor a row can carry — single, merged, and ids needing percent-encoding — parses back to
a non-empty turn and a single call id.

**Step 4 is still the owner's to decide** and is deliberately not done: a worker-side fallback to
`backend/relay_core/conv_index.py` for turns older than the 50 `Agent.turn_log` keeps. The
recommendation in the plan stands — file it separately if it still bites. Without it, a call from a
previous run of Relay cannot be re-opened at all; the fold now says so in those words.

## QA checklist

- [ ] A live session: scroll back past a few hundred tool calls, click an early call line — its
      detail opens rather than the "not available in this pane" note.
- [ ] Quit Relay with a pane holding call lines and restart it: clicking a restored call line gives
      the two-reason note ("before the pane's current agent … not kept across a restart"), not the
      worker's bare "only the last 50 turns are kept" and not "not available in this pane".
- [ ] A merged run's row (`read 6 files`) still unfolds to its member lines while the pane is alive,
      and after the record is gone falls back to the first member's detail rather than failing.
- [ ] Clicking a `relay://open-call` line whose record has gone opens the call's output in a preview
      pane.
- [ ] `ctest --test-dir build -R calllines` passes (41 cases).
