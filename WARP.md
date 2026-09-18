# Relay — project instructions for agents

Relay is a Linux terminal (C++/Qt, with its own terminal engine) with a composer,
per-pane BYOK agents, tabs/panes, file panes and an actions palette. Read `docs/README.md`
(index), `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` before large changes.

## Working rules

- **Issues:** file-based tracker in `issues/` (conventions in `issues/README.md`, based on the
  global issue-tracking skill). Implemented work goes to `needs_qa_llm/` with implementer
  evidence under `docs/qa_evidence/YYYY-MM-DD-<slug>/` and a QA checklist.
- **Tests:** `./scripts/test.sh` (backend + Bash/PTY) and `ctest --test-dir build` must pass.
  Verify GUI changes live under Xvfb with an isolated `XDG_CONFIG_HOME`.
- **Protocol:** GUI ↔ worker messages are specified in `docs/AGENT-SESSIONS-PROTOCOL.md`;
  update it when adding messages or events.
- **Decisions already made:** no Konsole fork (and KonsolePart itself retired 2026-09-18);
  no per-action tool approvals; BYOK only; no
  telemetry. See `docs/ROADMAP.md`.

## Shortcut hints (standing rule)

Relay teaches shortcuts Superhuman-style: when the user does something the slow way (mouse,
palette, typing a long form) and a faster keyboard path exists, it shows a brief hint.

**Whenever you add or change a feature that has a shortcut, slash command, prefix or other fast
path, add a hint for it** in the shortcut-hint registry (see `docs/ARCHITECTURE.md`, "Shortcut
hints"), covering the slow path that should trigger it. Keep hints short ("Next time: Ctrl+T"),
use the live Keymap text rather than hard-coded keys, and respect the per-hint show limit and
the global "Shortcut hints" setting.
