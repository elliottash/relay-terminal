# Relay validation status

Last updated 2026-09-17. Status: **Linux beta in preparation.** Nothing here is a QA verdict
from an independent model; see [The QA lane](#the-qa-lane).

Reference machine for local runs: Ubuntu 24.04.5, aarch64, kernel 7.0, Python 3.12.3,
Qt 5.15.13, KDE Frameworks 5.115, `konsole-kpart` 23.08.5.

## Test inventory

### Automated suites

| ctest name | Command | Content | Local result (2026-09-17) |
|---|---|---|---|
| `backend-and-bash` | `./scripts/test.sh` (`python3 -m unittest discover -s tests`) | Python backend, worker protocol, real Bash/PTY integration | **127 passed** |
| `editor` | `relay-editor-tests` (Qt Test, offscreen) | `RichEditor` interactions | **8 passed** |
| `filepanes` | `relay-filepanes-tests` (Qt Test, offscreen) | explorer and preview widgets | **9 passed** |

The two Qt counts exclude Qt Test's `initTestCase`/`cleanupTestCase` entries. They were run
from the existing `build/` binaries; `./scripts/build.sh` rebuilds and runs all three.

Backend tests by module:

| Module | Tests | What they check |
|---|---:|---|
| `tests/test_router.py` | 26 | Shell vs. language routing, explicit destinations and prefixes, live aliases/functions, every command word in pipelines, lists, subshells, groups and substitutions, assignments/wrappers/redirects, path words, syntax errors, control-character guard, validity fields, that parsing and validity checks never execute input, and a 99-input table of commands that are also English words (sentence vs. command) with false-positive sweeps |
| `tests/test_tools.py` | 16 | Preparation never executes, output and exit capture, secret env removal, timeouts (including after stdout closes), output cap, read and diff, stale-write refusal, new-file race, path escape and secret guard, symlink swap, FIFO, cancel before and during a command, unknown tools, create and list |
| `tests/test_keybindings.py` | 16 | Key normalization and validation, tool spec and enum, atomic write that keeps other content and reports conflicts, unbind, invalid existing file left alone, configure with and without a catalog, `keybindings` update keeps the conversation |
| `tests/test_agent.py` | 14 | Commands and writes run without approval and show previews, unknown tools refused, file tools confined, cancel during a command, route without a provider, configure makes no network call and never echoes the key, malformed requests, program context note (labelled, validated, control characters stripped, passed through the queue) |
| `tests/test_queue.py` | 14 | Ordered turns without overlap, `now` refused while busy, remove and clear, interrupt while streaming, idle and during a tool, FIFO interrupts, cancel pauses until resume, `now` while paused, cancel drops pending interrupts, configure/reset rules, validation, protocol errors, failed turn pauses |
| `tests/test_provider.py` | 12 | Local HTTP fixture: text and Unicode, fragmented tool arguments and reasoning, multiple tool calls, truncated and malformed streams, cancel, configuration guards, JSON fallback, redirect refusal, sanitized HTTP errors, OpenRouter `reasoning` kept but not displayed |
| `tests/test_skills.py` | 12 | Frontmatter parsing, skipped folders reported, prompt section cap, tools offered only with skills, `load_skill` and `read_skill_file` with refusals, worker configure count, indexing the real `~/.warp/skills` without errors |
| `tests/test_keystore.py` | 11 | Preset URL matching, secrets passed on stdin, env var overrides keyring, bad ids and keys, Warp TOML 1.1 tables, Warp default preset, custom preset matched by URL, import success, missing keys and no keys |
| `tests/test_shell.py` | 8 | Real Bash in a PTY: acknowledged command loading and exit status, cwd/env persistence, multiline Unicode, heredoc, native `read` and interrupt, alias reporting, existing prompt arrays, existing DEBUG trap falls back to native |
| `tests/test_isolation.py` | 4 | Worker raises its own `oom_score_adj`, never lowers it, starts with the raised score; integration script raises the shell's |
| `tests/test_images.py` | 27 | Image context: attachments loaded as bytes with the type sniffed from them, text attachments unchanged, the per-image cap, multimodal content parts and their base64 data URLs, `relay_*` keys never reaching the wire, an image estimated as a constant, which models read images, the GLM-5.3 → GLM-5.3-Flash swap and the swap back (including after a failed turn), the refusal when nothing can read images, a configured vision model, and the replacement of each image by its description and path once the turn is over |
| `tests/test_version.py` | 1 | `relay_core.__version__` matches `project(Relay VERSION …)` in `CMakeLists.txt` |
| **Total** | **127** | |

Qt tests:

| File | Tests |
|---|---|
| `tests/editor_test.cpp` | native selection and undo, submission shortcuts and multiline, Shift+click, mouse drag, paste never submits, IME preedit does not submit, history keeps the draft, Up inside multiline text moves the cursor |
| `tests/filepanes_test.cpp` | explorer navigates in and up, filter, hidden files, Enter opens a file, typing starts the filter, filter then Down+Enter opens the match, preview picks a viewer by type, large text truncated with a notice, missing path returns false |
| `tests/images_test.cpp` | image context: type sniffed from the bytes (including a mislabelled file), `@` tokens quoted for paths with spaces, capture names stamped and never colliding, PNG writing and the empty-image refusal, what a paste or a drop carries (file URLs used in place, inline data written to the cache), plain text is not an image, the cache sweep takes only old `relay-*` captures, and the 3 MiB cap matches the worker's |

Provider tests use a local mock server and synthetic responses. They do not validate real
accounts, quotas, billing or model behavior.

### CI and packaging checks (configured)

| Workflow | Checks |
|---|---|
| `.github/workflows/ci.yml` `ubuntu-qt5` | Ubuntu 24.04 Qt5/KF5 build, all ctest groups, staged install layout, `desktop-file-validate`, `appstreamcli validate` |
| `.github/workflows/ci.yml` `debian-qt6` | Debian 13 Qt6/KF6 build, ctest and `.deb` in a container (`packaging/deb/build-deb.sh`) |
| `.github/workflows/release.yml` | Per tag: `.deb` for Ubuntu 24.04, Debian 13, Ubuntu 26.04 on amd64 and arm64, each installed in a fresh container and smoke-tested (`packaging/smoke-installed.sh`: files, `--version`, worker `ready`, KonsolePart plugin, GUI start under Xvfb offscreen and xcb) |

These workflows exist in the repository. Their results on GitHub were not checked when this
page was written, and no release tag has been pushed.

## Verified live (implementer checks)

These are runs of the real app, mostly under Xvfb with `xdotool`, by the implementing
Claude session. They show the feature worked once; they are not independent QA.

| Feature | Evidence folder |
|---|---|
| Inline agent output, invalid command to agent, fix loop | [`qa_evidence/2026-09-17-inline-agent-output/`](qa_evidence/2026-09-17-inline-agent-output/) |
| Transcript panel while a program runs | [`qa_evidence/2026-09-17-agent-output-while-program-runs/`](qa_evidence/2026-09-17-agent-output-while-program-runs/) |
| Program context sent to the agent | [`qa_evidence/2026-09-17-agent-program-context/`](qa_evidence/2026-09-17-agent-program-context/) |
| PageUp/PageDown from the composer | [`qa_evidence/2026-09-17-composer-page-scroll/`](qa_evidence/2026-09-17-composer-page-scroll/) |
| Human/agent control, password prompt | [`qa_evidence/2026-09-17-control-and-passwords/`](qa_evidence/2026-09-17-control-and-passwords/) |
| Ctrl+I input toggle | [`qa_evidence/2026-09-17-ctrl-i-input-toggle/`](qa_evidence/2026-09-17-ctrl-i-input-toggle/) |
| Per-program control policy | [`qa_evidence/2026-09-17-program-control-policy/`](qa_evidence/2026-09-17-program-control-policy/) |
| File explorer and preview panes | [`qa_evidence/2026-09-17-file-panes/`](qa_evidence/2026-09-17-file-panes/) |
| Palette, presets, agent `set_keybinding`, copy on select | [`qa_evidence/2026-09-17-keyboard-and-palettes/`](qa_evidence/2026-09-17-keyboard-and-palettes/) |
| Windows, tabs, panes, restore | [`qa_evidence/2026-09-17-windows-tabs-panes/`](qa_evidence/2026-09-17-windows-tabs-panes/) |
| Relay's own title bar (move, resize, maximize) and the notification bell | [`qa_evidence/2026-09-17-window-header/`](qa_evidence/2026-09-17-window-header/) |
| Per-pane systemd scopes, OOM banners, restart | [`qa_evidence/2026-09-17-pane-isolation/`](qa_evidence/2026-09-17-pane-isolation/) |
| Agent queue strip, remove, interrupt, pause/resume | [`qa_evidence/2026-09-17-queue-interrupt-gui/`](qa_evidence/2026-09-17-queue-interrupt-gui/) |
| libvterm engine spike (vim, less, htop, tmux, throughput) | [`qa_evidence/2026-09-17-engine-spike/`](qa_evidence/2026-09-17-engine-spike/), report in [ENGINE-SPIKE.md](ENGINE-SPIKE.md) |
| Terminal-first fallback (superseded behavior) | [`qa_evidence/2026-09-17-terminal-first-fallback/`](qa_evidence/2026-09-17-terminal-first-fallback/) |
| Image context: paste, pane screenshot, the GLM-5.3-Flash swap and back, the vision-model row | [`qa_evidence/2026-09-17-image-context/`](qa_evidence/2026-09-17-image-context/) |

Live provider smoke test, 2026-09-16, through the real `Agent` loop with keys imported from
Warp: Kimi K3 (`kimi`), GLM-5.3 Coding Plan (`glm-coding`) and DeepSeek V4.1 Flash via
OpenRouter (`openrouter`) each returned a plain reply and completed one `list_directory` tool
round trip, with the reasoning field retained. Per-action approval still existed at the time;
the tool was auto-approved for the test. The `glm` standard endpoint was not tested.
Several GUI checks above also used Kimi K3 as the live provider.

Image context (issue EM1E), 2026-09-17, live with the stored keys: on GLM-5.3 Coding Plan a prompt
carrying a PNG was served by `glm-5.3-flash` and answered from the picture, and the pane was back on
`glm-5.3` for the next turn (once through the worker API, once through the GUI). On OpenRouter
(`deepseek/deepseek-v4.1-flash`) the same prompt was refused with a message and nothing was sent.
Transcripts in [`qa_evidence/2026-09-17-image-context/`](qa_evidence/2026-09-17-image-context/).

Bugs found only by running the real app:

- Worker messages written before `QProcess` reported `Running` were dropped (fixed: buffered until `started`).
- `tcgetpgrp()` on the pane PTY returns `ENOTTY`; readiness now reads `tpgid` from `/proc/<pid>/stat`.
- Relay shortcuts stole keys from vim, for example Ctrl+W (addressed by `program_keys`).
- Interactive Bash ignores SIGTERM, so a stopped scope hung (fixed: `KillSignal=SIGHUP`); swap
  delayed memory kills (fixed: swap caps).
- The prompt redraw landed inside the next queued turn's output (fixed: redraw only when idle).

## Not verified

| Area | Why it matters |
|---|---|
| **Qt6/KF6 app on a real KDE Plasma desktop** | All GUI checks ran on Qt5/KF5 under Xvfb. The inline-output D-Bus hook, clipboard slots, hidden scrollbar and profile loading depend on KonsolePart internals that may differ in Konsole 24.02+. |
| **Wayland** | No Wayland session was tested: focus, Alt+Tab, notifications, clipboard, IME. The frameless window's move and resize go through `startSystemMove` / `startSystemResize`, which is the only supported path there. |
| **Window manager drags** | The frameless title bar was exercised under Xvfb, which has no WM, so only Relay's own fallback move/resize ran. WM snapping, tiling, minimize and real maximizing are unchecked. |
| **Real systemd-oomd kill** | Isolation was tested with scope `MemoryMax` limits and a manual `systemctl kill`, not with systemd-oomd acting under real memory pressure. |
| **IME** | Only the editor's preedit guard is unit-tested. No fcitx or ibus session was used. |
| **amd64 packages outside CI** | The development machine is arm64. amd64 `.deb`s are built only by the release workflow, which has not run. There is no recorded install of a `.deb` or AUR package on a real desktop. |
| HiDPI, light desktop themes, accessibility | Not checked |
| Zsh, Fish, SSH, tmux as the pane shell | Native input only; rich integration is Bash only |
| User prompt frameworks | Only a pre-existing DEBUG trap (native fallback) is tested |
| `glm` standard endpoint; other OpenAI-compatible providers | Not live-tested |
| Relay's own launch path for inherited `SIG_IGN` | The spike found this bug class in its launcher (see [ENGINE-SPIKE.md](ENGINE-SPIKE.md)); Relay's KonsolePart path was not checked |

## Security boundaries

- Agent tools run **without per-action approval** (removed 2026-09-17). Every action is
  previewed inline as it starts; Stop agent cancels but does not undo.
- `run_command` is not sandboxed. It has the user's filesystem and network access.
  Workspace and secret-file guards apply only to the file tools.
- Text the agent reads (files, command output, skills) can try to steer it.
- The shell bridge's token and file permissions protect against accidental cross-session
  events, not hostile same-user processes.

## The QA lane

Implemented features wait in `needs_qa_llm/` until a QA session from a **non-Claude** model
family runs the issue's checklist and records evidence under `docs/qa_evidence/`. See
[`issues/README.md`](../issues/README.md). No issue has passed this lane yet.

Waiting for QA (`issues/features/needs_qa_llm/`):

- `2026-09-17-agent-output-while-program-runs.md`
- `2026-09-17-agent-program-context.md`
- `2026-09-17-agent-responses-in-terminal.md`
- `2026-09-17-ctrl-i-input-toggle.md`
- `2026-09-17-file-explorer-and-preview-panes.md`
- `2026-09-17-fix-and-rerun-terminal-commands.md`
- `2026-09-17-human-agent-control-and-password-prompts.md`
- `2026-09-17-image-context.md`
- `2026-09-17-keyboard-shortcuts-and-palettes.md`
- `2026-09-17-load-global-warp-skills.md`
- `2026-09-17-model-roles-and-fast-agent.md`
- `2026-09-17-model-roles.md`
- `2026-09-17-pane-process-isolation.md`
- `2026-09-17-program-control-policy.md`
- `2026-09-17-queue-or-interrupt-agent-prompts.md`
- `2026-09-17-request-ledger-todos-completion.md`
- `2026-09-17-restore-windows-on-start.md`
- `2026-09-17-router-english-commands.md`
- `2026-09-17-routing-assist-thinking-skills-backend.md`
- `2026-09-17-voice-transcription.md`
- `2026-09-17-windows-tabs-panes.md`

Waiting for QA (`issues/changes/needs_qa_llm/`):

- `2026-09-17-composer-page-scroll.md`
- `2026-09-17-pre-submit-run-check.md`
- `2026-09-17-terminal-not-directly-typable.md`

Closed (`issues/features/done/`): `2026-09-17-review-opencode-agent-design.md` (research) and
`2026-09-17-terminal-first-agent-fallback.md` (superseded before QA).

[RELEASING.md](RELEASING.md) requires every feature shipped in a beta to have passed QA or be
listed as a known issue.
