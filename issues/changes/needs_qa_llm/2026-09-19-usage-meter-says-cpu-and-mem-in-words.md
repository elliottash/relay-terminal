---
id: 6BGA
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: "Claude Opus 5 subagent of a Claude Fable 5.1 session, 2026-09-19"
rank: zzzzzzzl
created: '2026-09-19'
acceptance: the pane chip, the tab suffix, the Sessions row's tag and the tooltips' first line all print the one string `cpu 12% · mem 3%` in plain words and the body face; no glyph is drawn anywhere in the meter; the halves, the narrow rung, the warning inks, the tooltip breakdown and the "usage meters" setting behave exactly as before
source: 'issues/bug_intake.txt, 2026-09-19'
links: {commits: [], evidence: [docs/qa_evidence/2026-09-19-usage-meter-words/], github: null, plans: [], related: [D03W, 0STR]}
---
# The usage meter says "cpu" and "mem" in words, in one string everywhere

## Issue

clean up the headers of tabs and panes. they are busy

Owner's verdict on the mock-up round (2026-09-19, after
`docs/qa_evidence/2026-09-19-usage-meter-numbers/`): *"the cpu / mem bar things are ugly and
unintuitive. i think it should be numbers."* From the six candidates on
`mock-relay-dark-all.png` he chose **variant a: `cpu 12% · mem 3%`** — plain words, body face,
the header's muted ink, colour only when a value is high — and **one string everywhere**: the pane
chip, the tab suffix and `relay::usage::liveTag` (and anywhere else a reading is printed) print the
same wording, so the reading is learned once.

## Decisions

- 2026-09-19, owner: **variant a, `cpu 12% · mem 3%`**, and one string on every surface. The
  mock-up round recommended **e** (`cpu 12% · mem 1.4 GB`, the memory half as a size); the owner
  took **a**, which is the same sentence one step cheaper in header width and can adopt the byte
  figure later without changing anything else.
- 2026-09-19, owner: the glyphs go. They are the "bar things": at 13 px the processor die's six
  pins read as two stacks of short bars and the memory module's four legs as a tiny bar chart, so
  the eye saw graphics where the meaning was entirely in the digits.
- This **reverses two decisions recorded on #D03W**, in the owner's words above. #D03W settled on
  a painted die and module with two bare percentages on the chip, and a separate `· 12% / 3%`
  suffix on the tab whose tooltip had to spell out which number was which. Both are gone: there is
  no glyph, and the tab carries the chip's string.
- 2026-09-19, agent: the tooltips' first line becomes `cpu 12% · mem 3% (2.1 GiB)` rather than
  `CPU 12% · memory 2.1 GiB (3%)`. "Anywhere else a reading is printed" includes the tooltip, and
  the byte figure — the thing a percent of an unknown total cannot give — stays, in brackets.
- 2026-09-19, agent: the breakdown lines turn round too, `cc1plus · cpu 40% · mem 2%`. Leaving
  them as `40% cpu · 2% mem` would have put the two word orders in the same tooltip, three lines
  apart, which is the inconsistency this card exists to remove.
- The header ladder's last rung keeps the grammar: `cpu 12%`, not a bare `12%`. Everything else
  #D03W decided stands — nothing shown while the pane is quiet, the halves shown alone when only
  one clears its floor, warning at 60 % and error at 85 %, the breakdown in the tooltips, and
  `appearance/pane_usage` turning the lot off.

## Tasks

- [x] One formatter: `relay::usage::readingText(sample, cpuOnly)`, and `readingParts()` — the same string cut into the pieces a painter colours separately <!-- t:n1 -->
- [x] `tabSuffix`, `liveTag` and `describe` all go through it; `processLine` takes the same word order <!-- t:n2 -->
- [x] `PaneChrome::PaneUsageChip`: `paintDie`/`paintModule`/`kGlyph`/`section()` deleted, the words painted in the body face, `fullWidth()`/`cpuWidth()` measured from the text <!-- t:n3 -->
- [x] `RelayWindow::tabTooltipText`: the "which of the label's two bare numbers is which" sentence removed — the label says it now <!-- t:n4 -->
- [x] `tests/paneusage_test.cpp`: the one string in the three places, the halves, the narrow rung, the painted pieces <!-- t:n5 -->
- [x] `docs/ARCHITECTURE.md`: the resource-meter section and the header ladder's rung 5 <!-- t:n6 -->
- [x] Live capture under Xvfb with an isolated HOME/XDG_CONFIG_HOME/XDG_RUNTIME_DIR/TMPDIR and `RELAY_KEYRING=off` <!-- t:n7 -->

## As built

`src/PaneUsage.h` / `src/PaneUsage.cpp` hold the one wording, and nothing else spells it:

- `readingText(sample, cpuOnly = false)` → `"cpu 12% · mem 3%"`, or the single half that is worth
  showing on its own (`"cpu 12%"`, `"mem 3%"`), or empty. `cpuOnly` drops the memory half and its
  separator together — the header ladder's `relay::panes::UsageForm::CpuOnly` rung.
- `readingParts()` is that string as a `QList<ReadingPart>`: the words with `value = false`, each
  percentage with `value = true` and the number behind it, so the chip can give a word the muted
  ink and its number the warning or error ink without writing the string a second time. Joining the
  parts' texts is `readingText()`, and a test asserts exactly that.
- `tabSuffix()` is the tab bar's own `"  ·  "` separator in front of `readingText()`; `liveTag()`
  *is* `readingText()`; `describe()` is the two halves plus the byte figure,
  `"cpu 12% · mem 3% (2.1 GiB)"`, and prints both halves whether or not they clear the floors,
  because a tooltip is asked for and the reader who asked wants the number rather than a gap.

`src/PaneChrome.h`, `PaneUsageChip`: `paintDie`, `paintModule`, `kGlyph` and `section()` are gone.
`paintEvent` walks `readingParts()`, drawing each piece at the running x in `font()` — the header's
body face, inherited as before — with `t.muted` for a word and the 60 %/85 % bands for a number.
`widthFor()` measures `readingText(m_sample, cpuOnly)` plus 7 px of padding each side, so the
ladder's two widths come from the text. The mock-up round measured the wording at 115 px of text
against the glyph form's 88 px, and 52 px against 43 px on the narrow rung — 27 px more header,
which the ladder already knows how to reclaim. The change detection, the hysteresis, the tooltip
and the "start hidden" behaviour are untouched.

`src/RelayWindow.h`: the tab tooltip's usage line is now `This tab's panes: cpu 12% · mem 3%
(2.1 GiB)` — the old prefix `CPU / memory of this tab's panes:` was there to name the two bare
numbers on the label, which name themselves now.

## QA checklist

- [ ] `./build/relay-paneusage-tests` passes (21 cases) and `ctest --test-dir build` passes
- [ ] A busy pane shows `cpu X% · mem Y%` in its header right of the state word — words, no glyphs, the header's muted ink
- [ ] The tab label ends `  ·  cpu X% · mem Y%`, the same string as the chip
- [ ] The Sessions pane's row still shows `cpu X% · mem Y%` beside "open" — the third surface, unchanged
- [ ] The chip's tooltip opens `cpu X% · mem Y% (N GiB)` and the lines under it read `<name> · cpu X% · mem Y%`, the same way round
- [ ] A pane using only CPU shows `cpu X%` alone; one using only memory shows `mem Y%` alone — never a bare number
- [ ] A narrow pane collapses the chip to `cpu X%` (the ladder's rung 5) and widening it brings the memory half back
- [ ] At high load the number goes amber at 60 % and red at 85 % while the word stays muted
- [ ] An idle pane shows no chip and no tab suffix — the header looks exactly as before
- [ ] Options → Appearance → "Pane CPU and memory" off removes all of it, on brings it back within a poll
- [x] Implementer evidence: `docs/qa_evidence/2026-09-19-usage-meter-words/`
