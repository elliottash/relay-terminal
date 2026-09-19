# #3KB7 — Security section, implementer evidence (2026-09-19)

Claude Opus 5. Live under Xvfb (`:77`), isolated `HOME`/`XDG_*`/`TMPDIR`, `RELAY_KEYRING=off`,
isolation off, a scripted OpenAI-compatible model that answers every prompt with one `run_command`
tool call. **Implementer evidence, not a QA verdict.**

Build: `/tmp/claude-1000/qa77b` from the working tree at the time of the run.

## implementer-01-security-section.png

Options › **Security**, between Agent and Privacy. The page shows the posture paragraph and the
three list rows. "Commands the agent never runs" reads `rm` — it was set in `relay.conf`
(`[security] command_denylist=rm`) before launch, so this also shows the settings round trip.
"Reset to defaults" correctly counts 3 options on the page.

## implementer-02-denied-command.png

The whole chain, end to end. The agent was asked to delete a file; its `run_command` call was
`rm -rf /tmp/relay-denylist-probe`. The pane printed, in the error ink:

```
▸ run rm -rf /tmp/relay-denylist-probe ✗ · Refused by this Relay's command denylist (the rule is 'rm', in Options › Security). Do …
```

and **`/tmp/relay-denylist-probe` still existed afterwards** — checked by the driver, not by eye.
That covers QSettings → `requestOptions()` → `configure` → `validate_turn_options` →
`security.validate` → `Policy` → `denied_command` → the refusal the model is given.

## Not covered here

- The two rows that wait on #R5TC (`security/unattended_full_tools`, `agent/cross_pane`): there is
  no unattended turn to govern until that card lands, and a control that does nothing is worse than
  no control.
- The OSC 52 row: `setClipboardWriteAllowed()` exists on `TerminalView` but not on the
  `TerminalBackend` interface the Pane speaks to, so it needs an engine-interface addition rather
  than a settings row. Clipboard writes are already off, which is the safe default.
- Moving the existing rows (`agent/terminal_handoff`, `isolation/*`, the turn bounds,
  `agent/audit_requests`) into this page. Mechanical, but it touches every reader of each key and
  `src/RelayWindow.h` had three sessions in it today.
