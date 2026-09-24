---
id: MXMG
type: work
status: executing
labels: [feature, design, switchboard, sessions, ui]
assignee: agent
implemented_by: glm/glm-5.3
session: f0559dc0-7c61-4eb9-8784-3ecd8e8acf7c
rank: zzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: visual, primary: script, also: [ai-visual], human: none, criteria: 'The two panes are distinguishable in a screenshot at a glance: different selection/header hue, different row shapes, different typography, stronger bands.', sign_off: none, effort: medium, stakes: nuisance}
source: Claude Code guest pane, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [SPBN, P7SJ], github: null}
---
# Make the Board and the Sessions manager look visually distinct

## Issue
is there a card on making the baord and sessinos manager look more visually distinct? ... yeah, file that, and then take a look at both panes and give me suggestions for how to make them more distinct

## Discussion points
Looked at both panes on 2026-09-24 (captures `docs/qa_evidence/2026-09-23-tryit-ESDF/captures/01-board-opens-flat-recent.png`,
`docs/qa_evidence/2026-09-22-sessions-resume/01-sessions.png`; code `src/PaneStatus.cpp` `typeStyle`).

**Why they read alike.** The only difference is the #SPBN header band: a 13 % tint, brass for
the Board and shell blue for Sessions, about 30 px tall. Below it the two panes share a skeleton: a
filter bar on top, a sorted single-line table with an *Updated* column, a key-hint strip at the
bottom, the same ground, and the same orange accent on the selected row. Their title-bar buttons
sit next to each other, too.

Suggestions, cheapest first:
1. **Carry the hue past the band.** Colour the selected row, the column-header row and a 2 px
   left edge (or the pane border) in the pane's own `typeStyle` hue, not the global accent. Then
   the selection in Sessions is blue and the Board's is brass. The change is small and reuses
   `typeStyle`.
2. **Give rows different shapes.** The Board keeps dense one-line task rows: `#ID` chip, stage as
   a coloured pill (it is plain text now), and label chips. Sessions becomes two-line conversation
   rows: the title in bold, then a muted line with the first prompt or recap, plus model and
   project chips and a relative time. That makes one a list of tasks and the other a list of chats.
3. **Give them different typography.** The Board already uses monospace small caps for stages and
   IDs; lean into `docs/SWITCHBOARD-AESTHETIC.md` there (instrument panel, brass). Sessions stays
   proportional and reads like a notebook.
4. **Group by default differently.** The Board groups by stage. Sessions defaults to a timeline
   with sticky day headers (Today, Yesterday, Last week), so the grouping alone tells the panes
   apart.
5. **Lighten Sessions' filter block.** It has two rows of combo boxes above the list, where the
   Board has a row of checkboxes. Folding Sessions' filters into the search field
   (`model:`, `project:`, which already exist) plus one chip row would free height for the list
   and set it apart from the Board's filter bar.
6. **Make the band stronger on these two**, for example a faint watermark of the pane glyph
   (jacks or list) at the right of the band, or a slightly stronger tint.
7. **Fix the name mismatch.** The band reads "Projects and Sessions", but the type table in
   `src/PaneStatus.cpp` calls it "Sessions & Projects". Pick one.

Recommendation: do 1 + 2 + 7 first. The hue is what you see at a glance, and the row shape is what
tells you what you are looking at. 3 to 6 are refinements to try once those land.

## Decisions
- 2026-09-24, the owner: "nice, lets do 1-3." — hue carried past the band, different row shapes, typography split.
- 2026-09-24, the owner: "6 ok" — stronger band on these two panes.
- 2026-09-24, the owner: "7 its Sessions & Projects, which is what i see in my pane." — the name is **Sessions & Projects** everywhere in code (`src/PaneStatus.cpp`, `src/RelayWindow.cpp`); the "Projects and Sessions" I read was a stale capture. Nothing to change.
- 2026-09-24, the owner: "5 yes, can you file a card for that you observed there." — filter-bar streamlining split out to its own card.

## Done means
- The Board pane and the Sessions & Projects pane each carry their own hue below the band: selected row, column-header row and a thin left edge in the pane's `typeStyle` hue instead of the shared orange accent.
- Board rows read as tasks: `#ID` chip, stage as a coloured pill, label chips; monospace small caps for stage and ID (switchboard instrument look).
- Sessions rows read as conversations: two-line rows, bold title over a muted summary line, model/project chips and a relative time, proportional type.
- The band on these two panes is stronger than the generic 13% tint (stronger tint and/or a faint pane-glyph watermark).
- The pane name stays "Sessions & Projects" (decision above); no rename.
- Screenshots of both panes side by side in the evidence folder show the two are distinguishable at a glance.
- Not in scope: filter-bar streamlining (#1Q5V), grouping changes (suggestion 4).
