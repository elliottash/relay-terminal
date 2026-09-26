---
id: ZPHJ
type: work
status: needs-verification
labels: [feature, panes]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: ca806657-fe9e-4354-bea5-0f77784e2ee7
parent: PBZ4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
source: terminal pane, 2026-09-25
links: {plans: [], commits: [c80f0c8047b2], evidence: [docs/qa_evidence/2026-09-25-zphj-agent-divider/], related: [PBZ4, 2FQ9], github: null}
---
# Moveable split between an artifact and its docked agent

## Issue
The divider between an artifact (file editor/preview) and its docked agent console should be draggable, so more of the agent thread — including its chain of thought — can be shown in place, instead of having to pop the console out into its own pane (#2FQ9). #PBZ4 landed the docked console; this makes its share of the pane adjustable.

> make the split with the artifact agent moveable. so you can see more of the thread / COT if you want, without popping it out.
> — elliott · [session:46dc7f7d90f746138bbd7773c040726f](relay://session/46dc7f7d90f746138bbd7773c040726f) · 2026-09-25

## Done means
In a file editor or text/Markdown preview, and in a standalone Card pane, dragging the divider gives the docked agent more or less room and visibly changes how much of its thread is shown.
Each pane restores its chosen split after closing and reopening or restoring the window; one pane's drag does not resize another pane's agent.
Folding and reopening the file agent keeps its collapsed one-row behavior and returns to the chosen expanded size. A handle that cannot drag in a Card pane, a size that resets, or neighboring panes that jump during folding fails this card.

## Plan
**Goal.** Make the content/agent boundary draggable in file editor and text/Markdown preview panes and in a standalone Card pane. Preserve each pane's chosen share through folding, reopening, and window restore.

**Findings.** `src/FilePanes.cpp` puts `FilePreview::m_stack` and `PlanEditor::m_editor` above `ArtifactDock` in ordinary vertical layouts; `ArtifactDock::updateHeight()` caps its body at 45% and `applyCollapsed()` swaps between row and body. `src/CardPane.h` puts `CardDetail::m_replyFrame` below the scrolling card document with no internal splitter; `BoardView::updateConsoleHeight()` in `src/BoardPane.cpp` caps its console at 40%, including in a solo Card pane. `ToolPane::node()` in `src/PaneChrome.h` saves Card, preview and plan leaves; `RelayWindow::buildNodeWidget()` and `serializeNode()` restore/save those leaves in `src/RelayWindowCore.cpp` and `src/RelayWindow.h`. `newSplitter()` saves outer pane drags, but cannot reach these internal layouts.

**Steps.**
1. Add a vertical, visibly grabbable splitter between each file pane's content area and `ArtifactDock` in `src/FilePanes.{h,cpp}`. Replace the expanded 45% height cap with splitter sizing and sensible minimums; keep the dock's collapsed row, console instance, focus, change list, and review controls intact. Remember the last expanded share while folded, restoring it after unfold and after the pane becomes visible.
2. Add an internal vertical splitter between the Card page's reading/editing area and `m_replyFrame` in `CardDetail` (`src/CardPane.h`), exposed through `BoardView` (`src/BoardPane.{h,cpp}`). Keep the busy strip and composer together, the document scrollable, and the existing list-versus-detail splitter separate. Remove the Card console's 40% maximum in `BoardView::updateConsoleHeight()` while leaving the list page's sizing rule intact.
3. Expose each internal split's expanded size/share and a change callback. Save it with the corresponding `card`, `preview`, or `plan` leaf in `ToolPane::node()` (`src/PaneChrome.h`); apply it on the matching restore paths in `RelayWindow::buildNodeWidget()` (`src/RelayWindowCore.cpp`). Wire user drags to the window's debounced `scheduleSave()`, and apply saved sizes after layout geometry exists so constructor defaults and first console creation do not overwrite them. Missing or invalid values use the present default.
4. Add focused Qt tests for drag changing both shares, per-pane save/restore, fold/unfold retaining the expanded file split, and a standalone Card page with an active transcript. Guard against programmatic splitter size changes saving collapsed or zero sizes.

**Risks.** The Card page differs from the file dock: its agent is an always-present reply frame, so the handle must move that frame without hiding its composer or disturbing the separate Board list/detail split. Widget minimum sizes and delayed console creation can force a restored split to drift or make adjacent panes jiggle; clamp to available height and verify after show/resize. No owner decision is needed.

**Verify.** Run `ctest --test-dir build -R 'filepanes|filesync|boardpane|boardsolo|windowstate|panelayout'` after a build via `scripts/relay-build` (read `docs/BUILDING.md` first). In an isolated live Relay window, open a text/Markdown preview and editor plus a standalone Card pane with a long agent thread; drag each boundary both ways, fold/unfold the file agent, close/reopen the panes and restart/restore the window. Capture before/after Card pane and restored-size evidence, and check neighboring pane edges stay still.

## Execution Summary
Commit c80f0c80. New `relay::AgentSplit` (`src/AgentSplit.h`), a vertical QSplitter with a visible grip. It holds the agent's share (0..1) across resizes, records only user drags (`splitterMoved`; programmatic `setSizes` never saves) and sizes a folded agent to its size hint with the handle disabled.
- File preview/editor and plan editor (`src/FilePanes.{h,cpp}`): content above, `ArtifactDock` below, default 45%. The dock's 45% cap is gone (floor of 6 lines); `ArtifactDock::onFoldChanged` folds and unfolds the split.
- Card page (`src/CardPane.h`): the reading area and `m_replyFrame` share an `AgentSplit` (default 40%), folded until the console exists. `BoardView::updateConsoleHeight` no longer caps the card console (floor of 8 lines); the list page's rule is unchanged. `BoardView::cardSplit()` exposes it.
- Persistence: `ToolPane::node()` saves `agent` on card/plan/preview leaves once dragged. `buildNodeWidget` restores it (`src/RelayWindowCore.cpp`), and a drag calls the window manager's debounced `scheduleSave` (`createToolPane`, `createCardPane`).
- Live limits: the Card agent grows until only the card header and about two lines of the document remain, and shrinks to the console minimum. `src/AgentSplit.h` is header-only and not added to the CMake source lists; it has no Q_OBJECT.

## Tests
Run in a land.py verify slot (tip + this change): `ctest -R '^(filesync|boardsolo|filepanes|boardpane|windowstate)$'`, 5/5 passed.
- filesync `theAgentDividerDragsFoldsAndRestores`: fold row and disabled handle, 45% default, drag up/down, fold/unfold keeps the choice, resize keeps the share, restored share, per-pane independence, invalid share falls back to the default.
- filesync `thePlanEditorsAgentDividerDrags`.
- boardsolo `theCardPanesAgentDividerDragsAndRestores`: standalone Card pane, drag past the old 40% cap and back, resize, restored share, per-pane independence.
- Live under Xvfb with an isolated profile (see NOTES.md in the evidence folder): restore at 70%/65%, Card drag down saved 0.2365 and restored after a restart, file drag saved 0.428, fold/unfold returned to 70% with steady outer pane edges.
