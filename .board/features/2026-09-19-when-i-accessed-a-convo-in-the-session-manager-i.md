---
id: 0TJ9
type: work
status: needs-verification
labels: [bug, terminal]
assignee: claude-code
implemented_by: kimi/kimi-k3
session: 8999d43e-c422-4f2e-bb2e-24245e08c5a3
priority: 2
rank: zzzzzzzzzzzzr
created: '2026-09-19'
links: {plans: [], commits: [73d50015, 590c20c0, d435d4a7, 4bcf9708, 49431dcf, 5fdc3705, 7dd13583, d99d0b20, 29833089, fed72147, 3c9375b1], evidence: [docs/qa_evidence/2026-09-20-session-resume-scrollback/], related: [], github: null}
---
# when i accessed a convo in the session manager, i couldnt scroll back

## Issue
when i accessed a convo in the session manager, i couldnt scroll back

## Findings (reproduced 2026-09-20)

Evidence: `docs/qa_evidence/2026-09-20-session-resume-scrollback/` (Xvfb + xdotool driver,
screenshots, state dumps).

Run 1: a pane with terminal content quits cleanly; `windows.json` pairs the pane's `session_id`
with its `scrollback` id and `state/scrollback/<id>.txt` holds the text (#SB7K works).
Run 2: fresh start (`--workspace`, old pane gone as it is days later), session manager →
Shift+Enter on the session: the new pane shows "Session loaded: … · 1 turn(s)" and **nothing
above it**; five PageUps change nothing.

Two layers, both measured:

1. **The resume path never looks the text up.** `Pane::openSavedSession` →
   `RelayWindow::openFork` → `createPane({cwd, workspace})`: the new pane's scrollback id is its
   own fresh token (Pane constructor), no `scrollback` key in the spec, so
   `m_restoredScrollback` stays empty. The layout's session-id ↔ scrollback-id pairing is only
   ever consumed by a full layout restore, never by the session manager.
2. **The text is gone soon anyway.** Run 2's own first layout write pruned run 1's scrollback
   file: `WindowManager::writeWindows` keeps only ids named by the live layout and the
   recently-closed list. A session whose pane has left the layout loses its saved text at the
   next layout write — so teaching resume to look up the old id alone would find nothing in
   exactly the case the owner hit.

## Plan
Rewritten 2026-09-20 by claude-code after the owner's second round of answers (thread). It replaces
the plan of 18:12Z; what changed and why is in the thread entry beside it.

**Goal.** Opening a conversation from the sessions manager shows what was there before, for every
session and every source. Terminal text is saved per *session*, not per pane. What a rewind undid
is kept, searchable and reconstructable. Everything saved is found by the sessions manager's
full-text search.

**Findings** (beyond the two layers above)
- Full-text search already exists and is already sqlite: `backend/relay_core/conv_index.py` is an
  FTS5 index (`index.db`) over prompts, replies, tool calls, tool output and Relay-run commands, for
  Relay sessions *and* claude/codex guests (`guest_sessions.py` parses their jsonl into the same
  `entries` rows). It is a cache by design — wiped on a schema bump, on corruption and by "rebuild
  the conversation index" — so nothing may live *only* there. Files are the record; sqlite indexes
  them.
- A rewind really loses text: `agent.rewind` (`agent.py:2834`) cuts `self.messages`, deletes the
  later snapshots and autosaves, so the next reconcile drops those rows from the index too.
- `ConversationIndex.conversation()` (`conv_index.py:2024`) already returns any conversation —
  agent, claude or codex — as one list of `{turn, kind, time, status, text}`. That is the common
  format and the "translator in"; the GUI's preview already consumes it.
- `state/scrollback/` cannot hold per-session text: `pruneScrollback` deletes every `*.txt` there
  that the live layout does not name, and `isScrollbackId` refuses any id with a prefix.
- `SessionStore.delete` (`sessions.py:267`) removes `<id>.json` and `<id>.meta.json` by name.

**Steps**
1. **One store, one format, keyed by (source, id)** — `relay::sessiontext` beside
   `windowstate` (`src/WindowState.{h,cpp}`): `path(source, id, sessionDir)`, `write`, `read`,
   reusing `clampScrollback` and the 5,000-line / 512 KiB caps, `QSaveFile`, 0600, plain UTF-8.
   Relay session: `<session_dir>/<id>.scrollback.txt`. Guest: `relay/sessions/guests/<source>/<id>.scrollback.txt`
   — same format, same code, same index kind; only the directory differs, because a guest has no
   Relay session directory and Relay does not write inside `~/.claude` / `~/.codex`
   (`guest-meta.json` is the precedent for Relay keeping a guest's sidecar in its own tree).
2. **Saves are by session.** `Pane::saveScrollback()` also writes the session's file whenever the
   pane holds a session (Relay's `m_sessionId` or `m_guestSession`). It is additionally called
   when the pane's session *changes* — "Resume here", `/new`, clear, a fork taking the pane, a guest
   exiting — under the outgoing id, before anything is replaced. The pane records the scrollback
   line it was at when a session began in it, and saves from that line, so session B's file does
   not start with session A's text.
3. **Replay on resume, with the transcript as the fallback.** `openSavedSession` (both "Resume
   here" and "Open in new pane") and the guest resume path read the session's file into
   `m_restoredScrollback`; the existing replay prints it, under a rule that says it is the
   conversation's saved text (the current wording is about "this pane's previous shell"). With no
   file — every session saved before this lands, a crash, a session made from the phone — the pane
   asks `conversation_get` and prints those entries through the same inline printer a live turn
   uses (prompt, reply, one line per tool call), capped at the same line budget, newest kept. One
   renderer for agent, claude and codex. Skipped when the pane is already showing that session.
4. **A fork carries the text.** On `fork_state` (and the guest hook that reports a forked id) the
   pane writes what it is holding under the new id; the parent's file is untouched.
5. **A rewind keeps what it undid — worker side.** `agent.rewind` appends one record to
   `<session_dir>/<id>.rewound.jsonl` before it cuts: `{at, turn, restore, epoch, prompt,
   messages: [the dropped messages, verbatim], restored_files, conflicts}`. `turn`/`epoch` are the
   location, so the rewound branch can be put back where it was. The GUI, on `rewound`, saves the
   pane's text from that turn's first line onward to `<id>.rewound-<n>.scrollback.txt`, `n` being the
   record's number, which the `rewound` event carries as `rewound_n`. Capped: newest 20 records per session.
6. **Index both** (`conv_index.py`, `SCHEMA_VERSION` 6, backfilled by `reconcile()` as v3 did):
   entry kinds `rewound` (from the jsonl, carrying its turn) and `terminal_text` (the saved text, in
   ~40-line chunks). Both rank below message text; `rewound` hits are labelled as rewound in the
   sessions manager's preview, and `has:rewound` filters for them. Guests get the same two kinds
   from their files under `sessions/guests/`. `reconcile()` stats the sidecars the way it stats
   session files, so an unchanged one is not re-read.
7. **Delete with the conversation, in the worker.** `SessionStore.delete` and
   `ConversationIndex.delete_session` remove `<id>.scrollback.txt`, `<id>.rewound.jsonl` and
   `<id>.rewound-*.scrollback.txt` (and the guest equivalents), so a delete from the phone or
   another window leaves nothing behind. Rename and pin untouched.
8. **Docs.** `docs/AGENT-SESSIONS-PROTOCOL.md` section 14 (the two kinds, `has:rewound`, the
   sidecar names) and `docs/ARCHITECTURE.md` (sessiontext store).

**Risks**
- Step 2's "line the session began at" is approximate once more than the engine's 20,000 lines have
  scrolled; the save then starts at the oldest line still held. Acceptable: the cap is 5,000.
- Replaying text into a pane whose shell is live ("Resume here") goes through the same
  idle-at-prompt gate the restart replay uses; a busy shell delays it to the next prompt.
- Index growth: 512 KiB a session worst case, ~8 KiB measured average (9 files, 72 K); against
  72 MB of session JSON already indexed it is noise. No compression, no second database.
- `src/Pane.h`, `src/Conversations.*` and `conv_index.py` are all being edited by live sessions
  (#PF4K/#MDSG); claim late and land in small commits.

**Verify**
- pytest: `tests/test_conv_index.py` (both kinds indexed, ranked below body, backfilled from v5,
  dropped on delete, unchanged sidecar not re-read), a rewind test that the jsonl holds the dropped
  messages and the turn, `tests/test_sessions.py` delete removes every sidecar.
- C++: `windowstate`/sessiontext unit tests (path per source, clamp, id validation); pane tests:
  resume with a file replays it, without one prints the transcript, session switch saves under the
  outgoing id and starts the next file clean, fork writes the new id, guest round-trip.
- Live: extend `docs/qa_evidence/2026-09-20-session-resume-scrollback/drive.sh` — resume an old
  session with no file (transcript appears), quit and resume (saved text appears, PageUp scrolls),
  rewind then search the sessions manager for a word only the rewound turn held.

## QA checklist
- [ ] Sessions manager → Shift+Enter (or Enter) on a conversation you quit earlier today: its terminal text is above "Session loaded", under the "saved terminal text from this conversation" rule, and PageUp scrolls it.
- [ ] Open a conversation from before 2026-09-20 (no saved text): the transcript is re-rendered — your prompts, the replies, one line per tool call — not an empty pane.
- [ ] Run session A, then `/new` in the same pane, then quit: `<session_dir>/<A>.scrollback.txt` and `<B>.scrollback.txt` both exist and B's does not start with A's text.
- [ ] Rewind a conversation: `<id>.rewound.jsonl` gains a record, `<id>.rewound-<n>.scrollback.txt` appears; a sessions-manager search for a word only the undone turn held finds it, and `has:rewound` lists the conversation.
- [ ] Search for a word that only appeared in terminal output: the conversation is found and the preview shows the matching terminal chunk.
- [ ] Delete the conversation from the sessions manager: every sidecar beside its `.json` is gone.
- [ ] Fork a conversation: the fork's pane shows the parent's text and the parent's file is unchanged.
- [ ] Resume a claude or codex session: the same rule and text; its file is under `sessions/guests/<source>/`.
- [ ] First worker start after the update spends ~3 s on the v6 backfill (688 conversations); later passes are ~13 ms.
