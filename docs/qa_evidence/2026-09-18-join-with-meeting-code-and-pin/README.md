# #97EG — implementer evidence: join with a meeting code and a PIN, live

**Live run of the whole acceptance path, 2026-09-20.** A real Relay desktop (HEAD `ee12ac1a`,
built clean in a worktree) in a jail of its own under Xvfb made a meeting code and PIN from its
share window ("Make a code"); a real Chrome on the same display opened the join page the
desktop's own sidecar serves, typed the code and PIN, ran CPace in the page, knocked; the owner
admitted from the Sharing pane. The guest ended up watching the pane.

```
docs/qa_evidence/2026-09-18-join-with-meeting-code-and-pin/live.sh [build dir]
```

`live.sh` drives the desktop with xdotool + OCR (tesseract; buttons are found by their words and
clicked by coordinates — there is no window manager on the Xvfb display) and the guest with
`drive_guest.mjs` over the DevTools protocol. It exits non-zero unless every check below passes.

## What the run showed (code `UNXM`, PIN on the screenshots; full console log in `live-output.txt`)

- The share window made the code and showed it large with the PIN and a 10-minute countdown
  (`implementer-live-01-desktop-code-and-pin.png` — "Meeting code PIN Expires in 9:58 — PRUV").
- Chrome reached the meeting-code form from the join page's quiet link and submitted it
  (`implementer-live-02-guest-code-form.png`).
- CPace ran between the page and the desktop; the sealed invite opened into the ordinary
  invitation screen; the guest knocked. Their waiting screen showed **31479**
  (`implementer-live-03-guest-waiting-to-be-let-in.png`) — the **same five digits the desktop's
  knock row showed**, read off both screens and compared by the script ("the five-digit codes
  match on both screens: 31479").
- "Admit as viewer" on the Sharing pane let the guest in: their screen went to the pane view,
  "guest · viewer · connected … Sam (live) (you) viewer · Watching. Only the owner and their
  editors can type." (`implementer-live-04-guest-joined.png`), and the desktop's Sharing pane
  showed "Here now — Sam (live) — viewer" (`implementer-live-05-desktop-guest-here-now.png`).
- The code closed behind them: the share window struck it through as **Used**
  (`implementer-live-06-desktop-code-used.png`), and the audit log says `code_used` then
  `knock` then `admitted` with `uses_left: 0` — the first knock claimed the invite.
- The PIN left nothing behind: not in the sidecar's stores (its rendezvous db included), not in
  the config, not in Relay's log — only in the `RELAY_REMOTE_CODE_FILE` QA hook that exists for
  this purpose.

Audit lines from the run (the jail's `…/relay/remote/`):

```
{"kind":"code_create","code":"UNXM","invite":"a0a56d72f20db470","role":"viewer",…}
{"kind":"code_used","code":"UNXM","invite":"a0a56d72f20db470","peer":"127.0.0.1","failures":0}
{"kind":"knock","participant":"dd41d676de86375c","invite":"a0a56d72f20db470","name":"Sam (live)",…}
{"kind":"admitted","participant":"dd41d676de86375c","role":"viewer","uses_left":0,…}
```

The same script passed four times end-to-end (codes `BKCG`, `SZQF`, `PRUV`, `UNXM`).

## Tests

The card's own suites are green: `test_cpace.py`, `test_remote_meetcode.py`,
`test_web_meet_code.py` (51 tests) and `test_remote_security.py`, `test_remote_noise.py`
(63 tests) — CPace vectors in both languages, the burn/used-code/forwarded-fragment refusals,
the code room never reaching the Noise channel. The `backend-and-bash` failures recorded on the
card 2026-09-19 were in-flight work and are gone from these modules.

## Caveats a verifier should know

- **Both ends ran on one machine** (loopback), not laptop → desktop as task t:ge words it: this
  session cannot reach sphinxpad. Every hop of the protocol still ran for real — the browser's
  CPace against the desktop through the sidecar's rendezvous, Noise IK, knock, admit — but a QA
  pass should redo it from the laptop against a desktop over the LAN/tailnet address.
- The desktop binary was built from HEAD (`ee12ac1a`) in a scratch worktree because the shared
  `build/` was mid-edit by other sessions; between 18:44 and the fix in `4109f930`, main crashed
  at startup on a fresh profile (#561P, not this card). The full python suite meanwhile shows
  7–9 failures in modules other sessions are editing uncommitted (board_protocol, guest_hook,
  system_prompt size, web_theme staleness, remote_wire allowlist) plus load-flaky real-Chrome
  browser tests — none in this card's files.
- The owner's clicks are OCR-driven, so `live.sh` is a harness, not a unit test: if a layout
  shifts, it fails loudly with a screenshot rather than clicking blind.
