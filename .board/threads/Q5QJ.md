<!-- relay:entry 20260924T130613Z-fk author=agent kind=event model=claude-fable-5-1 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/2a7ab073531c4d9a8a4117b919507eeb -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-24-two-relay-processes-with-one-identity-fight-over.md

<!-- relay:entry 20260924T130644Z-n2 author=agent kind=evidence model=claude-fable-5-1 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/2a7ab073531c4d9a8a4117b919507eeb -->
Diagnosis (Claude Fable, 2026-09-24).

**What the hosted rendezvous saw.** `journalctl -u relay-rendezvous` on elliott-main-1 shows desktop `df395b6e` going offline and back online ~36 times a minute (period ~1.6 s, the offline and online lines 3 ms apart) in these windows: 09-21 07:20–07:29, 08:17–08:27, 09:36–09:55, 19:10; 09-23 19:44–20:06, 22:31–22:34, 23:29–23:30; 09-24 08:15:50–08:15:52. The `events` table in `/var/lib/relay-rendezvous/rendezvous.sqlite3` has a matching `register` per cycle (36–38/min), because `Host.serve` re-registers before every retry.

**Cause.** Each window lines up to the second with a second Relay process on the desktop, started from the installed desktop entry: user journal `Started app-org.relayterminal.Relay-*.scope` at 09-23 19:43:57 (ran to 20:03:36), 20:03:38–20:06:09, 22:31:18–22:34:20, 23:29:43–23:30:41, 09-24 08:15:48–08:15:53, while `build/relay` (pid 3725290, then 3747251) was running the whole time. Both load the same identity from the keyring (`Identity.load` ignores the state dir when `directory is None`), so both register as the same `desktop_id`. `Hub.attach_desktop` makes the newest socket authoritative and closes the previous one with 4409; the displaced hub sleeps `reconnect_min` (1 s, jittered), re-registers and dials, displacing the other. `delay` resets to the minimum on every successful connect, so it never backs off.

**Effect on the phone.** Every replacement runs `detach_desktop` on the server (all client channels closed 4404 "the desktop went offline") and the displaced hub's `finally` closes every channel ("the link went down"). A phone cannot hold a session for more than ~1.5 s, and a pairing in flight is cut. In the 19:44–20:06 window that is 22 minutes of unusable remote.

**Fix, three layers, any one of which ends the fight:**
1. Rendezvous: an incumbent that is alive (answered a ping within the keepalive window) wins; refuse the newcomer with 4409 and a reason, replace only a dead incumbent.
2. Desktop: on 4409 treat it as "another Relay holds this desktop", back off to the maximum, and say so in `remote_state.reason`; never reset `delay` to the minimum if the previous link lasted under a few seconds.
3. Local: one hub per identity, a lock file beside `devices.json`; a second sidecar that cannot take it reports "another Relay on this machine is the remote hub" and does not register. A QA drive with a scratch `XDG_DATA_HOME` must not receive the owner's keyring identity (`try_run.py` already sets `RELAY_KEYRING=off`; make it the rule, not a convention).

Also: `DeviceStore.touch` never saves, so `last_seen` on disk is 0 for 10 of 13 records and the Sharing pane cannot show when a phone was last here.

<!-- relay:entry 20260924T130644Z-n3 author=agent kind=event model=claude-fable-5-1 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/2a7ab073531c4d9a8a4117b919507eeb -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260924T132201Z-m4 author=agent kind=evidence model=claude-opus-5-5 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/9f023a82c1004f1d99c0e10e85a4bad0 -->
Landed in 14d7f422. The hub now takes a per-identity `flock` at `/tmp/relay-<uid>-hub-<desktop_id>.lock` before it registers. A second hub waits, says "another Relay on this machine is already the remote hub", and takes over when the first one exits. A link closed with 4409 waits the longest interval (60 s), and only a link that stayed up 10 s resets the back-off. Tests: tests/test_remote_hub_lock.py passes 4 of 4. On HEAD's hub the flap test counted 31 takeovers in 1.5 s; it now counts at most 5. test_remote_host, test_remote_hosted_address and test_remote_gui_host pass 96 of 96. The rendezvous-side change (a live incumbent wins) was not made; the desktop fix is enough. Needs a Relay restart to take effect on the owner's desktop.

<!-- relay:entry 20260924T132205Z-hg author=agent kind=event model=claude-opus-5-5 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/9f023a82c1004f1d99c0e10e85a4bad0 -->
- ✦ agent moved this card · Discussing → Needs verification · Landed 14d7f422 with tests; needs a Relay restart and the owner's phone to confirm. · implemented_by anthropic/claude-opus-5-5 via claude-code
