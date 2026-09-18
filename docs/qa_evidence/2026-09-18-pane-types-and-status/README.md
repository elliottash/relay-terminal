# Pane types, pane states and remote sessions: implementer evidence

Cards [#SPBN](../../../issues/features/needs_qa_llm/2026-09-18-distinct-headers-or-colors-for-each-pane-type.md)
and [#XM0T](../../../issues/features/needs_qa_llm/2026-09-18-terminal-and-agent-status-icons-with-notificatio.md).
Implementer screenshots (Claude Opus 5), not QA verdicts. Taken under Xvfb, 1440×900, with an
isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`, by `drive.sh`:

    docs/qa_evidence/2026-09-18-pane-types-and-status/drive.sh <build-dir> [layout] [tabs]

- **No provider account.** The profile's model is a local endpoint pointed at `stub-provider.py`
  on 127.0.0.1: a prompt with "spawn" starts a background subagent, "ask" ends on a question,
  "fail" gets an HTTP 400, "slow" takes a minute, anything else answers "Done." after 4 s.
- **No sshd.** `ssh` on the pane's PATH is `fake-ssh.c`: like ssh it stays in the terminal's
  foreground process group and relays a pty of its own (via `script`), so Relay reads what it would
  read for a real session (`ssh demo@build-box`); the shell inside is local.

| File | Shows |
|---|---|
| `implementer-layout-relay-dark-type.png` | Pane colours **by type**, Relay Dark. Left column: a terminal whose agent started a background subagent (subagents glyph in its header and on the tab), and under it a terminal in `ssh demo@build-box` (hatched red title row, `⇄ demo@build-box` chip, running glyph). Then the subagent pane (violet), the Switchboard (brass), Options (green, the gear). |
| `implementer-layout-relay-dark-group.png` | The same, **by group**: Switchboard and Options share brass, the subagent pane keeps violet. |
| `implementer-layout-relay-dark-off.png` | **Off**: the bands stay, as neutral headers; the ssh band does not change (it ignores the setting). |
| `implementer-layout-relay-light-type.png`, `-group.png` | The same two in Relay Light. |
| `implementer-tabs-relay-dark.png` (and `-bar.png`, 2×) | Seven tabs, the first one current: a background turn that asked a question (diamond `!`), one that finished (tick), one that failed (disc ×), one still working (star), a running `sleep` (triangle), an ssh session (red ⇄). |
| `implementer-tabs-relay-dark-bell.png` | The bell for those tabs: "Agent needs you", "Agent finished", "Agent turn failed" — and nothing for the running, working or ssh tabs. |
| `implementer-tabs-relay-light*.png` | The same in Relay Light. |

`tests/panestatus_test.cpp` checks, for every shipped theme, that every band's label is at least
4.5:1 and its glyph at least 3:1 on its tint, and that the pane title is 4.5:1 on the ssh band.
