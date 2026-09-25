---
id: VD2M
type: work
status: needs-verification
labels: [feature, design, remote, terminal]
assignee: agent
implemented_by: glm/glm-5.3
session: 8c549c94-8513-4d49-be07-82c38d276ed9
rank: zzzzzzzzzzzzzzzzzzzz
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: 'run zellij inside a Relay pane: prompt is recognised, Alt+arrows reach zellij, Shift+Alt+arrows move Relay focus; the persistent palette entry reattaches a real session', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Claude Code guest session in Relay, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-zellij-VD2M/], related: [S5SH, 87HB, A6SH, S7KC], github: null}
---
# Zellij support, and mosh plus zellij as Relay's persistent remote session

## Issue
feature request to discuss and explore: mosh and zellij integration

## Decisions
- Owner, 2026-09-24: "1 A and B yes" — tier A (zellij works inside Relay) and tier B (persistent remote pane via mosh + multiplexer) approved; tier C stays deferred.
- Owner, 2026-09-24: "2 yes" — persistence layer is zellij or tmux, whichever the host has, preferring zellij; nothing installed on the host.
- Owner, 2026-09-24: "3 i agree, but shift+alt+arrows for relay" — when a program owns the pane's keyboard, Alt+arrows go to the program; Shift+Alt+arrows are Relay's pane navigation fallback there.

## Done means
- Tier A: with zellij running in a Relay pane, the shell prompt is recognised (cursor-row read clips at the cursor, so pane separators and neighbour-pane text after the prompt no longer defeat it); Alt+arrows reach zellij; Shift+Alt+arrows switch Relay panes while a program owns the keyboard; `shell/remote-integration.sh` detects `$ZELLIJ` and behaves sensibly whether or not zellij passes OSC 133/7 through (verified empirically, documented in `docs/SSH-AND-MOSH.md`).
- Tier B: the command palette has "Connect to host (persistent)" which runs mosh (falling back to ssh) plus `zellij attach -c relay-<pane>` on the host, or `tmux new -A -s relay-<pane>` when the host has tmux but no zellij; closing and reopening the pane/window re-attaches to the same remote session.
- Failure looks like: zellij's Alt+arrow pane focus still eaten by Relay, prompt detection still scanning the whole row, or the persistent command not surviving a Relay restart.

## Plan
Empirical basis (zellij 0.45.1, aarch64, installed at ~/.local/bin/zellij; probes /tmp/zellij_probe*.py, dump /tmp/zellij-screen6.txt):

- OSC 133 / OSC 7 / OSC 52 written by the shell inside zellij do NOT reach the outer terminal. Zellij has no tmux-style DCS passthrough. So shell-integration marks cannot work through zellij; prompt detection must come from the screen classifier.
- `$ZELLIJ` is set to `0` in panes (truthy by presence only — detection must test non-empty, never non-zero). `$ZELLIJ_SESSION_NAME`, `$ZELLIJ_PANE_ID` also set.
- First run shows a tips overlay that eats input until ESC — worth documenting.
- Sessions persist across attach/detach and `zellij -s <name> action …` works out-of-band (useful later for tier C).

Plan:

1. **Tier A — zellij works inside Relay.**
   a. `shell/remote-integration.sh`: detect `[ -n "$ZELLIJ" ]` and skip emitting OSC marks under zellij (they cannot escape; the screen classifier covers prompt detection). Keep the tmux DCS branch untouched.
   b. `src/ScreenPrompt.h`: the cursor-row prompt read clips at the cursor column, so zellij/tmux pane separators and neighbour-pane text after the prompt no longer defeat classification.
   c. `src/Keymap.h` + wherever inPrograms overrides apply: plain Alt+arrows go to the program; Shift+Alt+arrows become Relay's pane navigation inside programs. Alt+-/Alt+= stay Relay's (dimmer) per existing behaviour; owner only moved the arrows.
2. **Tier B — persistent remote pane.** Palette entry "Connect to host (persistent)" runs `mosh <host> -- sh -c 'command -v zellij … && exec zellij attach -c relay-<name> || exec tmux new -A -s relay-<name>'` (ssh -t fallback when mosh is absent locally). Reopening the pane reruns the same command and re-attaches.
3. **Tests.** Screen-classifier unit test with a bordered/split row; keymap test (Alt+arrow in program → forwarded, Shift+Alt+arrow → Relay action); integration-script test under real zellij mirroring `tests/test_ssh_shell.py`'s tmux test; persistent-command construction test.
4. **Docs.** `docs/SSH-AND-MOSH.md`: zellij chapter — no passthrough, classifier fallback, tips overlay, persistent session recipe.

## Execution Summary
Landed in `0afa8dd1` (evidence `68a9a730`), all three tiers of the plan:

- **Tier A.** `shell/remote-integration.sh` now detects `$ZELLIJ` (set to `"0"` — presence, never value), prints one line that marks do not pass, and no-ops `__relay_r_o` under it. `rowHoldsPrompt()` (`src/ScreenPrompt.{h,cpp}`) reads the cursor's row up to the cursor and accepts a pane separator after it; `Pane::updateLoginPrompt` uses it instead of the cursor-at-row-end check. `Keymap::actsInsidePrograms` gives plain Alt+arrows to the program and keeps Shift+Alt+arrows (bound alongside plain Alt+arrows in the default table) for Relay pane focus inside programs.
- **Tier B.** New `ssh.connectPersistent` action (Keymap, RelayWindowCore dispatch, palette item in the actions list + "Remote and sharing" search keywords) opens the host picker with "Connect to SSH (persistent)"; `relay::ssh::persistentCommand()` builds `mosh <target> -- sh -c '…'` (or `ssh -t` when `hasLocalMosh()` is false) whose host script runs `zellij attach --create relay-<sanitized target>`, falling back to `tmux new -A -s <session>`, then a login shell. Session name is per user+host, so reconnecting meets the same session.
- **Docs.** `docs/SSH-AND-MOSH.md` section 11 covers the no-passthrough fact, the screen fallback, the keys, the tips overlay and the persistent recipe.

Commit notes: hunk-selected land (`--only-hunk src/Pane.h:2`, `src/RelayWindowCore.cpp:1`) so other sessions' uncommitted hunks in those files stayed in the working tree; the build gate built the exact tree before the swap.

## Tests
- `tests/screenprompt_test.cpp` — `rowHoldsPromptAcceptsASeparatorSplit`, `rowHoldsPromptRejectsAnythingElseAfterTheCursor`: separator (`│ ┃ |`) after the prompt accepted; the row's own text after the cursor, non-prompts and empty rows rejected. ctest `screen`: passed.
- `tests/keymap_test.cpp` — `programKeysGivePlainAltArrowsToTheProgram`: plain Alt+arrow and Ctrl+Alt+arrow do not act inside programs; Shift+Alt+arrows do and resolve to `pane.focus*`. ctest `keymap`: passed.
- `tests/sshconfig_test.cpp` — `persistentCommandBuildsTheFallingChain`, `persistentSessionKeepsOneNamePerUserAndHost`: mosh/ssh construction, zellij→tmux→shell chain, quoting, session sanitizing. ctest `sshconfig`: passed.
- `tests/test_zellij.py` — runs a real zellij 0.45.1 (skips when none found; `RELAY_TEST_ZELLIJ` overrides): `$ZELLIJ` set to "0" in a pane; OSC 133 written inside never reaches the outer pty; sourcing `shell/remote-integration.sh` prints the note once and `__relay_r_o` emits nothing. 2/2 passed in 30s.
- Build gate: `land.py commit` built the exact tree it put on `main` (`/tmp/claude-1000/land/zellij-vd2m/verify`).

Evidence: `docs/qa_evidence/2026-09-24-zellij-VD2M/` (ctest.txt, test_zellij.txt, probe6-screen-dump.txt, README.txt).
