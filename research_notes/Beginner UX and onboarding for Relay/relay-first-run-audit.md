# Relay first-run audit — ground truth for onboarding design

Read-only audit of the working tree at `/home/elliott/repos/relay-terminal`. "First run" below means: packaged binary (`.deb`/`.dmg`/`.exe`), no existing per-user Relay state (no `~/.local/share/relay` and equivalent), no API keys on the machine. Where I could verify an exact string I quote it; where the audit pass ran out of budget before a file was read, I say so explicitly rather than guess. Two areas are flagged as **partial**: the full enumeration of hint ids (§4) and the `issues/` card sweep (§8) — anchors are given so one `rg` each completes them.

## 1. Launch: what a fresh profile actually sees

**Order of events, first 60 seconds:**

1. `src/main.cpp` contains no first-run branch of its own — `rg "first.run|firstRun"` over `src/` puts every fresh-profile decision in the window manager and the pane, not in `main()`. `main()` is boot plumbing (Qt app, paths, window construction).
2. `src/WindowManagerImpl.h:295-318` — `newWindowAt` decides the *layout*. When the profile is fresh it opens **one terminal pane on the left and the Models pane pinned right, on the "providers" tab**; the comment at `src/WindowManagerImpl.h:308` explains the gating: the welcome is offered or dismissed — "so the models pane greets a fresh profile once." The gate is the `instructions/onboarded` setting. `src/RelayWindowModels.cpp:~85` carries the matching "first run opens on this tab" comment on the providers tab.
3. `src/Pane.h:5799-5812` — `onSessionConfigured`. Once the terminal's session is up, the pane sets `m_onboarding = !settings …("instructions/onboarded")` (`src/Pane.h:5802`) and fires **two timers**: `QTimer::singleShot(400, … openInstructions())` (`src/Pane.h:5803`) and a second shot at ~800 ms that raises the **first-run approvals choice** (card `#K2FV`, referenced in the same block). So the user is ~0.4 s into their first pane when dialog #1 appears, and ~0.8 s when choice #2 appears on top of it.
4. **Dialog 1 — instructions.** `Pane::openInstructions()` at `src/Pane.h:4495`; the dialog itself is `relay::agentui::chooseInstructions(this, files, settings.value("instructions/project_auto", true), relayMdPath())` at `src/Pane.h:8376`, declared in `src/AgentUi.h:49` (`struct OnboardingResult` at `src/AgentUi.h:41`; implementation `src/AgentUi.cpp:132-199`, result assembled at `src/AgentUi.cpp:187`). If no instruction files exist, the first-run path is deliberately *quiet*: the comment near `src/Pane.h:8309` (starter `relay.md`) describes writing a starter `relay.md` without ceremony. In practice a truly fresh machine has nothing to import, so the dialog is skipped or dismissed in one click and a starter `relay.md` lands in the workspace.
5. **Choice 2 — approvals.** `src/ApprovalsPane.h:48-103` defines `ApprovalsView`; its two callbacks are wired there (`onAllowEverything` connect at `src/ApprovalsPane.h:92`). This is the "Allow everything / Ask every time" first-run card; answering it is what sets `instructions/onboarded` and silences steps 3-5 forever.

**What is *not* there:** no welcome window, no tour, no sample project. `src/Pane.h:7180-7320` (the area around 7230 the brief asked about) is session *sharing/handoff* plumbing, not onboarding — nothing beginner-facing fires from it. Note also `src/Pane.h:641-642`: the pane constructor immediately does `m_data = dataRoot(); m_python = relayPython();` — the terminal is live and a shell is running before any dialog closes.

**Paths:** `src/AppPaths.h` `dataRoot()` is the *program* data root (the installed Python backend), not the user profile; "fresh data dir" in practice means Qt settings/sessions/scrollback defaults are absent, so every read falls back (`instructions/onboarded` → false at `src/Pane.h:5802`; `instructions/project_auto` → true at `src/Pane.h:8376`).

**Net first-minute experience:** a real terminal with a running shell, a Models pane open on "providers" (advertising Anthropic/OpenAI/… provider cards), one quickly-vanished instructions dialog, and one approvals choice. Then silence: an empty prompt box.

## 2. Relay Free — what a no-key user gets

`docs/RELAY-FREE.md` (read in full) plus backend evidence in `backend/relay_core/presets.py`:

- With **no API keys**, the default preset lands on **`relay-free`** — the fallback position of the default-preset chain in `backend/relay_core/presets.py:258-285`; the model definition/mapping (`relay_main` → hosted GLM, GLM 4.7 via Z.ai per the doc) sits at `backend/relay_core/presets.py:1285-1330`. No key, no signup: the first agent turn works out of the box through the project's hosted gateway (see `docs/RELAY-FREE.md`; the handoff/ops side is `docs/RELAY-FREE-HANDOFF.md`).
- **Limits:** a per-install/per-device daily prompt allowance, metered server-side. The UI surfaces it as a small disclosure in the status strip — the `relay_free` branch at `src/Pane.h:5431-5560` renders a quota chip ("Free · N% left"-style) and, when the allowance is spent for the day, a message that explains the free tier and points at adding a real key.
- What the UI *says* is minimal: the Models pane presents `relay-free` as one more provider card (`src/RelayWindowModels.cpp`), so a beginner sees a working default without being told it is metered until it runs out.

## 3. The prompt box and Enter routing

- **Placeholder on a fresh pane:** the ladder in `src/RichEditor.cpp:157-161` — `"Shell commands or agent prompts…      ?  for help"`, falling back to `"Shell commands or agent prompts…"`, `"Commands or prompts…"`, `"Commands…"`, `"…"`. The terminal context's own line is `src/Pane.h:620-624` (`"Shell commands or agent prompts…      ?  for help"`). Shorter rungs are built mechanically by `placeholderRungs()` (`src/AgentContext.cpp:46-64`): split on `", "` and `" — "`, append `"…"` — the ladder exists because a card page's box "said nothing at all where the owner asked for the three chords (#VZ69)" (`src/Pane.h:4046-4056`).
- **Routing:** the terminal context answers `routing = "auto"` (`src/Pane.h:612-616`); `Ctrl+I` cycles the mode between shell / agent / auto. `docs/ARCHITECTURE.md` §5 (routing table, ~lines 960-1060) is the authority: lines beginning `/` are slash commands, `?` is help, and a bare line is classified by the backend heuristic — recognizable commands (`ls`, `git …`) go to the shell, everything else goes to the agent.
- **"hello"** → not a command → sent as an agent prompt to `relay-free`. **"how do I list files"** → same. A beginner's first plain-English sentence works *if* the free quota is alive — but nothing on screen tells them the difference between typing `ls` (runs locally, instant) and `list the files` (spends a free-turn, takes seconds, streams an answer).

## 4. Shortcut hints / tips system (partial enumeration)

- Mechanism (`src/Hints.h`, 51 lines; `src/Hints.cpp`, 80 lines): a `ShortcutHints` object keys hints by string id; `hint(id, text)` shows one only if the id has not been *seen* (persisted in settings); `nextTime` re-arms a hint for a later moment; dismissal is implicit (seen = never again until reset). `nextIdleTip` is pane-side: an idle timer (`m_idleTip`, stopped the moment the user types — `src/Pane.h:13101`) that surfaces tips into the status line, fed by the tips list around `src/Pane.h:10180-10210`.
- Verified example: `hint(QStringLiteral("help.slash"), QStringLiteral("Next time: press ? in an empty prompt box"))` in `src/Pane.cpp`. **Partial:** the full id inventory needs one `rg "hint\(QStringLiteral"` pass (the audit's copy of that output was reclaimed before transcription). Related machinery an onboarding can borrow: the ask-placeholder ownership rule (#R3YN, `src/Pane.h:9080-9095`, `m_askPlaceholderShown` at `src/Pane.h:16094`).

## 5. Discovery surfaces

- **Actions palette (Ctrl+Shift+A)** and **Options (Ctrl+Shift+O)**: keybindings per the README key table; the palette is a searchable list over registered actions (Settings/Options surfaces, `src/SettingsPane.cpp`). The Options/Actions composer reuses the same hosted prompt box (`surface` normalization at `src/Pane.h:13109-13112`).
- **Board (Ctrl+Shift+S)**: the sessions/projects board. Empty on a fresh profile except the live pane; drafts persist per surface (`console/…/sessions-pane`, `src/Pane.h:13119-13125`).
- **Models pane**: right dock, tabs (providers / keys / models), first run opens on providers (`src/WindowManagerImpl.h:308`, `src/RelayWindowModels.cpp:~85`). Its "empty state" is actually *full* — provider cards — which is good gravity for key setup but says nothing about the free default.
- **/commands list**: popup driven by `updateSlashPopup()` on every keystroke (`src/Pane.h:13097`); the only *advertised* entry point is the placeholder's "`?  for help`" suffix and the one-shot `help.slash` hint.
- **Sessions/projects/globals surfaces** share one prompt box and one draft store (`src/Pane.h:13109-13125`).

## 6. Key setup path

Options/Models → provider card → `askForKey` (declared on the window; verified present via `rg` across `src/RelayWindow.h` / `src/ModelSettings.cpp`): a small dialog with a secret field and a **test** action; success triggers a `presets` re-request (`src/Pane.h:7749`, `src/Pane.h:10349` — `m_keysDialog->onKeysChanged` → `send({{"type","presets"}})`). **Import from Warp / Claude Code / Codex** scans those tools' config files for existing keys (documented with the Relay Free handoff material, `docs/RELAY-FREE-HANDOFF.md`). Silent-failure modes a beginner hits: the dialog's test can fail with no inline diagnosis (network, provider, format); import finds *nothing* on a machine without those tools and the affordance stays visible anyway; and because `relay-free` already "works", a broken key never announces itself until the free tier is exhausted.

## 7. Existing docs and design language

Present in `docs/`: `SWITCHBOARD-AESTHETIC.md`, `SWITCHBOARD-DESIGN.md`, `ROADMAP.md`, `NEXT-STEPS-RESEARCH.md`, README "Quick start". **Not read in this pass** (budget), so treat the following as code-derived, not doc-quoted: the house style is visible in the sources — long, chatty-but-precise owner comments with card ids ("owner design, 2026-09-17", `src/PaneUi.cpp` strip under the prompt box), lowercase quiet surfaces, muted color, "three chords" minimalism (#VZ69), and an explicit bias that *first-run path is quiet* (`src/Pane.h:8309`). Any onboarding copy should be checked against those two SWITCHBOARD docs before writing a word.

## 8. Existing cards (partial sweep)

`issues/` sweep was not completed. Cards referenced from code comments (all verified in-tree): `#K2FV` first-run approvals choice (`src/Pane.h:~5810`), `#VZ69` placeholder ladder (`src/Pane.h:4046`), `#R3YN` ask owns the placeholder (`src/Pane.h:9087`), `#057J` history-suggestion cache (`src/Pane.h:13143`), `#TCXT` output rides along (`src/PaneUi.cpp`), `#PBX1` action-row letters (`src/Pane.h:4060`), `#AGNT` no-shell pane (`src/Pane.h:634`). Run the brief's `rg -l -i "onboard|first run|welcome|…"` over `issues/` to get status/ids before filing anything new.

## 9. Packaging

`packaging/` contains the platform packaging plus `installed-smoke.py` (per the brief's pointer; **not independently read**), and `docs/RELEASING.md` documents release flow. The smoke test's role is to catch *installed*-binary regressions (bundled Python found, backend importable from `dataRoot()`, window opens) — i.e. first-run environment checks exist for CI, not for the user: nothing in-app verifies python/bash/fonts for the person who just double-clicked.

## Friction points for a first-time user

1. **Two stacked interruptions in the first second** — instructions dialog at 400 ms, approvals choice at 800 ms (`src/Pane.h:5803`, `#K2FV` timer), before the user has typed anything.
2. **No welcome/empty-state copy at all** after those dialogs: the first-run layout's message is a providers list (`src/WindowManagerImpl.h:295-318`), i.e. "configure us", not "here's what to do".
3. **First-run path is deliberately quiet** (`src/Pane.h:8309`) — a design value that currently reads as "nothing happened" to a novice.
4. **Placeholder tells the *grammar*, not the *goal***: `"Shell commands or agent prompts…      ?  for help"` (`src/RichEditor.cpp:157`) assumes the user already knows what an "agent prompt" is.
5. **`auto` routing is invisible** (`src/Pane.h:612-616`): nothing explains that `ls` runs locally while `list the files` spends a hosted model turn.
6. **Relay Free is metered but unlabeled** until exhaustion (`src/Pane.h:5431-5560`); the day it dies is the first time the beginner learns there was a quota.
7. **Exhaustion message arrives at the worst moment** — mid-task, with a "add a key" detour into the Models pane (`src/Pane.h:5494+`).
8. **Key dialog failures are quiet**: `askForKey` test/import failures give no inline diagnosis; `presets` just re-requests (`src/Pane.h:7749`, `10349`).
9. **Import affordance ("Warp / Claude Code / Codex") is shown to everyone**, including machines where it can never find anything (`docs/RELAY-FREE-HANDOFF.md` scope).
10. **Hints are one-shot and id-keyed** (`src/Hints.h`/`Hints.cpp`): a beginner who misses the single `help.slash` dismissal has no way back except a settings reset.
11. **`?` help and `/commands` are hidden behind a keystroke** the placeholder mentions once and only while the box is empty (`src/Pane.h:13097`, `help.slash`).
12. **Approvals choice (#K2FV) is binary and unexplained** — "Allow everything" vs "Ask every time" with no third "explain later" path (`src/ApprovalsPane.h:48-103`).
13. **The Board/Actions/Options trio is undiscoverable** — three Ctrl+Shift chords, only the key table documents them.
14. **No first-run health checks in-app** (python/bash/fonts): `installed-smoke.py` covers CI only, so a broken install fails in the user's face.
15. **Placeholder ladder mechanics constrain copy**: rungs are prefix-truncations on `", "`/`" — "` (`src/AgentContext.cpp:46-64`), so any onboarding sentence in a placeholder must be written comma-first to degrade gracefully.

## Assets we already have

- **Hints system** (`src/Hints.h`, `src/Hints.cpp`, idle tips at `src/Pane.h:~10180`): a ready once-per-user channel with ids, dismissal and reset semantics.
- **Relay Free** (`docs/RELAY-FREE.md`, `presets.py:258-285/1285-1330`, `src/Pane.h:5431-5560`): a working, keyless default model with a quota surface already in the UI.
- **First-run layout hook** (`src/WindowManagerImpl.h:295-318`, `instructions/onboarded`): a proven, single-bit gate that already opens a chosen pane/tab on a fresh profile.
- **Staged-timer hook** (`src/Pane.h:5799-5812`): the 400/800 ms sequence is exactly where a tour step could slot in.
- **Placeholder rungs** (`src/AgentContext.cpp:46-64`, `src/RichEditor.cpp:147-165`): responsive, width-aware teaching surface in the prompt box itself.
- **`?` help + `/` slash popup + AI ghost suggestions** (`src/Pane.h:13097`, `updateGhost()` at `src/Pane.h:13127+`): an in-composer suggestion channel, including "Suggested prompt (AI) · Tab accepts".
- **Action row** (#PBX1, `src/Pane.h:4059+`): one-letter buttons for no-typing actions — a natural "try me" strip.
- **Actions palette / Options / Board / Models pane**: four existing surfaces an onboarding can route to by name.
- **Approvals first-run card** (#K2FV, `src/ApprovalsPane.h`): precedent for a full-pane choice screen with two buttons and a status line.
- **Code-referenced card system** (`#K2FV`, `#VZ69`, `#R3YN`, `#057J`, `#TCXT`, `#PBX1`, `#AGNT`) and the SWITCHBOARD docs — the design language and issue ledger any new onboarding card should join.
