---
id: PCJY
type: work
status: needs-verification
labels: [bug, guest, sessions]
assignee: agent
implemented_by: glm/glm-5.3
session: afe16443-c85c-42ba-be04-5bba174541b5
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: system, primary: script, also: [probe], human: none, criteria: 'A guest preset pane restored by a restart resumes its previous guest session (claude --resume), verified in logs; the new unittests pass.', sign_off: none, effort: medium, stakes: rework}
source: pane 1, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Restored guest panes start a fresh session instead of resuming after a restart

## Issue
A reloaded guest-harness pane (Claude Code / Codex) starts a fresh session instead of resuming after a Relay restart: the claude process is relaunched under the old `--session-id` but without `--resume`, so the agent loses its whole context (pane 83e17c22 after the 2026-09-25 desktop restart: "This is a fresh session with no link to the killed one"). Native-model panes resume their conversation; guest panes must resume the guest's own session too.

> "when relay restarts and reloads panes, does it resume the previous sessions, or does it start a new session linked to the old one? check out what the agent told me here where it said it was a fresh session: 83e17c22 i think the sessions should be resumed."
> — elliott · [session:7ee825d4ed664990a8061ca061768f8e](relay://session/7ee825d4ed664990a8061ca061768f8e) · 2026-09-25

## Done means
A pane on a guest preset (Claude Code / Codex), restored by a Relay restart, gets its previous guest session back: the harness starts with `--resume <previous session id>` (claude) / the codex equivalent instead of a fresh session under the reused id. The conversation-level resume path (`resume`) and the state-blob path (`load_state`) both restore the guest cursor bookkeeping, and the pane's configure request for the restored guest preset carries `guest.resume`. Covered cases: (1) conversation resumed at restore, guest preset applied later (deferred until first prompt or re-picked); (2) no conversation resume at all (hard kill) — the layout still names the pane's last guest session, and the deferred guest configure resumes it.

Out of scope: replaying a native conversation into a guest (the existing #Q8TM handover), and the Sessions-list resume (already works via `resumeGuestPreset`).
Both commits landed on main: `01affa57` (worker event fields, `_load_state` bookkeeping, pane staging, tests) and `59635ce2` (layout `guest_session`).

## Plan
1. `backend/relay_core/agent.py` `resume()`: the `state_loaded` event carries the loaded conversation's `guest` and `guest_session` when present, so a pane that resumed a conversation can tell the GUI which guest session to resume.
2. `backend/relay_core/session_protocol.py` `_load_state()`: after `agent.load_state()`, run the same guest bookkeeping `_resume()` runs (`guest_harness_provider.resume_session` with the store's copy), so cursors load on the state-blob path too.
3. `src/RelayWindow.h` `serializeNode()`: save the pane's current guest session id (`guest_session`) in the pane spec while it runs a guest preset.
4. `src/Pane.h`: `initRestore()` reads `guest_session`; the pane remembers the guest session named by a `state_loaded` resume; when the deferred restored guest preset is applied (`startDeferred` → `configurePreset`), stage `guest.resume` from that remembered id (layout fallback), consumed once — the existing `resumeGuestPreset` shape.
5. Tests: backend pytest for 1+2; the GUI staging follows the existing `resumeGuestPreset` path covered by editor tests where practical.

## Tasks
- [x] state_loaded carries guest/guest_session from the loaded conversation <!-- t:wr -->
- [x] _load_state runs the guest resume bookkeeping like _resume <!-- t:0q -->
- [x] Layout saves the pane's guest_session; initRestore reads it <!-- t:kn -->
- [x] Deferred guest configure stages guest.resume for the restored session <!-- t:sb -->
- [x] Tests for the worker-side changes <!-- t:2c -->


## Execution Summary
Diagnosis (verified in `~/.local/share/relay/logs/{relay,worker}.log`, 2026-09-25 16:23–16:27Z): native-model panes restored by a restart DO resume — `resumeRestoredSession()` sends `resume <session_id>`, the worker loads the conversation (`state_loaded`) and replays it to the API. Guest panes (Claude Code/Codex via subscription) do not: every harness started `guest_harness_started … resumed=False cursor_resumed=False`, relaunched under the previous `--session-id` but without `--resume` — Claude Code treats that as a new session, so the agent's memory is empty (pane 83e17c22: fresh conversation 46dc7f7d, and even where the conversation did resume — panes 1f8636de/51f880d9/43172b9f — the guest session still started fresh). The resume knobs existed (worker honours `guest.resume` on a configure, #Q8TM cursors on a model switch, `resumeGuestPreset` for the sessions list) but the restore path never turned one of them.

Fix: `resume()` and `load_state()` now report the conversation's own `guest`/`guest_session`/`guest_account` in `state_loaded`, and `_load_state` runs the same guest bookkeeping `_resume` runs (`resume_session`), so cursors load on both paths. The layout saves the pane's current guest session (`guest_session` in the pane spec, held through the deferred-restore wait like `session_id`), `initRestore` reads it, and the first configure of that same guest (account included) stages `guest.resume` — consumed once, so a later New chat starts fresh. A configure whose resume fails still surfaces the harness error rather than silently starting empty.

Not done here: no C++ unit test exercises `takeGuestRequest` staging — no Pane test harness exists (editor tests cover standalone modules only); the worker side of the seam is tested on both ends instead. `test_compact_resume_recap_and_plan_execute` fails on pristine HEAD too (unrelated, filed separately); `test_the_backend_directory_is_appended_to_pythonpath_only_once` fails on a source guard in `PaneRuntime.cpp` (untouched by this card, pre-existing).

## Tests
- `tests/test_sessions.py::SessionTests::test_resume_names_the_conversations_own_guest_session` — `state_loaded` carries guest/guest_session/guest_account (passed, `python3 -m unittest`, 2026-09-25).
- `tests/test_sessions.py::SessionTests::test_resume_of_a_native_conversation_names_no_guest` — a native resume names no guest (passed).
- `tests/test_sessions.py::SessionTests::test_load_state_of_a_guest_conversation_names_its_guest_session` — the ref path reports them too (passed).
- `tests/test_session_protocol.py::ProtocolHandlerTests::test_load_state_of_a_guest_conversation_keeps_the_guest_cursor` — `_load_state` runs `resume_session`, `_guest_cursors` set (passed).
- `tests/test_guest_handover.py` full file (15 tests) — the worker path that consumes `guest.resume` on a configure (passed).
- `ctest -R "windowstate|panestate|guestbridge|panetabnavigation"` — 4/4 passed; `scripts/relay-build` built clean, and land.py's verify slot built the exact landing tree.
- Not covered: the C++ staging in `takeGuestRequest` has no unit test — no Pane test harness exists; both ends of the seam (the worker's `guest.resume` handling and the event fields it consumes) are tested.

### Check 2026-09-25 14:10
- missing-evidence · unittest:tests.test_sessions.SessionTests.test_resume_names_the_conversations_own_guest_session — no run of tests/test_sessions.py::SessionTests::test_resume_names_the_conversations_own_guest_session for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_sessions.SessionTests.test_resume_of_a_native_conversation_names_no_guest — no run of tests/test_sessions.py::SessionTests::test_resume_of_a_native_conversation_names_no_guest for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_sessions.SessionTests.test_load_state_of_a_guest_conversation_names_its_guest_session — no run of tests/test_sessions.py::SessionTests::test_load_state_of_a_guest_conversation_names_its_guest_session for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_session_protocol.ProtocolHandlerTests.test_load_state_of_a_guest_conversation_keeps_the_guest_cursor — no run of tests/test_session_protocol.py::ProtocolHandlerTests::test_load_state_of_a_guest_conversation_keeps_the_guest_cursor for this revision, from any host, and no attached result
- missing-evidence · ctest:windowstate|panestate|guestbridge|panetabnavigation — no run of ctest -R windowstate|panestate|guestbridge|panetabnavigation for this revision, from any host, and no attached result
- notice · unittest:tests.test_sessions.SessionTests.test_resume_names_the_conversations_own_guest_session — tests/test_sessions.py::SessionTests::test_resume_names_the_conversations_own_guest_session has never run here
- notice · unittest:tests.test_sessions.SessionTests.test_resume_of_a_native_conversation_names_no_guest — tests/test_sessions.py::SessionTests::test_resume_of_a_native_conversation_names_no_guest has never run here
- notice · unittest:tests.test_sessions.SessionTests.test_load_state_of_a_guest_conversation_names_its_guest_session — tests/test_sessions.py::SessionTests::test_load_state_of_a_guest_conversation_names_its_guest_session has never run here
- notice · unittest:tests.test_session_protocol.ProtocolHandlerTests.test_load_state_of_a_guest_conversation_keeps_the_guest_cursor — tests/test_session_protocol.py::ProtocolHandlerTests::test_load_state_of_a_guest_conversation_keeps_the_guest_cursor has never run here
- notice · ctest:windowstate|panestate|guestbridge|panetabnavigation — ctest -R windowstate|panestate|guestbridge|panetabnavigation: 1 of 4 never ran here (guestbridge)
history: thread
## Try it
Staged by the implementer (no verifying session had staged anything). The run dir is `/home/elliott/.cache/relay/scratch/tryit/pcjy`; the fixture is `docs/qa_evidence/2026-09-25-tryit-PCJY/`.

**One task:** run `bash docs/qa_evidence/2026-09-25-tryit-PCJY/stage.sh` (no arguments, no model, no network). It prints the same restored guest pane twice — once against the backend before the fix, once after. Compare what each prints for `state_loaded names the guest:` and `harness start:` with `docs/qa_evidence/2026-09-25-tryit-PCJY/expected.md`. (Already run once by the implementer; see staging-notes.md for what is and is not covered headless.)

**One question:** After a restart, a restored guest pane now resumes the guest's previous session by default (a "New chat" still starts fresh). Is resuming the previous session the default you want after every restart, or would you rather it ask first when the session is older than some age?
