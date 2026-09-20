---
id: SDXE
type: work
status: planned
rank: zzzzzzzzzzzzzzzi
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# bug: pane sizes jiggle in response to content

## Issue
bug: pane sizes jiggle in response to content.

when agents / shells are working and content is coming into panes, they can become wider dynamically, in a jerky / ugly way. pane sizes should not change in response to content.

## Plan
**Goal** — Pane widths must stay where the user (or the dock/equalize arithmetic) put them. Content arriving in a pane — agent output, subagent badges, usage-chip updates, ssh/phone chips — must never change a pane's width.

**Findings**
- Panes live in nested `QSplitter`s made by `RelayWindow::newSplitter` (`src/RelayWindow.h:5192`), with `setChildrenCollapsible(false)`. A splitter must satisfy every child's minimum size: when a child's `minimumSizeHint()` grows, the splitter widens that pane and steals the room from its neighbours, and the next redistribution moves them back — the reported jiggle. This exact mechanism is documented on `relay::panes::enclosingSplitters` (`src/PaneLayout.h`, "Showing or hiding something inside a pane changes that pane's minimum size …").
- The content-driven minimum widths live in the pane header's chrome widgets (`src/PaneChrome.h`):
  - `PaneUsageChip::minimumSizeHint()` returns `sizeHint()` (line 1182–1183) — it follows the CPU/memory text, which changes *constantly while agents and shells work*. Prime suspect.
  - `PaneSubagentBadge::minimumSizeHint()` returns `sizeHint()` (line 1268–1273) — appears, disappears and re-counts with subagent activity.
  - `PaneHeaderChip::minimumSizeHint()` (line 1061–1062) falls back to the full natural width whenever `m_allowed == 0` — i.e. before the header ladder has granted a width, and permanently for chips the ladder never drives (the phone/sharing chip, "anything else that never shrinks" in `HeaderWants::chips`).
- The header ladder itself (`relay::panes::headerFit`, `src/PaneLayout.cpp:122`, driven from `Pane::updateHeader()` at `src/Pane.h:14791` via `onHeaderWants`/`onHeaderFit`) already elides everything into the width the pane actually has — but the widgets' `minimumSizeHint()`s don't consistently reflect the ladder's floors, so Qt's layout can demand more width than the ladder granted and force the splitter to move.
- Already safe: `PaneBusyLine` (`setMinimumWidth(1)`, `src/Pane.h:293`), the queue strip (`QSizePolicy::Ignored` horizontal, `src/Pane.h:3692`), the floating program/guest bars (positioned over the terminal, not in the layout), `CurrentTextComboBox` (fixed "MM" minimum).

**Steps**
1. **Reproduce and identify.** Run Relay under Xvfb with an isolated `XDG_CONFIG_HOME`, three panes side by side, an agent streaming in one (with subagents and the usage chip live). Log each pane's `minimumSizeHint().width()` and each splitter's `sizes()` on a timer while content streams. Confirm exactly which widget's minimum moves (hypothesis: the usage chip first, then the badge and non-ladder chips).
2. **Clamp the chrome minimums to the ladder floors.** In `src/PaneChrome.h`: `PaneUsageChip::minimumSizeHint()` → its CPU-only floor width (or ellipsis); `PaneSubagentBadge::minimumSizeHint()` → a fixed, content-independent width; `PaneHeaderChip::minimumSizeHint()` → `ellipsisWidth()` even when `m_allowed == 0`, and a fixed floor for the chips the ladder never drives. Content that no longer fits elides or drops, per the ladder — the pane never grows for it.
3. **Audit the rest of the pane layout.** Walk every widget in `Pane`'s layout tree (header title/auto labels, model box, composer rows) for a `minimumSizeHint`/minimum width that follows content; clamp any the reproduction in step 1 flags. Do not touch the transcript/terminal widgets unless the measurement names them.
4. **Regression test.** Add a case to `tests/panelayout_test.cpp` pinning the ladder floors the new minimums encode (headerFit already tested there). If a Qt-level test harness for widgets exists in `tests/`, add one asserting a pane's `minimumSizeHint().width()` is identical before and after streaming content; otherwise cover it in the live verification below.

**Risks**
- Clamped minimums mean chips elide/drop sooner in very narrow panes — that is the ladder's existing intent, but check the narrowest layouts (three-wide plus board) still read well.
- The named widgets are a hypothesis from reading the hints; step 1's measurement comes first, and the fix follows what it shows. If the terminal/transcript widget turns out to be the mover, the plan's shape is the same: its minimum must stop following content.
- Do **not** "fix" this by re-asserting splitter sizes after every redistribution (`restoreSizes`, `src/Pane.h:14661`): that fights legitimate minimums (e.g. a Switchboard pane's floor, card #BXCN) and trades jiggle for fighting layouts.

**Verify**
- `ctest --test-dir build -R panelayout` (and any new widget test).
- Live under Xvfb, isolated `XDG_CONFIG_HOME`: three panes, one running an agent with subagents and heavy output, one a busy shell (`yes` / a build). Record splitter `sizes()` over 60 s of streaming: no pane width changes except by user drag. Capture before/after numbers as evidence under `docs/qa_evidence/`.
