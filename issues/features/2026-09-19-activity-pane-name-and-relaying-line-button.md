---
id: 4X53
type: work
status: ready
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
rank: zzzzzzzy
created: '2026-09-19'
acceptance: the pane that card #QT8C calls "agent internals" is called "Activity" in every user-facing place (keymap label, palette row, pane band and title, hint text, docs), and a violet relay-icon button at the left of the "Relaying – …" line above the prompt box opens it, with the key in its tooltip and a shortcut hint on first use
source: 'issues/feature_intake.txt, 2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [QT8C, UDC8, HQ2B, 4E13], github: null}
---
# The internals pane is called "Activity", and a relay-icon button on the Relaying line opens it

## Issue
add a circled purple relay icon next to relaying ...  it opens the agent internals pane -- or help me come up with a better name for that.

## Decisions (owner, 2026-09-19, planning conversation)

- **The name is "Activity".** Offered against "Workings", "Trace" and keeping "Agent internals";
  chosen because it is what ChatGPT calls the same side panel of thinking and tool steps (standing
  rule: copy the reference design), and it reads right in a tab, on the band and in a sentence.
  User-facing strings only: the keymap action stays `agent.internalsPane`, the pane type stays
  `internals`, the classes stay `AgentInternalsView` / `InternalsLedger`, the layout node stays
  `{"internals": …}` so saved sessions keep restoring.
- **Where the button lives: on the "Relaying – …" line** above the prompt box (`PaneBusyLine`,
  card #4E13 / #HQ2B / #UDC8), at its left, before the word. It is therefore on screen exactly while
  the pane is relaying, which is when the person wants to look inside. The pane header's blinking
  state glyph was offered as a second place, reachable when idle; the owner chose the line only.
- **What the icon is: the app icon**, the one relay mark used on every surface (no second icon), in
  the agent violet on a circle — the "circled purple relay icon" of the request. Painted from the
  same source the tab icon and the pane glyph use, not a second asset.
- **Depends on #QT8C landing** (the pane itself, in the QA lane, being landed by the `internals`
  session as this card is written). Nothing here starts before that commit is on `main`.

## Shape

- The button is a small `QToolButton`-like painted widget in `PaneBusyLine`'s row, in the state's
  ink (violet for an agent turn, blue while a program runs — the icon follows the line's colour,
  so it says whose work you would be looking into). Clicking it calls the window's
  `openInternalsPane` for that pane; if the pane is already open it is focused. Tooltip: "Open
  Activity (Alt+Shift+R)" with the live Keymap text.
- Shortcut hint (standing rule): the existing `internals.open` hint fires from this path too.
- Rename in place: `Keymap.h` action label, the palette row and its two descriptions in
  `RelayWindow.h`, the band label in `PaneStatus.cpp`, the view's title label and `paneTitle()` in
  `AgentInternalsView.cpp`, the "✦ thinking moved to the internals pane" anchor text, the settled
  inline anchor, the hint text, `docs/ARCHITECTURE.md`, `docs/KEYBINDING-PRESETS.md`, and the #QT8C
  card gets a line saying the pane was renamed here. Comments may keep "internals" where they name
  the identifiers.

## Tasks

- [ ] Rename every user-facing "Agent internals" / "agent internals" string to "Activity" <!-- t:n1 -->
- [ ] Relay-icon button on the busy line, in the line's ink, opening the pane; tooltip with the live key <!-- t:b2 -->
- [ ] Shortcut hint fires from the button path <!-- t:h3 -->
- [ ] Live under Xvfb: button during an agent turn and during a program, click opens the pane, second click focuses it <!-- t:v4 -->
- [ ] Docs and the #QT8C card updated; ctest green <!-- t:d5 -->
