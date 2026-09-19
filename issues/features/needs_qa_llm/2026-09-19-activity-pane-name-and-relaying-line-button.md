---
id: 4X53
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 subagent of a Claude Fable 5.1 session, 2026-09-19
rank: zzzzzzzy
created: '2026-09-19'
acceptance: the pane that card #QT8C calls "agent internals" is called "Activity" in every user-facing place (keymap label, palette row, pane band and title, hint text, docs), and a violet relay-icon button at the left of the "Relaying – …" line above the prompt box opens it, with the key in its tooltip and a shortcut hint on first use
source: 'issues/feature_intake.txt, 2026-09-19'
links: {plans: [], commits: [c132851, 4eb3bee, 84deb03], evidence: [docs/qa_evidence/2026-09-19-activity-pane-button/], related: [QT8C, RR0G, 0STR, HQ2B, 4E13], github: null}
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
  card #4E13 / #HQ2B / #RR0G), at its left, before the word. It is therefore on screen exactly while
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

- [x] Rename every user-facing "Agent internals" / "agent internals" string to "Activity" <!-- t:n1 -->
- [x] Relay-icon button on the busy line, in the line's ink, opening the pane; tooltip with the live key <!-- t:b2 -->
- [x] Shortcut hint fires from the button path <!-- t:h3 -->
- [x] Live under Xvfb: button during an agent turn and during a program, click opens the pane, second click focuses it <!-- t:v4 -->
- [x] Docs and the #QT8C card updated; ctest green <!-- t:d5 -->

## As built

**The name.** Every user-facing string is "Activity": the keymap label (`src/Keymap.h`), the
Actions palette row and its "Bring this pane's Activity pane forward" description
(`src/RelayWindow.h`), the pane-type band (`src/PaneStatus.cpp`), the view's "✦ Activity" title and
`paneTitle()`, which is what the tab reads (`src/AgentInternalsView.cpp`), the anchor a block
settles to when the pane opens mid-thought ("✦ thinking moved to the Activity pane"), the
"… N earlier turns went to the Activity pane" row, the `internals.open` hint text, and
`docs/ARCHITECTURE.md` (the shortcut table and the section). `docs/KEYBINDING-PRESETS.md` never
named the pane, so nothing there changed. Not renamed, on the owner's decision: `agent.internalsPane`,
the pane type `internals`, `AgentInternalsView` / `InternalsLedger` / `relay::internals::Ledger`,
the `{"internals": …}` layout node and the `int-` request ids — a saved session and a keymap from
before the rename restore unchanged. Comments that call the pane by its name now say Activity; the
ones that name an identifier keep its spelling.

**The button.** `PaneBusyLine` paints it itself (`src/Pane.h`) rather than parenting a
`QToolButton`, so the row stays one widget to lay out, to elide and to hide: the mark is drawn in
`paintEvent`, `mousePressEvent` / `mouseReleaseEvent` give it press-and-cancel behaviour, the
pointer changes over it, and `QEvent::ToolTip` answers with "Open Activity  (Alt+Shift+R)", the key
read live from `Keymap::instance().shortcutText("agent.internalsPane")` so a rebound key is what it
says. It is painted only when the line has text, which is exactly when the row is shown, and only
when `onOpenActivity` is set, so a `PaneBusyLine` with nowhere to go shows no way to go there.

**The mark is the app's own.** `paintRelayMark` moved from `src/PaneChrome.h` to a header of its
own, `src/RelayMark.h`, because `Pane.h` is included before `PaneChrome.h` in the one translation
unit and both need it; the drawing is unchanged and the pane header's live glyph and the tab icons
still call it. `paintCircledRelayMark` beside it puts that mark on the disc the owner asked for:
the ring and the mark in the caller's one ink, the disc that ink laid thinly over the pane's
ground, and the blend passed back in as the ground the mark's cut hole is filled with. Hover and
press change the weight, never the colour.

**The ink is the line's ink**, from the same `panestatus::stateText(m_state, …)` call that paints
the word: violet for an agent turn, the terminal's blue while a program runs, amber when the turn
is blocked on your answer.

**The layout.** The prompt box's text inset is 8 px with the shipped theme — narrower than the
mark — so the icon could not sit in the gutter left of the word without overhanging the text's left
edge. The row keeps its left edge on the prompt text (#HQ2B): the button takes that edge and the
word moves right by the button's width and a 5 px gap. `sizeHint()` and the middle elision both
count that offset, so a narrow pane still clips the row rather than growing for it.

**The click** calls `Pane::openInternalsPane(QString(), /*fromMouse=*/true)` — the same call the
keymap action and the palette make through `RelayWindow::openInternalsPane`, which brings an open
pane forward instead of making a second one, and `fromMouse` is what fires the `internals.open`
hint. The pane header's state glyph was left alone: the owner chose the line only.

The commits: `c132851` (the docs and the #QT8C card's rename line), `4eb3bee` (the palette row),
`84deb03` (the mark's own header), and the commit that carries this card (the button, `src/Pane.h`,
and the architecture paragraph). The rename's code strings — the keymap label, the band, the view's
title, the two test headers — were written in this shared checkout a few minutes before the AGPL
relicence committed the whole working tree, so they reached `main` inside `d0e1628`.

## QA checklist

- [ ] Start a turn: the "Relaying – …" line carries a circled relay mark at its left, in the same
      violet as the word, about the height of the text.
- [ ] Run a program (`sleep 20`): the same mark, in the terminal's blue.
- [ ] Hover it: the pointer becomes a hand and the tip says "Open Activity  (Alt+Shift+R)"; rebind
      `agent.internalsPane` in keybindings.json and the tip says the new key.
- [ ] Click it: the Activity pane opens beside the terminal (under it in a narrow pane), and the
      band, the title and the tab all say Activity.
- [ ] Click it again during a later turn: the same pane comes forward — no second pane, no second
      tab entry.
- [ ] The first mouse open teaches the key ("Next time: Alt+Shift+R · opens the Activity pane"),
      subject to the hints' own 20 s global gap and 3-showings limit.
- [ ] Between turns, with the line hidden, there is no stray mark above the prompt box.
- [ ] Narrow the pane until the line elides: the mark stays, the text elides from its middle, and
      the composer does not grow.
- [ ] The palette's row reads "Activity"; Alt+Shift+R still opens it; a session saved with the pane
      open restores it.
- [ ] The pane header's state glyph is still not clickable.
