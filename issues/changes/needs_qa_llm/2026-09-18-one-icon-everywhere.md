---
id: D5MC
type: work
status: needs-qa-llm
labels: [change]
component: [gui, theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: 'The window header, the launcher, the task bar and the web client all show the same app icon'
source: 'owner, 2026-09-18: "it seems like there are 3 different icons being used", then "i want this icon everywhere" with the launcher tile attached, then "actually 2 is ok" (one icon, no per-theme variant)'
links: {plans: [], commits: [9ae273f], evidence: ['docs/qa_evidence/2026-09-18-one-icon-everywhere/'], related: [], github: null}
---
# One icon everywhere: the window header and the web client wear the app's mark

## Report

Three marks were on screen at once. The launcher, the task bar and the site had the patch cord in
its tile (7d70e35). The window header drew the mark *without* its tile, from
`data/theme/icons/relay-mark.svg`, on the grounds that the tile vanishes into the chrome at 22px.
The web client on a phone or tablet still had the old three blue bars in `app/icon.svg`, which
nothing had updated when the icon was redrawn.

## Change

- The window header draws `QApplication::windowIcon()` — the installed app icon, the same image
  the launcher shows. On a light theme the tile is what makes it read as the app's mark rather
  than a stray chevron.
- `app/icon.svg` is the app icon, scaled. It is a copy on purpose: `app/` ships by itself to a
  browser and cannot reach the desktop data directory, so the file carries a comment saying to
  keep it in step by hand.

Left alone deliberately: `org.relayterminal.Relay-small.svg`, which `render-icons.sh` uses at 16
and 22px — the same drawing with a fatter cord and a larger plug so that it survives being drawn
that small, not a different icon. A beige cut of the icon exists as a rendering only, in
`docs/qa_evidence/2026-09-18-icon-ideas/` (candidate G plus the IBM Beige variable set in
`sheet.html`); the owner chose one icon everywhere over a per-theme variant.

## QA checklist

1. **In the app.** The top-left mark in the window header is the tiled icon, in both Dark Copper
   and IBM Beige, and is not clipped or blurred at its drawn size.
2. **Outside it.** The launcher entry and the task bar show the same icon (`gtk-launch
   org.relayterminal.Relay` if the desktop entry is installed).
3. **The web client.** Share a pane to a phone or tablet: the browser tab's favicon and the
   "add to home screen" icon are the patch cord, not blue bars.
4. **Small sizes.** The 16 and 22px renders are still the legible fatter cut.

## Known gaps

- `data/theme/icons/relay-mark.svg` is now unused by the app. A theme that ships its own mark will
  no longer be picked up; nothing in the tree does today.
- The website still uses a bare mark beside the wordmark in its header and footer. That is a logo
  lockup rather than an app icon and was left as it was.
