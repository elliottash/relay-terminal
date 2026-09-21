<!-- relay:entry 20260921T011000Z-a1 author=claude-code kind=plan -->
### Claude Code · 2026-09-21 01:10
Filed from the owner's two messages after #PH0N landed: the friction audit of the phone path and
"even step 1 of enabling remote, that was not obvious to me". Plan in the card body; claimed and
executing. Three Opus subagents in parallel on disjoint files: the desktop entry point and code
dialog (C++), the sidecar's pairing code (Python, on #97EG's meeting-code machinery), the phone's
welcome screen and iOS/Android install path (web). Then the hosted drive and a deploy.

<!-- relay:entry 20260921T024000Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 02:40
Tasks 1–3 landed and deployed. Desktop (`7fc3f58d`, `546bda03`, evidence `07367a19`): "Pair a
phone…" first in the plug menu and in the palette; on an off desktop it writes the switch and the
hosted address, starts the service, and opens the dialog with the code beside the QR, a Copy
button, and the always-on line; the plug menu's "Disconnect all" became "Remote control: on/off"
(one act, one name). Sidecar (`3bbb1bb8`): `pair_code` mints a code bound to a pairing room of its
own; #97EG's conditions hold for both kinds; a pairing fragment on `/join` and an invite fragment
on the pairing path are both refused. Phone (`e5ee36e2`, `1ee6b653`): the welcome screen leads
with the code; iOS-not-installed shows the install card and `/pair` in Safari hands the link over
instead of pairing; Android Install button; first arrival leads with "Turn on notifications". The
typed code pairs end to end in `tests/test_web_meet_code.py` against the real hub. Next: the hosted
drive, then the owner's iPad and iPhone.
