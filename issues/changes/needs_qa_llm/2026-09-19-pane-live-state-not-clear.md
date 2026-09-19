---
id: V8KT
type: work
status: needs-qa-llm
labels: [change]
assignee: agent
implemented_by: Warp agent (Oz; model unspecified), 2026-09-19
rank: zzzz103
created: '2026-09-19'
acceptance: at a glance, without hovering or reading a tooltip, a pane and its tab read as "work is happening now" while an agent turn, subagents or a foreground program runs, and stop reading that way when idle
source: 'conversation, 2026-09-19: "its no clear enough if a pane agent or program is running"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-pane-live-state/], related: [XM0T], github: null}
---
# A running agent or program is not clear enough in a pane

## Issue
its no clear enough if a pane agent or program is running. this is a visual lan guage / UI issue

## What changed
All three directions from the thread, blue for terminal work and violet for agent work (owner, 2026-09-19):

- `src/PaneStatus.{h,cpp}`: `isLive`, `liveMarker` (a tab's live mark; agent work outranks a command), `pulseScale` (four breath steps, a scale never an opacity), `stateText` (the word's ink lifted to 4.5:1).
- `src/PaneChrome.h`: the header's state glyph breathes while live (600 ms, still at full size when the desktop asks for no animation via `cursorFlashTime()` 0) and a painted state word sits beside the title; `tabIcon` breathes when it is the live state, else carries a breathing corner dot in the live state's colour.
- `src/RelayWindow.h`: the 400 ms status poll drives the pulse phase; tabs repaint only while something is live.
- `tests/panestatus_test.cpp`, `tests/pulsepaint_test.cpp` (+ `pulsepaint_paint.cpp`), `docs/ARCHITECTURE.md` "Live states".

## QA checklist
- [ ] `ctest --test-dir build -R 'pulsepaint|panestatus'` passes
- [ ] `./scripts/test.sh` and `ctest --test-dir build` pass in full
- [ ] `docs/qa_evidence/2026-09-19-pane-live-state/drive.sh <build-dir>` regenerates the frames; `implementer-notes.txt` shows a nonzero *max* pairwise diff for running, relaying and mix (a zero *min* is expected: pulse phases 1 and 3 both draw 0.90)
- [ ] Manual: `sleep 30` — blue triangle breathes in the header, "Command running" beside the title, blue dot pulses on the tab; all stop when the command ends
- [ ] Manual: an agent turn — violet star breathes, "Relaying…", violet tab dot; a turn with subagents shows the star-and-dots glyph and "Subagents working"
- [ ] Manual: a failed turn in one pane beside a running command in another — the tab shows the failed disc *and* the breathing blue dot
- [ ] With `cursorFlashTime` 0 (reduce motion) the marks are still at full size and do not pulse
- [ ] Switching themes retints the marks live

Implementer evidence: docs/qa_evidence/2026-09-19-pane-live-state/
