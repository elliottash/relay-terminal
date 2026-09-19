# Evidence — guest sessions sources `claude` / `codex` (protocol §26.7) (#GT7X)

Child: `sessions-index`. Branch `orchestrator/sessions-index`, worktree
`/home/elliott/repos/wt-sessions-index`.

Change: `backend/relay_core/guest_sessions.py` (new, 771 lines) parses the guests' own
transcripts into the protocol §26.7 record
`{source, id, title, mtime, workspace, message_count, resume_command}`, and
`backend/relay_core/conv_index.py` gained the two guest source kinds, `update_guest()` and
`guest_file_stamps()` (the source listing I own). No C++ and no Conversations UI in wave 1; the
resume-spawn call sites go to the lead at integration.

## What was run (2026-09-19, implementer)

- `./scripts/test.sh` (worktree root) → see "Test results" below.
- `python3 docs/qa_evidence/2026-09-19-claude-codex-guest-integration/sessions-index-measure-real-data.py`
  (read-only; `--limit N` for the newest N files per source, `--json` for raw numbers).
- No C++ touched, so no build dir and no ctest; no GUI change, so no Xvfb run. The pane wiring
  that would show this on screen lands with the lead.

## Test results

- `tests.test_guest_sessions` → `Ran 51 tests … OK` (698 lines, was 49 tests; two added for the
  mtime skip).
- `./scripts/test.sh` → `Ran 2283 tests in 265.638s, OK (skipped=12)` — the whole backend suite,
  including the pre-existing `tests/test_conv_index.py`, passes with the `SOURCES` change.
- One earlier run of the same script failed once on
  `tests.test_remote_meetcode.CodePhaseTests.test_the_pin_never_reaches_the_rendezvous_or_the_audit_log`,
  which asserts a 4-digit PIN never appears in the traffic the rendezvous saw. It is flaky and
  unrelated: the registration challenge is `secrets.token_urlsafe(24)`, so any given 4-digit
  string has roughly a 0.24 % chance of appearing in a challenge, and that run found the PIN
  inside one. Alone it passed 120 times in a row, the file passed 3 further runs, and the final
  full run passed. Not touched by me — flagged rather than papered over.

## Measurements over the real data (read-only, this machine)

claude (`~/.claude/projects`), full history:

- 7 project directories; 77 session transcripts (501.3 MiB, 126k lines) — plus 508 nested
  `subagents/` transcripts (1046 MiB) that are not sessions of their own and are not listed,
  the same line the index draws between a conversation and a subagent thread.
- 77 records, 7 workspaces, every one titled (58 `ai`, 7 `custom`, 12 first-prompt), none empty.
- 32,024 conversation messages (median 269 per session, max 3,066) → 21,754 entries.
- `scan_claude` 1.19 s (≈15 ms/file, ≈420 MiB/s of transcript).

codex (`~/.codex`): 1 rollout (0.9 MiB) and `state_5.sqlite` (1 `threads` row, read in 0.5 ms);
`scan_codex` 3 ms.

Index (`ConversationIndex` in a temporary directory):

- cold `reconcile()` 2.14 s → 78 rows added (the extra ~0.95 s over the scan is writing 21,794
  entries into SQLite); index 20.1 MiB, ≈270 KB per record.
- warm `reconcile()` **1 ms** → nothing changed. Transcripts whose indexed mtime matches are not
  read at all, so the second and later reconciles cost a `stat()` per file.
- `list_sessions(limit=1000)` 1.6 ms; `search_sessions("the")` 45 ms for 50 items.
- live tail: first read of a running claude transcript 20 ms, then 0.02 ms per no-op `refresh()`;
  codex 3.5 ms and 0.01 ms.

## New behaviour pinned by tests

- Record shape and `resume_command` (`claude -r <id> [--fork-session]`, `codex resume <id>` /
  `codex fork <id>`), including `ValueError` for an unknown guest or an empty id.
- claude: prompt/reply/tool-call extraction, tool-result carriers and sidechains excluded,
  title precedence `custom > ai > summary > first prompt`, workspace from the transcript's own
  `cwd`, id from the file name when the transcript is empty, slug encoding.
- codex: `session_meta`/`turn_context` workspace, developer messages excluded, `function_call`
  entries, thread `name > title > first_user_message` from the highest `state_*.sqlite`, a
  missing/busy/alien database as "no metadata".
- Index: both sources reconcile and list, search spans them (and only them when asked), a guest
  row is workspace-scoped like any other, a rebuild keeps guest rows, `delete_session` drops the
  row and never the guest's file, a rename survives a re-index, a vanished transcript drops its
  row, and an unchanged mtime is not re-read (content rewritten under a kept mtime is skipped;
  a touch brings it in).
- `LiveTail`: a running session appears and grows, a partial line waits for its newline,
  a truncated transcript is read again, a missing file is empty, and codex takes its name from
  the threads database.
- `tests/fixtures/guest/` holds one real-shaped transcript of each guest (plus `state_5.sqlite`);
  parsing them fails if either format drifts.

## Deviations from §26.7

None in behaviour. Two notes for the lead:

1. §26.7's live tail tails the jsonl read-only, as specified; nothing writes `guest.json` and
   nothing writes into `~/.claude` or `~/.codex` (`codex_thread_meta` opens the threads database
   with `mode=ro`, so a running codex is never blocked).
2. `resume_command` is complete and correct; the UI call sites (proposed in my report to the
   lead) are `session_protocol._conversations()` → `guest_sessions.list_sessions(index, scope=...)`
   / `search_sessions(...)`, spawning `record["resume_command"]` in the chosen pane, and
   `guest_sessions.reconcile(index)` plus `guest_sessions.LiveTail(path)` for the active pane.
