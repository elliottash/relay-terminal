# Detect programs waiting for input from the screen text (new engine)

- **Status**: open
- **Component**: gui, terminal engine
- **Milestone**: cross-platform
- **Workstream**: terminal
- **Acceptance evidence**: with the Relay engine, `sudo apt upgrade` reaching "Do you want to continue? [Y/n]" shows the waiting-for-input hint and moves focus to the terminal within about a second, without false hints during downloads
- **Assignee**: unassigned
- **Source**: owner, 2026-09-17: "lets do the first option now, and then reading the last line with the new engine later"

## Context

sudo 1.9.14+ runs the command in its own pseudo-terminal, so Relay's terminal stays in raw mode and the real reader (apt, as root) cannot be inspected. KonsolePart 23.08 exposes no screen text. The interim behavior (keep focus in the terminal while sudo runs, prompt box visible, Ctrl+Shift+H to type a prompt) ships with the prompt-visibility work.

## Plan

With the Relay engine (`issues/features/2026-09-17-portable-terminal-engine.md`, `docs/TERMINAL-ENGINE-OPTIONS.md`):
- After the foreground program has produced no output for about one second, read the cursor line and the line above.
- Match prompt patterns: `[Y/n]`, `[y/N]`, `(yes/no)`, `[Y/n/?]`, `Password:`, `passphrase`, `Continue?`, trailing `? ` or `: ` with the cursor at end of line; configurable list.
- On a match, show the waiting-for-input hint and focus the terminal; clear it when output resumes or the user types.
- Also use OSC 133 prompt marks where the shell emits them, which make shell prompts unambiguous.
