# Guest event channel on a live pane (GT7X, child `claude-hooks`)

Evidence for §26.3's event channel and §26.4's hook wiring, driven through the built `relay` on an
Xvfb screen. The channel is a spool directory the pane polls, so this harness sits outside the GUI
and speaks the protocol exactly as a guest's shim does: event files dropped into the pane's
`guest-events/`, with `RELAY_RUNTIME_DIR`, `RELAY_SESSION_TOKEN`, `RELAY_GUEST_EVENT` (the spool
directory) and `RELAY_BACKEND_DIR` set. Most of the writes go through `relay_core.guest_hook`
itself, run **by absolute path with the `--relay-guest` marker and no `PYTHONPATH`**, which is
character for character what the installer puts in `.claude/settings.local.json`.

## Running it

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 claude-hooks-drive.py

`claude-hooks-drive.py` starts the worktree's `build/relay` under a fresh temp root that holds
`HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`, so nothing of the
user's session is read or written. `RELAY_DATA_DIR` is the worktree, so the helper and the backend
under test are this build's and not an installed Relay's. `RelayTerminal/relay.conf` carries
`[instructions] onboarded=true` so the first-launch instruction dialog stays out of the pictures.
The prompt-box strip — the row of chips under the terminal — is OCR'd on its own (`STRIP_ROWS`),
because the chip is small and a whole-screen pass mangles it.

Real Claude Code is not installed, so the harness puts a stand-in of that name in the foreground:
it types `!bash -c 'exec -a claude sleep 900'` into the composer, which is the composer's own "run
this line in the terminal", making the pane's `/proc/<pgid>/cmdline` read `claude` — the whole
input to `guestProgram()`'s classifier — while leaving the pane's shell and its Bash bridge alive,
which is what a real `claude` in a pane looks like. The harness then waits for `/proc` to agree
that a process called `claude` is a descendant of the Relay process before it takes any picture.

Each picture is verified, not just taken: the harness OCRs the screenshot and logs what the strip
and the terminal say, so the run's own output is the evidence's verification.

## What each picture is evidence of

`implementer-claude-hooks-01-statusline-chip.png` — the statusline shim run as the installer writes it
(`guest_hook.py statusline --relay-guest` with Claude's statusline JSON on stdin). It forwards the
model and context share through the channel and prints its one passthrough line, logged as
`'Claude Sonnet 4.5 · workspace'`, so Claude's own statusline is unchanged. The strip then reads
`Claude Sonnet` and `42%`: the chip is the model and the share, fed by the shim.

`implementer-claude-hooks-02-permission-question.png` — a held `PermissionRequest` hook
(`RELAY_GUEST_PERMISSION_TIMEOUT` 90 s) asking about `rm -rf build/ && cmake -S . -B build`. This
is the hook Claude Code fires only when it is really about to ask the user; `PreToolUse`, which
fires before every tool call including the auto-allowed ones, is no longer installed. The terminal
reads `wants to run Bash` and `rm -rf build`: the pane asked the user rather than answering the
hook itself. The harness finds the bar's `Deny` button in tesseract's TSV — the button glyphs come
back with stray punctuation (`[(Deny]`), so a box is matched on a substring — and clicks 28 px to
its left, which is the middle of `Allow` (`buildUi`'s `questionRow` puts Allow then Deny with 8 px
between them).

`implementer-claude-hooks-03-answered.png` — the click landed: the bar is gone (the screen no longer says
`wants to run`), the pane wrote its answer to `guest-answers/<question>.json`, the shim read it and
deleted it (the directory listing logged right after is empty, which is the point — no tool input
and no decision is left lying in the runtime dir), and the held hook returned

```json
{"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": {"behavior": "allow"}}}
```

— Claude Code's `PermissionRequest` output shape, which is *not* `PreToolUse`'s
`permissionDecision`. That is the round trip the section is about; a hook that is never answered
times out printing nothing, so it is never auto-approved.

`implementer-claude-hooks-06-two-questions-queued.png`,
`implementer-claude-hooks-07-second-question.png`,
`implementer-claude-hooks-08-queue-empty.png` — two
`PermissionRequest` hooks held open at the same time. The spool keeps both (the single `guest.json`
slot it replaced would have lost the first), and the bar reads `1 of 2 · … git push --force`. Then
the keyboard: `Y` allows the one on screen and the second takes its place (`rm -rf /tmp/relay-qa-two`,
and `git push` is gone), `N` denies that one and the bar goes. The two held hooks returned
`"behavior": "allow"` and `"behavior": "deny"` in that order — so the answers went to the questions
they belonged to, and both were given without touching the mouse.

`implementer-claude-hooks-04-context-warn.png` — a share past the warn line (`94%`, model `Claude Opus 4.6`)
written straight onto the spool rather than through the shim, so the envelope and the poll are
exercised on their own, plus a `state` event moving the busy field. The strip reads `Claude Opus`
and `94%`, and the chip carries its `warn` property at or above 90%.

`implementer-claude-hooks-05-guest-gone.png` — the stand-in guest is killed, and the chip goes with it: the
screen says `workspace` and no longer `Claude Opus` or `94%`, because the pane's program poll sees
the foreground program is no longer a guest and clears the guest state.

## Not covered here

**The installed path has not been driven.** Every hook in this run was started by the harness, with
the pane's environment handed to it directly. Nothing here shows a real `claude` reading
`.claude/settings.local.json`, expanding `$RELAY_BACKEND_DIR` in its own shell and invoking the
shim — Claude Code is not installed on this machine, so that end of §26.4 is covered only by
`tests/test_guest_install.py`, which runs the installer's exact command string through `/bin/sh`
both with a pane's environment and without one. The first real `claude` in a pane is what will
prove the settings file itself.

The channel writer's no-op invariant, the envelope's token rejection, the payload cap, the answer
files and the installer's add/remove round-trip are unit-tested instead
(`tests/test_guest_hook.py`, `tests/test_guest_install.py`), since they are about the files rather
than about pixels. Turning a project's installation on is Options › Guests, driven on its own screen
in `options-guests-drive.py` beside this one; the command line it calls
(`python -m relay_core.guest_install --project <dir> --on|--off|--status`) is what a script or a
test uses.
