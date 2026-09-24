---
id: WD83
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent (subagents UI)
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, subagent of the main session, in the shared checkout), 2026-09-18
rank: zzzzzi
created: '2026-09-18'
source: issues/feature_intake.txt, 2026-09-18
acceptance: '`tests/subagents_test.cpp` (ctest `subagents`), `tests/windowstate_test.cpp`, live run in `docs/qa_evidence/2026-09-18-subagents-tabbed-pane/`'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-subagents-tabbed-pane/], related: [M9T4], github: null}
---
# Subagents in one tabbed pane, a tab per subagent

## Issue
i think subagents should be a tabbed pane, one tab per subagent. when you access a subagent, it opens a new subagent pane and goes to that tab. if the pane is already open, it just zooms to the pane and to that tab. it needs to be easy to move back to the main agent thread from the linked subagent pane.

## Decisions

Owner, 2026-09-18:

- Keep the running-agents strip under the prompt box as the default. Most people won't use the
  tabbed pane.
- The tabbed pane opens only when you click (or Enter on) a strip row, and from the other places
  that open a subagent transcript: the palette's Agents list, `/agents`, and the ✦ lines in the
  terminal.
- One subagent pane per main pane, one tab per subagent. The tab shows the status glyph and
  `type id`. Opening a subagent: if the pane is closed, it opens split beside the owning pane on
  that tab; if it is open, it is brought forward (its window tab, focus) and switches to that tab.
  This replaces the old one-pane-per-transcript.
- While the pane is open the strip folds to one line, e.g. "2 subagents running · Alt+A to open"
  (the live key). Closing the pane brings the full strip back.
- A "← main agent" control in the pane's header, plus a key, goes back to the owning pane's
  prompt box.
- The pane's ToolPane carries `paneType = "subagent"` for the pane-type header colours.
- Tabs persist across a restart like other panes; finished subagents' tabs stay until dismissed,
  by the strip's rules for finished rows.

## Change

Code: `relay::SubagentTabsView` in `src/SubagentTranscript.h/.cpp`; the fold in
`src/SubagentsPanel.*`; Pane hooks under "subagents UI" in `src/Pane.h`; the window's
`openSubagentTab` and helpers in `src/RelayWindow.h`; `ToolPane` holds the tabs view
(`src/PaneChrome.h`, four lines); `isUsableNode` accepts the saved pane (`src/WindowState.cpp`);
key `agent.subagentPane` (`src/Keymap.h`).

- **Entry point:** `RelayWindow::openSubagentTab(Pane *ownerPane, const QString &subagentId)`
  (public). Every path goes through it: a click or Enter on a strip row, Enter or a click on the
  folded line and Alt+A (the pane's current tab, else the selected row, else the first running
  one), the palette's Agents list and `/agents`, and a click on a ✦ start/finish line in the
  terminal (now an OSC 8 link to `relay://subagent/<pane>/<id>`). An id the pane does not know
  toasts "No subagent … in this pane." and opens nothing.
- **Pane:** header row "← main agent", then a tab bar (glyph ○ ● ✓ ✗ ■, `type id`, the
  description as tooltip, a plain × per tab, movable). Each tab is the existing transcript view
  with its message box. Split beside the owner, or below it when the owner is under 900 px wide.
  The narrow-window overlay is gone: the pane is used at every width.
- **Back to the main agent:** "← main agent", Esc in a message box, or Alt+A inside the pane
  focus the owning pane's prompt box (switching window tab if the pane was moved). Alt+A from the
  prompt box goes the other way. A mouse click on "← main agent" hints the key; a mouse click on
  a strip row or the folded line hints Alt+A / ↓ Enter.
- **Folded strip:** while the pane is open the strip is one line ("1 subagent running · 2
  finished · Alt+A to open"). Down from the prompt box still enters it; Enter or a click opens the
  pane, Down goes on to the jobs list, Up/Esc return. Closing the pane unfolds it.
- **Tabs follow the list:** a tab closes when its row leaves the list (dismissed with `x`, cleared
  by the next user prompt or New chat, a worker restart); the pane closes with its last tab. The
  other way round (coordinator for the owner's "fix clear gaps", 2026-09-18): the tab's × on a
  *finished* agent also dismisses its row, so the two agree; on a *running* agent it closes only
  the tab, the agent keeps running and its row opens the tab again.
- **Restart:** the pane is saved as `{"subagents": {owner, cwd, current, tabs: [{id, type,
  description, status, text}]}}` — each tab's last 16,000 characters — and relinked to its owner
  by the owner's scrollback id. The agents themselves end with the worker, so a restored tab says
  "ended with the previous session" (a running one reads ■ stopped) and its message box is off.
  Restored tabs go at the next user prompt or New chat, like finished rows. A live subagent with
  the same id (ids restart at a1) replaces the restored tab in place. Ctrl+Shift+Z on a closed
  subagent pane brings it back with live tabs for the agents still listed.

## Implementer check (not a QA verdict)

- `ctest`: `subagents`, 5 new cases (a finished tab's × dismisses its row, a running one's does
  not; click opens and the fold; `onFinishedCleared` is the
  list's rule, not a worker restart; tabs open, switch, follow the list, close; tabs survive a
  restart as text, replace in place, drop at the next prompt), `windowstate` (the new node).
  Every other ctest passed except `backend-and-bash`, whose three failures are Python tests this
  change does not touch (`test_tools…absolute_and_parent_paths…`, two
  `test_remote_gui_host.VoiceTests`).
- Live under Xvfb, isolated HOME/XDG_*/TMPDIR, `RELAY_KEYRING=off`, against
  `fake-provider.py` as a local model endpoint (no key, no credits): one prompt starts three
  background agents. Screenshots in `docs/qa_evidence/2026-09-18-subagents-tabbed-pane/`:
  01 strip by default; 02 a click on a2's row opens the pane on a2; 03 Ctrl+click on a1's ✦ line
  adds and selects a1; 04 a click on a2's tab; 05 "← main agent" back to the prompt box, with the
  Alt+A hint; 06 Alt+A from the prompt box brings the pane forward; 07 Esc in the message box goes
  back; 08 Down enters the folded line; 09 closing the pane brings the full strip back; 10
  Ctrl+Shift+Z brings the pane back live; 11 after quitting (SIGTERM) and restarting, the pane is
  back with both tabs as text, ■ stopped; 12 the next prompt drops them, the pane closes and the
  strip returns.

## QA checklist

1. With subagents running, the strip is under the prompt box and no subagent pane exists.
2. Click a row: a pane opens beside the pane (below it in a narrow pane) on that subagent's tab;
   the strip becomes one line with the live key.
3. Open a second subagent (palette › Agents…, a ✦ line, or Alt+A after picking a row): the same
   pane switches to a new tab; no second pane. Opening an open one only switches tabs.
4. Move the subagent pane to another window tab; from the prompt box Alt+A (or Enter on the folded
   line) switches to it; "← main agent", Esc and Alt+A switch back to the owner's prompt box.
5. The tab glyph follows status (○ ● ✓ ✗ ■). `x` on a finished row in the strip — reach the strip
   by closing the pane first — or the next prompt closes that tab; the last one closes the pane.
6. The tab × on a running agent closes the tab without stopping it; the row stays and reopens it
   with its snapshot. The tab × on a finished agent also removes its row from the strip.
7. Close the pane: the full strip returns. Ctrl+Shift+Z brings the pane back with live tabs.
8. Quit and restart with the pane open: it comes back beside its pane with the tabs' text, ■
   stopped, message box disabled; the next prompt drops them.
9. Shortcut hints: a mouse click on a row or on "← main agent" shows the Alt+A hint (within the
   hint limits).
10. The pane's header gets the subagent pane-type colours once the chrome work reads
    `paneType`.
