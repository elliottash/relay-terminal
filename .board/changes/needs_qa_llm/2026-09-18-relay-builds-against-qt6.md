---
id: WV4V
type: work
status: needs-qa-llm
labels: [bug]
component: [gui, packaging]
milestone: beta
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-18
rank: hf
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: 'owner, 2026-09-18: "can you install relay-terminal on sphinxpad and check it works" / "you can fix the qt6 compile error first if you want"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-qt6-build-and-sphinxpad-install/'], related: [XXP5], github: null}
---
# Relay compiles and runs against Qt 6, and the Ubuntu 26.04 `.deb` builds

## Behaviour as implemented

`-DRELAY_QT_MAJOR=6` now builds the whole tree, links `relay` and passes every C++ test.
Before this, five places in the source stopped the build — the follow-up on card `#XXP5`
named the first of them, and `packaging/deb/build-deb.sh` has been selecting Qt 6 for
`debian:13`, `ubuntu:25.10` and `ubuntu:26.04` all along, so that `.deb` could not have been
built by anyone.

Four are `qsizetype` narrowing: Qt 6 returns `qsizetype` where Qt 5 returned `int`, so
`std::max(0, list.indexOf(x))` no longer deduces one type. Each is now `int(...)` around the
Qt call, which is what the surrounding code already does (`int(taken.size())` in
`src/OutputLinks.cpp`):

- `src/ModelSettings.cpp` — the pinned-model dialog's current row.
- `src/ScreenPrompt.cpp` — the last `kInspectRows` rows of the screen.
- `src/SettingsPane.cpp` — the current settings tab, and `moveCurrent()`'s clamp.

The fifth was a real defect, not a port detail. `RelayWindow::applySingleClickSetting()` used
`top->findChildren<relay::FileExplorer *>()`, and `FileExplorer` has no `Q_OBJECT`. Qt 6
static-asserts on that; **Qt 5 compiles it and matches every `QWidget` child**, because
`FileExplorer::staticMetaObject` resolves to `QWidget`'s. So on the Qt 5 builds everyone runs,
turning "Open items with a single click" on or off called `setSingleClick()` on unrelated
widgets reinterpreted as explorers. It now walks `findChildren<QWidget *>()` and
`dynamic_cast`s, the same way `chromeOf()` already finds a `PaneChrome`.

`AUTO` still picks Qt 5 wherever it is installed. That stays a product decision: Qt 6 has now
been built, tested and smoke-tested, but Qt 5 is what the desktop has been run on for months.

## Why

Relay would not build on the owner's laptop (`sphinxpad`, Ubuntu 26.04), which ships Qt 6 and
is the machine the LAN multiplayer work needs a second Relay on.

## Checks

- `-DRELAY_QT_MAJOR=6` on Ubuntu 26.04 x86_64 (Qt 6.9, KF6 highlighter present): builds clean,
  `ctest` 44/45.
- `-DRELAY_QT_MAJOR=6` on aarch64 (Qt 6.4, no KF6, no Qt PDF): builds clean.
- `-DRELAY_QT_MAJOR=5` on aarch64: builds clean, `ctest` 44/45 — unchanged by this card.
- `backend-and-bash` also failed on `main` for reasons outside this card: two `test_roles`
  cases read the developer's own settings (fixed separately in `4a81e1f`, which isolates
  `XDG_CONFIG_HOME`), and `test_tools`'
  `test_absolute_and_parent_paths_allowed_inside_workspace` is card `#E99H`, being restored by
  another session. Neither is affected by this change.
- `cpack -G DEB` produces `relay_0.1.0-1~ubuntu26.04_amd64.deb`; `apt install` of it, then
  `packaging/smoke-installed.sh`, passes — window mapped, agent worker and Bash integration
  shell both started.
- Driven live under Xvfb on sphinxpad: a typed command runs and renders
  (`docs/qa_evidence/2026-09-18-qt6-build-and-sphinxpad-install/relay-on-sphinxpad.png`).

## Left for QA

- The single-click fix is behavioural: with an explorer pane and a terminal pane open, toggling
  "Open items with a single click" must change the explorer and leave every other pane alone —
  on a Qt 5 build, where the old code touched them.
- Qt 6 at the keyboard rather than under Xvfb: fonts, the composer, drag-to-split, the file
  preview with the KF6 highlighter.
- PDF preview stays off in the Qt 6 packages (`packaging/deb/build-deb.sh` omits `qt6-pdf-dev`
  pending the scoped-enum work in `src/FilePanes.cpp`); that is still open, see ROADMAP.
