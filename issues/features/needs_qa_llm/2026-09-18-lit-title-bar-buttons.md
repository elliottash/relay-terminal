---
id: E01Z
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'Each title-bar tool button is lit in its pane type''s own colour while that pane is open in this window''s current tab, and a second click closes it'
source: 'owner, 2026-09-18: "the sessions / actions / switchboard / options buttons at the top right should be highlighted when they are open (using the header colors). click again to close those panes."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-actions-red-orange-and-lit-buttons/'], related: [SPBN, SRM2, V4NA], github: null}
---
# The title-bar buttons are lit while their pane is open, and close it on a second click

## Issue

the sessions / actions / switchboard / options buttons at the top right should be highlighted when
they are open (using the header colors). click again to close those panes.

## Change

- **Lit in the pane's own colour.** A button whose pane is open takes that type's header-band fill
  as its ground and the band's ink for its glyph (`relay::panestatus::openButtonStyle()`), so the
  button and the band read as one thing: Switchboard brass, Sessions blue, Options green, Actions
  red-orange (#SRM2).
- **With pane colours off** the lit state is a neutral raised ground and an outline, because which
  panes are open is information, not decoration.
- **Click again to close.** The button runs its pane's own action when the pane is shut, and closes
  that pane when it is open — through the pane's own close path, so anything it saves still runs.
  The tooltip says which the click will do.
- **Per window, per tab.** `syncChromeButtons()` runs after every open, close, split, move,
  restore, tab change and Settings mode swap, so a second window is not lit by the first window's
  panes, and a background tab's panes do not light this one's buttons.
- Actions and Options are one Settings pane in two modes, so at most one of those two is ever lit;
  swapping the mode moves the light from one button to the other.

## Evidence

`docs/qa_evidence/2026-09-18-actions-red-orange-and-lit-buttons/`, `drive.sh` scenes `buttons` and
`toggle`:

- `implementer-buttons-relay-{dark,light}-type-{none,actions,all}.png` and their `-bar` crops.
- `implementer-toggle-relay-dark-type-{1-open,2-closed,3-board}.png` — a click opens, the same click
  closes, the ground follows.
- `implementer-toggle-relay-dark-off-*.png` — the same with pane colours off.

## QA checklist

1. With nothing open, no tool button is lit.
2. Ctrl+Shift+S: the Switchboard button lights brass; Ctrl+Shift+Y lights Sessions blue; Ctrl+,
   lights Options green; Ctrl+Shift+A lights Actions red-orange.
3. Clicking a lit button closes that pane and the light goes out.
4. Clicking an unlit button opens its pane, as before.
5. Options › Appearance › Pane colours "off": a lit button is still obvious (raised ground and
   outline), and no tint appears.
6. A second window's buttons reflect only its own panes; switching tabs updates them.
7. Closing a pane with its own × , with Ctrl+Shift+W, or restoring it with Ctrl+Shift+Z, updates the
   buttons.
8. The tooltip reads "Close the Switchboard" while it is open and names the key.
