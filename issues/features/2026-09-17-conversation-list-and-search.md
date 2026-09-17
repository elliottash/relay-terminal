# Conversation list with full-text search

- **Status**: open
- **Component**: gui, worker
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: a list of past conversations across panes and projects, searchable by any word in any message or tool output, opening the match in context and resuming it in a pane
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, 2026-09-17: "need a conversation list. this needs to be full text searchable (that is a big downside of warp / codex / etc, finding old convos is so hard). lets plan a much better system." and "what hot key for that? ctrl shift y is warp. see what the others do or what would be a chrome like hotkey."

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
