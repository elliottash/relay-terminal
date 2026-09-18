# Multiplayer on the desktop, live — evidence, 2026-09-18

Feature `#W5N2`, `docs/REMOTE-PROTOCOL.md` section 10. The owner's side: inviting another person to
one pane, and the Sharing pane where the answering happens. Driven under Xvfb against a real guest
— `remote/client.py`, the protocol's own Python client, so the run does not depend on the web
client.

| File | What it shows |
|---|---|
| `implementer-01-pane-before-sharing.png` | An engine pane with output on screen |
| `implementer-02-share-window-pairing-first.png` | The share window. Pairing your own phone is still the first thing offered; "Invite someone to this pane" is underneath it |
| `implementer-03-invite-role-editor.png` | The invite row: role, expiry, uses, and the sentence under it changing when the role does |
| `implementer-04-invite-link-and-qr.png` | The link as a read-only field (a link is one unbreakable word; a label would cut the secret off), its QR, and Copy link |
| `implementer-05-typing-before-the-knock.png` | The keyboard is in the prompt box before anybody knocks |
| `implementer-06-knock-opened-the-sharing-pane.png` | The knock. The Sharing pane opened **by itself**, the five-digit code is shown large, the countdown is running, and **Refuse is first** with "Admit as editor" present only because the invite was an editor one |
| `implementer-07-focus-was-not-stolen.png` | Still typing into the prompt box while the knock row is up: the pane arrived without taking the keyboard, which is the whole point — the next keystroke would otherwise land on Admit |
| `implementer-08-admitted-as-editor.png` | alice on the people list with her platform, key fingerprint and when her access ends; the pane header chip now reads "1 guest" |
| `implementer-09-asked-for-the-keyboard.png` | "alice asks to type in this pane": Refuse, Let them type, 60-second countdown |
| `implementer-10-alice-is-typing.png` | Control handed over: the pane header says **alice is typing** in the remote-session hue, with the hatched band across the title row |
| `implementer-11-a-guest-prompt-waiting.png` | A guest prompt, the whole text wrapped and not elided, with Refuse and Approve once — and no "Approve always", by design (section 10.5) |
| `implementer-12-prompt-approved.png` | After approving: the turn is running in the pane, and its queue row names its author — `guest:f092f2a3...` under the prompt text (section 10.4) |
| `implementer-13-typing-took-control-back.png` | The owner types in the pane alice was driving: the chip goes back to "1 guest", "typing in this pane right now" and "Take the keyboard back" are gone, the guest is told `control owner`, and every keystroke reached the prompt box — taking control back never costs the key that did it |
| `guest.log` | What the guest saw, including its own copy of the five-digit code |
| `relay-stderr.log` | The desktop's stderr, sidecar included |

The code in `implementer-06` and the one in `guest.log` are derived independently at each end from
the Noise handshake, so they agree only if it is the same session.

Reproduce:

```sh
cmake --build build
docs/qa_evidence/2026-09-18-remote-multiplayer-desktop/drive.sh
```

`RELAY_REMOTE_INVITE_FILE` is a QA hook beside the existing `RELAY_REMOTE_PAIR_FILE`: an invite
link holds an unguessable secret in its fragment, so Relay writes it out only when that variable
names a file, and never to a log.

The run isolates `XDG_RUNTIME_DIR` and `TMPDIR` as well as the three XDG directories. Without those
two a Relay already running on this machine shares sockets and temp files with the run.
