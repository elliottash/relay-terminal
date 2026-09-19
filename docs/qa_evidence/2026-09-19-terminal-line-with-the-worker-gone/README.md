# A terminal line while the agent worker is gone (2026-09-19, #N8VK follow-on)

## The report

From an agent re-shooting the IDE-bridge evidence, 2026-09-19:

> With the agent-worker banner up ("The agent worker exited" in the pane's banner), a composer line
> cannot be sent to the terminal at all: the evidence script types `!bash -c 'exec -a claude sleep
> 900'` into the composer, the `! terminal` chip lights, and Enter sends nothing, so no command
> reaches the shell.

It was seen while #N8VK was reworking Enter/queue routing, so the first question was whether
ad7b455 / d633d6b had already fixed it. They had not: #N8VK took the explicit **agent** submit off
the router's round trip and left the explicit **terminal** submit on it.

## It reproduces, on the current `main`

`before-*.png` is `main` at `40b4500` (`git archive refs/heads/main` into its own tree, configured
and built there — never the shared checkout, which holds six sessions' uncommitted code).
`after-*.png` is that same tree plus this change and nothing else. Both runs are the same script,
`drive.py`, on the same display, one after the other.

Five scenes on one live pane. A marker file in the pane's working directory is the proof: it exists
only if the shell ran the line.

| | typed | key | main `40b4500` | with this change |
|---|---|---|---|---|
| a | `!echo RELAY-ENTER-A > a.txt`, worker healthy | Enter | **ran** | **ran** |
| b | `!echo RELAY-ENTER-B > b.txt`, banner up | Enter | **nothing** | **ran** |
| c | `ls`, banner up, AUTO | Enter | **nothing** | refused, with the way out |
| d | `echo RELAY-ENTER-D > d.txt`, banner up | Ctrl+Shift+Enter | **nothing** | **ran** |
| e | `echo RELAY-ENTER-E > e.txt`, after Restart agent | Enter | **ran** | **ran** |

`before-result.json` and `after-result.json` are those columns as the script recorded them;
`before-run.log` and `after-run.log` have every screenshot's OCR.

On `main`, scenes b, c and d all end the same way (`before-05`, `before-06`, `before-07`): the
status line reads

    Local router is not ready; use the native terminal or restart Relay.

the text stays in the prompt box, and no marker file is written. Scene (a) proves the pane and the
shell are otherwise fine, and scene (e) proves nothing is broken but the worker: the banner's own
Restart agent brings AUTO straight back.

With the change, `after-05` and `after-07` show `RELAY-ENTER-B` and `RELAY-ENTER-D` run at the
shell's own prompt with the banner still up, and `after-06` shows what AUTO — the one mode that
really does need the router — is told instead:

    The agent worker is not running, so Auto cannot tell a command from a prompt.
    Restart agent (Ctrl+Shift+R), or press Ctrl+Shift+Enter to run this line in the terminal.

## The cause

`Pane::requestRoute` (`src/Pane.h`) opened with

```cpp
if (!m_workerReady && mode != QStringLiteral("agent")) {
    if (submit) status(QStringLiteral("Local router is not ready; use the native terminal or restart Relay."));
    return;
}
```

The `!` prefix and the TERMINAL chip both resolve to `mode == "shell"`, and Ctrl+Shift+Enter
submits with that mode outright — so every one of them hit that gate and returned. The router is a
message to the worker (`{"type": "route"}`), and it is the worker that had died; but the shell had
not, and a forced-shell line has nothing to ask it. The only thing the router contributes to such a
line is the syntax check that hands a broken command to the agent to fix — and with no agent to
hand it to, the shell judges the command itself, exactly as it does for anything typed natively.

## The fix

- `relay::input::withoutRouter(mode)` (`src/InputPolicy.{h,cpp}`) is the rule, next to the rest of
  the prompt-box rules and testable without a shell or a worker: `shell` → run it, `agent` → the
  agent's own path, anything else (`auto`) → refuse.
- `requestRoute` dispatches an explicit terminal submit locally when the worker is down, through
  the same `dispatch()` every other terminal line goes through, so the login, the queue and the
  hand-off path all behave exactly as they always did. The route chip reads `TERMINAL · explicit`,
  the mirror of `AGENT · explicit` from #N8VK.
- `relay::input::noRouterText` is what AUTO is told: the banner's own action by its live key, and
  the key that sends this very line to the terminal anyway.
- The worker-exit handler now also clears `m_pendingSubmit`, `m_previewId` and `m_heldDecision`. A
  `route` in flight when the worker dies can never be answered, and `m_pendingSubmit` is a
  one-submission guard that only a reply releases: left behind, it swallowed every later terminal
  submit in that pane in silence.

`tests/inputpolicy_test.cpp` (`ctest -R input`) holds both rules —
`aLineTheUserAddressedDoesNotNeedTheWorker` and `theAutoRefusalNamesTheWayOut`.

## Running it again

```
Xvfb :57 -screen 0 1600x1000x24 &
DISPLAY=:57 python3 drive.py <relay binary> <RELAY_DATA_DIR> <output dir> <before|after>
```

Everything is under a fresh temp root (`HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`,
`XDG_RUNTIME_DIR` at 0700, `TMPDIR`), with `RELAY_KEYRING=off`; nothing of the owner's real session
is read or written.

**One thing worth knowing about this setup.** The run turns pane isolation off in its own settings.
With a made-up `XDG_RUNTIME_DIR` and no session bus, `systemd-run --user` cannot start anything, so
an isolated pane's worker dies the instant it is launched — which is how the original report came
about, on a harness that did not think about isolation. Scene (a) needs a worker that lives, and
the scenes after it kill the worker themselves, on purpose, with SIGHUP.
