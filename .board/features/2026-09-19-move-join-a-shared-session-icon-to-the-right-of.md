---
id: 20XA
type: work
status: needs-verification
labels: [feature, gui]
assignee: agent
implemented_by: glm/glm-5.3
priority: 3
rank: zzzzzzz
created: '2026-09-19'
links: {plans: [], commits: [d178197f87d9c3d00756472d0ac180dea3f7af49, 06c4a92326f14eda49ed6736274fc19b5bb236c9], evidence: [docs/qa_evidence/2026-09-20-join-plug-header/], related: [], github: null}
---
# move "join a shared session" icon to the right of the settings button.

## Issue
move "join a shared session" icon at the top right, to the right of the settings button, with a small separator on each side of it. Decided row order (owner, 2026-09-20): bell ‖ separator ‖ Actions, Sessions, Switchboard, settings ‖ separator ‖ join-plug ‖ separator ‖ minimize, maximize, close.

## Plan
**Goal.** The header's right row becomes, per the owner's decision of 2026-09-20: bell ‖ hairline separator ‖ Actions, Sessions, Switchboard, Options gear ‖ hairline separator ‖ join-plug ‖ hairline separator ‖ minimize, maximize, close. The "Join a shared session" plug moves from the far left of the row (where it sits today, before the bell) to after the gear; the third separator lives inside the `!m_nativeFrame` block, so under `window/native_frame` the row ends … gear ‖ separator ‖ plug.

**Findings.** (Re-verified 2026-09-20; line numbers from the current tree.)
- The row is built in `RelayWindow::buildWindowChrome` (`src/RelayWindow.h` ~5743–5829) as a `QHBoxLayout rightRow` (5773–5775: margins 6/0/6/0, spacing 2). Today's order: the `m_connect` block (5778–5791 — creation with `ChromeButton::Glyph::Connect`, tooltip "Join a shared session", menu with "Join with a code…" → `joinSharedSession()` and "Open a pane your other desktop shares…", `rightRow->addWidget(m_connect)`), then `m_bell` (5792–5794), `addSpacing(6)` (5795), then one `ChromeButton` per `relay::panestatus::toolButtons()` (`src/PaneStatus.cpp:384–399`: actions, sessions, board, options — options last), then `if (!m_nativeFrame)` (5808–5821): `addSpacing(8)`, minimize, maximize, close.
- There is no separator widget: `Theme.cpp:345` styles `QToolBar::separator` (1px `@border`) but this row is not a toolbar. The hairline token is `relay::theme::Border` (`src/Theme.h:33`); `ChromeButton` reads theme colours at paint time, so a painted separator follows theme switches for free.
- Nothing asserts the row's order: `tests/panestatus_test.cpp` covers `toolButtons()` (order unchanged by this card); `tests/themeswitch_test.cpp` checks CSS strings. `docs/ARCHITECTURE.md:257` ("Top right" table row) and the header comments at `src/Theme.cpp:521–522` and `src/WindowChrome.h:29–35` describe the row and do not yet name the plug.

**Steps.**
1. `src/WindowChrome.h`: add a `ChromeSeparator` beside `ChromeButton` — a `QWidget` with `setFixedSize(1, ChromeButton::kSize)` painting a 1px vertical line in `relay::theme::Border`, inset ~7px top and bottom, drawn at the half-pixel for crispness. Refresh the file-top comment (~4–6) and the window-header comment (~29–35) to name it and the new order.
2. `src/RelayWindow.h`, `buildWindowChrome`: move the whole `m_connect` block (5778–5791) from before the bell to after the tool-button loop (after 5806). The loop appends options last, so a plain `rightRow->addWidget(m_connect);` lands the plug after the gear — no `insertWidget` needed. Keep the member comment at ~7080.
3. Same function, three separators, each as `addSpacing(4)`, `ChromeSeparator`, `addSpacing(4)` (same idiom as today's gaps; the layout's spacing 2 still applies between items):
   a. Replace the `addSpacing(6)` after the bell (5795) — bell ‖ sep ‖ tool buttons.
   b. After the tool loop, before `addWidget(m_connect)` — gear ‖ sep ‖ plug.
   c. Inside the `if (!m_nativeFrame)` block, replacing `addSpacing(8)` (5809) — plug ‖ sep ‖ window buttons; this one disappears with them.
   Refresh the section comment at ~5738 ("window header: Relay icon, bell, actions, minimize/maximize/close") to the new order.
4. Docs/comments: `docs/ARCHITECTURE.md` "Top right" row (~257) — bell ‖ separator ‖ Actions, Sessions, Switchboard, gear ‖ separator ‖ plug ‖ separator ‖ window buttons, and the fallback note (~270–271): under the system title bar the row ends at the plug, no trailing separator. `src/Theme.cpp` ~521–522 header comment — same order. `src/PaneStatus.cpp:384`: note the plug is not a `toolButtons()` entry and now sits after the gear, before the window buttons.

**Risks.**
- *Third separator's scope (settled per the decision, restating for the implementer):* it sits inside `!m_nativeFrame`, so the native-frame row ends … gear ‖ sep ‖ plug. If the owner wants it always present, that is a one-line move out of the block — say so before Execute.
- The separators are child widgets, so a press starting on their 1px will not drag the window (`headerDrag` at ~6009 already ignores children) — same as the buttons, negligible.
- A 1px line on fractional scaling can render soft; same width and token as `QToolBar::separator`.
- No automated test covers the row order; the check is visual, and building a test seam for `buildWindowChrome` is not worth it at this size.

**Verify.**
- Build with `scripts/relay-build`.
- Under Xvfb with an isolated `XDG_CONFIG_HOME` (the `docs/qa_evidence/2026-09-17-window-header/drive.sh` pattern): capture the header at 1× and 3× — bell, hairline, Actions/Sessions/Switchboard/gear, hairline, plug, hairline, window buttons; click the plug (menu still offers "Join with a code…" / "Open a pane your other desktop shares…"); open the bell popup; switch dark/light theme (separators repaint); open a window with `window/native_frame` set (row ends … gear ‖ hairline ‖ plug; no trailing separator, no window buttons).
- Evidence under `docs/qa_evidence/<date>-join-plug-header/`; card then moves to `needs-verification` with this checklist.

## Decisions
- 2026-09-20 (owner): "i meant it should be bell - separator - actions sessions settings - separator - connect - separator - minimize maximuize close". Three separators: after the bell, and one each side of the plug. The plug sits *right* of the settings gear — the card's original title was right, and the planned Issue's "left of the settings button" is reversed. "Actions sessions settings" reads as the whole tool-button group, Switchboard included.
- This decision supersedes parts of the `## Plan` above: its Goal order and steps 2–3 build the old row (plug left of the gear via `insertWidget`, one separator only). Rework before Execute — the plug now lands with a plain `addWidget` after the gear, and the separator after the plug belongs inside the `!m_nativeFrame` block so it disappears with the window buttons.

## QA checklist
From the card's Verify step — all PASS in `docs/qa_evidence/2026-09-20-join-plug-header/` (drive.py, 14 checks, 2026-09-20):

- [x] `scripts/relay-build` clean; land.py's verify build of the exact tree passed; `ctest -R panestatus` passes.
- [x] Header at 1× and 300% (dark-copper default): bell ‖ hairline ‖ Actions, Sessions, Switchboard, gear ‖ hairline ‖ plug ‖ hairline ‖ minimize, maximize, close — 12 ink clusters, hairlines the 1px ones, painted in the theme's border token (`implementer-01*`).
- [x] Clicking the plug (right of the gear) opens the menu with "Join with a code…" and "Open a pane your other desktop shares…" (`implementer-02`).
- [x] The bell popup still opens ("NOTIFICATIONS / Clear all / Nothing yet…", `implementer-03`).
- [x] relay-light: same row, hairlines repaint in the light border `#d3d7e0` (`implementer-04*`).
- [x] `window/native_frame`: row ends … gear ‖ hairline ‖ plug — no trailing hairline, no window buttons, and the last button is the plug (its menu opens there too; `implementer-05*`, `implementer-06`).

Verify by eye, if you want more than the drive: `Xvfb :58 -screen 0 1600x1000x24 &` then `DISPLAY=:58 python3 docs/qa_evidence/2026-09-20-join-plug-header/drive.py`.
