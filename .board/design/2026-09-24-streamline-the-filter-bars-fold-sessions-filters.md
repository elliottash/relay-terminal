---
id: 1Q5V
type: work
status: discussing
labels: [feature, design, sessions, switchboard, ui]
rank: zzzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
source: Relay pane, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [MXMG, P7SJ], github: null}
---
# Streamline the filter bars: fold Sessions' filters into search, drop the Board's label chips row

## Issue
5 yes, can you file a card for that you observed there. add your opinion about how to streamline the top of sessions and also probably remove the list of labels from the top of the board.

## Discussion points
What is there today (2026-09-24, code reading):

- **Sessions & Projects** (`src/Conversations.cpp`): a search field plus a filter row of eight combo
  boxes — Scope, Kind, Project, Model, Date, Branch, Group, Sort — and a "More ▾" menu of three-state
  filters. The search already speaks `"a phrase"`, `file:`, `model:`, `branch:`, `is:pinned`, `-not`.
- **Board list page** (`src/BoardPane.cpp`): a filter line ("any word in the card, label:bug, …"), a
  wrapping row of section checkboxes, and a row of label chips (#VKFV).

**My opinion.**

1. **Sessions: one row, two combos, everything else typed.** Keep the search field (it grows with
   the pane) and keep **Project** and **Model** as combo boxes — those two browse a list you may not
   know by heart, which is what a combo is for. Move the rest into the field and the More menu:
   Date → a `when:` token (or stays in More), Branch → the `branch:` token it already has, Group and
   Sort → a small "Sort ▾" next to More, Scope/Kind → they duplicate what the pane's own tabs and
   the Projects/Globals tabs already choose. Active filters should show as one removable chip each
   under the field (click the × to clear), so what the field is hiding stays visible. That takes the
   top of Sessions from two-plus rows to one, and makes it a *search bar*, which is the opposite
   shape from the Board's *control strip*.
2. **Board: drop the label chips row.** The chips duplicate the filter field (`label:bug` already
   works) and a click on any row's label badge already copies that filter term (#S53Z). Removing
   the row reclaims a line on every board pane and the one people scan is the cards. If label
   discovery is the worry, offering the known labels as completions when the field holds `label:`
   covers it without a permanent row.
3. Keep the Board's **section checkboxes** — they choose what the list shows at all (the stage
   sections), so they are view controls, not filters; they can share the filter line's row when
   there is room and wrap only when the pane is narrow.
