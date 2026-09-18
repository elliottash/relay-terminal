# Relay validation status

Last updated 2026-09-17. Status: **Linux beta in preparation.** Nothing here is a QA verdict
from an independent model; see [The QA lane](#the-qa-lane).

Reference machine for local runs: Ubuntu 24.04.5, aarch64, kernel 7.0, Python 3.12.3,
Qt 5.15.13 (KDE Frameworks 5.115 for the optional syntax highlighter). KonsolePart, used up to
2026-09-18, is no longer a dependency.

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
| `tests/test_aliases.py` | 34 | Aliases: `{{arg}}` substitution and defaults, a value with shell metacharacters staying one literal word (checked against a real interactive bash, not only against our reading), quoted placeholders, prompts substituted as plain text, positional arguments, the card round trip including a default holding commas, braces and backticks, name and kind validation, the `## Parameters` grammar, global vs local resolution and precedence with shadowing, the `.relay/` fallback, an unreadable card becoming a problem, `relay-board.py check` accepting a saved alias, and repeated-command detection |
| `tests/test_alias_import.py` | 22 | Importing: Warp workflows out of a copied read-only database (with their parameters and an `agent_mode` workflow importing as a prompt), workflow YAML, shell aliases with bash's own quote escape, malformed rows / YAML / alias lines each skipped with a reason, a database that is not one, a symlinked startup file, nothing executed and nothing written by a preview, an apply writing only what was chosen, a name that was not previewed refused, renaming on import, the conflict and warning fields, and this machine's real Warp workflows when Warp is installed |
| `tests/test_alias_protocol.py` | 18 | The section-20 messages: defining an alias and the list that follows, a bad definition refused with a sentence, delete, the list readable with no provider configured, the palette / `/name` / typed-name paths all expanding the same way, defaults, a metacharacter value quoted before it reaches the GUI, a missing required value, a prompt alias, local beating global, the preview writing nothing and the apply writing what was chosen, an apply without a preview, and the agent's alias suggestion (proposed, rejected, and no repeats meaning no model call) |
| `tests/test_images.py` | 27 | Image context: attachments loaded as bytes with the type sniffed from them, text attachments unchanged, the per-image cap, multimodal content parts and their base64 data URLs, `relay_*` keys never reaching the wire, an image estimated as a constant, which models read images, the GLM-5.3 → GLM-5.3-Flash swap and the swap back (including after a failed turn), the refusal when nothing can read images, a configured vision model, and the replacement of each image by its description and path once the turn is over |
| `tests/test_version.py` | 1 | `relay_core.__version__` matches `project(Relay VERSION …)` in `CMakeLists.txt` |
| **Total** | **201** | |

Qt tests:

| File | Tests |
|---|---|
| `tests/editor_test.cpp` | native selection and undo, submission shortcuts and multiline, Shift+click, mouse drag, paste never submits, IME preedit does not submit, history keeps the draft, Up inside multiline text moves the cursor |
| `tests/settingspane_test.cpp` | Settings pane: tabs follow the catalog and skip headings and info lines, ← → switch tabs from the search (wrapping), ↑ ↓ move the highlight without wrapping, Enter flips the highlighted toggle and the rebuild keeps the tab and the highlight, search mixes settings rows and actions (a submenu entry found through its parent, an alias word, nothing matching), Enter hands an action to the owner (or runs it itself), the Actions tab lists Recent then sections with submenus inline, Esc clears then closes and from a control goes back to the search first, a rebuild keeps the tab, the scroll offset and the focused control, every control writes through its row, fuzzy scoring |
| `tests/filepanes_test.cpp` | explorer navigates in and up, filter, hidden files, Enter opens a file, typing starts the filter, filter then Down+Enter opens the match, preview picks a viewer by type, large text truncated with a notice, missing path returns false |
| `tests/aliases_test.cpp` | aliases: name and slug rules matching the worker's, a template rendered into composer fields with its defaults, Tab and Shift+Tab walking and wrapping, typing into a field moving the later ones, the values sent to the worker, a field still showing its own name counting as unfilled, reading the values back out of an edited line and refusing a line that was rewritten, a trailing field running to the end, `/name` never shadowing a built-in, a typed name matching only when it is exactly an alias (not a path, a prefix, an assignment or a longer word), a shadowed global alias not being a name, the palette row text, and the fast-path hint |
| `tests/images_test.cpp` | image context: type sniffed from the bytes (including a mislabelled file), `@` tokens quoted for paths with spaces, capture names stamped and never colliding, PNG writing and the empty-image refusal, what a paste or a drop carries (file URLs used in place, inline data written to the cache), plain text is not an image, the cache sweep takes only old `relay-*` captures, and the 3 MiB cap matches the worker's |

Provider tests use a local mock server and synthetic responses. They do not validate real
accounts, quotas, billing or model behavior.

### CI and packaging checks (configured)

| Workflow | Checks |
|---|---|
| `.github/workflows/ci.yml` `ubuntu-qt5` | Ubuntu 24.04 Qt5/KF5 build, all ctest groups, staged install layout, `desktop-file-validate`, `appstreamcli validate` |
| `.github/workflows/ci.yml` `debian-qt6` | Debian 13 Qt6/KF6 build, ctest and `.deb` in a container (`packaging/deb/build-deb.sh`) |
| `.github/workflows/release.yml` | Per tag: `.deb` for Ubuntu 24.04, Debian 13, Ubuntu 26.04 on amd64 and arm64, each installed in a fresh container and smoke-tested (`packaging/smoke-installed.sh`: files, `--version`, worker `ready`, GUI start under Xvfb offscreen and xcb) |

These workflows exist in the repository. Their results on GitHub were not checked when this
page was written, and no release tag has been pushed.

## Verified live (implementer checks)

These are runs of the real app, mostly under Xvfb with `xdotool`, by the implementing
Claude session. They show the feature worked once; they are not independent QA.

| Feature | Evidence folder |
|---|---|
| Aliases and workflows: defining one, running it from the palette, `/name` and the typed name, and the import preview against this machine's real Warp workflows | [`qa_evidence/2026-09-17-aliases-and-workflows/`](qa_evidence/2026-09-17-aliases-and-workflows/) |
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
| Model-written pane titles, tab labels, `/rename`, `/rename-tab` (loopback stub and, separately, a real model) | [`qa_evidence/2026-09-17-pane-title-summary/`](qa_evidence/2026-09-17-pane-title-summary/) |
| Colour themes: four built-ins switched in one running process, every light surface, the Relay engine and a user theme | [`qa_evidence/2026-09-17-color-themes/`](qa_evidence/2026-09-17-color-themes/) |
| An unknown `/command` answered by Relay (with a suggestion), `/help`, a real command, and `/bin/echo` and `/tmp` still going to the shell | [`qa_evidence/2026-09-18-unknown-slash-command/`](qa_evidence/2026-09-18-unknown-slash-command/) |
| Terminal scrollback surviving a quit and restart: 120 lines printed, Relay quit, reopened with the text back, scrollable, and a second quit saving the restored text with the new output | [`qa_evidence/2026-09-18-scrollback-survives-restart/`](qa_evidence/2026-09-18-scrollback-survives-restart/) |

Live provider smoke test, 2026-09-16, through the real `Agent` loop with keys imported from
Warp: Kimi K3 (`kimi`), GLM-5.3 Coding Plan (`glm-coding`) and DeepSeek V4.1 Flash via
OpenRouter (`openrouter`) each returned a plain reply and completed one `list_directory` tool
round trip, with the reasoning field retained. Per-action approval still existed at the time;
the tool was auto-approved for the test. The `glm` standard endpoint was not tested.
Several GUI checks above also used Kimi K3 as the live provider. The pane-title run
(2026-09-17) added GLM-5.3 Coding Plan as the pane's model with the chores role falling through
to the Lite tier on OpenRouter, and confirmed that the header and the tab label fill with text a
real model wrote.

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
| **Qt 6 at all** | Every check ran on Qt 5. Nothing has been built against Qt 6 since KonsolePart was retired, and the build stops at `qsizetype` narrowing (`src/ScreenPrompt.cpp`). |
| **A real KDE Plasma desktop** | All GUI checks ran under Xvfb. |
| **Wayland** | No Wayland session was tested: focus, Alt+Tab, notifications, clipboard, IME. The frameless window's move and resize go through `startSystemMove` / `startSystemResize`, which is the only supported path there. |
| **Window manager drags** | The frameless title bar was exercised under Xvfb, which has no WM, so only Relay's own fallback move/resize ran. WM snapping, tiling, minimize and real maximizing are unchecked. |
| **Real systemd-oomd kill** | Isolation was tested with scope `MemoryMax` limits and a manual `systemctl kill`, not with systemd-oomd acting under real memory pressure. |
| **IME** | Only the editor's preedit guard is unit-tested. No fcitx or ibus session was used. |
| **amd64 packages outside CI** | The development machine is arm64. amd64 `.deb`s are built only by the release workflow, which has not run. There is no recorded install of a `.deb` or AUR package on a real desktop. |
| HiDPI, light desktop themes, accessibility | Not checked |
| Zsh, Fish, SSH, tmux as the pane shell | Native input only; rich integration is Bash only |
| User prompt frameworks | Only a pre-existing DEBUG trap (native fallback) is tested |
| `glm` standard endpoint; other OpenAI-compatible providers | Not live-tested |
| Inherited `SIG_IGN` outside the engine's own launcher | The spike found this bug class (see [ENGINE-SPIKE.md](ENGINE-SPIKE.md)); `PtyUnix.cpp` resets dispositions and is tested, other launch paths are not |

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

- `2026-09-17-agent-delegate-and-take-over.md`
- `2026-09-17-agent-output-while-program-runs.md`
- `2026-09-17-agent-program-context.md`
- `2026-09-17-agent-responses-in-terminal.md`
- `2026-09-17-clickable-paths.md`
- `2026-09-17-aliases-and-workflows.md`
- `2026-09-17-color-themes.md`
- `2026-09-17-ctrl-i-input-toggle.md`
- `2026-09-17-file-explorer-and-preview-panes.md`
- `2026-09-17-fix-and-rerun-terminal-commands.md`
- `2026-09-17-human-agent-control-and-password-prompts.md`
- `2026-09-17-image-context.md`
- `2026-09-17-keyboard-jump-to-output-links.md`
- `2026-09-17-keyboard-shortcuts-and-palettes.md`
- `2026-09-17-load-global-warp-skills.md`
- `2026-09-17-model-roles-and-fast-agent.md`
- `2026-09-17-model-roles.md`
- `2026-09-17-pane-process-isolation.md`
- `2026-09-17-pane-title-summary.md`
- `2026-09-17-program-control-policy.md`
- `2026-09-17-queue-or-interrupt-agent-prompts.md`
- `2026-09-17-request-ledger-todos-completion.md`
- `2026-09-17-restore-windows-on-start.md`
- `2026-09-17-router-english-commands.md`
- `2026-09-17-routing-assist-thinking-skills-backend.md`
- `2026-09-17-screen-text-input-detection.md`
- `2026-09-17-voice-transcription.md`
- `2026-09-17-windows-tabs-panes.md`

Waiting for QA (`issues/changes/needs_qa_llm/`):

- `2026-09-17-composer-page-scroll.md`
- `2026-09-17-pre-submit-run-check.md`
- `2026-09-17-terminal-not-directly-typable.md`
- `2026-09-18-unknown-slash-command.md`
- `2026-09-18-scrollback-survives-restart.md`
- `2026-09-18-output-token-limit-defaults-to-32k.md`

Closed (`issues/features/done/`): `2026-09-17-review-opencode-agent-design.md` (research) and
`2026-09-17-terminal-first-agent-fallback.md` (superseded before QA).

[RELEASING.md](RELEASING.md) requires every feature shipped in a beta to have passed QA or be
listed as a known issue.
