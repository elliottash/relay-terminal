# The guest's web client for multiplayer (#W5N2)

What somebody sees when the owner hands them an invite link and they open it on a phone. Every
shot below is a real headless Chrome at 390×844 (2× scale), against a real rendezvous, a real
Noise session and the real hub — the owner's three answers (the knock, the keyboard, the prompt)
are driven from the test, which is where the desktop's Sharing pane plugs in.

Reproduce:

```
python3 docs/qa_evidence/2026-09-18-remote-multiplayer-guest/browser_guest.py
```

It prints the code the phone shows beside the one the hub derived — they must be equal — and ends
with `console problems: []`.

| Shot | What it shows |
|---|---|
| `guest-01-join.png` | The link opens here. Who they are joining is the desktop's key fingerprint, because nothing in a bare link has been authenticated by anybody yet. The name is required and remembered for next time. The secret is already out of the address bar (`location.hash` is empty by this point — the script prints it). |
| `guest-02-waiting.png` | The five digits, large, the sentence that says to compare them, and a countdown against the hub's own two minutes. Cancel closes the channel. |
| `guest-03-viewer.png` | Admitted as a viewer. The warm rule and "Ada's desktop / guest · viewer · ends in 24 h" say whose computer this is. One strip of context: the pane, the owner, this guest. The terminal is the painter the owner's own phone uses, scrollback and all. There is nothing to type into, and the line at the foot says why. |
| `guest-04-asked-to-type.png` | Promoted to editor mid-session, with no reload, and asking for the keyboard: "Asked to type…" while the owner decides. The hub drops an unanswered request after a minute and says nothing, so the waiting state ends by itself. |
| `guest-05-editor-driving.png` | Granted. The extra-keys row and the line box, and a prompt box that cannot be mistaken for a shell: "Ask their agent…", captioned "Goes to their agent — they approve it first." |
| `guest-06-prompt-pending.png` | The prompt is a request. Its row says *waiting for Ada's desktop to approve this* until the owner answers; three may wait at once and one lapses after ten minutes. |
| `guest-07-prompt-approved.png` | Approved, and it went to their agent — the guest sees the turn start on the terminal, because that is where Relay prints it. |
| `guest-08-owner-took-it-back.png` | The owner's own keystroke takes control back without asking. The change is immediate, and the half-typed `rm -r bui` is still on screen, greyed and unsendable, rather than gone. The sentence that explains it is held for eight seconds so the agent's routine "working…" cannot wipe it off first. |
| `guest-09-paused.png` | The owner paused guests. The screen keeps streaming; typing and prompts are off and say why. |
| `guest-10-ended.png` | Removed. The screen stops, the stored record is discarded, and the reason is stated rather than the heading repeated. |

## Checked here, and by `tests/test_remote_guest_browser.py`

- `/join` serves the app shell byte for byte as `/pair` does, under the same CSP, and is not a way
  out of the static root.
- The code on the phone equals the one the hub derived from the handshake hash. The client checks
  this itself and refuses a desktop that sends a different one.
- A viewer's session builds no extra-keys row and no key handler at all; the password field, the
  microphone, the notification row, the unpair button and the desktop's other panes are never
  drawn (asserted by computed visibility, never by the `hidden` property).
- Scrollback pages in for a viewer, with no gap and no repeat.
- An owner record and a guest record coexist in one browser, on one origin, with neither loader
  returning the other's row.
- Removal ends the session and clears the record; a reload then has nothing to reconnect with.

## Not covered here

A real phone and Safari. Everything above is Chrome's rendering of the phone viewport, not iOS.
