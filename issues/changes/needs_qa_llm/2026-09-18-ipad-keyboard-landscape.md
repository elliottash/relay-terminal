---
id: KBFT
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
milestone: beta
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-18
rank: hf
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist on a real iPad (or iPhone) and records it under `docs/qa_evidence/`
source: 'owner, 2026-09-18, feature intake: "in browser app on ipad, it doesnt adapt well to the keyboard. in portrait mode without on screen keyboard it looks good, but once you haev the on screen keyboard, it looks good in portrait but not landscape, it goes off screen." Then, relayed by relay-terminal-71: "same on portrait actually, test it with the on screen keyboard"'
links: {plans: [], commits: [d14a4b4], evidence: ['docs/qa_evidence/2026-09-18-ipad-keyboard-landscape/'], related: [W5N2], github: null}
---
# The browser app fits the screen above an iPad's keyboard, in either orientation

## Issue

> in browser app on ipad, it doesnt adapt well to the keyboard. in portrait mode without on screen
> keyboard it looks good, but once you haev the on screen keyboard,
>
>  it looks good in portrait but not landscape, it goes off screen.

> same on portrait actually, test it with the on screen keyboard

## What was wrong

Two things, one inside the other.

- **The pane view never had a height of its own.** `#pane-view` was a plain block and the terminal
  slot (`.rp-terminal`) a block too, so the pane took its content's height: the terminal's whole
  24-row grid, then the reasoning, the queue and the prompt box under it. Measured in the real
  client paired to a hub, on an iPad-sized window, the prompt box ended at **1,074 px** — below
  the screen in landscape even *without* a keyboard (834 px), and far below it with one (about
  330 px left). Portrait with no keyboard is 1,180 px tall, which is why it looked fine there;
  with the keyboard up about 740 px are left, and it went off in portrait too.
- **Safari does not shrink the page for its keyboard.** It lays the keyboard over a layout
  viewport that stays full height and scrolls the page to the focused box, which pushed the bar
  and the top of the pane off the screen. `100%`, `vh` and `innerHeight` all keep meaning the whole
  screen; only `window.visualViewport` knows the visible part.

## Behaviour as implemented

- `app/viewport.js` writes the visual viewport's height and offset to `--app-height` / `--app-top`
  on `<html>`, and `style.css` pins the body to them, so the whole client is exactly the strip
  above the keyboard and Safari has nothing to scroll. A pinch-zoom is not taken for a keyboard.
  `interactive-widget=resizes-content` in the viewport meta has Chrome on Android do the same.
- `#pane-view` and the terminal slot are flex columns now, so the terminal is the part that gives
  and grows: at 834 px everything fits with the terminal at ~17 rows, and at 330 px it holds two.
- Under 520 px visible (an iPad in landscape with its keyboard up) the chrome gives way first, in
  the desktop's own order: the desktop-name bar steps aside while the link is up (it stays when it
  says "offline", which is said nowhere else), the thread bar keeps one line, the reasoning panel
  steps aside (the turn's clock and step stay on the prompt box's strip), the queue keeps one row
  and scrolls. The prompt box and its strip — the send button is on it — never give way.
- Under 260 px (a phone in landscape with its keyboard up, about 185 px) the thread bar and the
  queue step aside as well and the terminal keeps one line: the terminal and the prompt box are
  what is left. relay-terminal-71's end-to-end matrix found this case after the first round.
- The prompt box's own cap (30% of the screen) is taken from the visible height, not `innerHeight`.

## Checks

- [ ] On an iPad, the browser app with a live pane open, **landscape, keyboard down**: the bar,
      the thread bar, the terminal, the reasoning, the queue and the prompt box are all on screen.
- [ ] Same, **keyboard up** (tap the prompt box): the thread bar is at the top, the prompt box and
      its strip (send, model, mic) are whole just above the keyboard, and the terminal shows at
      least two lines. Nothing scrolls off when you type.
- [ ] **Portrait, keyboard up**: the same — the prompt box and its strip whole above the keyboard,
      the bar and the thread bar on screen.
- [ ] Rotate with the keyboard up and back: nothing is left off screen either way, and the
      reasoning panel and the bar come back when the keyboard goes.
- [ ] An iPhone in portrait with the keyboard up: the prompt box and send whole above it.
- [ ] An iPhone in landscape with the keyboard up: the terminal and the prompt box with send, and
      the thread bar and queue back as soon as the keyboard goes.
- [ ] Turn off Wi-Fi with the keyboard up: the bar comes back, saying offline.
- [ ] Pinch-zoom in the terminal: the layout does not jump.
- [ ] `tests/test_web_viewport.py` passes (it fails on the code before this change, with the
      prompt box at 954–1,074 px).
