# Guest event channel on a live pane (GT7X, child `claude-hooks`)

Evidence for §26.3's event channel and §26.4's hook wiring, driven through the built `relay` on an
Xvfb screen. The channel is a file the pane polls, so this harness sits outside the GUI and speaks
the protocol exactly as a guest's shim does: `shell/guest-event.py` with `RELAY_RUNTIME_DIR`,
`RELAY_SESSION_TOKEN` and `RELAY_GUEST_EVENT` set, and — for the two writes that a hook and a
statusline make — `relay_core.guest_hook` itself, the module the installer configures.

## Running it

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 claude-hooks-drive.py

`claude-hooks-drive.py` starts the worktree's `build/relay` under a fresh temp root that holds
`HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`, so nothing of the
user's session is read or written. `RELAY_DATA_DIR` is the worktree, so the helper and the backend
under test are this build's and not an installed Relay's. `RelayTerminal/relay.conf` carries
`[instructions] onboarded=true` so the first-launch instruction dialog stays out of the pictures.
The screen is 1600x1000 and the prompt-box strip — the row of chips under the terminal — is at
y 804–840; `STRIP_ROWS` says so, and the strip is OCR'd on its own because the chip is small and a
whole-screen pass mangles it.

Real Claude Code is not installed, so the harness puts a stand-in of that name in the foreground:
it types `!bash -c 'exec -a claude sleep 900'` into the composer, which is the composer's own "run
this line in the terminal", making the pane's `/proc/<pgid>/cmdline` read `claude` — the whole input
to `guestProgram()`'s classifier — while leaving the pane's shell and its Bash bridge alive, which
is what a real `claude` in a pane looks like. The harness then waits for `/proc` to agree that a
process called `claude` is a descendant of the Relay process before it takes any picture.

Each picture is verified, not just taken: the harness OCRs the screenshot and logs what the strip
and the terminal say, so the run's own output is the evidence's verification. The whole-screen pass
proves the permission question and that the chip is gone; the strip pass proves the chip itself.

## What each picture is evidence of

`implementer-claude-hooks-01-statusline-chip.png` — the statusline shim run as the installer writes it
(`python -m relay_core.guest_hook statusline` with Claude's statusline JSON on stdin). It forwards
the model and context share through the channel and prints its one passthrough line, logged as
`'Claude Sonnet 4.5 · workspace'`, so Claude's own statusline is unchanged. The strip then reads
`Claude Sonnet` and `42%`: the chip is the model and the share, fed by the shim.

`implementer-claude-hooks-02-permission-question.png` — a held `PreToolUse` hook (`RELAY_GUEST_PERMISSION_TIMEOUT`
90 s) asking about `rm -rf build/ && cmake -S . -B build`. The terminal reads `wants to run Bash` and
`rm -rf build`: the pane asked the user rather than answering the hook itself. The harness finds the
bar's `Deny` button in tesseract's TSV — the button glyphs come back with stray punctuation
(`[(Deny]`), so a box is matched on a substring — and clicks 28 px to its left, which is the middle
of `Allow` (`buildUi`'s `questionRow` puts Allow then Deny with 8 px between them).

`implementer-claude-hooks-03-answered.png` — the click landed: the bar is gone (the screen no longer says
`wants to run`), the pane wrote
`{"decision":"allow","sequence":"9f4f2c38-…","token":"c67a68c3-…"}` to `guest-answer.json`, and the
held hook returned
`{"hookSpecificOutput": {"hookEventName": "PreToolUse", "permissionDecision": "allow",
"permissionDecisionReason": "Allowed in Relay."}}`. That is the round trip the section is about: the
pane's answer reaches Claude through the hook's contract, and a hook that is never answered times
out printing nothing — it is never auto-approved.

`implementer-claude-hooks-04-context-warn.png` — a share past the warn line (`94%`, model `Claude Opus 4.6`)
written through the helper rather than the shim, so the envelope and the poll are exercised on their
own, plus a `state` event moving the busy field. The strip reads `Claude Opus` and `94%`, and the
chip carries its `warn` property at or above 90%.

`implementer-claude-hooks-05-guest-gone.png` — the stand-in guest is killed, and the chip goes with it: the
screen says `workspace` and no longer `Claude Opus` or `94%`, because the pane's program poll sees
the foreground program is no longer a guest and clears the guest state.

## Not covered here

The helper's no-op invariant, the envelope's token/sequence rejection and the installer's
add/remove round-trip are unit-tested instead (`tests/test_guest_hook.py`,
`tests/test_guest_install.py`), since they are about the files rather than about pixels. The
Options > Guests control that turns a project's installation on is the lead's Pane.h/OptionsDialog
work; the installer's CLI (`python -m relay_core.guest_install --project <dir> --on|--off|--status`)
is what it calls.
