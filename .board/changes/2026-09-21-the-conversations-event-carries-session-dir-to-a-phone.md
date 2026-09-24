---
id: SDR1
type: work
status: needs-verification
labels: [bug, remote]
component: [remote]
assignee: agent
implemented_by: kimi/kimi-k3
session: bfdb8216-0f4a-4401-b281-ee5c1e7d9e06
priority: 1
rank: m
created: '2026-09-21'
source: 'Claude Code session on #SWPH, 2026-09-21: found by the hosted drive while grepping every frame the phone decrypted for machine paths'
links: {plans: [], commits: [a3594364, 4e4b7783, 69cb5a35], evidence: [docs/qa_evidence/2026-09-21-swph-hosted-drive/notes.txt, docs/qa_evidence/2026-09-24-sdr1-conversations-scrub/notes.txt, docs/qa_evidence/2026-09-24-sdr1-conversations-scrub/], related: [SWPH, 0VT4, W5N2], github: null}
---
# The pane agent's `conversations` event carries `session_dir`, an absolute path, to `full` devices

## Issue

`docs/REMOTE-PROTOCOL.md` says no path crosses the wire. The #SWPH drive's path check (step 12)
passed for every `board_event`, and while making it the drive saw that the pane agent's
`conversations` event, forwarded to `full` devices, carries `session_dir`: an absolute path under
the owner's data directory. It predates #SWPH. The forwarding filter in `remote/wire.py` /
`remote/host.py` should strip it (the phone opens a conversation by the per-publish token in
`pane_state`, never by a path), with a test that greps a forwarded `conversations` for `/`.

## Done means
A `conversations` event from a pane agent reaches a `full` device — live fan-out and the replay ring a resume reads — with `session_dir` and every other machine path (`workspace`, `cwd`, `raw_cwd`, `resume_cwd`, `files`, `resume_command`) stripped at every depth, while conversation ids and titles survive and the desktop's own event object is not mutated. Failure shows as a forwarded or replayed frame grepping positive for an absolute path under the owner's data directory, or as the phone's Conversations sheet losing titles it had before.

## Execution Summary
Scrub local conversation metadata before inserting an event into the replay ring or forwarding it. Preserve conversation titles and IDs without mutating the desktop's original event. Real-socket regression checks live forwarding and replay.

## Tests
- `tests/test_remote_gui_host.py::ComposeTests::test_conversation_locations_never_enter_wire_or_replay`

## Plan
**Goal.** No absolute path leaves the desktop in a pane agent's `conversations` event — not in the live fan-out to a `full` device, and not in the replay ring a device reads on `resume` — with the phone's usable metadata (ids, titles) intact.

**Findings.**
- `remote/host.py:1068` already defines `_scrub(event)`: for a `conversations` event it recursively walks dicts and lists and drops the keys `session_dir`, `workspace`, `cwd`, `raw_cwd`, `resume_cwd`, `files`, `path`, `resume_command`, `fork_command` at every depth. It is called at `remote/host.py:1062`, on the single fan-out path every agent event takes, so it runs before both live send and replay-ring insertion.
- `tests/test_remote_gui_host.py::ComposeTests::test_conversation_locations_never_enter_wire_or_replay` (line 146) already exists, over a real socket: it sends a `conversations` event laden with `/private/...` values (including nested `threads`), then asserts the forwarded frame and the replay ring contain no `/private`, that the title survives, and that the source event still holds its `session_dir` (no mutation of the desktop's copy).
- `remote/wire.py:543` already classifies `conversations` as FULL, so only owner-level devices receive it at all.
- This matches the card's Execution Summary: the codex session that claimed the card on 2026-09-22 (see thread) appears to have landed the fix and the test in the tree, but the card was never moved out of `executing`. The work here is mostly verification, not new code.

**Steps.**
1. Establish what is committed: `git log --oneline -5 -- remote/host.py tests/test_remote_gui_host.py` and `git status -- remote/host.py tests/test_remote_gui_host.py`. If either file has uncommitted hunks, they are another session's — do not commit them yourself; per `CLAUDE.md` use `scripts/land.py` only for your own changes.
2. Run the regression: `python3 -m pytest tests/test_remote_gui_host.py::ComposeTests::test_conversation_locations_never_enter_wire_or_replay -v` (or the equivalent `unittest` invocation). It must pass.
3. If the test fails or the scrub is partial — e.g. a path still reaches the replay ring, a key from the Done-means list is missing, or the source event is mutated — fix `_scrub` in `remote/host.py` (deep-copy before stripping; never mutate the caller's event) until the test passes. Keep new keys consistent with the path-key list `docs/REMOTE-PROTOCOL.md` §17.1 already publishes.
4. Check `docs/REMOTE-PROTOCOL.md` §16/§17 for the statement that no path crosses the wire; if it names the `conversations` fields explicitly, make the doc and `_scrub`'s key set agree.
5. Land per policy: this is a small/medium bug fix — if the code was already committed and only the card lagged, move the card with a thread note citing the passing test run; if you changed code, commit with `scripts/land.py` and move the card in the same commit.

**Risks.**
- The scrub and test may be uncommitted work of the codex session that claimed the card. Editing or committing over another session's hunks in this shared checkout is exactly what `scripts/land.py`'s claim markers exist to prevent — run `python3 scripts/land.py who` before touching either file.
- Over-stripping is the other failure direction: the phone's Conversations sheet needs ids and titles, so the fix must strip the named path keys, not whole rows.

**Verify.** `tests/test_remote_gui_host.py::ComposeTests::test_conversation_locations_never_enter_wire_or_replay` passes; it is itself the end-to-end check (real socket, live frame plus replay ring, grep for `/private`).
