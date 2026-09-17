# Relay 0.1 validation report

## Release status

**Source development preview.** Checked on 2026-09-16/17 on Ubuntu 24.04.5 (aarch64),
kernel 7.0, Python 3.12.3, Qt 5.15 / KDE Frameworks 5.115, Konsole part 23.08.

## Executed checks

| Check | Result | Notes |
|---|---|---|
| Python backend and Bash/PTY suite | **79 tests passed** | `./scripts/test.sh` |
| Native build, Qt5 / KF5 | **Built and linked** | `./scripts/build.sh`, auto-selected Qt5 |
| Native ctest | **2 of 2 passed** | Qt editor tests plus the backend suite |
| Qt6 editor-only build | Built, editor tests passed | Full Qt6 app not built: no KF6 on Ubuntu 24.04 |
| App launch under Xvfb | Konsole embedded, Bash prompt, worker ready | Screenshots taken, not committed |
| GUI end to end under Xvfb | Auto-configured GLM-5.3 from keyring; agent answered; unknown command fell back to the agent and reached a tool approval | Driven with xdotool |
| Live provider requests (2026-09-16) | **Passed** for Kimi K3, GLM-5.3 Coding Plan, OpenRouter DeepSeek V4.1 Flash: plain reply plus one approved `list_directory` tool round trip each | Keys imported from Warp via keyring; see "Live provider smoke test" below |
| CI | None configured | |

Two bugs were found only by running the real app:

- Worker messages sent before `QProcess` reported `Running` were dropped, so the
  preset list and configuration were sometimes lost at startup.
- `tcgetpgrp()` on the shell's terminal returns `ENOTTY` on this kernel because it is
  not Relay's controlling terminal, so composer commands never reached the shell.
  Readiness now reads the foreground group from `/proc/<pid>/stat`.

## Coverage of the original 51 tests

Approval tests in this table were replaced when approvals were removed: tools now
run immediately, show a preview, and can be stopped mid-command.

Later additions cover presets and keyring storage, Warp import, OpenRouter reasoning,
the interrupt/queue dispatcher, and ambiguous-route syntax reporting.

| Area | Tests | Verified behavior |
|---|---:|---|
| Local routing | 8 | Commands versus language, explicit routing, aliases/functions, ambiguous input, control-character rejection, multiline parsing, non-execution during syntax checking |
| Real Bash / PTY integration | 8 | Loaded-command acknowledgement, exit status, cwd/environment persistence, aliases, Unicode/multiline commands, heredoc, interactive `read`, interrupt, existing prompt array, DEBUG-hook fallback |
| Tool execution and approval preparation | 16 | No execution during preparation, output/exit capture, timeouts, cancellation, output limits, restricted file paths and secret guards, diffs, stale-write rejection, symlink/file-appearance races, FIFO rejection, environment scrubbing |
| Provider transport | 11 | Local HTTP fixture, auth/request payload, SSE and JSON responses, fragmented/multiple tool calls, reasoning fields, Unicode, malformed/truncated streams, redirect refusal, cancellation, configuration guards, sanitized HTTP errors |
| Agent loop and worker protocol | 8 | Explicit approval before execution, denial, cancellation during approval, unknown-tool refusal, configuration without a network call or key echo, route-only use, malformed input, missing provider configuration |
| **Total** | **51** | All passed on the final source state |

Provider tests use a local mock HTTP server and synthetic model responses.
They do not validate real account access, model behavior, billing, quotas,
provider-side compatibility, or end-to-end live coding performance.

## Native/editor acceptance gates that remain

1. Install Qt6/KF6/Konsole development/runtime packages and compile the app with
   warnings enabled. Confirm `kf6/parts/konsolepart` loads on the target distro.
2. Run the seven Qt tests for word selection/undo, multiline/submit shortcuts,
   Shift+click, mouse drag, safe paste, IME submission, and draft-preserving history.
3. Manually exercise mouse and keyboard selection on both Wayland and X11 where
   relevant, including HiDPI, light/dark palettes, wrapped lines, and the user's IME.
4. Verify editor submission through the actual embedded KonsolePart, return-to-
   prompt behavior, failed-ack fallback, keyboard focus, direct Readline typing,
   partially entered native lines, and shell exit/window cleanup.
5. Test native-mode applications such as Vim, `less`, interactive Python, SSH,
   tmux, and password prompts. Only Bash has rich integration in this preview.
6. Test with the user's real prompt/plugins. An existing DEBUG trap intentionally
   uses native fallback; `--clean-shell` is the diagnostic configuration.
7. In a disposable workspace, configure a provider key and verify one simple
   response, one read, one command, one write, and cancellation mid-command. Inspect provider account usage independently.

These are outstanding acceptance checks, not completed test results.

## Security boundaries and known functional gaps

Agent tools run without per-action approval (removed 2026-09-17), and shell
commands are **not sandboxed**. It has the user's normal filesystem and network permissions.
File-tool workspace checks and secret-file guards are limited defenses, not an
OS-level sandbox or a guarantee against hostile same-user filesystem races.
Cancellation does not undo a completed write or command; blocked network I/O can
last until its 30-second timeout.

Agent commands use separate non-interactive processes, not the live terminal
session. Terminal output/history is not automatically captured or sent to the
model. Rich shell integration supports Bash only. Tabs/splits, shell completion,
command blocks, KWallet persistence, checkpoints, and installable desktop packages
are not implemented. The native application itself remains unverified.

See `../README.md` for build/use instructions and `ARCHITECTURE.md` for design.

## Live provider smoke test (2026-09-16)

Run through the real `Agent` loop with keys imported from Warp into the keyring.
Each provider got a plain prompt and a prompt requiring one `list_directory` call
in a scratch workspace. Only that tool was auto-approved.

| Preset | Plain reply | Tool round trip | Reasoning field retained |
|---|---|---|---|
| `kimi` | Passed, 5.8 s | Passed, 25.1 s | Yes |
| `glm-coding` | Passed, 4.9 s | Passed, 12.0 s | Yes, on the tool turn |
| `openrouter` | Passed, 3.0 s | Passed, 5.1 s | Yes |

The Z.AI standard endpoint (`glm`) was not tested because Warp only had a Coding
Plan key. Warp's `settings.toml` uses TOML 1.1 multi-line inline tables; the
importer normalizes them for Python 3.12's TOML 1.0 parser.
