---
id: SPBN
type: work
status: needs-qa-llm
labels: [feature, design]
component: [gui, theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code subagent), 2026-09-18
rank: zzzzz
created: '2026-09-18'
acceptance: every special pane has a header band that says what it is; tints by type and by group, switchable live; terminals plain; a terminal in an ssh, mosh or telnet session is unmistakable in its header and on its tab, in every theme
source: issues/feature_intake.txt, 2026-09-18
links: {plans: [], commits: [ac8582e, f1ff47a], evidence: [docs/qa_evidence/2026-09-18-pane-types-and-status/], related: [XM0T, 8E4Q, 0JA7], github: null}
---
# Distinct headers or colors for each pane type

## Issue
need different headers, distinctive colors or some other style distinction for different pane types: especially subagents, switchboard, options, etc.

Owner, added the same day: "another important aspect to consider: visual language is needed to
communicate a pane is in an ssh / remote session."

## Decisions
- **Owner, 2026-09-18:** a low-strength tinted header on the special pane types; plain terminal
  panes stay plain; the focused pane keeps its own highlight so the two never fight.
- **Owner, 2026-09-18, to try live:** a grouped variant — "settings, actions, sessions, and
  switchboard all get the same second color, but with different icons / headers, then a third color
  for subagent panes". Built as a setting, `appearance/pane_colours` = by type (default) | by group
  | off, so the two can be compared by flipping it. The row itself is in the Options pane (session ad).
- **Keyed on a property, not on classes.** A pane says what it is with `paneType` on the leaf; a new
  pane type is styled by setting it. Contract in `docs/ARCHITECTURE.md`, "Pane types, pane states
  and remote sessions".
- **By type:** Switchboard brass (the switchboard aesthetic's metal), Options and Actions green —
  one tint, because they were one Settings pane until today, told apart by glyph and name — Sessions
  the shell blue, subagent and agent-turn panes violet. A fifth hue would have had to come from
  outside each theme's own colours. **By group:** every tool pane brass, every agent pane violet.
- **Off keeps the band, in neutral ink.** The band is the pane's name (the Options pane no longer
  draws its own title), so "off" takes the colour away, not the header.
- **Plain:** terminals, the explorer, previews, plans and the diff pane (`paneType` `diff`,
  coordinator's call) — their own first row is already their header.
- **Options wears the gear** (coordinator, 2026-09-18): the same glyph as the title-bar button that
  opens it, so a tool pane's button and its band match.
- **Remote sessions (owner's addition):** a safety signal, so it ignores the colour setting. A
  hatched band in the error hue behind the terminal's title row, a firm red line under it, a
  `⇄ user@host` chip, and a red mark on the tab (the ⇄ itself when nothing more urgent is going on).
  Hatching is used by nothing else, so it reads as "not here" without colour. It follows the
  terminal's foreground process group live, so it is true exactly while ssh/mosh/telnet runs and
  clears when it exits or is suspended.
- **Remote share is a different thing and looks different:** a pane shared with a phone gets a
  `phone` chip in the shell blue in its title row (the share chip under the prompt box already said
  so, but only down there).

## What landed
- `ac8582e` — `src/PaneStatus.{h,cpp}` (library `relay-panestatus`) and `tests/panestatus_test.cpp`:
  the tints, the groups, the ssh/mosh/telnet destination parser, and a check that every tint keeps
  4.5:1 text and 3:1 glyphs in every shipped theme.
- `f1ff47a` — `PaneTypeBand` and `relay::chrome` in `src/PaneChrome.h` (the band, every glyph,
  `PaneChrome::refreshAll()`), the remote band and chips on terminal headers, the tab icon's remote
  mark, `Pane::remoteCommandLine()` / `sharedWithPhone()`, and the ARCHITECTURE section.
- The subagent pane's tab bar gets its tab text colours back (`src/Theme.cpp`: the app's
  `QTabBar::tab` colour rule no longer overrides `setTabTextColor`), so each subagent tab's
  state shows in colour as well as in its ○ ● ✓ ✗ ■ glyph.

Evidence: [`docs/qa_evidence/2026-09-18-pane-types-and-status/`](../../../docs/qa_evidence/2026-09-18-pane-types-and-status/)
(`drive.sh` reproduces every screenshot; `ssh` there is `fake-ssh.c`, which keeps ssh's command
line and process group but runs a local shell, because there is no sshd to log in to under Xvfb).

## For QA
- [ ] Open the Switchboard (Ctrl+Shift+S), Options (Ctrl+Shift+O), Actions and a subagent pane beside a terminal: each special pane has a tinted band with its glyph and name; the terminal has none
- [ ] The pane buttons sit on the band, and the pane's first row (Switchboard tools, Options search, subagent tabs) uses the full width under it
- [ ] Options › Appearance › Pane colours: By group makes Switchboard, Options, Actions and Sessions one colour and subagents another; Off leaves neutral bands; the change is immediate
- [ ] Switch theme (Relay Light, IBM Beige, Solarized, Gruvbox, Dark Copper): every band stays low-strength and its text readable
- [ ] Focus moves between panes: the focused pane's outline is the same as before and is never tinted
- [ ] Drag a special pane by its band: it moves like any pane
- [ ] `ssh somehost` in a terminal: the title row turns hatched red with `⇄ somehost`, the tab shows ⇄; `exit` clears both; Ctrl+Z on ssh clears them too; with Pane colours Off it still shows
- [ ] `ssh -p 2222 -l me box`: the chip says `me@box`
- [ ] ssh inside a local tmux shows nothing (Relay sees tmux, not ssh) — expected, recorded
- [ ] Share a pane with the phone: a `phone` chip appears in its title row, distinct from the ssh one
- [ ] A subagent pane with running, done and failed subagents: each tab's text is in its state's colour
