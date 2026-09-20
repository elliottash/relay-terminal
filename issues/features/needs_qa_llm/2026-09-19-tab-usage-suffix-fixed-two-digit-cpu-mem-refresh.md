---
id: MERX
type: work
status: needs-qa-llm
labels: [feature, gui]
implemented_by: glm/glm-5.3
rank: zzzzzzzz
created: '2026-09-19'
source: pane 1, 2026-09-20
links: {plans: [], commits: ['0bbb4b6c', e932c7ab], evidence: [docs/qa_evidence/2026-09-19-tab-usage-fixed-width/], related: [], github: null}
---
# Tab usage suffix: fixed two-digit cpu/mem, refreshed every 5 s from the 5 s average

## Issue
for the tab headers, always show cpu 00% mem 00% and 01% or 05%, always use 2 digits, so they dont keep on widening and narrowing. update only once every ~5 secs or so, using the 5 sec average.

## Notes
The tab label is the one surface whose width is every other tab's layout, so it gets its own
form and clock while the chip, the Sessions tag and the tooltips keep the poll's 2.5 Hz and
`readingText()`'s quiet-halves-out rule:

- `relay::usage::formatPercent2()` — whole percents two digits wide ("00"…"99", "100" alone
  in three).
- `relay::usage::tabSuffix()` — both halves always: `  ·  cpu 07% · mem 00%`; a `Sample` with
  no reading formats as 00/00. A tab with no terminal pane takes no suffix at all.
- `relay::usage::RollingMean` + `kTabUpdateMs`/`kTabWindowMs` (5 s/5 s) — the window the label
  averages; invalid samples stay out of the numbers, entries older than the window drop.
- `RelayWindow::refreshPaneStatus` fills the window each poll; the label takes from it once
  every 5 s (`TabUsageState` replaces `m_tabUsageKey`/`Shown`/`At` and the
  `labelShouldFollow` hysteresis, which had no other caller and is gone).

The old "hold still ±3 points or 1 s" hysteresis is superseded: the request asks for a fixed
cadence and a fixed width, which make wobble impossible rather than filtered.

## Tasks

- [x] formatPercent2 + tabSuffix: both halves always, two digits each <!-- t:vt -->
- [x] RollingMean + kTabUpdateMs/kTabWindowMs; label takes the 5 s mean every 5 s in refreshPaneStatus <!-- t:3q -->
- [x] tests: paneusage covers the padding, the fixed form and the window <!-- t:5h -->
- [x] live check under Xvfb: shape, 5 s cadence, 5 s average, idle 00/00 <!-- t:yv -->
- [x] docs/ARCHITECTURE.md usage-meter paragraph <!-- t:3m -->

## QA checklist
Evidence: `docs/qa_evidence/2026-09-19-tab-usage-fixed-width/` (implementer shots +
`drive.sh` + `cadence.log`). Build `scripts/relay-build` clean; `ctest -R paneusage` passes.

- [ ] Tab label always reads `· cpu NN% · mem NN%` with two digits on both halves —
      `implementer-tab-*.png` and `tests/paneusage_test.cpp` (`oneWordingInEveryPlace`,
      `tabPercentsAreTwoDigitsWide`).
- [ ] Idle keeps the suffix at `cpu 00% · mem 00%`; the pane chip still disappears
      (`implementer-tab-idle.png` vs `implementer-idle-header.png`).
- [ ] The label moves at most once per 5 s and shows the 5 s mean — `cadence.log`'s `halved`
      stage (30% → 28% → 15%, steps ≥ 5 s apart) and `theTabAverageComesFromAWindow`.
- [ ] The chip, the Sessions tag and the tooltips are unchanged (still `readingText()`, still
      2.5 Hz) — `git diff src/PaneChrome.h` is empty; `liveTag` untouched.
- [ ] `labelShouldFollow`/`kLabelStep`/`kLabelHoldMs` gone with no dangling references
      (`rg labelShouldFollow src/ tests/` → only historical evidence/qa_evidence mentions).
- [ ] A tab with no terminal pane (Sessions only) carries no usage suffix; turning
      `appearance/pane_usage` off clears it; both recover when panes/meters return.

## Verdict
QA (codex, independent of the GLM implementer, 2026-09-20; `docs/qa_evidence/2026-09-19-tab-usage-fixed-width/qa-codex-2026-09-20.md`): **PASS, 7/7.** 24/24 paneusage tests; live Xvfb run: idle keeps `cpu 00% · mem 00%` while the chip disappears, the label stepped 30 → 29 → 15 on a ≥5 s clock showing the window's mean (not the pane's instant 15), the chip/Sessions tag/tooltips are unchanged, `labelShouldFollow`/`kLabelStep`/`kLabelHoldMs` are gone, `pane_usage` off clears the suffix and on restores it. Includes follow-up `e932c7ab` (owner, 2026-09-20: tab header "title (pane count) · cpu mem" — count now in parens, still only when a tab has more than one pane), verified live: `pj (2) · cpu 30% · mem 05%` → single pane back to no "(1)", label width pixel-stable across 30/29/15/00.
