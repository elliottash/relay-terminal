---
id: RC7Z
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code, session relay-terminal-d4), 2026-09-18
rank: zzrc
created: '2026-09-18'
acceptance: 'Close a pane, a tab and a window; each is in Sessions › Recently closed with where and when; Enter reopens it where it was with its terminal text and conversation; the list survives a restart; typing `exit` is reopenable; `ctest` passes'
source: '`issues/feature_intake.txt`, 2026-09-18: "ideally we could add soemthing like recently closed windows or recently closed tabs or recently closed panes." Owner in session, 2026-09-18: "persist it and save it", and "this will be part of the session manager right? i think there should be a ''recently closed'' button or something there."'
links: {plans: [], commits: [bc14a49], evidence: ['docs/qa_evidence/2026-09-18-recently-closed/'], related: [R6J0, CCKY, 64KE, SB7K, JRWQ], github: null}
---
# Recently closed panes, tabs and windows

## Issue

Owner, feature intake: "add a resume closed sessions item in options that will list recent sessions,
ideally we could add soemthing like recently closed windows or recently closed tabs or recently closed
panes."

The session-list half of that line is card #R6J0 (the session manager pane). This card is the other
half. What existed was `closed.restore` (Ctrl+Shift+Z): a 25-deep stack in memory that popped the
newest item. It had no list, it was lost on quit, a reopened pane came back **empty** (its text was
only ever written on quit, and the layout's prune deleted it), a pane closed by typing `exit` was not
recorded at all, hand-set tab names and divider positions were dropped, and the close-window dialog
and two docs named the wrong key (Ctrl+Shift+W, which is *close pane*).

## Decisions

- **Persist the list** (owner, 2026-09-18). This reverses the non-goal card #64KE recorded ("One saved
  layout; no history of layouts … no reopen closed window set"). It is a separate file,
  `state/closed.json`; `windows.json` is unchanged and is still the one layout that comes back on
  start.
- **Inside the session manager, as its own tab** (owner asked; recommended and built that way). One
  surface answers "get me back to what I had". It is a tab and not a filter on the session list
  because a closed item is a layout, not a conversation: a window is several tabs, a tab is a split
  tree, and a plain shell pane has no session at all. `closed.restore` stays the instant, no-UI path.

## Change

- `src/ClosedStack.{h,cpp}` (new, in `relay-windowstate`; `tests/closedstack_test.cpp`, 14 cases): the
  record — kind, id, closed-at, the layout nodes the saved layout already uses, split direction,
  divider sizes and slot, tab position, window geometry, hand-set tab names, what each pane was
  called — its JSON, the file (0600, atomic, version-checked, bad records dropped one by one, capped
  at 25), the scrollback ids the prune must spare, stripping a `session_id` that is already open, the
  filter, and the words a list shows ("Tab · release · 2 panes", "~/code/relay", "5 min ago").
- `src/ClosedList.{h,cpp}` (new library `relay-closedlist`; `tests/closedlist_test.cpp`, 9 cases): the
  list widget. Newest first; a filter over names, titles and directories; → unfolds an item into its
  tabs and panes, and under each terminal pane the last 8 lines of the text it closed with, read from
  the scrollback store only when unfolded; Enter or double click reopens, Delete drops one, "Clear
  list" drops all (asks first). It takes the keyboard when its tab is shown, typing on the rows goes
  to the filter, and a refresh keeps the selection and what was unfolded.
- `WindowManager` (`src/RelayWindow.h`, `src/WindowManagerImpl.h`): `remember` stamps and saves;
  `restoreClosed(id)`, `discardClosed(id)`, `forgetClosed()`, `closedRecords()`,
  `closedItemForSession(id)`, `watchClosed()`. Loaded on start and written on every change, under the
  saved layout's rules: only the Relay that owns the layout, and not at all while "Reopen windows on
  start" is off. "Start a fresh window set" deletes the file with the scrollback it names.
- **Text at close.** `closePane` and `closeTab` write the panes' terminal text before the shell goes
  (window close already did), and `writeWindows()`'s prune keeps every id the list names.
- **`exit` is a close.** `onShellExited` records like ×.
- **Quit is not a close.** Windows that all go together are what the saved layout reopens, so they
  leave the list again (`settleClosed()`, also run from `aboutToQuit`); listing them as well would
  offer each twice and resume its conversations twice. A window closed while others stay is kept.
- **Back where it was.** A pane returns beside what it sat beside — a pane *or* a whole nested split
  (`ClosedItem::anchor`), falling back to the pane that took its focus — at the divider sizes it had
  (applied after `insertBeside()`'s own queued sizing). Tabs and windows get their hand-set names
  back. An item loaded from the file has no live window and opens as a tab of the window that asked
  (a closed tab used to open a whole new window in that case).
- **Never two workers on one session.** If a conversation in the item is already open in a pane, the
  item still comes back — directory and text — without that `session_id`, and says so.
- `closed.list` (new action, unbound) opens Sessions on the tab; Actions has "Recently closed…" and a
  searchable "Recently closed" group with one row per item.
- The restored-text rule now reads "— scrollback from this pane's previous shell —", true of a reopen
  as well as a restart; text saved under the old wording is still filtered.
- The close-window dialog names the real key through `Keymap::shortcutText()`; `README.md` and
  `docs/ARCHITECTURE.md` no longer say Ctrl+Shift+W restores.

## QA checklist

Isolate `XDG_RUNTIME_DIR XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME XDG_CACHE_HOME TMPDIR`, and use
an Xvfb display nobody else is on.

1. Split (Ctrl+E), run a command and `cd` in the new pane, drag the divider off centre, close the pane.
   `state/closed.json` exists, mode 600, one `pane` record with `sizes`/`slot`; its scrollback file
   holds the command and is still there two seconds later (after the layout's prune).
2. Ctrl+Shift+Z: the pane is back in the same slot at the same width, in the directory it was in,
   with its text between the two rules; `closed.json` is gone (the list is empty).
3. Type `exit` in a split pane: it is in the list.
4. `/rename-tab release`, close the tab, reopen it: it is called "release" and is where it was.
5. Close a tab, quit (SIGTERM or the window's ×), start again without `--fresh`: the list still has
   the tab and the pane, and does **not** list the window the quit closed.
6. Bind `closed.list` (or Actions › Recently closed…): Sessions opens on the tab with the keyboard in
   the list; → shows the text preview; Enter reopens; Delete drops; typing filters.
7. With two windows open, close one: it is listed as "Window · N tabs · M panes" and reopens with its
   geometry and tab names.
8. Resume a closed pane's conversation elsewhere first, then reopen the pane: it comes back with a
   new conversation and a notice, and the session file has one writer.

Done here under Xvfb (evidence folder): 1–7, except that the divider was never dragged off centre, so
"same width" was only seen for an even split (`sizes [624, 623]`). Not driven: 8 — it needs a live provider key in the
isolated profile to make a conversation; the stripping is covered by `closedstack_test`
(`anOpenConversationIsNotResumedTwice`), and #R6J0 separately focuses an already-open session.

## Left for someone else, and why

- **A default key for `closed.list`.** Owner's choice: Ctrl+Shift+T is `tab.new`, and the presets
  already disagree about `closed.restore`.
- **"closed 5 min ago" on a session row, with "Reopen where it was" beside Resume.**
  `WindowManager::closedItemForSession(id)` is there for it; the rows are `src/Conversations.cpp`,
  card #R6J0's file.
- **Turn transcripts and the Settings pane are not recorded.** They are views onto something still
  open, not things with state of their own; reopening one is its own action.
- **The preview is plain text**, like the scrollback store it reads (#SB7K explains why colour is not
  saved).
