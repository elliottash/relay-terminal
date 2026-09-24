---
id: JQ7R
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, remote]
milestone: beta
workstream: remote
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session relay-terminal-71) with Claude Opus 5 subagents, 2026-09-18
rank: '07'
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist with two isolated Relays under Xvfb (or the owner's desktop and sphinxpad) and records it under `docs/qa_evidence/2026-09-18-join-a-shared-session-from-relay/`
source: 'owner, 2026-09-18: "add another icon at the top right to connect to a remote session, and add a /connect command. i think it would be good if you could type /connect FGHJ or /join FGHJ, then it would ask you to type in the pin to join. and there is an action for doing it as well, accessible from the top right icon"'
links: {plans: [], commits: [709d7e6, 44bf6e1], evidence: ['docs/qa_evidence/2026-09-18-join-with-code-in-relay/'], related: [97EG, W5N2, T4BS], github: null}
---
# Join someone's shared session from Relay itself: /join CODE, then the PIN

## Issue

A friend's meeting code and PIN (#97EG) could only be used from a browser. The owner wants to join
from Relay: `/join CODE` or `/connect CODE`, a PIN prompt, a palette action, and an icon at the top
right.

## What landed

- **Entry points** (709d7e6): `/join [code]` and `/connect [code]` (`Pane::slashCommands`,
  `onJoinShared`); palette action `remote.join` "Join a shared session…" (`src/Keymap.h`); a plug
  (`ChromeButton::Glyph::Connect`, `src/WindowChrome.h`) left of the bell whose menu offers "Join
  with a code…" and "Open a pane your other desktop shares…". Clicking the plug hints "Next time:
  type /join and the code in any prompt box" (`remote.join.button`). The PIN is only ever typed in
  the dialog, never on the command line.
- **Viewer** (`remote/viewer.py --guest`): code and PIN through CPace (`Client.join_with_code`), the
  knock with its five-digit check code, admission, then the device mode's session and resume loop as
  a guest; a 0600 guest record to rejoin until the invite expires; `ended` when the host removes
  them. `Client.knock_admitted` and `Client.rejoin_reply` are additive in `remote/client.py`.
- **Qt** (`src/RemotePane.{h,cpp}`): `RemoteViewer::guest()` is a second viewer process, so a guest
  session never disturbs the owner's own device session; `JoinDialog` (code, PIN, name, server behind
  "Server…"); guest mode on `RemotePane` (no model, conversation or queue controls; "Ask to type"
  for editors; prompts wait for the host); `GuestSession` opens panes entering the guest's scope
  (a host's whole-tab share, #T4BS) and ends those leaving it. `RelayWindow::joinSharedSession`
  puts the first pane in a new tab and the rest beside it; each pane's band names it.
- **Fixed from the live drive** (44bf6e1): Enter after the PIN pressed Cancel as well as Join; a
  guest pane asked for `pane_state` and showed the refusal; every band read "Shared pane".

## Implementer evidence (not a QA verdict)

- `tests/test_remote_viewer.py` `GuestTests` (real rendezvous and host: join order, wrong PIN, no
  secrets in output, removal, rejoin after restart, expiry, stdio); `relay-remotepane-tests`
  (dialog pages, Enter after the PIN, scope following, guest pane controls, ended pane read-only).
- `docs/qa_evidence/2026-09-18-join-with-code-in-relay/live.sh`: a Python host shares two panes as
  a tab behind a code; Relay under Xvfb types `/join CODE`, the PIN and Enter, is admitted, and the
  pane the host adds afterwards opens beside the others (screenshots `implementer-live-01…04`).
- **Not yet driven: Relay to Relay with a real desktop sharing**, and a real person on the other
  side. The host in the drive is Python, not A's share dialog; items 1–4 below close that.

## QA checklist

Two isolated Relays, A sharing and B joining (or the owner's desktop and sphinxpad on the LAN).

1. On A, share a pane and click **Make a code**; note the four letters and the PIN. On B type
   `/join` plus the code in the prompt box and press Enter: the dialog opens with the code filled
   in and the cursor in the PIN field. Type the PIN, press **Enter** (not the button): the dialog
   shows the five-digit check code; A shows the same digits in its knock. Admit on A.
2. B opens A's pane in a new tab, band "<pane title> · <A's name>". Output on A appears on B;
   scrolling back on B pages history.
3. As an editor on B: **Ask to type**, grant on A, type `echo JOINED` on B — it runs on A. Typing on
   A takes the keyboard back and B says so. A prompt from B's composer waits for approval on A.
4. Admitted as a viewer instead: B has no composer and no "Ask to type".
5. Wrong PIN: the dialog stays, says "That PIN is not the one on their screen.", clears the PIN.
   Three wrong PINs burn the code: the next attempt says it was used up.
6. `/connect CODE` behaves exactly like `/join CODE`; the palette's "Join a shared session…" and the
   plug's "Join with a code…" open the same dialog empty; the plug click shows the `/join` hint (at
   most three times).
7. While B is joined, B's own "Open a shared pane…" to the owner's other desktop still works and
   neither session drops the other.
8. Quit B and start it again, then open the join dialog with no code: it rejoins the stored share if
   the invite has not expired.
9. On A, remove B from the Sharing pane: B's panes say access ended and become read-only; B does not
   reconnect.
10. A code made on A while A shares through relay-terminal.ai is joinable from B with the default
    server; one made on the LAN address needs B's **Server…** set to A's address — confirm the
    error for the wrong server is readable ("No live share has that code…").

## Notes for the QA session

- `RELAY_KEYRING=off` for every run; jail each Relay's `HOME`, XDG dirs, `TMPDIR` and a 0700
  `XDG_RUNTIME_DIR`; a free X display. `docs/qa_evidence/2026-09-18-join-with-code-in-relay/live.sh`
  is a working starting point (its host is `host.py`, a real hub).
- The owner's phone and iPad pairings were broken by an overwritten identity key on 2026-09-18 and
  need pairing again before any test with those devices.
