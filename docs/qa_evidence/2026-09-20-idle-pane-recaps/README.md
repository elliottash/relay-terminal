# #D54R — away recaps written into idle panes a few minutes after work ends

Implementer evidence for `issues/features/2026-09-20-away-recaps-written-into-idle-panes-a-few-minute.md`
(status → needs-verification).

## What the owner asked

Relay's away recap used to wait for the user to *come back* to the window before it asked for
one. Claude Code (v2.1.108+, confirmed in the installed 2.1.278 binary) writes its "session
recap" into the unfocused session about three minutes after a turn ends — a timer anchored at the
turn's end (180 s, configurable ≥ 30 s), cancelled by focus returning, the recap appended to the
transcript as an `away_summary` system message so it is simply there when the user comes back.
Idle *panes* — background tabs, splits the user isn't working in — never got anything in Relay.

## The change

All in `src/Pane.h`, reusing the existing `recap_request {reason: "away"}` path end to end:

- `awayRecapThresholdMs()` — the wait (default 180 s, `RELAY_RECAP_AWAY_SECONDS` env override),
  shared by the old on-return path and the new timer.
- `awayRecapAllowed()` — the shared guards: `recap/away` setting, agent configured, nothing
  running or queued, nothing being drafted, ≥ 3 turns and turns the last recap didn't cover.
  Claude Code's equivalents (cache-warmth, draft input, "two user messages between recaps") map
  onto these.
- `updateIdleRecap()` — arms, re-anchors or stops a single-shot `QTimer`. The anchor is the
  turn's end (`m_idleSince`), like Claude Code's: looking away late still fires on the same
  schedule. `watched()` is the focus test (active window, shown tab, active leaf), so a hidden
  tab or an unfocused split counts as away; watched again, or a new turn starting, stops it.
- Hooks: `agent_finished` (starts the anchor), `agent_started` (stops), window activation both
  ways (`noteWindowActivation`, which keeps its old on-return behavior — the in-flight guard and
  the turns-covered guard keep the two paths from asking twice), `showEvent`/`hideEvent` (tab
  switches) and a new `event()` override for `QEvent::DynamicPropertyChange` (the `relayActive`
  flip when focus moves between split panes).
- `m_recapInFlight` — set when either path asks, cleared by the worker's `recap` event (both the
  printed and the skipped/failed paths).
- `src/RelayWindow.h` — the Options row's subtitle now describes writing into unwatched panes.
- `docs/AGENT-SESSIONS-PROTOCOL.md` §5 — the recap bullet names the idle-pane trigger.

## Machine evidence

- `scripts/relay-build` — 43 s, green (`2026-09-20.07H.01`).
- `ctest --test-dir build -R '^transcriptgaps$'` — Passed (recap block layout unchanged).

## Live under Xvfb (`drive.sh <build-dir>`, OCR-checked)

Loopback stub provider (`recap-stub-provider.py`), isolated `HOME`/`XDG_*`, no network, no key,
`RELAY_RECAP_AWAY_SECONDS=2`. `run-final.log` holds the full OCR. All 8 checks PASS
(`implementer-*.png`):

- **02** three stub turns completed in the pane.
- **03** idle but *watched* past the threshold: no recap — nothing over the shoulder of someone
  reading the pane (`implementer-03-watched-idle.png`).
- **04** Ctrl+T hides the pane's tab (the window stays active and focused the whole time): the
  recap is written into the hidden pane with no refocusing; switching back shows
  (`implementer-04-tab-away-recap.png`):

  ```
  [end of message]
  finished at 08:36
  Recap · 08:36 → 08:36 · <1m
  Three stub turns ran while the pane sat idle and unwatched. Nothing is unverified
  ```

  (OCR renders `·`/`→` as `+`/`-`; the screenshot shows the real glyphs.)
- **05** back on the watched pane, past the threshold again: still exactly one recap — no
  duplicate without new work (`implementer-05-no-duplicate.png`).
- **06** Ctrl+E split: a fourth turn runs in the left pane, the click moves the focus to the
  right pane (the `relayActive` flip, nothing hides), and the left pane gets its second recap
  while both stay on screen (`implementer-06-split-left.png` crop).

## Findings while driving it

- The provider dialog's Save is followed by the **approvals chooser**, which holds the first
  prompt until answered; the drive clicks "Allow everything — recommended" and only then counts
  turns, after an early run saw the held prompt lost and the 3-turn guard quietly refuse the
  recap (an away recap skipped by the worker prints nothing by design — it is invisible in the
  pane, which made the debug log essential).
- Under bare Xvfb (no window manager) `xdotool windowactivate` errors; `windowfocus` and clicks
  still give Qt focus, which is all the pane paths need.
