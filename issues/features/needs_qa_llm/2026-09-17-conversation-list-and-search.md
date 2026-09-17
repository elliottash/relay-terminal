---
id: CCKY
type: work
status: needs-qa-llm
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (Claude Code, conversation-search worktree), 2026-09-17
rank: i1
created: '2026-09-17'
acceptance: a list of past conversations across panes and projects, searchable by any word in any message or tool output, opening the match in context and resuming it in a pane
source: '`issues/feature_intake.txt`, 2026-09-17: "need a conversation list. this needs to be full text searchable (that is a big downside of warp / codex / etc, finding old convos is so hard). lets plan a much better system." and "what hot key for that? ctrl shift y is warp. see what the others do or what would be a chrome like hotkey."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Conversation list with full-text search

## Today

`/resume` lists sessions for the current workspace only: title, turn count, time, open tasks. There is no
search, no cross-project view, and no way to find the turn that mentioned a word.

## Proposed

1. **Index**: an SQLite FTS index beside the session files (`~/.local/share/relay/index.db`), updated on
   autosave; every user prompt, agent reply, tool call and (capped) tool output, with session id, turn,
   workspace, model, time. Rebuildable from the session JSON, so it is a cache, never the source of truth.
2. **List pane / dialog**: all conversations, newest first, grouped by project, with filters (this project,
   all projects, model, has open tasks, date). Each row: title, first prompt, turn count, open tasks.
3. **Search**: type to search titles and message text; results show the matching lines with context and the
   turn number; Enter opens the conversation in a preview with the match highlighted; a second key resumes it
   in the current pane (or a new pane).
4. **Titles**: keep the existing generated title; allow renaming and pinning.
5. **Privacy**: the index holds message text, so it lives under the same 0700 directory and is never synced.
   A "Delete conversation" action removes the session, its blobs and its index rows.

## Open questions
1. Hotkey. Taken in Relay today: Ctrl+Shift+A palette, Ctrl+Shift+K tasks, Ctrl+H / Ctrl+Shift+H control,
   Ctrl+Shift+S (planned Switchboard), Ctrl+Shift+W restore closed, Ctrl+Shift+X stop subagents.
   Elsewhere: Chrome history is Ctrl+H (taken here), Warp is Ctrl+Shift+Y for a new chat, VS Code opens files
   with Ctrl+P, Claude Code uses `/resume` and Ctrl+R for shell history. **Recommended: Ctrl+Shift+O** ("open
   conversation"), free in all four Relay presets, plus `/conversations` and a palette entry.
2. Scope of the default view: this project (recommended) or all projects?
3. Should terminal command history be searchable in the same list, or stay separate?
4. Retention: keep everything (recommended) or prune after N months, with a setting?

## Decisions (owner, 2026-09-17)
- **Ctrl+Shift+O** opens the conversation list. Its search covers **both** terminal history (commands and
  output) and agent threads; results say which they are.
- **Ctrl+F** searches the current conversation and this pane's terminal scrollback (find in view, with
  next/previous and a match count), separate from the global list.
- Remaining recommendations stand unless the owner says otherwise: default view is this project, everything
  is kept, and the index lives beside the sessions.

## Implemented (2026-09-17)

All five proposals landed, with the owner's decisions. Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md`
section 14. Evidence: `docs/qa_evidence/2026-09-17-conversation-search/` (`drive.sh`,
`stub-provider.py`, `implementer-*.png`).

**Index** — `backend/relay_core/conv_index.py`. SQLite FTS5 at `$XDG_DATA_HOME/relay/index.db`
(0600, in the 0700 directory that already holds the sessions), with a `conversations` row per
conversation and an `entries` row per user prompt, assistant reply, tool call, capped tool output,
Relay-run terminal command and captured command output. Refreshed by `SessionStore.save`, i.e. on
every autosave; `index_rebuild` recreates it from the session JSON; `meta.schema_version` and a
corrupt database are both handled by deleting and recreating the file, in the constructor and again
if SQLite reports corruption mid-query. Only sessions under `$XDG_DATA_HOME/relay/sessions` are
indexed (tests and custom `session_dir` panes stay out), and `RELAY_INDEX=off` disables it.
User prompts are taken from the checkpoints so they survive compaction; Relay's own
`[Relay context: …]` blocks are not indexed.

**Terminal history** — a synthetic conversation per workspace (`term-<16 hex>`, `source:
"terminal"`). The pane sends `terminal_history` when the shell reports the prompt again after a
command Relay staged: the command line, its **exit status**, the directory and, on the Relay
engine, the **output** captured through `TerminalBackend::onOutput` between "command loaded" and
"shell ready" (64 KiB captured, control sequences stripped, 4000 characters stored). Settings
`index/terminal_history` and `index/terminal_output` turn either half off.

*What is and is not captured.* Captured: every command submitted through the prompt box — typed,
queued, re-run by the terminal fix loop — with its exit status, working directory and time; its
output on engine panes (KonsolePart has no output callback, so those panes index the command line
and status only). **Not captured:** anything typed straight into the terminal in native mode
(Ctrl+H / F12). Relay never sees that text — it is typed into the PTY by the user, not staged by
Relay — so neither the command nor its output is indexed; the shell's own history file and Ctrl+R
still cover it. Output produced by a full-screen program (vim, less) is captured as raw screen
updates and mostly reduces to noise after control sequences are stripped; that is accepted.

**Protocol** — `conversations`, `conversation_get`, `conversation_delete`, `conversation_rename`,
`conversation_pin`, `terminal_history`, `index_rebuild`, handled in `session_protocol.py`. None of
them needs a configured agent. Details in protocol section 14.

**GUI** — `src/Conversations.{h,cpp}`. Ctrl+Shift+O (`conversations.open`), `/conversations [words]`
and Actions › Conversations… open a dialog grouped by project, newest first (pinned on top), with a
search field that filters as you type (120 ms debounce), filters for scope (this project / all
projects), kind (everything / agent threads / terminal history), model, open tasks and date, a
preview of the matching turns with the match highlighted, and Rename / Pin / Delete. Enter resumes
in the current pane, Shift+Enter opens the conversation in a new pane; both reuse the existing
resume path (`resume`, or `load_state` with a session reference for another workspace, which is the
same mechanism a fork uses). Terminal-history rows cannot be resumed and say so.

**Ctrl+F** — `relay::conversations::FindBar`, `find.inView`, also `/find [words]`. Searches the
terminal scrollback through `TerminalBackend::find()` (next/previous, wrap, match count; the engine
draws its own `n/m` overlay) and asks the worker how often the word appears in this pane's saved
conversation, with a button that opens the list on that word. Esc closes. Like Ctrl+H it only acts
from the prompt box, so Readline keeps Ctrl+F in native mode.

**Privacy** — the index holds message text and lives in the same 0700 directory as the sessions,
0600, never synced, never sent anywhere; deleting a conversation deletes its session file, its
checkpoint blobs and its index rows. No telemetry.

**Numbers.** Real set of 248 saved sessions (499 KiB of session JSON, 769 entries): index 388 KiB,
full rebuild 61 ms, search 0.03–1.2 ms. Synthetic 300 conversations × 30 turns (36 000 entries,
36.4 MiB of JSON): index 44.7 MiB (1.23× the JSON), full rebuild 1.2 s, autosave update 3.4 ms,
worst-case search (a word present in every entry) 56–64 ms median / 110 ms p95.

**Tests.** `tests/test_conv_index.py` (37 tests: build, incremental update, rebuild, FTS prefix and
phrase queries, operator escaping, scope/model/date/open-task filters, rename and pin surviving
re-indexing, terminal history, deletion of files/blobs/rows, corrupt-database recovery at open and
mid-query, schema-version change, autosave integration) and `tests/conversations_test.cpp`
(10 slots: highlight ranges, escaping, relative times, date filters, ANSI stripping, dialog
grouping and the find bar). `./scripts/test.sh` 420 tests, `ctest` 10 tests.

## QA checklist

1. **Open it.** Ctrl+Shift+O in a pane with saved conversations. Rows are grouped by project,
   newest first; the status line shows the count and the query time. `/conversations` and Actions ›
   Conversations… do the same, and the slash command shows the "Next time: Ctrl+Shift+O" hint once.
2. **Search an agent turn.** Type a word you used in an older conversation. Results appear as you
   type; the row's preview shows the matching turn with the word highlighted and says which kind it
   is (You / Agent / Tool / Tool output). Try `"two words"` in quotes for a phrase and a prefix such
   as `pelic`.
3. **Search terminal history.** Run a command from the prompt box, then search a word from it. A
   `$ Terminal · <project>` row appears with kind **Command**, its exit status, and (Relay engine)
   **Command output**. Resume is disabled for it.
4. **Resume.** Enter on an agent row replaces this pane's conversation ("Session loaded: … · N
   turn(s)" plus a recap). Shift+Enter opens it in a new pane instead, and the new pane says
   "Session loaded", not "Forked from".
5. **Cross-project.** Switch the scope filter to All projects: conversations from other workspaces
   appear under their own group and Enter still loads them.
6. **Filters.** Model, Open tasks, date and the agent/terminal filter each narrow the list, and
   combining them with a query works.
7. **Rename, pin, delete.** Rename a conversation (empty title restores the generated one); pin it
   and confirm it sorts to the top and stays pinned after a new turn is saved. Delete one and check
   the `<id>.json`, `<id>.meta.json` and `<id>.blobs/` files are gone from
   `~/.local/share/relay/sessions/<digest>/` and that searching its text finds nothing.
8. **Ctrl+F.** From the prompt box: the bar shows "N in terminal · M in conversation", Enter and
   the arrows step through the terminal matches and wrap, Esc closes and returns focus to the
   prompt box. In native mode (F12) Ctrl+F must still reach Readline.
9. **Rebuild.** Actions › Rebuild the conversation index: the status line reports the conversation
   and entry counts and the time, and searches still work afterwards. Delete `index.db` while Relay
   runs, then search again: the index is recreated (empty) and a rebuild restores it.
10. **Privacy.** `ls -l ~/.local/share/relay/index.db` is 0600 in a 0700 directory; `RELAY_INDEX=off
    relay` disables indexing and reports a clear error from the conversation list; no keys or
    prompts appear in any log.

## Known gaps

- Several words are an AND **inside one message or command**, like grep over a line; a query whose
  words are spread over different turns finds nothing. Quoted phrases and single words are the
  intended use.
- Commands typed in native mode are not indexed at all (see above).
- KonsolePart panes have no `find()` and no output callback, so on them Ctrl+F searches only the
  conversation and terminal rows carry no output. The Relay engine is the process default.
- `conversations` returns at most five matching turns per conversation; the rest need
  `conversation_get` (the preview shows them when a row is selected).
- The preview is the indexed text, not the rendered transcript: no diffs, no colour, no images.
- Retention is "keep everything", as decided; there is no pruning setting yet.
- The live QA run used a loopback stub endpoint (`stub-provider.py`) instead of a real provider, so
  that the run needs no API key and no network; everything else in it is the real code path
  (real worker, real autosave, real index, real terminal capture).
