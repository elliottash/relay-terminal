---
id: KKYC
type: work
status: needs-verification
labels: [feature]
assignee: agent
implemented_by: glm/glm-5.3
session: b837d345-98c6-4d52-9b69-3910f3bd87cb
rank: zzzz109
created: '2026-09-18'
acceptance: the owner has chosen between a menu on click and the modifier scheme
verify: {artifact: system, primary: script, also: [person], human: none, criteria: left click opens the explorer; right/Ctrl opens the click menu; Alt navigates; Shift opens externally; files mirror the same modifiers, sign_off: none, effort: low, stakes: nuisance}
source: 'issues/feature_intake.txt, 2026-09-18: "for folders, when you click on them -- should they open the explorer or navigate there? maybe just a single click opens a menu to do one or the oth…"'
links: {plans: [], commits: [c43874b93e2d, f60949c59941, f12f76315d11, 6a06bb0c487a], evidence: [docs/qa_evidence/2026-09-25-kkyc-folder-click/, docs/qa_evidence/2026-09-25-kkyc-folder-click-scheme/], related: [], github: null}
---
# Clicking a folder: open the explorer or navigate there?

## Request
for folders, when you click on them -- should they open the explorer or navigate there? maybe just a single click opens a menu to do one or the other. ctrl click to open the explorer, shift click to navigate.

## Done means
- Left click on a local folder path in terminal output opens the explorer pane; Ctrl+click and right-click open the folder's click menu (Open in explorer · Navigate here · Open in file manager · Copy path); Alt+click `cd`s that pane's shell into it; Shift+click opens the system file manager. During a link walk: Enter opens the explorer, Alt+Enter navigates, Shift+Enter opens externally.
- Files mirror the modifiers: left click opens in Relay, right-click or Ctrl+click opens a file click menu (Open · Edit · Navigate to its folder · Open with the default app · Copy path), Alt+click `cd`s to the file's folder, Shift+click opens externally.
- Failure looks like: a folder click opening a menu, Alt+click starting a rectangular selection instead of navigating, Ctrl+click on a file opening the editor instead of the menu, or `cd` typed into a running program.
- verify: backends_test (menu builders, pure functions) → build → a person clicks a folder and a file in `ls` output (effort: low).

## Execution Summary
- 2026-09-25, commit `2485f9b3` (evidence `3c7d5c03`, `docs/qa_evidence/2026-09-25-kkyc-folder-click-scheme/`):
- Folders: `folderClickAction(control, alt, shift, fromMouse)` now maps plain click → explorer, Ctrl+click (mouse) → the click menu, Alt+click → navigate, Shift+click → external; a keyboard walk's Ctrl+Enter falls back to the explorer (no pointer for a menu).
- Files: the same chords — left opens in Relay, Ctrl+click opens a new `fileClickMenu` (Open · Edit · Navigate to its folder · Open with the default app · Copy path; Edit moved there from direct Ctrl+click, Ctrl+Enter still edits on a walk), Alt+click `cd`s to the file's folder, Shift+click opens externally.
- Right-click on a local path in the output now opens that path's own menu (`showTerminalMenu` routes to `showFolderClickMenu`/`showFileClickMenu`); the terminal right-click menu lost its `Open …`/`Navigate here` path entries, and both click menus gained `Copy path`.
- `engine/view/TerminalView.cpp`: `Qt::AltModifier` joined Ctrl/Shift in the immediate link-activation mask, so Alt+click on a link reaches the pane instead of starting a rectangular selection (Alt away from a link still does).
- Walk hints updated: folders "Enter opens the explorer, Alt+Enter navigates here, Shift+Enter opens in the file manager"; files add "Alt+Enter navigates to its folder".
- Docs: `docs/ARCHITECTURE.md` link-scan paragraph now names the single modifier scheme.
- 2026-09-25, commit `6a06bb0c` (owner's follow-up): the prompt box's directory chip takes Shift+click — it hands the pane's folder to the system file manager via `openPathExternally`, the same chord Shift+click on a folder link in the output uses; a plain click keeps opening the explorer pane, a remote (SSH) chip is unchanged, and the chip's tooltip now teaches both chords.

## Tests
- `tests/backends_test.cpp`: `folderClickFollowsTheModifiers` — the full modifier table, mouse and keyboard, Ctrl>Alt>Shift precedence; `folderClickMenuNamesBothChoicesAndTheirChords` and new `fileClickMenuNamesTheChords` — entry ids, chord labels, `navigate` greyed without a shell, `edit` greyed without an editor; the terminal-menu tests assert the path entries are gone.
- `engine/tests/ViewTest.cpp`: new `altClickCarriesItsModifier` — Alt+click on a link emits `linkActivated` with `Qt::AltModifier` (both engine cores).
- `ctest --test-dir build -R '^backends$|^relay-engine-tests$'` — 2/2 passed (16 backends functions, 77 ViewTest functions) on the landed tree's build-gate run.

## Decisions
- 2026-09-25, owner: folders — left click opens the explorer, right/Ctrl opens the context menu, Alt navigates the pane there, Shift opens externally.
- 2026-09-25, consistency: files mirror the same modifiers — left opens in Relay, right/Ctrl opens a file context menu (Edit moves there from Ctrl+click; Ctrl+Enter still edits during a link walk), Alt cds to the file's folder, Shift opens externally.
- Both click menus gain "Copy path"; the terminal right-click menu loses its path entries ("Open …", "Navigate here") because a right-click on a path now opens the dedicated menu for that path.

## Try it
- **Open:** run `docs/qa_evidence/2026-09-25-tryit-KKYC/stage.sh`, then the one line it prints (`…/scratch/tryit/kkyc-clicks/open-relay.sh`) — a sandboxed Relay window opens with `ls` output listing `reports` (a folder) and `notes.md`. Close the window when done.
- **Task (2 min):** on `reports` and on `notes.md`, try the four chords — left click, right-click or Ctrl+click, Alt+click, Shift+click — and one keyboard walk (Ctrl+Shift+L, arrows, Enter / Alt+Enter / Shift+Enter).
- **Question:** Ctrl+click on a *file* now opens its menu (Edit is inside it) instead of opening the editor directly — kept for consistency with folders. Right call, or should Ctrl+click on a file stay the direct edit?
- Screenshots and notes go beside the staging in `docs/qa_evidence/2026-09-25-tryit-KKYC/` (the ai-pass screenshots there already show each outcome).
- The mechanical pass has been run (`ai-pass.sh`, all ten chords green on Xvfb); this try is the person's judgement on the scheme, not a first look.
