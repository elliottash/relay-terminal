# Website refresh — #W9ST

Baseline: `58aeb17f`, the last commit touching `site/` before this refresh. Reviewed the intervening commit history through `10328881` (876 commits including evidence and tracker changes). Checked the final implementation rather than retaining superseded model-picker designs.

## Source audit

- Agent consoles in Switchboard, card pages, Options/Actions and Sessions: `docs/ARCHITECTURE.md`, “Agents are consoles; contexts are what they are about”; #AGNT and #CTRN.
- Model classes, effort levels and ranking: #MDL1, `src/ModelCatalog.cpp`, `docs/AGENT-SESSIONS-PROTOCOL.md`; model swap shortcut `f8b21300`, takeover `9628872d`.
- Recaps: `7b1dc9ef`; empty-prompt continuation: `472ae1a2`.
- Guest subagents: `d40085fc`; guest background chores on Relay Free: `edb4a21b`.
- Approval policy: `backend/relay_core/approvals.py`, `d8319b42`, `74fb6feb`. These supersede older unconditional no-approval marketing copy.
- New boards use `.switchboard/` while existing `issues/` remains supported: `docs/ARCHITECTURE.md` section 10a.
- Phone code/PIN pairing: `7fc3f58d`, `3bbb1bb8`; phone Switchboard: `fd9d2caf`, hosted evidence `2c19e11c`.
- Actions slash commands: `7c86a599`; pane dimming: `c9b00e16`; folded diffs: `16749b94`; output rewrapping: `121fd9a1`.
- Screenshots retained and explicitly identified as earlier beta builds, rather than presenting them as captures of the new consoles.

## Browser verification

Run from the repository root with Playwright installed and local Google Chrome:

```sh
NODE_PATH=/home/elliott/.npm/_npx/e41f203b7505f1fb/node_modules node docs/qa_evidence/2026-09-21-website-refresh/check-site.cjs
```

Passed: both pages at 360, 720 and 1280 pixels in light and dark; no horizontal document overflow; no page errors or failed asset requests; local links and anchors resolve; all gallery images decode; one H1 per page; radio keyboard navigation and single tab stops; routing demonstration switches both ways with matching prompt text; theme persists across pages/reloads. Both pages readable without JavaScript, with theme controls hidden. `desktop.png` and `mobile.png` capture the homepage after gallery images loaded.

`node --check site/site.js` and `git diff --check -- site` passed. `relay-board.py check --json` reports existing unrelated board findings; no finding for #W9ST or its thread. The generated shared board index is left to Relay, preserving other sessions’ pending edits.

## Deployment

`./deploy.sh -n` listed only `index.html`, `free.html`, `style.css`, and new `site.js`, with no deletions. `./deploy.sh` completed successfully: apex and www returned HTTP 200. Subsequent curl fetches of all four files on both hosts matched local committed bytes exactly.

Implementation commit: `e24a7d066c3da9a8b7c2f8439e5ef427134c0d65`.

Live SHA-256 prefixes (identical on both hosts): index.html `bb8952dba0bc`, free.html `129026fabcd5`, site.js `8cf0e5c4c7dc`, style.css `08b8947e018e`.

The card’s `tests_check` returned no findings, actions or blocks before landing.
