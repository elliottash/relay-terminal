---
id: 9YB2
type: work
status: needs-qa-llm
labels: [bug, gui, switchboard]
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: 'A card opened in the Switchboard can have its title and its issue text changed with the mouse and with the keyboard; the change is written to the card''s Markdown file by the worker and survives a restart; a card edited elsewhere meanwhile is never overwritten silently; new and edited cards write `## Issue`, cards that say `## Request` still read; `ctest` and `./scripts/test.sh` pass'
source: 'owner, 2026-09-18: "after adding a card, i couldn''t edit the title or the task. i''m not sure about ''request'' there, let''s call it issue."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-edit-card-title-and-issue/'], related: [], github: null}
---
# A card's title and issue text are edited on the card, and Request is now Issue

## Report

Adding a card with quick add takes one line of text, and that line becomes both the title and the
whole body. There was no way to improve either afterwards without leaving Relay: the card detail's
title was a `QLabel` and its body a read-only `QTextBrowser`, with no key, no button and no menu.
The design had planned `e` (4.3) and then listed it as not built (4.5). The owner also read the
`## Request` heading as the wrong word for what the section holds.

Nothing was broken underneath: `board_update` (protocol 19.3) and `board_update_card`'s `title` and
`replace_section` were already there, already hash-checked, and already logged the replaced text in
the card's thread. Only the pane never asked.

## Change

- **One edit mode for the title and the issue text**, because they are one thought and one write.
  It opens on `e`, on the **Edit** button, on a click on the title or a double-click in the text
  (the three mouse paths fire the shortcut hint, "Next time: e"). The title becomes a field in
  place, the document becomes the issue editor, the reply box stands down and the Edit button greys
  out, so a card is being read or being written and never both.
- **Saving** is Enter in the title, Ctrl+Enter in the text (Enter there is a newline) or the Save
  button; **Esc** and Cancel put the card back exactly as it was. Saving with nothing changed
  writes nothing. Save sends one `board_update` with the `base_hash` the card was read at and the
  patch `{title, replace_section: {heading: "Issue", text}}`. The GUI never writes the file.
- **A card that changed on disk under the edit** is never silently overwritten: the worker refuses
  the stale hash (`board_conflict`), the pane re-reads the card for the version and hash that are
  there now, keeps every word that was typed and says that a second Save writes over that version.
  The thread keeps the replaced text either way (decision 12.3), so even the overwrite is
  reversible. While a card is being edited the selection can move on the list without the open card
  being swapped out from under the typing.
- **`## Request` is `## Issue`.** `board.new_card` writes it, `board_read` hands the pane the text
  (`issue`) and the card's current spelling (`issue_heading`) so the GUI still parses no Markdown,
  and `_section_span` treats the two names as one section, so every card already filed keeps
  reading. A write settles that card on `## Issue` whichever name the caller used — so cards change
  one at a time, as they are edited, and no rewriting commit was made.
- Docs: design 4.8 (new), protocol 19.2/19.3, `SWITCHBOARD-FORMAT.md` 2, `issues/README.md`, the
  hint trigger list in `ARCHITECTURE.md`. The key lines in the pane name `e`.

## QA checklist

1. **Add and edit.** `n`, type a line, Enter, Esc, Enter to open the card. Press `e`: the title is a
   field holding the title, the editor holds the issue text. Change both, Ctrl+Enter. The card
   shows the new title and text, a "Saved #ID · Undo" notice appears, and the file under `issues/`
   holds both.
2. **The mouse alone.** On a fresh card, click the title (it becomes a field with the text
   selected), then the **Edit** button, then double-click the rendered text — each opens the same
   editor, and the first shows a "Next time: e" hint.
3. **Esc writes nothing.** Start an edit, retype the title, press Esc: the card is as it was and
   `git diff` in the workspace shows nothing.
4. **It survives.** Quit Relay, start it again, open the card: the edit is there. `git diff` shows
   the card file and its thread, nothing else.
5. **The thread.** After an edit, the card's thread has the update event and a `rewrite` entry
   holding the old *and* the new text for the title and for the section.
6. **Changed underneath.** Open a card, press `e`, type; from a shell, change that card's `## Issue`
   in the file; press Save. The save is refused, the typed text is still in the editor, the line
   says the card changed on disk, and the file still holds only the shell's version. Save again: it
   writes, and the thread's `rewrite` entry holds the shell's version.
7. **An older card.** Open a card whose file still says `## Request` (most of them). It reads
   normally, `e` seeds the editor with that text under the label "Issue", and saving leaves
   `## Issue` in the file with the rest of the body untouched.
8. **Nothing was mass-rewritten.** `grep -rc '^## Request' issues/ | grep -v ':0'` still finds the
   cards it found before this change.
9. **Tests.** `ctest --test-dir build -R board` and `./scripts/test.sh`.

## Known gaps

- Only the title and the `## Issue` section are editable in the pane. `## Tasks`, `## Decisions`,
  findings and QA sections are still edited in the file or by the agent; a card with no issue
  section gets one on the first save.
- The pane does not re-read a card when only its *body* changes on disk: `board_changed` diffs rows,
  and a body edit moves no row, so the open card keeps showing what it last read until something
  else about the card changes or it is reopened. The stale-hash refusal is what makes that safe, and
  it is what check 6 exercises; a body-level watch would make the warning arrive without a save
  attempt.
- Two `board_tools` tests fail on `main` for unrelated reasons while another session lands
  `board_merge_cards` / `board_split_card` / `board_sections` (`TOOL_NAMES`), and
  `test_board_protocol`'s `AskTests` error on a `StubTurns.busy` attribute from that session's
  `_busy_error`. Neither touches this change.
