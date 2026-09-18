# Relay roadmap

Last updated 2026-09-17. What Relay is today: [ARCHITECTURE.md](ARCHITECTURE.md). What has
been tested: [VALIDATION.md](VALIDATION.md). Issues are files under `issues/`; paths below
are relative to the repository root.

Estimates come from the research documents and are single-developer guesses, not measurements.

## First MVP scope (owner, 2026-09-17)

The owner named these as required for the first MVP, in addition to what already works:

| MVP item | Issue | Depends on |
|---|---|---|
| Delegate and take over (agent drives the visible pane) | `issues/features/2026-09-17-agent-delegate-and-take-over.md` | Relay engine (screen text, alt-screen state) |
| Clickable paths in the terminal | `issues/features/2026-09-17-clickable-paths.md` | Relay engine (link/path click events) |
| Keyboard jumping to links in output | `issues/features/2026-09-17-keyboard-jump-to-output-links.md` | Relay engine (screen text) |
| Portable engine and screen-text input detection | `issues/features/2026-09-17-portable-terminal-engine.md`, `issues/features/2026-09-17-screen-text-input-detection.md` | Done on Linux: the engine runs every pane and KonsolePart was retired (2026-09-18). Next: macOS and Windows |
| Website and beta release | `issues/features/2026-09-17-website-and-beta-release.md` | Owner actions in `docs/RELEASING.md` |
| Voice transcription | `issues/features/2026-09-17-voice-transcription.md` | OpenRouter key; `google/gemini-3.5-flash-lite` |

Consequence: the Relay engine becomes the MVP's terminal on Linux, not a later cross-platform
milestone; four of six items depend on its integration.

## Goals

1. A terminal where one prompt runs shell commands or asks an agent, and the agent's work
   shows up in the terminal itself.
2. The real terminal stays intact: vim, less, ssh, password prompts and TUIs get normal keys.
3. Bring your own key, any OpenAI-compatible provider, no Relay account. An included allowance
   (Relay Free) covers the first ask on a fresh install; it never replaces BYOK.
4. Keyboard first, with shortcuts users can change (and ask the agent to change).
5. Linux first, then macOS and Windows on a Relay-owned terminal engine.

## Decisions already made

> Update 2026-09-17: "no account" and "no Relay cloud service" are preferences, not strict constraints (owner). Remote access v1 is planned as a browser client on relay-terminal.ai backed by a Relay-operated, end-to-end encrypted relay; native phone apps later.


| Decision | Consequence | Source |
|---|---|---|
| **No Konsole fork**, and since 2026-09-18 no Konsole at all: Relay's own engine is the terminal. | Screen text, click signals and alternate-screen state are Relay's to provide; KDE Frameworks is no longer a runtime dependency | [NEXT-STEPS-RESEARCH.md](NEXT-STEPS-RESEARCH.md) section A; [ENGINE.md](ENGINE.md) |
| **No per-action approvals.** Agent tools run immediately; every action is previewed inline and Stop is always available. | Safety relies on previews, guards, limits and clear warnings. opencode-style permission rules are not planned. | commit `1ad28fa`; [OPENCODE-NOTES.md](OPENCODE-NOTES.md) status header |
| **BYOK first, Relay Free included** (owner, 2026-09-18; before that, BYOK only). Keys come from environment variables or the desktop keyring. A fresh install with no key lands on Relay Free, a quota-limited hosted provider whose gateway holds the upstream keys. | A BYOK request never touches Relay's server; Relay Free is one provider row, off the moment another provider is chosen. No billing, no account: an installation keypair is the only identity (`#HG7K`, [RELAY-FREE.md](RELAY-FREE.md)). A model server on this machine needs no key either (`#24XJ`, [LOCAL-MODELS.md](LOCAL-MODELS.md)) | `backend/relay_core/keystore.py`, `backend/relay_core/hosted.py`, `gateway/` |
| **No telemetry.** No analytics or crash reporting. The Relay-operated services (the remote rendezvous, the Relay Free gateway) keep request metadata only, never content, and only see traffic from the feature that uses them. | Any update check must be opt-in | `site/index.html` privacy section; [DISTRIBUTION-RESEARCH.md](DISTRIBUTION-RESEARCH.md) section 1 |
| The agent drives the user's **visible** pane, not a hidden one. | Delegate/take-over needs screen reading in the visible terminal | `issues/features/2026-09-17-agent-delegate-and-take-over.md` |
| File panes are plain Qt, not KDE parts. | They already work on the future macOS/Windows path | `issues/features/needs_qa_llm/2026-09-17-file-explorer-and-preview-panes.md` |
| Vertical tabs deferred; top tabs for now. | | `issues/features/2026-09-17-tab-placement-vertical-tabs.md` |

## Near term: Linux beta 0

| Work | State | Issue or doc |
|---|---|---|
| `.deb` for Ubuntu 24.04 (KF5), Debian 13 and Ubuntu 26.04 (KF6), amd64 and arm64, via the release workflow | Workflows and scripts exist; no tag pushed | `issues/features/2026-09-17-website-and-beta-release.md`, [RELEASING.md](RELEASING.md) |
| AUR `relay-terminal` and `relay-terminal-git` | PKGBUILDs exist; AUR packages not created; AUR job disabled | `packaging/arch/`, [RELEASING.md](RELEASING.md) |
| Landing page on GitHub Pages | `site/` exists; Pages disabled; screenshots need replacing | `site/`, [RELEASING.md](RELEASING.md) |
| **QA pass by a non-Claude model** for every feature in `needs_qa_llm/` | Not started; 15 issues waiting | [VALIDATION.md](VALIDATION.md#the-qa-lane), [`issues/README.md`](../issues/README.md) |
| Manual check on a real KF6 desktop (Wayland and X11) | Not done | [VALIDATION.md](VALIDATION.md#not-verified) |
| Re-enable PDF preview in Qt6 packages | The `QPdfView` enum fix is in `src/FilePanes.cpp`; Qt6 packaging still disables Qt PDF | `packaging/deb/build-deb.sh`, `packaging/arch/*/PKGBUILD` |
| Queue or interrupt **shell commands** while a program runs | Designed, not implemented | `issues/features/2026-09-17-queue-shell-commands-while-busy.md`, [QUEUE-INTERRUPT.md](QUEUE-INTERRUPT.md) |
| Release notes: known limitations and privacy summary | Checklist in [RELEASING.md](RELEASING.md) | |

Known limitations to ship with beta 0: Linux only; rich prompt integration is Bash only;
agent cannot see terminal output or type into programs; folder and image clicks in terminal
output open the desktop app; agent tools run without approval and are not sandboxed.

## Mid term

| Work | Why | Issue or doc |
|---|---|---|
| **The engine on macOS and Windows** (forkpty is done, ConPTY is a stub) | The engine is Linux-proven and is now Relay's only terminal, so the remaining cross-platform work is the PTY, the key mapper and packaging | `issues/features/2026-09-17-portable-terminal-engine.md`, [ENGINE.md](ENGINE.md) |
| Parity gate before switching Linux | Re-run the spike's scripts against the owned engine; decide libvterm scroll patch vs. libghostty-vt | [ENGINE-SPIKE.md](ENGINE-SPIKE.md) |
| **Delegate and take over**: the agent types into the visible pane and reads its screen; any user key takes over | Needs `screenText`/`altScreen` from the backend, an "agent in control" indicator and immediate stop | `issues/features/2026-09-17-agent-delegate-and-take-over.md` |
| **Clickable paths everywhere**: folders, images and PDFs in terminal output open Relay panes | Text files already work through `relay-open`; the rest needs `LinkClicks` from the owned engine | `issues/features/2026-09-17-clickable-paths.md` |
| Keyboard stepping through paths and links in output | Needs screen and scrollback text | `issues/features/2026-09-17-keyboard-jump-to-output-links.md` |
| **Flatpak** | Needs host-process introspection and shared runtime paths abstracted (`/proc`, private state dir), host shell via `flatpak-spawn --host` | [DISTRIBUTION-RESEARCH.md](DISTRIBUTION-RESEARCH.md) section 1 and phase "Beta 1" |
| macOS and Windows packaging, signing and updates | After the engine; per-OS keystore (Keychain, Credential Manager), bundled Python, zsh/PowerShell integration | [DISTRIBUTION-RESEARCH.md](DISTRIBUTION-RESEARCH.md) section 2 |
| Agent improvements (candidates, not decided) | `edit_file` exact-match edits, context accounting and compaction, read-only `grep`/`glob`, plan mode, project instructions | [OPENCODE-NOTES.md](OPENCODE-NOTES.md) P1, P3, P5, P7, P8 |

## Longer term

| Work | Notes | Issue or doc |
|---|---|---|
| **Terminal-only Relay** (`relay-tui`) that runs inside any terminal, including over SSH | Reuses `backend/worker.py` and `relay_core` over the same JSON protocol. Options: a Bash/Zsh prompt wrapper (1–2 weeks) or a Textual app with a shell pane (4–6 weeks). Needs an owner decision on scope. | `issues/features/2026-09-17-terminal-only-tui-relay.md`, [NEXT-STEPS-RESEARCH.md](NEXT-STEPS-RESEARCH.md) section C |
| Keep UI logic out of the window layer (`src/Pane.h`, `src/RelayWindow.h`; one `src/main.cpp` until the 2026-09-18 split) and document the worker protocol as stable | What makes a second frontend cheap | [NEXT-STEPS-RESEARCH.md](NEXT-STEPS-RESEARCH.md) section C |
| Out-of-process pane rendering | Now possible in principle: the engine is Relay's own code | [NEXT-STEPS-RESEARCH.md](NEXT-STEPS-RESEARCH.md) section B, option 5 |

## Non-goals

- Forking or patching Konsole.
- Per-action approval prompts for agent tools.
- A Relay account or billing. Relay Free ([RELAY-FREE.md](RELAY-FREE.md)) is a hosted allowance
  without either, and BYOK never routes through it.
- Telemetry, analytics or crash reporting.
- A browser-based terminal of its own. The browser shows a *desktop pane* as a remote
  control that acts and feels like the terminal (`REMOTE-PROTOCOL.md` section 16); it never
  runs a shell, and keys and provider settings never leave the desktop (owner, 2026-09-18).
- croft-level IDE features (LSP, debugging) in the terminal-only variant
  ([NEXT-STEPS-RESEARCH.md](NEXT-STEPS-RESEARCH.md) section C).
