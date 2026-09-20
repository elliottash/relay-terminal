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

**Current totals (2026-09-18, this checkout):** `./scripts/test.sh` **883 passed** and
`ctest --test-dir build` **29/29**, both clean. Neither is current: `python3 -m unittest discover
-s tests` **collects 3,350 cases in 103 modules** as of 2026-09-19 (how many pass is what the command
reports, and it has not been re-run here). `ctest` registers **56** tests, not 29
(`grep -c add_test CMakeLists.txt engine/CMakeLists.txt`) — `guestbridge`, `projectinit`,
`striplayout`, `pulsepaint`, `panestatus`, `boardworkspace`, `diffview` and `sharing` among
them. The three rows above are the 2026-09-17 snapshot; the two commands are the source of
truth for pass/fail.

**Since 2026-09-20 the live inventory is the Test suites pane** (card #7BM4, Switchboard tool
row → Tests): it discovers every ctest and unittest test in the checkout in about half a second
(`relay_core.test_probe`), shows each one's runs, reliability, p50/p95 and last failure from the
execution store `issues/.private/tests/history.jsonl` (`relay_core.test_history`, gitignored),
and lists the cards that name it. `scripts/relay-remote-tests --qt 6` runs a committed revision
on sphinxpad and its JUnit XML lands in the same store with `host: sphinxpad`. The tables below
are the last hand-taken snapshot and are kept for the record; the pane, `./scripts/test.sh` and
`ctest --test-dir build` are the source of truth.

The per-suite inventory below now names **every** module under `tests/` and `engine/tests/`
(2026-09-19): 99 Python modules, 54 Qt suites and 7 engine suites, no module left out. It is
generated rather than hand-kept, so it can be re-taken instead of edited:

- a Python module's count is what `unittest` collects from it (`loadTestsFromName`), which is what
  `./scripts/test.sh` then runs; a `subTest` inside a case is not a second case;
- a C++ count is the suite's test slots, excluding `initTestCase` / `cleanupTestCase` / `init` /
  `cleanup` and a `_data` provider. A data-driven slot runs **once per data row**, so QTest's own
  count for such a suite is higher than the number here;
- a description is the module's own docstring or header comment where the row did not already have
  a hand-written one.

The counts are what the source declares, not a pass report: the two commands above are the only
thing that says a test passes.

Backend tests by module:

| Module | Cases | What they check |
|---|---:|---|
| `tests/test_agent.py` | 30 | Commands and writes run without approval and show previews, unknown tools refused, file tools confined, cancel during a command, route without a provider, configure makes no network call and never echoes the key, malformed requests, program context note (labelled, validated, control characters stripped, passed through the queue) |
| `tests/test_agents_defs.py` | 12 | Subagent definitions on disk: discovery, the frontmatter a definition may carry, precedence between the project's and the user's, and what an invalid one reports |
| `tests/test_alias_import.py` | 22 | Importing: Warp workflows out of a copied read-only database (with their parameters and an `agent_mode` workflow importing as a prompt), workflow YAML, shell aliases with bash's own quote escape, malformed rows / YAML / alias lines each skipped with a reason, a database that is not one, a symlinked startup file, nothing executed and nothing written by a preview, an apply writing only what was chosen, a name that was not previewed refused, renaming on import, the conflict and warning fields, and this machine's real Warp workflows when Warp is installed |
| `tests/test_alias_protocol.py` | 18 | The section-20 messages: defining an alias and the list that follows, a bad definition refused with a sentence, delete, the list readable with no provider configured, the palette / `/name` / typed-name paths all expanding the same way, defaults, a metacharacter value quoted before it reaches the GUI, a missing required value, a prompt alias, local beating global, the preview writing nothing and the apply writing what was chosen, an apply without a preview, and the agent's alias suggestion (proposed, rejected, and no repeats meaning no model call) |
| `tests/test_aliases.py` | 34 | Aliases: `{{arg}}` substitution and defaults, a value with shell metacharacters staying one literal word (checked against a real interactive bash, not only against our reading), quoted placeholders, prompts substituted as plain text, positional arguments, the card round trip including a default holding commas, braces and backticks, name and kind validation, the `## Parameters` grammar, global vs local resolution and precedence with shadowing, the `.relay/` fallback, an unreadable card becoming a problem, `relay-board.py check` accepting a saved alias, and repeated-command detection |
| `tests/test_board.py` | 92 | Switchboard format tests: cards, ids, ranks, task markers, threads, check, migrate |
| `tests/test_board_import.py` | 50 | `relay_core.board_import`: proposing cards from a tracker, and writing the accepted ones |
| `tests/test_board_protocol.py` | 111 | Switchboard worker protocol (docs/AGENT-SESSIONS-PROTOCOL.md section 17) and the `board_*` tools' wiring into the agent |
| `tests/test_board_tools.py` | 125 | Switchboard agent tools: the six `board_*` tools, their refusals and their guardrails |
| `tests/test_configure_provider.py` | 15 | How `configure` / `set_model` turn a request into a provider, and what a refused key reads like |
| `tests/test_conv_index.py` | 90 | Conversation index and the conversation-list protocol commands (protocol section 14) |
| `tests/test_cpace.py` | 15 | CPACE-X25519-SHA512 (remote/cpace.py and app/cpace.js), card #97EG |
| `tests/test_failover.py` | 28 | Failover (card #G9VE): a turn whose provider keeps failing continues on another one |
| `tests/test_forge_github.py` | 40 | The GitHub provider: REST shape, pagination, conditional requests, rate limits, credentials |
| `tests/test_forge_sync.py` | 106 | Two-way sync between Switchboard cards and GitHub issues (`#GDQN`) |
| `tests/test_gateway.py` | 27 | The Relay Free gateway, end to end in one process: a fake model provider on loopback, the real gateway on a real port, and a client that registers, streams and gets refused the way the desktop will |
| `tests/test_guest.py` | 22 | Guest agent panes (protocol 26): detecting Claude Code and Codex, the `guest` block, the env injected at launch and the shim's own contract |
| `tests/test_guest_bridge.py` | 117 | The Claude IDE bridge (protocol 26.5): the lock file, the JSON-RPC/tool layer, and the blocking openDiff — the last two frame by frame over a real WebSocket, with a hand-rolled client standing in for claude (no claude process is ever started) |
| `tests/test_guest_codex.py` | 92 | Codex as a guest: the rollout tail, the `notify` hook and the retired settings writer kept as the launch's legacy cleanup (protocol 26.6, 26.9) |
| `tests/test_guest_harness_claude.py` | 68 | Tier A: the `claude -p` stream-json harness adapter, replayed from recorded transcripts (protocol 29.2) |
| `tests/test_guest_harness_codex.py` | 55 | Tier A: the `codex app-server` harness adapter, replayed from recorded transcripts (protocol 29.2) |
| `tests/test_guest_harness_provider.py` | 48 | Tier A: the worker's `HarnessProvider`, guest presets, configure/set_model/resume and the event translation, against a scripted fake harness (protocol 29.3) |
| `tests/test_guest_hook.py` | 38 | The Claude guest shim (GT7X, protocol 26.3/26.4) |
| `tests/test_guest_install.py` | 21 | The hook and statusline entries (`relay_entries`) and the retired installer's remove path, kept as the launch's legacy cleanup (GT7X, protocol 26.4, 26.9) |
| `tests/test_guest_launch.py` | 31 | Launch-time configuration for a guest picked in the model picker: the per-launch settings file, the claude/codex command lines, the bypass flags and the legacy cleanup (protocol 26.9) |
| `tests/test_guest_sessions.py` | 104 | Guest sessions: the `claude` and `codex` conversation sources (protocol 26.7) |
| `tests/test_guest_slash.py` | 5 | A guest's slash catalog: what the shim writes, what the composer popup is offered, and what a guest's own `/command` does |
| `tests/test_hosted.py` | 22 | Relay Free's client half (protocol 13.9): identity, token cache, the hosted transport, the worker |
| `tests/test_images.py` | 30 | Image context: attachments loaded as bytes with the type sniffed from them, text attachments unchanged, the per-image cap, multimodal content parts and their base64 data URLs, `relay_*` keys never reaching the wire, an image estimated as a constant, which models read images, the GLM-5.3 → GLM-5.3-Flash swap and the swap back (including after a failed turn), the refusal when nothing can read images, a configured vision model, and the replacement of each image by its description and path once the turn is over |
| `tests/test_isolation.py` | 4 | Worker raises its own `oom_score_adj`, never lowers it, starts with the raised score; integration script raises the shell's |
| `tests/test_jobs.py` | 23 | run_command jobs: a command outlives its timeout, is read and stopped by id, and ends with its conversation (relay_core/jobs.py) |
| `tests/test_keybindings.py` | 22 | Key normalization and validation, a tool schema that carries no catalog at all and does not move when a key is rebound (#GMCF), an unknown id refused with the closest ids, atomic write that keeps other content and reports conflicts, unbind, invalid existing file left alone, configure with and without a catalog, `keybindings` update keeps the conversation |
| `tests/test_keystore.py` | 24 | Preset URL matching, secrets passed on stdin, env var overrides keyring, bad ids and keys, Warp TOML 1.1 tables, Warp default preset, custom preset matched by URL, import success, missing keys and no keys |
| `tests/test_keytest.py` | 2 | The keys modal's Test button against a provider that says "not now" (protocol 13.8) |
| `tests/test_land.py` | 58 | `scripts/land.py`: landing one session's hunks on the shared checkout — the snapshot/tip/working-copy three-way merge, another session's uncommitted edit left untouched, two sessions in one file, `main` moving under the commit, a conflict that aborts, `--whole`, the intake files, the shared-index update, `doctor` and the pre-commit hook. Then the 2026-09-19 faults: contested-hunk detection from the markers `begin` leaves in the other session's data, the confirm-digest gate and a digest invalidated by a later edit or a moved tip, `--exclude-hunk` / `--only-hunk` landing a subset and a later commit picking the rest up, the stale-snapshot gate, `--contact` in every warning, `who` and idle sessions, `begin --base`/`--from-head`, `repair` with a later commit on the same path and `repair` conflicting, and the build gate (a tiny CMake project whose exact would-be tree does not compile while the working tree does, an incremental second verify, and `main` moving mid-verify) |
| `tests/test_local_keyless.py` | 18 | A model server on this machine has no key: configure, the role table, the Test button and the `presets` event (card #24XJ) |
| `tests/test_local_tier.py` | 26 | The Local tier and the `local` pane role (card #JH22, protocol 13.7) |
| `tests/test_localmodels.py` | 25 | Model servers on this machine: the registry file, the probe, and the worker messages (backend/relay_core/localmodels.py, protocol section 28) |
| `tests/test_localsmoke.py` | 13 | The five smoke checks (`relay-local.py smoke`), against scripted servers |
| `tests/test_localtext.py` | 15 | Reasoning tags and text tool calls in a local model's content (backend/relay_core/localtext.py) |
| `tests/test_logs.py` | 8 | Rotating diagnostics log (issue SQAM): location, permissions, rotation and what must never be in it |
| `tests/test_model_switch.py` | 14 | Changing the model while the agent is working (issue 3ES1) |
| `tests/test_pane_view.py` | 7 | The pane view (app/pane.js) in a real browser, against the fixtures the desktop would send |
| `tests/test_plan_turns.py` | 11 | Plan-mode turns (owner, 2026-09-19): the planning role serves them, for that turn only |
| `tests/test_presets.py` | 24 | Provider presets, the Main/Flash/Lite tier table, and the GUI mirror in src/Pane.h |
| `tests/test_program_input.py` | 40 | `type_into_program`: the tool, its per-turn grant, its caps and every refusal |
| `tests/test_project_probe.py` | 89 | `relay_core.project_probe`: what Relay can see in a project before it may do anything |
| `tests/test_provider.py` | 45 | Local HTTP fixture: text and Unicode, fragmented tool arguments and reasoning, multiple tool calls, truncated and malformed streams, cancel, configuration guards, JSON fallback, redirect refusal, sanitized HTTP errors, OpenRouter `reasoning` kept but not displayed |
| `tests/test_provider_local.py` | 22 | The transport against a model server on this machine (ProviderConfig.local), and the proof that a hosted provider is treated exactly as before |
| `tests/test_qa_verifiers.py` | 39 | Cross-provider QA (card #T71W): the verifier signature, the family table, the lineage and the ranking. Availability is passed in, so nothing reads PATH, the keyring or the local-endpoint registry |
| `tests/test_questions.py` | 31 | `ask_user`: validation, open and multiple-choice questions, the round trip, skipping, Stop |
| `tests/test_queue.py` | 23 | Ordered turns without overlap, `now` refused while busy, remove and clear, interrupt while streaming, idle and during a tool, FIFO interrupts, cancel pauses until resume, `now` while paused, cancel drops pending interrupts, configure/reset rules, validation, protocol errors, failed turn pauses |
| `tests/test_relay_build.py` | 6 | `scripts/relay-build`: the build lock (a second session waits and is told whose build it is, with a `--wait-seconds` cap), the artifacts stamped back to the build's start, and the header edited mid-compile that plain `cmake --build` misses and the wrapper then rebuilds. Throwaway CMake projects with a deliberately slow compile; the shared `build/` is never touched |
| `tests/test_relay_free_e2e.py` | 4 | Relay Free end to end: the desktop's own client against the real gateway in one process |
| `tests/test_remote_audit.py` | 5 | The remote-share audit log (remote/audit.py): capped parts, nothing rotated away |
| `tests/test_remote_browser.py` | 7 | The real web client, in a real browser, against the real rendezvous and host |
| `tests/test_remote_cloudflare.py` | 14 | The public link: a cloudflared quick tunnel (remote/cloudflare.py) |
| `tests/test_remote_control.py` | 59 | Multiplayer, part two: presence, the keyboard, guest prompts and pause (sections 10.3 to 10.6) |
| `tests/test_remote_guest_browser.py` | 14 | The guest's web client, in a real browser, against the real rendezvous and hub (section 10) |
| `tests/test_remote_guests.py` | 58 | Multiplayer: invites, knocking, and what a guest can and cannot reach (section 10) |
| `tests/test_remote_gui_host.py` | 24 | The sidecar Relay runs when you share a pane (remote/gui_host.py) |
| `tests/test_remote_host.py` | 26 | End to end: a client pairs through the rendezvous and drives a pane (RRP/1) |
| `tests/test_remote_hosted_address.py` | 12 | The hosted address (remote/gui_host.py): links through relay-terminal.ai instead of this machine |
| `tests/test_remote_meetcode.py` | 23 | Join with a meeting code and a PIN (card #97EG): the routes, the code phase and the handoff |
| `tests/test_remote_noise.py` | 8 | Noise_IK_25519_AESGCM_SHA256 (docs/REMOTE-PROTOCOL.md section 4) |
| `tests/test_remote_pane_state.py` | 36 | One pane model, two views: ``pane_state`` and the actions a phone sends back (section 16) |
| `tests/test_remote_push.py` | 62 | Web Push, end to end (docs/REMOTE-PROTOCOL.md section 9) |
| `tests/test_remote_security.py` | 55 | The security review of the four things P0's review did not cover (#W5N2) |
| `tests/test_remote_tab_share.py` | 11 | Share whole tab (owner, 2026-09-18): "the partner gets access to all panes in the tab, so you can add more panes and they immediately get access to the workspace" |
| `tests/test_remote_tailnet.py` | 23 | `tailscale serve`: the warning-free address the share dialog offers (remote/tailnet.py) |
| `tests/test_remote_terminal.py` | 12 | A real shell, shared to a real browser: the screen stream and take-over end to end |
| `tests/test_remote_user_agent.py` | 3 | Every request the desktop sends a rendezvous names itself, because Cloudflare insists |
| `tests/test_remote_viewer.py` | 20 | Relay-to-Relay: the laptop's viewer process (remote/viewer.py) against a real desktop hub |
| `tests/test_remote_wire.py` | 41 | The allow-lists, framing and sequencing rules (docs/REMOTE-PROTOCOL.md sections 3, 6 and 7) |
| `tests/test_requests.py` | 43 | Request ledger, todos, drop-path regressions (research G1-G3, G5, G7), carried compaction block, completion check, stale todo reminders and the audit side call. Stub providers only: no network, and the keyring and route_assist.router_provider are patched wherever the audit could reach them |
| `tests/test_roles.py` | 51 | Model roles (protocol 13): validation, per-provider defaults, fallbacks and wiring |
| `tests/test_router.py` | 43 | Shell vs. language routing, explicit destinations and prefixes, live aliases/functions, every command word in pipelines, lists, subshells, groups and substitutions, assignments/wrappers/redirects, path words, syntax errors, control-character guard, validity fields, that parsing and validity checks never execute input, and a 99-input table of commands that are also English words (sentence vs. command) with false-positive sweeps |
| `tests/test_routing_thinking_skills.py` | 29 | Protocol section 11: routing assist, thinking events, turn records, skill refine/import. Fake providers and local git repositories only (no network) |
| `tests/test_security.py` | 33 | The Security section's policy (card #3KB7): the command denylist, extra readable folders and extra secret patterns, as the worker enforces them |
| `tests/test_session_protocol.py` | 30 | Presets/effort table, provider usage, instruction files, and the worker protocol handlers for sessions (docs/AGENT-SESSIONS-PROTOCOL.md). Fake providers and local servers only |
| `tests/test_session_threads.py` | 13 | Session and subagent-thread index, and the session info view's data (cards #Y63Z, #R6J0) |
| `tests/test_sessions.py` | 35 | Agent sessions: model/effort switching, context and compaction, checkpoints, rewind, fork, sessions and recaps, plan mode, attachments. Fake providers only; no network |
| `tests/test_shell.py` | 8 | Real Bash in a PTY: acknowledged command loading and exit status, cwd/env persistence, multiline Unicode, heredoc, native `read` and interrupt, alias reporting, existing prompt arrays, existing DEBUG trap falls back to native |
| `tests/test_skills.py` | 25 | Frontmatter parsing, skipped folders reported, prompt section cap, tools offered only with skills, `load_skill` and `read_skill_file` with refusals, worker configure count, indexing the real `~/.warp/skills` without errors |
| `tests/test_ssh_remote.py` | 46 | Card #S5SH, docs/SSH-AND-MOSH.md sections 4 and 7: the router at a remote prompt, the agent's `remote_session` context, and `host` on run_command and on the file tools — all over the user's own ssh connection |
| `tests/test_ssh_shell.py` | 25 | The ssh/mosh wrapper in shell/integration.bash and shell/remote-integration.sh (card #S5SH) |
| `tests/test_subagents.py` | 21 | Subagents: the restricted executor, what a subagent may and may not do, its transcript, handoffs and the events a pane sees |
| `tests/test_summaries.py` | 22 | Agent-written session summaries (protocol section 18.4) |
| `tests/test_terminal_handoff.py` | 30 | `run_in_terminal`: the tool, the pane's ceiling on it, its cap and every refusal |
| `tests/test_titles.py` | 10 | Model-written pane titles (issue JRWQ, protocol section 18); tab labels are the GUI's own since 2026-09-19 (protocol 18.3) |
| `tests/test_todo_subagents.py` | 10 | Todos mapped to subagents (card #QHR1): a todo handed to a subagent follows it, both ways |
| `tests/test_tool_labels.py` | 57 | Tool-call labels (#TK9C, protocol § 23): the verb and title of every tool, a short command shown whole and a long one shrunk to its program (keeping the subcommand for a multi-command CLI), a pipeline or `&&` chain named by its first real command with the rest counted, the stats (lines, entries, exit code, duration, `+n −m`), `kind`, the `open` target, which calls merge and which never do (a failure, a command, an edit), and the `detail` sections `tool_output_get` replies with |
| `tests/test_tools.py` | 24 | Preparation never executes, output and exit capture, secret env removal, timeouts (including after stdout closes), output cap, read and diff, stale-write refusal, new-file race, path escape and secret guard, absolute and `..` paths confined but allowed inside the workspace, symlink swap, FIFO, cancel before and during a command, unknown tools, create and list, and `edit_file`: a unique replacement, an ambiguous one refused with its count and then done with `replace_all`, each refusal that names the fix, the stale-edit race, the path and secret guards, cancel before an edit, and the added/removed counts a write now reports |
| `tests/test_update.py` | 26 | scripts/relay-update.py decides, offline, which package a machine takes, and which release channel it takes it from |
| `tests/test_version.py` | 1 | `relay_core.__version__` matches `project(Relay VERSION …)` in `CMakeLists.txt` |
| `tests/test_voice.py` | 21 | Voice transcription (protocol 16): what leaves the machine, what comes back, and what the GUI is told when it fails. No audio device and no network are involved — the opener is a fake |
| `tests/test_web_meet_code.py` | 12 | Joining a shared pane with a meeting code and a PIN: the browser's half (card #97EG) |
| `tests/test_web_theme.py` | 7 | app/pane-theme.css is generated from the desktop theme, and must not drift from it |
| `tests/test_web_viewport.py` | 5 | The web client fits the part of the screen that is visible, on-screen keyboard or not |
| **Total** | **3346** | 103 modules, counted by `unittest`'s own collector; `./scripts/test.sh` is the live pass/fail |

Qt tests (one binary per file, `tests/*_test.cpp`; the ctest name is the file stem without
`_test`):

| File | Cases | What it checks |
|---|---:|---|
| `tests/aliases_test.cpp` | 23 | aliases: name and slug rules matching the worker's, a template rendered into composer fields with its defaults, Tab and Shift+Tab walking and wrapping, typing into a field moving the later ones, the values sent to the worker, a field still showing its own name counting as unfilled, reading the values back out of an edited line and refusing a line that was rewritten, a trailing field running to the end, `/name` never shadowing a built-in, a typed name matching only when it is exactly an alias (not a path, a prefix, an assignment or a longer word), a shadowed global alias not being a name, the palette row text, and the fast-path hint |
| `tests/backends_test.cpp` | 10 | The terminal engine's core selection (--engine-core / RELAY_ENGINE_CORE) and the terminal pane's right-click menu (issue #X2F1) |
| `tests/boardmodel_test.cpp` | 43 | The Switchboard pane's pure logic: which section a card falls into, the filter language, the ordering, the row list the one scrolling view draws, and the `#` picker's ranking. No worker, no files, no network |
| `tests/boardworkspace_test.cpp` | 8 | Which project's Switchboard a pane is looking at (src/BoardWorkspace.h). The Switchboard is per project, so the rule has to answer from the candidate directories it is handed and from nothing else — in particular never from the directory Relay itself was started in |
| `tests/buttonfit_test.cpp` | 3 | Button labels have to fit inside their buttons (owner report, 2026-09-18: "Add / replace…" in the API keys modal painted past its own edge). The cause is a Qt rule worth knowing: QStyleSheetStyle only folds a stylesheet rule's font into the widget's font when the rule carries no pseudo-state |
| `tests/calllines_test.cpp` | 38 | the terminal pane's rows, headless (#TK9C): the `relay://call/` and `relay://open-call/` URIs and reading them back, a row cut to the pane's columns so it never wraps, the rewrite-or-new-row state machine, a run of reads merging and being broken by anything else, and the `FoldLine` spans a fold is filled with (detail sections, a merged run's members, the inline diff, the `… N more lines · open in pane` cap) |
| `tests/closedlist_test.cpp` | 7 | The "Recently closed" list widget: newest first, the filter, what unfolding a row shows, and the keys. It is fed records and a fake scrollback reader, so no window and no state directory |
| `tests/closedstack_test.cpp` | 12 | Recently closed panes, tabs and windows: the parts that do not need a window — the record and its JSON, state/closed.json, the cap, the scrollback ids the layout's prune must spare, and the words a list shows |
| `tests/completion_test.cpp` | 8 | `src/Completion.h`: completing a path at the cursor — directories, the shared prefix of several matches, hidden files needing a dot, escaped spaces, a subdirectory, and what is left alone |
| `tests/conversations_test.cpp` | 32 | Pure helpers of the session manager and the find bar (src/Conversations.h), the session manager pane itself, and the ⓘ view's rendering (src/SessionInfo.h) |
| `tests/copyonselect_test.cpp` | 13 | Copy on highlight, the shared filter every read-only pane surface installs (src/CopyOnSelect.h) |
| `tests/diffview_test.cpp` | 19 | `src/DiffView.h`: reading the diff out of a tool preview and numbering both sides of every hunk, including a new file and a hunk that promised more lines than it has, plus the Accept / Reject decision a guest's openDiff hangs in the header (26.5) — answered once, rejected by a replacing diff or a closing view, dropped without an answer by `clearDecision` |
| `tests/editor_test.cpp` | 19 | native selection and undo, submission shortcuts and multiline, Shift+click, mouse drag, paste never submits, IME preedit does not submit, history keeps the draft, Up inside multiline text moves the cursor |
| `tests/fileindex_test.cpp` | 6 | `src/FileIndex.h`: the file index behind completion and the palette — tracked and untracked but not ignored, when a fresh index is rebuilt, a new directory superseding one in flight, the plain-directory walk, and a refresh returning before git does |
| `tests/filepanes_test.cpp` | 31 | explorer navigates in and up, filter, hidden files, Enter opens a file, typing starts the filter, filter then Down+Enter opens the match, preview picks a viewer by type, large text truncated with a notice, missing path returns false |
| `tests/guestbridge_test.cpp` | 6 | The Claude IDE bridge's C++ rules (issue GT7X, protocol 26.5): the environment a pane's shell gets, the two answers' vocabulary — and the rule that decides which reply files Relay may write |
| `tests/hints_test.cpp` | 4 | `src/Hints.h`: the shortcut-hint registry — the per-hint limit, the gap and cooldown, the global setting, "Next time:" and idle tips, and that checking records nothing |
| `tests/images_test.cpp` | 11 | image context: type sniffed from the bytes (including a mislabelled file), `@` tokens quoted for paths with spaces, capture names stamped and never colliding, PNG writing and the empty-image refusal, what a paste or a drop carries (file URLs used in place, inline data written to the cache), plain text is not an image, the cache sweep takes only old `relay-*` captures, and the 3 MiB cap matches the worker's |
| `tests/inputpolicy_test.cpp` | 24 | The rules behind "the prompt box is the only keyboard input": where a submitted line goes, when the pane offers "Take control", what may be kept, and how a password is wiped |
| `tests/jobs_test.cpp` | 6 | The jobs list under the prompt box: commands the agent left running (src/JobsPanel.h) |
| `tests/logging_test.cpp` | 5 | Rotating diagnostics log (issue SQAM): location, permissions, level filtering, rotation and redaction. Uses a private XDG_DATA_HOME, so it never touches the real profile |
| `tests/markdownansi_test.cpp` | 12 | Agent replies rendered into the terminal: Markdown in, ANSI out, whatever the chunking |
| `tests/modelsettings_test.cpp` | 27 | The roles modal picks a *provider*, so its lists name companies ("Kimi", "Z.AI (GLM)") and offer only providers whose key is actually stored. Owner, 2026-09-18: "in model roles, it shouldn't show options where you don't have a key assigned … it should not say the model name" |
| `tests/notifications_test.cpp` | 6 | `src/Notifications.h`: the bell's list — newest first with counts, an empty title ignored, seen/remove/clear, and the oldest entries dropped |
| `tests/outputlinks_test.cpp` | 29 | Clickable paths (#YZTK), card references (Switchboard design section 5) and the keyboard walk over them (#GWXM): which spans of a line of terminal output are links, what they resolve to against a pane's working directory and card index, and how the keyboard cursor moves over the ordered list |
| `tests/panelayout_test.cpp` | 25 | Pane movement: which pane Alt+arrow focuses and Ctrl+Alt+arrow moves past, what the splitter order is afterwards, and which edge a dragged pane is dropped on. The swap tests run against a real QSplitter |
| `tests/panestate_test.cpp` | 22 | relay::panestate: the v1 `pane_state` message a phone draws (docs/REMOTE-PROTOCOL.md section 16), the actions each queue row offers, the desktop-minted ids, and the coalescing publisher |
| `tests/panestatus_test.cpp` | 23 | Pane types, pane states and remote sessions (cards #SPBN and #XM0T): the urgency order a tab uses, how a pane's facts become one state, the ssh/mosh/telnet destination, and that every tint keeps its glyph and its label legible in every shipped theme |
| `tests/panetitles_test.cpp` | 7 | Pane titles and tab labels (issue JRWQ): tidying a model-written title, a tab's place label (the repo name of its project, else the folder of its directory), and the offline "same work" judgement and join that still label a tab with no terminal pane. The worker side is tested in tests/test_titles.py; the rules here are what the GUI applies without any model |
| `tests/paneusage_test.cpp` | 13 | A pane's CPU / memory share (issue #D03W): the arithmetic on Readings a test can make up — percentages over an interval, rounding, when a meter is worth showing, tab-level sums and the label suffixes — plus one /proc walk over this test's own process to prove the parsing |
| `tests/projectinit_test.cpp` | 19 | "Initialize a project and create a Switchboard here?" (src/ProjectInit.h, protocol 19.12/19.13). The owner's rule of 2026-09-18 is that `<project>/switchboard/` appears only after one yes, and that only five acts may ask |
| `tests/projects_test.cpp` | 28 | The project model behind the per-project Switchboard (src/Projects.h): the candidate project of a terminal directory, the project key that has to match the backend's workspace digest, which board a project's cards go to (and whether the user has to be asked first) |
| `tests/prompthistory_test.cpp` | 14 | The prompt box's history file (src/PromptHistory.h): what a line looks like on disk, what is worth keeping, and that two Relays appending at once do not lose each other's lines |
| `tests/pulsepaint_test.cpp` | 4 | Card #V8KT: a live pane's marks must not only exist, they must move. This renders the real tab icon and the header glyph (pulsepaint_paint.cpp) at every step of the pulse and fails when a step stops changing the painted pixels |
| `tests/queuenav_test.cpp` | 26 | Arrowing through the queue while its items are edited in the prompt box: when Up and Down move between queued items and when they belong to the text, and which keys save, cancel, reorder and remove |
| `tests/remotefiles_test.cpp` | 25 | Files on the host a pane is logged into (card #S5SH, docs/SSH-AND-MOSH.md section 9). The rules first, with no network and no process: how a path is quoted for the remote shell, what `stat -c %s:%Y:%a` parses to, when a save must refuse because the host's copy moved on, what the scripts say |
| `tests/remotepane_test.cpp` | 19 | Relay-to-Relay (src/RemotePane.h): the screen painter's cell grid from `screen_snapshot` and `screen_diff`, the scrollback column app/screen.js keeps, the key table, and a pane drawn from `pane_state` offering only what each row lists — plus control, compose and the sidecar contract, through a fake viewer script |
| `tests/remotesession_test.cpp` | 8 | SSH and mosh sessions (card #S5SH): which `ssh -G` arguments describe the running login, what the dump says, and that the typed bootstrap line decodes, with a row count that fits |
| `tests/requests_test.cpp` | 14 | `src/RequestLedger.h` and the Requests panel: parsing requests and todos, a prompt never becoming a task, verbatim text surviving a refresh, the chip when everything is settled, outcome mapping, and batches restarting |
| `tests/runtimedirs_test.cpp` | 11 | Private runtime directories: the owner mark, "is that Relay still running?", and the sweep that removes what a crash left in /tmp — without touching a live Relay's directories, a neighbour's files, or anything in /tmp that is not Relay's (issue 9JYK). Every test builds its own fake temp root in a QTemporaryDir |
| `tests/screenprompt_test.cpp` | 27 | "Is the foreground program waiting for me to type something?" decided from the last rows of the screen (src/ScreenPrompt.cpp), against recorded screens in tests/fixtures/screen/ |
| `tests/settingspane_test.cpp` | 30 | Settings pane: tabs follow the catalog and skip headings and info lines, ← → switch tabs from the search (wrapping), ↑ ↓ move the highlight without wrapping, Enter flips the highlighted toggle and the rebuild keeps the tab and the highlight, search mixes settings rows and actions (a submenu entry found through its parent, an alias word, nothing matching), Enter hands an action to the owner (or runs it itself), the Actions tab lists Recent then sections with submenus inline, Esc clears then closes and from a control goes back to the search first, a rebuild keeps the tab, the scroll offset and the focused control, every control writes through its row, fuzzy scoring |
| `tests/sharingpane_test.cpp` | 16 | The multiplayer model behind the Sharing pane (#W5N2, docs/REMOTE-PROTOCOL.md section 10). Everything here is the logic the owner's answers depend on — who is here, what is waiting, whose countdown has run out, what the pane header says — so it is exercised without a hub and without a display |
| `tests/slashcommands_test.cpp` | 5 | An unknown slash command is Relay's to answer, not the shell's: which lines are an attempt at a command, which are paths the shell keeps, and what the unknown-command line says |
| `tests/sshconfig_test.cpp` | 10 | SSH hosts for "Connect to host…" and the command line "Split on the same host" re-runs (card #S5SH, docs/SSH-AND-MOSH.md section 8). Every test writes its own ~/.ssh into a temporary directory; nothing reads the real one |
| `tests/striplayout_test.cpp` | 9 | The strip under the composer (owner, 2026-09-19): "the open task list … underneat the prompt … it shows up to (say) 5 tasks. if there are more than 5, it centers on the marginal task … with subagents on the left and tasks on the right … a subagent with two tasks gets two rows" |
| `tests/subagents_test.cpp` | 26 | `src/SubagentTranscript.h` and the subagents panel: the start/progress/finish lifecycle, handoffs past the cap, which events are routed and consumed and which only observed, dismiss and clear keeping live rows, and a resumed agent restarting its row |
| `tests/theme_test.cpp` | 28 | Colour themes (issue 0JA7): the theme file reader, the token contract every theme has to meet, discovery across the user folder and the packaged one, and the Konsole colour scheme generated from a theme |
| `tests/themeswitch_test.cpp` | 5 | The live end of the theme (src/Theme.cpp): what the running application does with the id in `theme/name`, as opposed to what the reader does with a file (tests/theme_test.cpp) |
| `tests/toollabel_test.cpp` | 20 | the shared label parser (#TK9C): one line per call for every tool and every `kind`, the command classifier, the stats in the order § 23.4 fixes, a failure's `✗` and exit code, what a click opens, and which consecutive calls merge |
| `tests/turntranscript_test.cpp` | 9 | the turn pane (#TK9C): every row is the label's line, a merged run is one parent row with its members, each call's duration, and `detail` rendered section by section |
| `tests/voice_test.cpp` | 10 | The rules behind voice transcription (issue NY7Z): which capture tool runs and how, which key event is the hold key, where a transcript lands in the composer's text, and the WAV repair that makes a clip from an interrupted recorder readable. No microphone and no provider are involved |
| `tests/windowstate_test.cpp` | 25 | Saved window layout ("reopen where I left off"): the parts that do not need a window — reading and writing state/windows.json, validating pane trees, clamping geometry onto a screen that still exists, the cwd/workspace/$HOME fallback, and the per-pane scrollback store a restored pane refills itself from |
| `tests/wordwrap_test.cpp` | 9 | Relay's own text in the terminal breaks between words, never inside one |
| **Total** | **888** | 54 suites; data-driven slots run more cases than this |

Engine suites (`relay-engine-tests`, one binary, `RELAY_ENGINE_TEST=<name>` runs one):

| Test object | Cases | What it checks |
|---|---:|---|
| `engine/tests/CoreTest.cpp` | 26 | Screen-model tests: feed byte sequences into every available VtCore and assert cells, text, modes and events |
| `engine/tests/FaintInkTest.cpp` | 6 | view/FaintInk.h: the colour SGR 2 and a fold's dim rows are drawn in (#LG7T, #TK9C). The rule is one sentence — as faint as it can be while it still reaches 4.5:1 on the background it is drawn on, and never fainter than the host's own ink — so the cases here are the ways that can go wrong: ink with room to fade |
| `engine/tests/FoldLayerTest.cpp` | 15 | relay::FoldLayer: the fold layer's maths without a GUI — wrapping, the visual-row <-> real-row mapping, anchoring, trimming and ordering |
| `engine/tests/FoldSearchTest.cpp` | 12 | relay::FoldSearch: finding inside expanded folds and merging those matches with a core's own into one sequence in visual order — without a GUI, without a core. The fake core below steps exactly the way LibVtermCore::searchStep() does (a cyclic index over the matches, the returned index counted from the newest) |
| `engine/tests/PtyTest.cpp` | 8 | relay::Pty tests: spawn, echo, resize, environment, exit codes, input |
| `engine/tests/SessionTest.cpp` | 7 | TerminalSession: threaded parsing, signals on the GUI thread, display injection |
| `engine/tests/ViewTest.cpp` | 33 | TerminalView / VTermBackend tests on the offscreen platform: rendering, keyboard, mouse selection, links, IME, scrolling, accessibility, key mapping |
| **Total** | **107** | 7 objects in one binary |

The last `ctest --test-dir build` run recorded here (2026-09-18, when 43 suites were registered):
**42 of 43** pass. The one that does not is
`backend-and-bash`, and not on a test: the suite takes longer than the `TIMEOUT 120` it is declared
with (`CMakeLists.txt`) — about 130 s on an idle machine, more when several sessions are building
at once — so ctest kills it. Run on its own (`./scripts/test.sh`) it is clean.

Provider tests use a local mock server and synthetic responses. They do not validate real
accounts, quotas, billing or model behavior.

### CI and packaging checks (configured)

| Workflow | Checks |
|---|---|
| `.github/workflows/ci.yml` `ubuntu-qt5` | Ubuntu 24.04 Qt5/KF5 build, all ctest groups, staged install layout, `desktop-file-validate`, `appstreamcli validate` |
| `.github/workflows/ci.yml` `qt6` | Debian 13 (Qt 6.8) and Ubuntu 26.04 (Qt 6.10) Qt6/KF6 build, ctest and `.deb`, each in its container (`packaging/deb/build-deb.sh`) |
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
| Model roles: provider names, only providers with a key offered, the two Kimi plans told apart, Flash put on Z.AI with `glm-5.3-flash`, and "Auto" replacing "Auto detect" | [`qa_evidence/2026-09-18-provider-names-and-tier-providers/`](qa_evidence/2026-09-18-provider-names-and-tier-providers/) |
| A Switchboard card's title and its `## Issue` text edited on the card (`e`, the Edit button, a click on the title), written by the worker, surviving a restart, a save refused when the file changed underneath, and a card still headed `## Request` settling on `## Issue` | [`qa_evidence/2026-09-18-edit-card-title-and-issue/`](qa_evidence/2026-09-18-edit-card-title-and-issue/) |
| The agent handing a command to the terminal (`run_in_terminal`): run in the pane with a real tty and the follow-up turn carrying exit status and output, a prefill submitted by the user, Ctrl+C sending nothing, the `prefill` setting downgrading a run, and a draft in the prompt box never overwritten | [`qa_evidence/2026-09-18-agent-terminal-handoff/`](qa_evidence/2026-09-18-agent-terminal-handoff/) |

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
- `2026-09-17-agent-queue-steering-and-editing.md`
- `2026-09-17-agent-responses-in-terminal.md`
- `2026-09-17-agent-sessions-ui.md`
- `2026-09-17-ai-command-and-prompt-suggestions.md`
- `2026-09-17-aliases-and-workflows.md`
- `2026-09-17-at-file-picker.md`
- `2026-09-17-backend-sessions.md`
- `2026-09-17-backend-subagents.md`
- `2026-09-17-clickable-paths.md`
- `2026-09-17-color-themes.md`
- `2026-09-17-combined-terminal-agent-queue.md`
- `2026-09-17-conversation-list-and-search.md`
- `2026-09-17-ctrl-i-input-toggle.md`
- `2026-09-17-engine-integration.md`
- `2026-09-17-explorer-right-click-and-toggle.md`
- `2026-09-17-file-explorer-and-preview-panes.md`
- `2026-09-17-fix-and-rerun-terminal-commands.md`
- `2026-09-17-history-suggestions.md`
- `2026-09-17-human-agent-control-and-password-prompts.md`
- `2026-09-17-image-context.md`
- `2026-09-17-in-app-shortcut-alternates.md`
- `2026-09-17-instructions-onboarding.md`
- `2026-09-17-keyboard-jump-to-output-links.md`
- `2026-09-17-keyboard-shortcuts-and-palettes.md`
- `2026-09-17-load-global-warp-skills.md`
- `2026-09-17-model-roles-and-fast-agent.md`
- `2026-09-17-model-roles.md`
- `2026-09-17-model-settings.md`
- `2026-09-17-new-pane-direction-by-arrow.md`
- `2026-09-17-palette-search-aliases.md`
- `2026-09-17-pane-process-isolation.md`
- `2026-09-17-pane-tab-buttons-and-moving.md`
- `2026-09-17-pane-title-summary.md`
- `2026-09-17-plan-mode-and-plan-pane.md`
- `2026-09-17-port-konsole-context-menus.md`
- `2026-09-17-prefix-modes.md`
- `2026-09-17-program-control-policy.md`
- `2026-09-17-prompt-visibility-and-waiting-input.md`
- `2026-09-17-queue-or-interrupt-agent-prompts.md`
- `2026-09-17-queue-shell-commands-while-busy.md`
- `2026-09-17-request-ledger-todos-completion.md`
- `2026-09-17-requests-ui.md`
- `2026-09-17-restore-windows-on-start.md`
- `2026-09-17-router-english-commands.md`
- `2026-09-17-routing-assist-thinking-skills-backend.md`
- `2026-09-17-routing-assist-ui.md`
- `2026-09-17-screen-text-input-detection.md`
- `2026-09-17-shortcut-hints.md`
- `2026-09-17-skills-dialog.md`
- `2026-09-17-steering-running-agent-turn.md`
- `2026-09-17-subagents-ui.md`
- `2026-09-17-switchboard-phase0.md`
- `2026-09-17-switchboard-phase1.md`
- `2026-09-17-thinking-and-tool-call-summaries.md`
- `2026-09-17-voice-transcription.md`
- `2026-09-17-website-update-engine-features.md`
- `2026-09-17-window-header-and-notifications.md`
- `2026-09-17-windows-tabs-panes.md`
- `2026-09-17-wrong-mode-hints.md`
- `2026-09-18-agent-hands-commands-to-the-terminal.md`
- `2026-09-18-concise-tool-call-lines.md`
- `2026-09-18-distinct-headers-or-colors-for-each-pane-type.md`
- `2026-09-18-edit-file-tool.md`
- `2026-09-18-light-and-dark-commands.md`
- `2026-09-18-local-models.md`
- `2026-09-18-open-external-in-the-right-click-menu.md`
- `2026-09-18-reasoning-panel-shortcut.md`
- `2026-09-18-subagents-in-one-tabbed-pane-a-tab-per-subagent.md`
- `2026-09-18-terminal-and-agent-status-icons-with-notificatio.md`
- `2026-09-18-waiting-for-jobs.md`
- `2026-09-18-waiting-for-subagents.md`
- `2026-09-19-claude-codex-guest-integration.md`
- `2026-09-19-cross-provider-qa-a-provider-model-signature-on.md`
- `2026-09-19-the-open-task-list-under-the-prompt.md`

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
- `2026-09-18-edit-file-tool.md`

Waiting for QA (`issues/changes/needs_qa_llm/`):

- `2026-09-17-cleanup-quick-wins-dead-code-removal-logging-for.md`
- `2026-09-17-composer-page-scroll.md`
- `2026-09-17-ctrl-h-shrinks-pane.md`
- `2026-09-17-ctrl-question-shortcuts.md`
- `2026-09-17-keypad-enter-submits.md`
- `2026-09-17-new-pane-not-active.md`
- `2026-09-17-no-list-nudge.md`
- `2026-09-17-pane-move-keys-and-drag-broken.md`
- `2026-09-17-pre-submit-run-check.md`
- `2026-09-17-provider-stalls-and-no-logs.md`
- `2026-09-17-second-window-on-start.md`
- `2026-09-17-single-click-folders.md`
- `2026-09-17-suggestions-not-working.md`
- `2026-09-17-tasks-are-todos-only.md`
- `2026-09-17-terminal-not-directly-typable.md`
- `2026-09-18-bubbles-take-the-column.md`
- `2026-09-18-button-labels-clipped.md`
- `2026-09-18-command-not-found-under-a-request.md`
- `2026-09-18-command-not-found-under-a-sentence-naming-a-fi.md`
- `2026-09-18-edit-card-title-and-issue.md`
- `2026-09-18-markdown-files-do-not-render.md`
- `2026-09-18-markdown-view-labels-say-md.md`
- `2026-09-18-model-dropdown-selection.md`
- `2026-09-18-model-roles-provider-names.md`
- `2026-09-18-model-tier-commands-and-flash-naming.md`
- `2026-09-18-new-panes-keep-the-main-agent.md`
- `2026-09-18-one-icon-everywhere.md`
- `2026-09-18-output-token-limit-defaults-to-32k.md`
- `2026-09-18-pane-buttons-and-header-drag.md`
- `2026-09-18-preview-link-has-no-way-back.md`
- `2026-09-18-queue-items-edit-in-the-prompt-box.md`
- `2026-09-18-retire-konsolepart.md`
- `2026-09-18-scrollback-survives-restart.md`
- `2026-09-18-settings-as-a-full-pane.md`
- `2026-09-18-skills-every-name-in-the-prompt.md`
- `2026-09-18-unknown-slash-command.md`
- `2026-09-19-the-planner-asks-the-user-questions.md`

This list has fallen behind the folder (97 cards there, 139 lines here across both categories, and
no 2026-09-19 entries but this one). `ls issues/features/needs_qa_llm/` is the truth; a session
that lands a card should add its line here, and one that has a minute should reconcile the rest.

Closed (`issues/features/done/`): `2026-09-17-review-opencode-agent-design.md` (research) and
`2026-09-17-terminal-first-agent-fallback.md` (superseded before QA).

[RELEASING.md](RELEASING.md) requires every feature shipped in a beta to have passed QA or be
listed as a known issue.
