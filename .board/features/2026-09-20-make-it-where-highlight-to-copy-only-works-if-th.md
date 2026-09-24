---
id: C9VT
type: work
status: needs-verification
assignee: agent
implemented_by: deepseek/deepseek-v4.1-flash
session: f32a3a80-bff1-4c2d-bc96-d2f5a89f672d
rank: zzzzzzzzzzzzzzzzr
created: '2026-09-20'
links: {commits: [46d7e92a, 23f5525e, 585bbeec], evidence: [docs/qa_evidence/2026-09-21-copy-on-highlight-three-chars/], github: null, plans: [], related: []}
---
# make it where, highlight to copy only works if there are at least 3 letters or n…

## Issue
make it where, highlight to copy only works if there are at least 3 letters or numbers in the string

## Plan
**Goal.** Copy-on-highlight fires only when the selected text holds at least 3 letters or digits (`QChar::isLetterOrNumber`), so an accidental one- or two-character drag no longer clobbers the clipboard/PRIMARY. Explicit copies (Ctrl+Shift+C, context-menu Copy, copy buttons) are untouched.

**Findings.** Three separate copy-on-highlight paths exist today, all reading the `terminal/copy_on_select` setting:

1. `src/CopyOnSelect.h` — the shared filter every read-only pane surface installs (`installCopyOnSelect`, ~20 call sites). `CopyOnSelectFilter::eventFilter` runs on left-button release, takes `copyOnSelectText(target)`, and `copyOnSelectPut(text)` writes PRIMARY and the clipboard. Only an empty string is rejected today.
2. `engine/view/TerminalView.cpp` `mouseReleaseEvent` (~line 1588) — the terminal's own path: after a drag (`m_selectionMoved`), when `m_copyOnSelect` is set it writes `selectedText()` to PRIMARY. The engine library (`relay-terminal-engine`) does not include headers from `src/`.
3. `src/RemotePane.cpp` `RemoteScreen::mouseReleaseEvent` (~line 780) — the remote-pane screen: always writes PRIMARY on any drag selection, and writes the clipboard too when the setting is on.

Tests for the pane filter live in `tests/copyonselect_test.cpp`.

**Steps.**

1. In `src/CopyOnSelect.h` add an inline predicate, e.g. `inline bool copyOnSelectWorthCopying(const QString &text)`, returning true when the string contains at least 3 characters for which `QChar::isLetterOrNumber()` holds (Unicode-aware; punctuation, whitespace and symbols do not count). Comment it with the owner's rule (#C9VT).
2. Gate `CopyOnSelectFilter::eventFilter`'s deferred copy on the predicate: below the threshold, do not write either buffer and do not fire the `notify` toast (no copy happened, so nothing to announce).
3. Gate `RemoteScreen::mouseReleaseEvent` in `src/RemotePane.cpp` on the same predicate (include `CopyOnSelect.h` — already included by that file), wrapping both the PRIMARY write and the clipboard write.
4. Gate the terminal path in `engine/view/TerminalView.cpp` `mouseReleaseEvent`: the engine cannot include `src/CopyOnSelect.h`, so add the same tiny check in the file's anonymous namespace with a comment saying the rule is shared with `src/CopyOnSelect.h` (#C9VT) and must stay in step. Apply it to the `m_copyOnSelect` PRIMARY write.
5. Extend `tests/copyonselect_test.cpp`: unit cases for the predicate (`"ab"`/`"a-b"`/`"12"` → false; `"abc"`, `"a1b"`, `"a b c"` → true; empty → false) and one filter-level test: a read-only `QPlainTextEdit` with a two-character selection copied on release leaves the clipboard as it was, while a three-character one copies.

**Risks.**

- Scope decision the plan takes (flag to the owner): the threshold gates PRIMARY as well as the clipboard, and the always-on PRIMARY write in `RemoteScreen` too — the card says "highlight to copy only works if…", read as the whole gesture, not just the clipboard half. If the owner wants PRIMARY left alone, say so before Execute.
- A deliberate short copy (a two-letter variable name, an `id`) now needs Ctrl+Shift+C or the context menu. That is the requested trade-off.
- The duplicated predicate in the engine can drift from the one in `src/CopyOnSelect.h`; the cross-referencing comments are the mitigation (no shared header exists between `engine/` and `src/`).

**Verify.**

- `ctest --test-dir build -R copyonselect` (build first with `scripts/relay-build`).
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: turn on Options › Terminal › Copy on select; in a terminal pane drag-select two characters — clipboard keeps its previous contents; drag-select three or more — clipboard takes the selection. Repeat on a read-only pane surface (e.g. an info pane).

## Execution Summary
A highlight is copied only when it holds at least three letters or digits (`QChar::isLetterOrNumber`, so Unicode letters count and punctuation, whitespace and symbols do not: `a-b` is two, `a b c` is three). A shorter one leaves the clipboard and PRIMARY exactly as they were and says nothing, because nothing was copied. Explicit copies — Ctrl+Shift+C, Ctrl+C in the prompt box, the context menu's Copy, the copy buttons — are untouched and still take a one- or two-character selection.

`relay::copyOnSelectWorthCopying()` is the rule, in `src/CopyOnSelect.h` (`46d7e92a`). Every copy-on-highlight path is gated:

| Path | Where | Gated writes |
| --- | --- | --- |
| the shared pane filter (plan steps 1, 2) | `src/CopyOnSelect.h`, `CopyOnSelectFilter::eventFilter` | PRIMARY, the clipboard, and the pane's toast |
| the terminal's own release (plan step 4) | `engine/view/TerminalView.cpp`, `mouseReleaseEvent` | PRIMARY |
| the terminal pane's release (not in the plan) | `src/Pane.h`, the app-wide event filter that calls `copySelection()` on a release | the clipboard, and the toast |
| the remote pane's screen (plan step 3) | `src/RemotePane.cpp`, `RemoteScreen::mouseReleaseEvent` | PRIMARY and the clipboard |

**Deviation from the plan, for the owner:** the plan listed three paths and missed the fourth — the *terminal pane's* clipboard half, the app-wide filter in `src/Pane.h` that copied on a left-button release in the terminal. Without it a two-character drag in a terminal pane would still have taken the clipboard, which is the case the card is about (the engine's release only writes PRIMARY). `src/Pane.h`'s filter read the terminal's selection through a path shared with Ctrl+Shift+C, so the gate asks `m_backend->selectedText()` for the rule and leaves the explicit copy alone. Nothing else in that pane changed.

The engine cannot include a header from `src/`, so `engine/view/TerminalView.cpp` carries the same three-line predicate in its anonymous namespace; both comments name the other and say the two must stay in step.

## Tests
`tests/copyonselect_test.cpp` (`46d7e92a`):

- `onlyThreeLettersOrDigitsAreWorthCopying` — the rule itself: empty, `ab`, `12`, `a-b`, `...`, `  a ` are false; `abc`, `a1b`, `a b c`, `/home/elliott`, `ünï` are true.
- `aTwoCharacterHighlightLeavesTheClipboardAlone` — the filter on a read-only `QPlainTextEdit`: a two-character selection (with the setting on) leaves the clipboard as it was and does not notify, while a three-character one copies.

Run, green on this tree:

    ctest --test-dir build -R copyonselect      # 1/1 passed
    ctest --test-dir build -R remotepane        # 1/1 passed (the RemoteScreen change)

The live drive — a real highlight on the OS clipboard and PRIMARY — is
`docs/qa_evidence/2026-09-21-copy-on-highlight-three-chars/drive.sh` (`23f5525e`); it reported **6 passed, 0 failed** (see the Verification below).

## QA checklist
- [ ] Options › Terminal › **Copy on select** on. In a terminal pane, drag-select **two letters** — the clipboard still holds what it did before, and nothing is announced.
- [ ] Drag-select **three or more** — the clipboard takes the selection and the pane toasts the count, as before.
- [ ] PRIMARY too, where the platform has it: a two-character highlight in the terminal leaves the selection buffer alone; three characters take it (`xclip -o -selection primary`).
- [ ] The same on a **read-only pane surface** (the conversation ⓘ pane, a Markdown file preview, a card's body): a one- or two-character drag leaves the clipboard alone and toasts nothing; three characters copy.
- [ ] **Explicit copies still take a short selection:** Ctrl+Shift+C in the terminal, Ctrl+C with the prompt box empty, the context menu's Copy, a copy button beside a session id — all still copy two characters.
- [ ] The setting **off** (how it ships) still copies nothing on any highlight.
- [ ] Punctuation, whitespace and symbols do not count: a highlight of `a-b` (two letters) copies nothing; `a b c` (three letters) copies.
- [ ] A **remote pane** (a shared desktop): a two-character drag on the remote screen leaves the clipboard and PRIMARY alone; three characters take both.
- [ ] Nothing else moved in the terminal's own behaviour: the shortcut hint on a plain click still appears, and dragging a long selection still copies as it did.
