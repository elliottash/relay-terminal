---
id: YR21
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, terminal engine]
milestone: cross-platform
workstream: terminal
rank: 6y
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-17
created: '2026-09-17'
acceptance: with the Relay engine, `sudo apt upgrade` reaching "Do you want to continue? [Y/n]" shows the waiting-for-input hint and moves focus to the terminal within about a second, without false hints during downloads
source: 'owner, 2026-09-17: "lets do the first option now, and then reading the last line with the new engine later"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-agent-drives-programs'], related: ['C1HH'], github: null}
---
# Detect programs waiting for input from the screen text (new engine)

## Context

sudo 1.9.14+ runs the command in its own pseudo-terminal, so Relay's terminal stays in raw mode and the real reader (apt, as root) cannot be inspected. KonsolePart 23.08 exposes no screen text. The interim behavior (keep focus in the terminal while sudo runs, prompt box visible, Ctrl+Shift+H to type a prompt) ships with the prompt-visibility work.

## Plan

With the Relay engine (`issues/features/2026-09-17-portable-terminal-engine.md`, `docs/TERMINAL-ENGINE-OPTIONS.md`):
- After the foreground program has produced no output for about one second, read the cursor line and the line above.
- Match prompt patterns: `[Y/n]`, `[y/N]`, `(yes/no)`, `[Y/n/?]`, `Password:`, `passphrase`, `Continue?`, trailing `? ` or `: ` with the cursor at end of line; configurable list.
- On a match, show the waiting-for-input hint and focus the terminal; clear it when output resumes or the user types.
- Also use OSC 133 prompt marks where the shell emits them, which make shell prompts unambiguous.

## Implemented (2026-09-17)

`relay::screen::detect` in `src/ScreenPrompt.{h,cpp}` (library `relay-screen`) decides, from the
last rows of the screen, whether the foreground program is waiting for input and for what. It is a
pure function over `QStringList rows` plus a `Signals` struct (the termios line discipline, whether
a foreground program runs, whether a process of it is blocked in `read()`, the alternate screen,
and whether the pane's engine can read the screen at all), so it is tested against recorded
screens with no terminal: `tests/screenprompt_test.cpp`, fixtures in `tests/fixtures/screen/`
(apt's `[Y/n]`, apt mid-download, a wrapped apt question, an ssh host key, an ssh passphrase,
`[sudo] password for …`, `read -p`, `input()`, the python REPL, psql, `update-alternatives`,
"Press [ENTER] to continue", a shell prompt, a `git rebase -i` todo list, and a grep hit that
quotes `[Y/n]`, `(yes/no)` and `Password:` without asking anything).

Kinds: `none`, `shell_prompt`, `yes_no` (with the option token and the capitalized default),
`choice`, `password`, `press_key`, `free_text`. Confidence combines the pattern with the /proc
signals — canonical input with echo +0.15, a reader blocked in `read()` +0.15, nothing running
−0.30 — and 0.60 is the bar for acting. The alternate screen is never a line prompt.

Used for three things:

1. **The banner.** A floating bar over the top of the terminal reads "apt is asking: Do you want
   to continue? [Y/n]", and the composer row says "· Enter sends your answer to it". A password is
   named, never quoted with a line beside it.
2. **The prompt box's "answer the program" path.** `relay::input::State` gained `screenAsking` and
   `screenMasked`; `lineRequested` now accepts *either* the /proc proof *or* the screen, which is
   what makes the `sudo apt` case work — sudo runs its child in its own pseudo-terminal, so no
   process Relay may inspect is blocked in `read()`.
3. **Handing the program to the agent** (card `#C1HH`), which reuses the same detection.

The pane polls it every 250 ms while a command runs and needs two agreeing ticks (~0.5 s) before
the waiting hint appears, so a question that scrolls past during a download raises nothing.

Gated on `TerminalBackend::ScreenText`. A pane without it (KonsolePart on KF5) keeps exactly the
behaviour that shipped before: the banner says "bash is asking for input" with no question, and
the delegate button is disabled with a status line saying why.

Not implemented: OSC 133 prompt marks are not used by the classifier (the shell-prompt pattern
covers the same case and needs no opt-in integration), and the pattern list is not a user setting.

## Implementer check (not a QA verdict)

Xvfb `:95`, Relay-engine pane, kimi-k3. `read -p "Continue? "` → "bash is asking: Continue?";
apt's own wording → "bash is asking: Do you want to continue? [Y/n]"; the same `read -p` in a
KonsolePart pane → "bash is asking for input" and delegation refused.
Evidence: `docs/qa_evidence/2026-09-17-agent-drives-programs/` (shots 01, 03, 11, 12).
Tests: `tests/screenprompt_test.cpp` (29) and the new cases in `tests/inputpolicy_test.cpp`.

## QA checklist

1. Real `sudo apt upgrade` on a machine with updates: the hint and the banner name the question
   within about a second, and nothing appears during the download.
2. `ssh` to an unknown host: the host-key question is read, and answering `yes` from the prompt
   box works.
3. A long `grep`/`rg` whose output quotes `[Y/n]` or ends with `Password:`: no hint, no banner.
4. `less` and `git log` (pagers), `python3`, `psql`, `update-alternatives --config editor`.
5. A KonsolePart pane on KF5 and on KF6: KF5 must degrade to "is asking for input"; if KF6's
   `getDisplayedText` is present it should behave like the Relay engine.
6. A narrow window that wraps apt's question over two rows.
