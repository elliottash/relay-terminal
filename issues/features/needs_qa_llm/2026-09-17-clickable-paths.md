---
id: YZTK
type: work
status: needs-qa-llm
component: [gui, shell-integration]
milestone: desktop-alpha
workstream: terminal
assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-17
rank: 2b
created: '2026-09-17'
acceptance: clicking a directory path in terminal output opens the explorer pane; clicking a file opens the preview pane
source: '`issues/feature_intake.txt`, "parse all folders and filenames and highlight them", "clicking a directory or the terminal working directory opens a dolphin-like file explorer pane", "clicking a previewable file opens it in a pane"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-clickable-paths/'], related: ['issues/features/needs_qa_llm/2026-09-17-keyboard-jump-to-output-links.md'], github: null}
---
# Clickable file and folder paths open Relay panes

## Findings (2026-09-17)

Konsole 23.08.5 source, `src/filterHotSpots/FileFilterHotspot.cpp`: clicking a file hotspot opens
directories and non-text files with `KIO::OpenUrlJob` (the system default app). Text files go to
the profile's `TextEditorCmd` when set. KonsolePart emits no click signal and exposes no screen text.

## Options

1. **Fork-free, now:** `relay open PATH` shell command, clicking the pane's directory label, the
   actions palette, and the profile text-editor command pointing at a Relay helper (text files only).
2. **Konsole fork / patched KonsolePart:** patch `FileFilterHotSpot::activate` to hand every click to
   the host; also expose screen text and alternate-screen state. Full coverage on Linux; cost is
   building and shipping Konsole and rebasing the patch.
3. **Relay-owned terminal engine** (libvterm + ConPTY): full control on every platform; see
   `issues/features/needs_qa_llm/2026-09-17-engine-integration.md`.

Option 1 landed first with the plain-Qt file panes. Option 3 is what this card now implements:
the Relay engine is the process default, so path clicking is handled by Relay itself, and the
KonsolePart path is kept exactly as it was for `--engine=konsole` panes.

## Behavior as implemented (2026-09-17, option 3)

**The rules are a library, not terminal code.** `src/OutputLinks.{h,cpp}` (`relay-outputlinks`,
namespace `relay::links`) takes one logical line of text plus a filesystem probe and returns the
links in it. No widget, no terminal, and the probe is injected, so every format is unit-tested
(`tests/outputlinks_test.cpp`, 25 cases).

- `candidates(text)` finds non-overlapping spans, left to right:
  - **URLs of any scheme** (`https://`, `ftp://`, `ssh://`, `foo://`) stay URLs and are never
    treated as relative paths; a trailing `.,;:!?)]}` is not part of them.
  - **Python tracebacks**: `File "/path/x.py", line 12, in <module>` (both quote styles) — the
    span is the path, the line comes from the text.
  - **tsc / MSVC / Qt Creator**: `src/app.ts(12,5): error TS2304` — span covers `file(12,5)`.
  - **Quoted names**, including names with spaces (`'my file.txt'`, what GNU `ls` prints on a
    terminal), with a `:line[:column]` after the closing quote if there is one.
  - **Bare tokens**: `ls` names, `file:line[:column]` from gcc, clang, cargo (`--> src/main.rs:4:9`),
    `grep -n` (`notes.txt:42:the matching text` — the prefix before the number wins), pytest node
    ids (`tests/test_x.py::test_case`), stack frames inside brackets
    (`at Object.<anonymous> (/p/app.ts:12:5)`), and backslash-escaped spaces (`my\ file.txt`).
- `resolve(candidate, cwd, home, probe)` expands `~` and `~/`, resolves a relative path against
  the pane's directory and asks the probe. **A path that does not exist is not a link.** Before
  the probe it rejects `--flags`, bare numbers, version strings and IPs (`1.2.3`, `192.168.0.1`),
  times (`12:30`), `FOO=bar`, and anything with shell metacharacters. A name that really contains
  a colon (`weird:name`) is tried before reading the same text as `file:line`.
- `links::Cursor` is the keyboard cursor over the ordered list (see the sibling card GWXM).

**The pane's directory** is `TerminalView::currentDirectory()`: the OSC 7 directory when the shell
integration is on, else `/proc/<foreground or shell pid>/cwd`, else the process cwd.

**In an engine pane** (`engine/view/TerminalView.cpp`):

- **Hover** underlines the link under the pointer, sets the pointing-hand cursor and shows the
  resolved absolute target (with `:line`, and "(folder)" for a directory) as the tooltip. No Ctrl
  needed any more; the scan runs once per cell the pointer enters, not per pixel.
- **Plain left click** follows the link; **Ctrl+click** does too, as in KonsolePart. A click that
  drags still selects text: the plain click opens on *release*, only if the pointer neither moved
  out of the link nor started a selection. A plain click only opens **in the active pane**
  (`setPlainClickOpensLinks`, driven from `RelayWindow::setActiveLeaf`), so the click that moves
  the focus into another pane cannot open a file by accident. Ctrl+click always opens.
- **Right click** on a link adds three entries above the usual menu: **Open `<name>`** (the Relay
  pane), **Open in the system editor** (`QDesktopServices` → the desktop default app) and
  **Copy path** / **Copy link**.
- Soft-wrapped rows are joined first, so a path that wrapped at the terminal edge is one link.
- An OSC 8 hyperlink still wins over the text; a `file://` OSC 8 target now opens in a Relay pane
  instead of the browser.

**Routing** (`Pane::openOutputTarget` in `src/main.cpp`): a URL goes to `QDesktopServices`, a
folder opens an explorer pane, a file opens a preview pane **at its line** (`onOpenPath` now
carries the line, and `RelayWindow::openPath` already scrolls the preview there). A click also
shows the shortcut hint for the keyboard walk ("Next time: Ctrl+Shift+L …", WARP.md standing rule).

**KonsolePart panes are unchanged**: the profile's `UnderlineFilesEnabled=true` and
`TextEditorCmdCustom=relay-open PATH:LINE:COLUMN` still hand text files to `scripts/relay-open`,
which still falls back to `xdg-open`.

Docs: `docs/ARCHITECTURE.md` section 10 (new "Clickable paths in terminal output"), section 16
(capabilities), `docs/ENGINE.md` (parity table, `TerminalBackend` API).

## Explicit gaps

1. **KonsolePart panes keep the old limits**: only text files, only Ctrl+click, no `:line:column`
   (Konsole 23.08 does not make `src/main.cpp:3:24` a hotspot at all), folders and images go to
   KIO, no hover tooltip, no context-menu entries. Verified live; see the evidence.
2. **A bare word in prose that happens to name a file in the pane's directory becomes a link.**
   That is deliberate (it is what makes `ls` output clickable) but it means a sentence mentioning
   `Makefile` in a directory that has one is underlined.
3. **No `line` for a folder, and no column anywhere.** `openPath` takes a line; the column is
   parsed, passed to the host and dropped there (the preview has no column API).
4. **The keyboard walk's row mapping is approximate for wide characters.** It maps a text index to
   a cell by dividing by the column count, so a line with CJK or emoji before the link can put the
   highlight a cell or two off. The click path uses exact cell positions and is unaffected.
5. **No setting.** Plain-click-opens cannot be turned off from the UI (only by the host call), and
   there is no "always use the system editor" preference.
6. **Not tried:** the ghostty core (no libghostty-vt prefix on this machine), remote paths over
   SSH (the probe stats the local filesystem, so a remote path simply is not a link), Windows-style
   paths, and OSC 8 links whose URI is a `file://` on another host.

## Implementer check (not a QA verdict)

Build: `cmake --build build` clean; a fresh configure + full build in an empty directory produced
no warnings. `./scripts/test.sh`: 508 tests OK. `ctest --test-dir build`: 17/17 (the 16 groups that
existed before plus the new `outputlinks`).

New tests: `tests/outputlinks_test.cpp` (absolute, relative, `~`, bare `ls` names, `file:line:col`,
grep, cargo, tsc, python tracebacks, pytest, eslint, stack frames, quoted and backslash-escaped
spaces, a colon in a real name; false positives: `--flag`, `http://` stays a URL, bare numbers and
versions, `a/b` in prose, a path that does not exist; relative resolution against two different
directories; the cursor) and `engine/tests/ViewTest.cpp::plainClickFollowsAPath`.

Live run under Xvfb on **both engines**:
`docs/qa_evidence/2026-09-17-clickable-paths/` (`drive.sh`, `NOTES.md`, screenshots). `ls`, a
`grep -n` hit, a Python traceback, cargo- and tsc-style errors and a name with spaces; hover
underline + tooltip; a plain click on a folder opened the explorer; Ctrl+click on
`src/main.cpp:3:24` opened the preview. On KonsolePart, Ctrl+click on `code.py` opened the preview
through `relay-open`, and the folder click did nothing (gap 1).

## QA checklist

1. In a default pane (Relay engine), run `ls` in a directory with files and folders. Hover a name:
   it underlines, the cursor becomes a hand, and the tooltip shows the absolute path (plus
   "(folder)" for a directory). Hover a word that is not a file: nothing happens.
2. Click a folder name → an explorer pane opens on it. Click a file name → a preview pane opens.
   Ctrl+click both → the same. Check an image and a PDF too.
3. `grep -n <word> <file>` → click the `file:line:` hit: the preview opens **and scrolls to that
   line**. Same for a `gcc`/`clang` error (`x.cpp:12:5: error:`), a cargo `--> src/x.rs:4:9`, a
   `tsc` `src/x.ts(12,5):`, a Python traceback `File "…", line N`, and a pytest
   `tests/test_x.py::test_y` / `tests/test_x.py:88:`.
4. A name with spaces: `touch 'my file.txt'; ls` and `ls -l 'my file.txt'` → the quoted name is one
   link and opens. Also `echo my\ file.txt`.
5. Relative and `~` paths: `echo src/main.cpp`, `echo ./src/main.cpp`, `echo ~/.bashrc`, `echo ~`.
   `cd` somewhere else and confirm the same relative text is **not** a link there.
6. False positives: `ls --color=always -la`, `echo 1.2.3 192.168.0.1 42`, a sentence with `a/b` in
   it, `echo --flag=value` — none of them underline. `echo https://example.com/src/main.cpp` stays
   a URL and opens in the browser, not in a pane.
7. Drag across a path with the left button: it selects text and does **not** open anything.
8. Click into an *inactive* pane on top of a path: the first click only moves the focus; the second
   one (or Ctrl+click on the first) opens.
9. Right-click a path: "Open …", "Open in the system editor" (opens the desktop default app) and
   "Copy path" (paste it somewhere to check). Right-click a URL: "Copy link".
10. A long path that wraps at the terminal edge: hovering either half underlines it and clicking
    opens the whole path.
11. With the shell integration on (`terminal/shell_integration`), `cd` into a subdirectory and
    confirm relative paths resolve against the new directory.
12. `relay --engine=konsole`: Ctrl+click a text file still opens Relay's preview (`relay-open`),
    and `relay open PATH` in the shell still works. Confirm the documented Konsole limits (gap 1).
13. Nothing regressed: `./scripts/test.sh`, `ctest --test-dir build`.
