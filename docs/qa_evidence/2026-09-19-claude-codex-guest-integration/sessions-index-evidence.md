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

## Reviewed again, 2026-09-19 (second pass)

Re-measured read-only on the same machine: `~/.claude/projects` is now **1.6 GiB** over 7 project
directories and **626** `.jsonl` files, of which **546 (87 %) are nested `subagents/` transcripts**
that `_claude_paths`' depth-1 glob correctly excludes — **80** are sessions. The largest single
transcript is **98.9 MB**, and any mtime change re-reads it whole (the active pane's transcript
changes every turn), which is the one real cost left in a warm reconcile; `LiveTail` already holds
the incremental state that would fix it but nothing shares it with `reconcile`. `~/.codex/sessions`
is 904 KiB, one rollout; `state_5.sqlite` is 236 KiB.

Six correctness fixes landed in `backend/relay_core/guest_sessions.py`, with tests:

* a full reconcile no longer prunes a source whose **session directory is absent** (a wrong or
  late `$HOME` used to empty the pane, and a guest row's pin and custom title live only in the
  index), nor a transcript that is on disk but could not be stat'ed or parsed;
* `limit=0` read *everything* and `limit=-1` silently dropped the oldest file (`[:limit or None]`);
* `claude_workspace_from_slug` decodes against the real directories, so
  `-home-elliott-repos-relay-terminal` is `…/relay-terminal` and not `…/relay/terminal` — four of
  the seven real project directories were being decoded to paths that do not exist, which is the
  row's `workspace` **and** its `resume_cwd`;
* `codex_live_transcript` takes the newest matching thread (the `SELECT … FROM threads` has no
  `ORDER BY`, so it was following whichever row sqlite returned first — usually the oldest);
* `LiveTail` notices a transcript replaced by one of the same size (st_dev/st_ino), and `_by_age`
  really does break ties by name;
* `to_record` and `item_to_record` agree on what an untitled session is called.

The privacy surface is now pinned by a test and written down in §26.7: indexing a guest session
copies its title, prompts, assistant text and tool-call names into Relay's `entries` table and its
FTS index. There is no guest-specific opt-out.

## Wired up, 2026-09-19 (third pass)

`guest_sessions` has a production caller now (commit below). What the review listed as missing:

* `session_protocol._conversations()` answers from the index first, annotates the guest rows
  (`annotate_items`, plus a `fork_command` beside `resume_command`) and sets
  `guest_sessions.reconcile()` going on a worker thread — never in front of the answer. One
  rescan at a time, at most one every `GUEST_RECONCILE_EVERY` (5 s), and the follow-up
  `conversations` event is built for the *latest* request, not the one that triggered it.
* `sources` is validated against the five real sources and the error names all five. A request
  that names no guest source still gets Relay's own rows only, so the phone's session list and
  every other client are unchanged.
* A guest id no longer goes through `sessions.check_id`: `_conversation_id` accepts an id the
  index holds under a guest source, so `conversation_get` / `_delete` / `_rename` / `_pin` work
  on a dashed UUID while junk is still "Invalid session id.".
* Rename and pin write the index row (`rename` / `set_pinned`), delete drops the rows only
  (`remove_files=False`). Nothing under `~/.claude` or `~/.codex` is written, and a test asserts
  the transcript's bytes and its directory listing are unchanged after a rename, a pin and a
  delete.
* The Sessions pane's Kind filter lists "Claude Code sessions" and "Codex sessions"; "Everything"
  now names all four sources explicitly. A guest row shows its guest where a session shows its
  model, resumes with Enter (in this pane, behind a `cd` to `resume_cwd`), Shift+Enter (a new pane
  created in that directory) and Ctrl+Enter (`fork_command`, always a new pane). The ⓘ view and
  the summary button are off for guest rows, which have no Relay session file to read.

Measured on a synthetic home in `tests.test_session_protocol.GuestSessionRows`
(`test_a_warm_reconcile_reads_nothing`): 120 claude sessions, cold reconcile ~30 ms, warm 0 ms
(`added/refreshed/removed` all zero — no transcript is opened). The owner's real
`~/.claude/projects` was only sized read-only for this pass: 636 transcripts, 1.7 GB, which is
why the first reconcile may not sit in front of a listing.

**What is still not wired.** `guest_sessions.LiveTail` has no caller: a running guest session's
row is refreshed by the next reconcile (its transcript's mtime changed) rather than tailed, so it
updates when the pane next queries and not keystroke by keystroke. Tailing the active pane's
transcript is the remaining half of the card's sessions task and is listed there as its own item.

## Deviations from §26.7

None in behaviour. Two notes for the lead:

1. §26.7's live tail tails the jsonl read-only, as specified; nothing writes `guest.json` and
   nothing writes into `~/.claude` or `~/.codex` (`codex_thread_meta` opens the threads database
   with `mode=ro`, so a running codex is never blocked).
2. `resume_command` is complete and correct; the UI call sites (proposed in my report to the
   lead) are `session_protocol._conversations()` → `guest_sessions.list_sessions(index, scope=...)`
   / `search_sessions(...)`, spawning `record["resume_command"]` in the chosen pane, and
   `guest_sessions.reconcile(index)` plus `guest_sessions.LiveTail(path)` for the active pane.
