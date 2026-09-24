---
id: C0PY
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zzzzzm
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session relay-terminal, subagent), 2026-09-18
created: '2026-09-18'
acceptance: with `terminal/copy_on_select` on, a mouse drag over text in the conversation info pane, the file preview and the Switchboard card detail puts that text on the clipboard and on PRIMARY; with it off, nothing Relay owns is copied; an editable field is never copied from
source: owner, in a Claude Code session, 2026-09-18
links: {plans: [], commits: ['d5db918'], evidence: ['docs/qa_evidence/2026-09-18-copy-on-highlight-in-panes'], related: [], github: null}
---
# Copy on highlight works in the info pane and every other read-only pane, not only the terminal

## Issue

> allow copy on highlight in info and other panes.

## Decisions

- **One setting, and it keeps its old key.** The behaviour is no longer terminal-only, but the
  QSettings key stays `terminal/copy_on_select`. Renaming it would silently turn the setting off
  for everybody who had switched it on. Only the settings-pane wording changed: the description
  was "Selecting terminal text copies it" and now reads "Highlighting text copies it, in the
  terminal and in read-only panes"; the label is still "Copy on select", and the search terms
  gained *primary, mouse, pane, info panes, transcript, preview, diff, board*. The row stays in
  Options › Terminal, where it has always been.
- **One shared helper, not copied event filters.** `src/CopyOnSelect.h` holds the whole thing:
  `relay::copyOnSelectEnabled()`, `relay::copyOnSelectText(widget)` and
  `relay::installCopyOnSelect(widget, notify)`. It is header-only because the surfaces are spread
  across a dozen static libraries (relay-filepanes, relay-board, relay-conversations, …) and the
  header-only `Pane`; a library would have to be linked into all of them for one small filter.
- **It matches the terminal exactly.** Copy happens on a left-button *release* when a selection
  exists, to PRIMARY where the platform has one and to the clipboard — which is what the terminal
  already did between `TerminalView::mouseReleaseEvent` (PRIMARY) and `Pane::copySelection`
  (clipboard). A keyboard-only selection never copies; Ctrl+C is still the way to copy that.
- **Editable fields are never copied from.** `copyOnSelectText()` returns nothing for an editable
  `QTextEdit`, `QPlainTextEdit` or `QLineEdit`, so the prompt box, the composer, the plan editor,
  the settings inputs, and a file preview switched to editing (`FilePreview::setEditable`) are
  untouched. The guard is on the widget's own read-only state rather than on where it is installed,
  so a surface that becomes editable later stops copying by itself.
- **Quiet by default.** Only a surface that passes a `notify` says anything: the pane keeps its
  "N characters copied" toast for the reasoning bubble and the program transcript. The board, the
  info pane and the file preview have nowhere to put a toast and stay silent, as PRIMARY always has.
- **The two ad-hoc call sites in `src/Pane.h` were folded in.** The reasoning bubble's release
  handler in `Pane::eventFilter` is gone; the bubble now installs the shared filter where it is
  built, and `Pane::copyOnSelect()` is one line delegating to `relay::copyOnSelectEnabled()`.
  `copyThinkingSelection()` stays for Ctrl+C in the prompt box and shares the new `toastCopied()`.
  The terminal's own site in `Pane::eventFilter` was **left alone**: the terminal is not a text
  widget, its selection comes from the backend (`m_backend->copySelection()`), and folding it in
  would have meant a second selection interface on the helper for no gain.
- **The settings pane's own labels were left non-selectable.** Nothing in `src/SettingsPane.cpp`
  sets `Qt::TextSelectableByMouse` today, so there is nothing there to highlight. Making those
  labels selectable is a separate change and not a safe side effect: `theme::polishWindow()`
  renames *every* selectable `QLabel` to the `cwd` object name (`src/Theme.cpp`), which would take
  the settings rows out of their own stylesheet rules — the Switchboard card's meta line already
  carries a comment saying exactly that. Flagged, not done.

## Surfaces covered

Found by auditing for `setReadOnly`, `QTextBrowser`, `QTextSelectableByMouse`/
`setTextInteractionFlags` and every `new QTextEdit` / `QPlainTextEdit` / `QTextBrowser` under
`src/` and `engine/`, rather than by guessing.

| Surface | Widget | Where |
|---|---|---|
| Terminal output | engine view | `engine/view/TerminalView.cpp`, `src/Pane.h` — unchanged, this is the behaviour being matched |
| Reasoning bubble | `QPlainTextEdit` | `src/Pane.h` (`m_thinkingView`) — folded onto the shared filter, keeps its toast |
| Program transcript | `QPlainTextEdit` | `src/Pane.h` (`m_transcriptView`) — new, with the toast |
| Conversation info pane (ⓘ / Alt+I) | `QTextBrowser` | `src/SessionInfo.cpp` |
| Conversations pane preview | `QTextBrowser` | `src/Conversations.cpp` |
| Turn log (turn details pane) | `QPlainTextEdit` | `src/TurnTranscript.cpp` |
| Subagent transcript | `QPlainTextEdit` | `src/SubagentTranscript.cpp` |
| Diff view | `DiffTextEdit` | `src/DiffView.cpp` |
| File explorer path line | selectable `QLabel` | `src/FilePanes.cpp` |
| File preview, plain text | `QPlainTextEdit` | `src/FilePanes.cpp` — only while it is a preview; editing turns it off |
| File preview, rendered Markdown | `QTextBrowser` | `src/FilePanes.cpp` |
| File preview, info page (binaries, images) | selectable `QLabel` | `src/FilePanes.cpp` |
| Switchboard card detail | `QTextBrowser` | `src/BoardPane.cpp` (`m_doc`) |
| Switchboard cleanup panel | `QTextBrowser` | `src/BoardPane.cpp` (`m_cleanupBody`) |
| Tasks panel detail | selectable `QLabel` | `src/RequestsPanel.cpp` |
| Sharing pane, a guest's request text | selectable `QLabel` | `src/SharingPane.cpp` |
| Share dialog address, and the invite link | `QLabel`, read-only `QLineEdit` | `src/RemoteShare.cpp` |

Deliberately **not** covered, and why: the prompt box / composer (`src/RichEditor.cpp`), the plan
editor (`src/FilePanes.cpp`), the Switchboard card's issue editor and the settings pane's "extra"
JSON box (`src/BoardPane.cpp`, `src/Pane.h`) — all editable; the Switchboard card's meta line,
which is deliberately links-only; and the settings pane's labels, which are not selectable at all
(see Decisions).

## Tasks

- [x] `src/CopyOnSelect.h`: the setting, the selection reader, the filter and `installCopyOnSelect()` <!-- t:cc -->
- [x] Installed on all 16 read-only surfaces above <!-- t:yg -->
- [x] The two ad-hoc sites in `src/Pane.h` folded in (reasoning bubble) or documented as staying (terminal) <!-- t:4c -->
- [x] Settings-pane wording and search terms; the QSettings key left alone <!-- t:mp -->
- [x] `tests/copyonselect_test.cpp` (15 cases) and its ctest target <!-- t:n3 -->
- [x] `docs/ARCHITECTURE.md` § 4 "Copy on highlight" and the source map <!-- t:ne -->

## Findings

**With the setting off, X11's PRIMARY still changes on a text-widget selection — and always did.**
The `off` run shows the clipboard untouched (the sentinel survives every drag) while PRIMARY carries
the highlighted text in the three Qt-widget panes. That is `QWidgetTextControl`'s own behaviour on
X11, shared by every Qt and GTK application, and it is unchanged by this work: Relay's filter does
nothing at all when the setting is off, which is what the clipboard column proves. The terminal's
PRIMARY does *not* change when the setting is off, because the engine paints its own selection and
only writes PRIMARY under `setCopyOnSelect`. Worth knowing when reading the evidence: the promise of
the setting is about the clipboard, not about PRIMARY, which the toolkit was already writing.

## QA checklist

Relay built from main. Options › Terminal › "Copy on select".

1. **Off (default).** Highlight text in the conversation info pane (ⓘ or Alt+I), then paste
   (Ctrl+V) somewhere: the old clipboard contents come back, not the highlighted text. Same in a
   file preview and in a Switchboard card. (Middle-click paste may still paste it — see Findings.)
2. **On.** Options › Terminal, switch "Copy on select" on. The row reads "Highlighting text copies
   it, in the terminal and in read-only panes", and searching Options for *copy*, *highlight*,
   *clipboard*, *primary* or *selection* finds it.
3. Highlight a line in the conversation info pane with the mouse and paste: exactly that line.
4. Open a Markdown file in a file preview (Ctrl+Shift+B, then the file), highlight a sentence,
   paste: exactly that sentence, with real newlines across paragraphs — no U+2029 boxes.
5. Open a Switchboard card (Ctrl+Shift+S, Enter on a card), highlight a paragraph of the card
   document, paste: that paragraph.
6. Highlight in the turn details pane, a subagent transcript, a diff pane and the Tasks panel's
   detail: each copies.
7. The terminal still behaves exactly as before, with the setting on and off.
8. **Never from an input.** In the prompt box, type a few words and select them with the mouse:
   the clipboard does not change. Same in the composer, in a plan pane, and in a file preview
   switched to editing (the Edit affordance) — after switching it to editing, a highlight there
   copies nothing, and switching back to preview makes it copy again.
9. **Keyboard selections do not copy.** In the reasoning bubble or a transcript, select with
   Shift+Arrow only: the clipboard does not change until Ctrl+C.
10. **A click does not wipe the clipboard.** Copy something, then single-click (no drag) in the
    info pane: what you copied is still there.
11. The reasoning bubble and the program transcript still toast "N characters copied"; the other
    panes stay silent.
12. Switch the setting off again: every surface stops copying, and a profile that had the setting
    on before this change still has it on (the key never moved).

## Evidence

Implementer run: [`docs/qa_evidence/2026-09-18-copy-on-highlight-in-panes`](../../../docs/qa_evidence/2026-09-18-copy-on-highlight-in-panes)

Under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` and
`TMPDIR`, driven by `drive.sh`, with `stub-provider.py` standing in for a provider account.
`implementer-clipboard.txt` reads back both selections after every drag; before each drag they are
set to `SENTINEL-nothing-was-copied` so "nothing happened" is visible rather than inferred.

With the setting **on**, all four drags land on the clipboard and on PRIMARY:

| Surface | Clipboard after the drag |
|---|---|
| Terminal (`implementer-on-1-terminal.png`) | `To run a command as administrator (user "root"), use "sudo <command>".…` |
| Conversation info pane (`implementer-on-2-info-pane.png`) | `Model stub · Stub (local:stub) · effort high Context 36 / 131.1k tokens…` |
| File preview, Markdown (`implementer-on-3-file-preview.png`) | `The quick brown fox jumps over the lazy dog. Highlighting any of these words should copy them.` |
| Switchboard card (`implementer-on-4-board-card.png`) | `Potential feature request, deferred by the owner on 2026-09-17.…` |

With the setting **off** (`implementer-off-*.png`), the clipboard still holds
`SENTINEL-nothing-was-copied` after all four drags.

Tests: `ctest --test-dir build` — 46/47, the new `copyonselect` target passing 15 cases; the one
failure is `backend-and-bash` timing out under ctest's own 120 s limit, which is inherited and not
from this work. `./scripts/test.sh` (the same Python and Bash suite without the limit) is the
authority for those.
