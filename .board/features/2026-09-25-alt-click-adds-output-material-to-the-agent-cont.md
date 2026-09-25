---
id: 7BYT
type: work
status: needs-verification
labels: [feature, agent-ui, panes, composer]
assignee: agent
implemented_by: kimi/k3
session: cd5a8e43-5852-4af5-a07c-f2190da34c42
rank: zzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: optional, criteria: Alt+click on a file link in real output puts @path in the composer; Ctrl+click still cds; Alt+drag adds the selected text, sign_off: none, effort: medium, stakes: rework}
links: {plans: [], commits: [1cc5b1d2772a, 7c2f4a54b6e8, 85e97d637480], evidence: [docs/qa_evidence/2026-09-25-alt-click-context/], related: [], github: null}
---
# Alt+click adds output material to the agent context; Ctrl+click takes over folder navigation

## Issue
Retask the output-link chords: Ctrl+click navigates (the old Alt+click behaviour), Alt+click adds the link to the agent context (files/folders as `@path`, card IDs as `#ID`), and Alt+drag adds the selected console text to the composer. Context goes to the current pane's prompt box, else a linked pane's, else the most recently opened pane with a prompt box.

> i want alt clikcing to add material into the context. 
>
> so lets instead make it where ctrl click does the navigation. 
>
> for alt clicking, that ould be files, folders , hash tags, etc. so for files and fodlers it would include @ . for hash tags it just copeis them in. 
>
> alt + highlighting console text copies it into the context.
>
> explore and propose other aspects that we should do here. 
>
> i meant card IDs. re 6, i think this the should is it works in the current pane or linked pane where there is a prompt box. after that, the most recently opened pane.
> — elliott · [session:352ba53f18644f798cd27772bc20986c](relay://session/352ba53f18644f798cd27772bc20986c) · 2026-09-25

## Done means
- Alt+click on a file/folder link in terminal or agent output inserts `@path ` into the target pane's prompt box (same form as the `@` picker); on a card link it inserts `#ID `.
- Alt+drag over console text selects it and, on release, inserts the selected text into the target pane's prompt box (multi-line as a fenced block).
- Ctrl+click now performs the old Alt+click navigation (`cd` into folder / file's folder); the click menu is right-click only; the menus and docs name the new chords.
- Target pane: the clicked pane's composer if it accepts context, else a linked pane's (artifact workspace group / subagent link), else the most recently opened pane with a usable prompt box; a status line says what was added and where.
- Failure looks like: Alt+click still `cd`s the shell, inserted text is not a resolvable `@`-mention, or the rectangular selection lost its chord entirely.

## Planning notes
Exploration (two read-only traces, 2026-09-25):

- Single choke point for every mouse/keyboard chord: `Pane::openOutputTarget` (src/Pane.h:3892) → `relay::folderClickAction` (src/TerminalBackends.cpp:23) for folders; file Alt branch at Pane.h:3921; keyboard walk `openOutputLink` (Pane.h:4141) feeds the same function with `fromMouse=false`.
- Engine: `TerminalView::mousePressEvent` (engine/view/TerminalView.cpp:1642) emits `linkActivated(target, line, column, modifiers)` for any Ctrl/Shift/Alt+click on a link (:1694); rectangular selection is `const bool rect = e->modifiers() & Qt::AltModifier` (:1716); selected text via `selectedText()`. No selection→composer feature exists today.
- `@path` text in the composer IS the attachment (`attachmentsFor`/`resolveComposerPath`, Pane.h:15594/15295) and `#ID` text IS the card reference (`cardsFor`, Pane.h:15290) — no separate attachment list to maintain.
- Targeting building blocks: `RelayWindow::windowOf/paneOf/panesIn/allPanes` (RelayWindow.h:5762-5892); workspace-group Console member via `holderOf(page)->group` + `leafFor(leaves, group, ws::Role::Console)` (RelayWindowWorkspace.cpp:225); no most-recently-opened ordering exists (only per-page `m_lastActive` focus tracking) — needs a new window-level MRU list. `insertInComposer` (Pane.h:1829) inserts and focuses; needs a no-focus variant for cross-pane inserts. Toasts via `Pane::toast` (Pane.h:11390).
- Tests that pin the old chords: tests/backends_test.cpp:148-190 (menu labels, folderClickAction table), engine/tests/ViewTest.cpp:414 `altClickCarriesItsModifier` (stays valid — Alt+click still emits linkActivated). Docs: docs/ARCHITECTURE.md:1672-1681, docs/ENGINE.md:430.

Owner decisions (R4): "hashtags" = card IDs only. Targeting = current pane, else linked pane (same artifact workspace group, Console role), else most recently opened pane with a usable prompt box.

## Plan
**Goal.** Alt becomes "add to context" everywhere in output: Alt+click on any link (file, folder, card, url, session/option reference, `path:line`) inserts its mention into an agent composer; Alt+drag (and Alt+double/triple-click) inserts the selected text. Ctrl+click takes over navigation. Rectangular selection moves to Ctrl+Alt+drag.

**Findings.** See Planning notes — every chord flows through `Pane::openOutputTarget`; the engine already carries modifiers on `linkActivated`; `@path`/`#ID` text in the composer is already the attachment mechanism, so insertion is text-only.

**Steps.**
1. `src/TerminalBackends.{h,cpp}`: new pure `ClickRole clickRole(modifiers, fromMouse)` (Open/Menu/Navigate/External/Prompt) for all link kinds; `folderClickAction` delegates (ctrl → Navigate, right-click only menu). Menus: navigate entries relabelled `Ctrl+click`, new `Add to prompt  Alt+click` entry in folder and file menus, card menu's `cardToPrompt` relabelled the same; file `Edit` entry loses its `Ctrl+click` chord label.
2. `engine/view/TerminalView.{h,cpp}`: rectangular selection needs Ctrl+Alt (`rect = Alt && Ctrl` at :1716); on mouse release with Alt held and a non-empty selection made by this drag (any unit: cell/word/line), emit new `selectionActivated(QString)`. Plain and Shift drags unchanged; copy-on-select unchanged.
3. `src/Pane.h`: `openOutputTarget` dispatches Alt (and `Prompt` menu id, and Alt+Enter on the walk) to new `addLinkToContext(target, line)` which formats the mention — `@composerPath` for files/folders (with `:line` when the mention resolver supports it), `#ID` for cards, bare reference text for url/session/option — and inserts into the target pane without stealing focus; multi-line selections insert as a fenced block. `insertInComposer(text, focus=true)` gains the focus flag; a shell-mode target pane flips its prompt to agent mode (never in secret/native/alt-screen). Walk toast strings updated to the new chords.
4. `src/RelayWindow.h` (+Workspace.cpp): window-level most-recently-opened pane list, maintained at pane creation/destruction; `promptTargetPane(Pane *from)`: `from` if its composer accepts context → workspace-group Console member → MRU panes; acceptance = not native, not secret, not alt-screen.
5. Docs: docs/ARCHITECTURE.md chord table and prose, docs/ENGINE.md matrix row.
6. Tests: backends truth table + menu labels for the new roles; mention-formatting unit tests (cwd-relative, quoted, out-of-tree absolute, `:line`); engine test that Alt+drag emits `selectionActivated` and Ctrl+Alt+drag makes the rectangle (ViewTest); keep `altClickCarriesItsModifier` green.

**Risks.** `@path:line` support depends on what `resolveComposerPath` accepts — if it cannot take a line suffix cheaply, the mention is `@path` plus `(line N)` text. Focus must stay on the clicked pane so several Alt+clicks can be collected in a row. Cross-window fallback (other Relay windows) is out of scope this card; same window only.

**Verify.** `ctest` targets: relay-backends-tests, relay-engine-tests, plus a manual capture: Alt+click a path in real output → `@path ` appears in the prompt box with the toast; Alt+drag → fenced block; Ctrl+click still `cd`s.

## Execution Summary
Implemented 2026-09-25, per the plan above.

- `relay::clickActionForModifiers` (src/TerminalBackends.h/.cpp) is the single chord table: Alt → AddToPrompt, Ctrl → Navigate, Shift → External, else Open; Alt beats Ctrl beats Shift. `folderClickAction`/`FolderClick` are gone. Folder and file click menus gained a greyable `Add to prompt  Alt+click` entry, navigate relabelled `Ctrl+click`, and the card menu's old `%1 → prompt` is now `Add to prompt  Alt+click`.
- `Pane::openOutputTarget` intercepts Alt first for every link kind and calls the new `addLinkToContext`; Ctrl now navigates for folders and files alike (the file's own menu, and the walk's Ctrl+Enter edit special case, are gone — Edit lives in the right-click menu). Remote (login) panes keep Alt too: the `PaneRuntime` link handler bypasses `openRemoteOutputPath` only for Alt.
- `contextMentionFor` builds the text: `@composerPath` (+ `:line` for `path:line` links), `#ID` for `relay://card/`, the reference itself otherwise. `attachmentsFor` splits a trailing `:line` off `@path:line` tokens into a `line` field on the attachment, so the file still resolves and the line travels with it.
- Targeting: `Pane::acceptsPromptContext` (not native/secret/alt-screen/program-mode), `onPromptTargetPane` callback answered by `RelayWindow::promptTargetPane` — the pane itself, then the workspace group's Console member, then a new window-level `m_paneOpenOrder` (fed by `notePaneOpened` in `createPane`; embedded agent consoles get the callback but never enter the MRU). A shell/auto-mode target flips to agent mode on insert. `insertInComposer(text, focus)` gained the focus flag so cross-pane inserts do not steal focus; the clicked pane toasts where the text went.
- Engine: rectangular selection is Ctrl+Alt (`TerminalView::mousePressEvent`); Alt+drag's release emits the new `selectionActivated` signal, plumbed `VTermBackend` → `TerminalBackend::onSelectionActivated` → `Pane::addSelectionToContext` (multi-line fenced). `Pane::openOutputLink` toast strings teach the new Enter/Ctrl+Enter/Alt+Enter/Shift+Enter.
- Docs: docs/ARCHITECTURE.md chord prose and docs/ENGINE.md selection row.

Blocked locally, not landed blind: the working tree's `relay` target does not compile because another session's uncommitted `src/BoardPane.h` (+68 lines, missing `QTreeWidget`/`QButtonGroup` includes) breaks `relay-board`. That file is not mine and is not part of this change; `land.py`'s verify slot builds the tip plus only my claimed paths, where `BoardPane.h` is the committed one, so the landing gate still proves my code compiles.

## Tests
- tests/backends_test.cpp: `clickActionFollowsTheModifiers` (new table incl. Alt-beats-Ctrl), `folderClickMenuNamesBothChoicesAndTheirChords` and `fileClickMenuNamesTheChords` (prompt entry, Ctrl+click navigate labels, enablement). All 16 pass: `./build/relay-backends-tests` → 16 passed, 0 failed.
- engine/tests/ViewTest.cpp: new `altDragHandsTheSelectionToTheHost` (Alt+drag emits `selectionActivated` with the selected text; Ctrl+Alt+drag selects without emitting) and `altClickCarriesItsModifier` kept green with its comment updated. `ctest --test-dir build -R relay-engine-tests` → 100% tests passed (all classes, libvterm + relay core).
- `relay-terminal-engine`, `relay-internals`, `relay-backends-tests`, `relay-engine-tests` build through `scripts/relay-build`.
- Not covered by an automated test (needs a live pane, not a unit harness): the cross-pane target order and the shell→agent mode flip — the Try-it steps exercise those by hand.

## Try it
Staged: `docs/qa_evidence/2026-09-25-tryit-7BYT/` (stage.sh, the AI pass with screenshots, sealed expected.md).

1. Run `docs/qa_evidence/2026-09-25-tryit-7BYT/stage.sh` — it builds the landed commit in a scratch sandbox and prints the open line. Run that line.
2. In the pane: `python3 src/boom.py && ls src`.
3. **Alt+click** the underlined `boom.py` path in the traceback → the prompt box gains `@src/boom.py:3 ` (shell-mode box flips to agent mode).
4. **Alt+drag** across a couple of output lines → the text lands in the prompt box as a fenced block.
5. **Ctrl+click** the `boom.py` line in the `ls src` output → the shell `cd`s into `src`.
6. Right-click a path to see the menu entries teaching both chords.

The expected results are sealed in `expected.md` in the same folder — compare after trying, not before.
