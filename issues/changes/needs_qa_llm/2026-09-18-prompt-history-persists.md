---
id: H8UP
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'After quitting and reopening Relay, Up in the prompt box walks back through what was typed before; a new pane starts with the same history; the store is `$XDG_DATA_HOME/relay/state/prompt-history.txt` (0600); "Clear prompt history" forgets it; `ctest` (48 groups, including the new `prompthistory` group) and `./scripts/test.sh` pass'
source: 'owner, 2026-09-18: "conversation history isnt persisting on exit and re-open. i cant do up arrows to see what i did before"'
links: {plans: [], commits: [5838740, 1640d08], evidence: ['docs/qa_evidence/2026-09-18-prompt-history-persists/'], related: [], github: null}
---
# The prompt box remembers across a restart, and every pane shares one history

## Issue

Owner, 2026-09-18: "conversation history isnt persisting on exit and re-open. i cant do up arrows
to see what i did before".

## Report

`RichEditor::remember()` appended to a `QStringList` member capped at 200 and nothing ever read or
wrote it anywhere else. So the history was per prompt box and per run: it died with the pane, a
pane opened beside another started empty, and quitting Relay forgot everything. Up in a fresh
window did nothing at all, which is what the owner hit. Every other terminal on the machine — and
Relay's own ghost-text suggestions, which already fall back to `$HISTFILE` — keeps this across
restarts.

## Change

A file, `$XDG_DATA_HOME/relay/state/prompt-history.txt` (0600, in the 0700 directory
`windows.json` already lives in), and a small unit that owns it: `relay::prompthistory`
(`src/PromptHistory.h`, `src/PromptHistory.cpp`, library `relay-prompthistory`, QtCore only so it
is tested headless).

- **One history for the person, not one per pane.** Every composer reads the same file
  (`RichEditor::useHistoryFile()`, called where `Pane` builds its editor). A browse that begins —
  the Up that leaves the draft — re-reads the file first (`refreshHistory()`), so a box that has
  been open for an hour picks up what the pane beside it has run since. The read happens only when
  the file's size or mtime has changed, so an idle box does no work, and never mid-browse, so the
  position under Up/Down cannot shift while it is being walked.
- **Written when the line is submitted**, not on the way out: one appending write per entry. A
  Relay that is `kill -9`d keeps every line up to the last one, and two Relays running at once
  interleave rather than overwrite (O_APPEND makes each write atomic). The file is rewritten whole
  only to trim it, once it passes 512 KiB, back to the newest 1,000 entries.
- **One line per entry.** A prompt can be several lines, so a newline is stored as `\n` and a
  backslash as `\\`. A file of plain one-line entries — a hand-written one, or another tool's —
  reads back exactly as it looks, and a backslash that is not one of those two escapes is kept as
  it reads. An entry over 10,000 characters (a big paste) stays in the live history but is not
  written.
- **What never reaches the file.** Nothing new: a line written to a running program's stdin is
  already refused by `relay::input::retainable`, and a password is typed into the separate masked
  field (`m_secretEdit`), which never calls `remember()`. The file holds what was typed at Relay.
- **Forgetting it.** A new palette action, `history.clear` — "Clear prompt history · Forget every
  line Up recalls, in every pane". It confirms, deletes the file, and calls
  `RichEditor::forgetHistory()`, which drops the copy every open prompt box is holding, including
  one part-way through a browse: without that, a box mid-browse kept walking back into what had
  just been forgotten (caught in the live run, first pass). Text already in a box is left alone —
  it is the person's text now.
- **The box that never calls this** — the Switchboard's reply editor — keeps exactly the
  session-only history it had, and writes nothing.
- **A prompt from a paired device is kept like any other** (owner, 2026-09-18, on the question
  this card first left open: "these should always be saved"). `Pane::submitRemote()` writes it to
  the file at the door — before routing, so a prompt that bounces off an unconfigured agent is
  still there to recall, and whether it ends up at the agent, in the shell or steering a running
  turn. Not through the composer: the desktop's draft and browse position belong to whoever is
  sitting there, and every box takes the line in on its next Up. A guest's line counts as much as
  the owner's phone's; the queue row already says who wrote it. A remote shell command was already
  kept, by the same path as a locally typed one.
- **The same line twice running is stored once.** A prompt box dedupes against its own last line,
  which cannot see what another pane or a phone appended in between, so `append()` checks the end
  of the file (`lastEntry()`, which reads only the tail). That is also what keeps a routed remote
  prompt — written at the door, then remembered again when the command runs — to one entry.

`RichEditor`'s in-memory cap goes from 200 to 1,000 when a file is attached, so the list and the
file agree on how far back Up reaches.

Documented in `docs/ARCHITECTURE.md` (section 3, "Prompt history across a restart"). Tests:
`tests/prompthistory_test.cpp` (the new `prompthistory` ctest group: encoding round-trip, a plain
hand-written file, what is storable, order and caps, interleaved appends, trimming, 0600, clear,
unusable paths) and four new cases in `tests/editor_test.cpp` (history outlives the editor, a
browse takes in another pane's line, clearing empties a box mid-browse, an editor without a file
writes nothing).

## QA checklist

1. **The report itself.** Run Relay, submit two or three commands, quit it, start it again: Up in
   the prompt box brings back the newest, Up again the one before, Down walks forward and returns
   to the empty draft.
2. **Multi-line entries.** Submit a prompt with Shift+Enter in it. After a restart, Up brings it
   back as two lines (`implementer-04`), and Up from the *first* line of it walks further back
   (Up inside the text still moves the cursor, as before).
3. **One history, many panes.** Open a second pane (Ctrl+E): its first Up offers what the first
   pane ran. Run something in the left pane, press Up in the right one: the new line is there.
   Same across two windows, and across two Relays started at once.
4. **Killed, not quit.** `kill -9` Relay after submitting a line; start it again: the line is
   there (`implementer-12` shows it recalled after a SIGKILL).
5. **Clearing.** Ctrl+Shift+A → "clear prompt history" → Yes: the toast reads "Prompt history
   cleared.", `$XDG_DATA_HOME/relay/state/prompt-history.txt` is gone, and Up in any open box —
   including one part-way through a browse — offers nothing. No clears nothing. A line submitted
   afterwards starts the file again with only that line.
6. **What must not be stored.** At a `sudo` (or `ssh`) password prompt the box is the masked
   field: type a throwaway wrong password, submit, and check the password is in neither the file
   nor Up. Answer a program's `[Y/n]` from the prompt box: that answer is not in the file either
   (`relay::input::retainable`). Check the file is 0600 in a 0700 directory.
7. **The file.** `cat` it: one line per entry, oldest first, newlines as `\n`. Add a plain line by
   hand and restart: Up offers it. Truncate or corrupt it: Relay starts, Up offers what is
   readable, nothing crashes. Delete the directory: Relay recreates it on the next submission.
8. **Suggestions still work.** The ghost-text suggestion (Settings › Terminal › "Command
   suggestions from history") still completes from this pane's own commands first, then the prompt
   history, then `$HISTFILE`.
9. **From a phone or a guest.** Pair a device (or run `remote-drive.sh`), send a prompt from it
   and approve it: the line is in the file and the desktop's next Up offers it, while the desktop's
   own unfinished draft and browse position are untouched. Send the same line twice: it is stored
   once. Send one while no provider is configured (the pane says so): it is still in the history.
10. **The Switchboard reply box** (a card's reply editor) still has its own Up/Down history within
   the session and does not write to the file.
11. **Tests.** `ctest --test-dir build` (48 groups, `prompthistory` among them) and
    `./scripts/test.sh` pass.

## Evidence

`docs/qa_evidence/2026-09-18-prompt-history-persists/` — `drive.sh` (Xvfb + xdotool, three Relay
runs in an isolated `XDG_DATA_HOME`), fourteen screenshots, the store itself as
`history-file.txt`, and the run log showing the file after each stage. `remote-drive.sh` is the
second half: a real guest over the remote protocol (the #W5N2 run's `guest.py`, which is
`remote/client.py`, not the web client) joining a shared pane, sending a prompt, and the desktop's
Up offering that line afterwards.
