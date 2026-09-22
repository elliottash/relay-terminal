Relay 0.1.0 beta 4 adds native macOS builds for Apple Silicon and Intel, alongside Linux and native Windows.

## Changes

- Board, Sessions and Globals panes, reusable memories and saved model profiles.
- Agent consoles in Board cards, Options, Actions and Sessions. Check, Verify and Try it support test review and staged hands-on QA.
- Model classes, ranked choices, supported reasoning levels and conversation-preserving model switching.
- Claude Code and Codex guests can delegate into Relay subagent tabs.
- Phone pairing by code/PIN, phone Board access and notifications for work waiting on you.
- Configurable approvals, session recaps, keyboard navigation, pane dimming and readable folded output.
- Linux packages include the pinned libghostty-vt core, with libvterm still available.

## Native macOS application

Choose `relay_0.1.0-beta.4_macos_arm64.dmg` for Apple Silicon or `relay_0.1.0-beta.4_macos_x64.dmg` for Intel. macOS 13 Ventura or newer is required. Verify the SHA256 checksum, open the disk image and drag Relay to Applications.

Qt, modern Bash and Python are included; Homebrew and Rosetta are not required. API keys use the macOS Keychain. Relay uses Bash for its terminal and agent commands without changing your login shell.

These beta bundles are ad-hoc signed, but are not Developer ID signed or notarized. After attempting to open a trusted download, use System Settings → Privacy & Security → Open Anyway if macOS blocks it. [Apple documents the process](https://support.apple.com/en-us/102445).

The native gate runs Keychain/storage, worker, Darwin process tracking, crash logging and PTY tests on both architectures. It mounts each DMG, copies the app to a different location, ejects the image, and checks private runtimes, HTTPS trust, GUI startup and shutdown, and signatures after execution.

The first Mac beta uses libvterm. Voice capture, systemd resource limits and remote terminal typing are unavailable; ordinary local Bash and SSH remain available. Update by replacing the app with the newer download. Independent real-desktop QA remains ongoing.

## Native Windows installer

Download `relay_0.1.0-beta.4_windows_x64_setup.exe`, verify its SHA256 checksum, and install for your user.
Open Relay from the Start menu. This is a native Qt6/ConPTY application with PowerShell 7 and Python
included; WSL and separate runtime installations are not required. API keys use Windows Credential Manager.

The Windows installer is unsigned. Windows may show an unknown-publisher warning. This first native
beta uses libvterm; voice capture, SSH connection multiplexing, systemd resource limits and remote terminal
typing are unavailable on Windows. Local PowerShell and ordinary SSH commands work independently of those
features. Windows builds are tested on Windows Server 2022; real Windows desktop QA remains ongoing.

The native release gate compiles the desktop, tests shell input/hash acknowledgement, command jobs and
cancellation, Unicode worker messages, credentials and file locking, then installs, launches and uninstalls
the actual installer. Update Windows builds by running the newer installer.

## Linux installation

Download the `.deb` matching your distribution (Ubuntu 24.04, Ubuntu 26.04 or Debian 13) and CPU (amd64 or arm64). Verify it against `SHA256SUMS`, then install it with `sudo apt install ./relay_*.deb` and run `relay`.

A source archive is also included. Package builds run the test suite and install/start smoke checks in clean distribution containers.

## Beta status and privacy

Features are still undergoing independent and real-desktop QA. Package/container checks do not establish that every UI feature works on every desktop.

No telemetry or Relay account. Relay Free requests pass through Relay's gateway; requests using your own key go directly to the selected provider. Background jobs can select a different provider. Agent commands run as your user; approval controls do not sandbox commands or undo changes. Guest agents also follow their own permission settings.

## Build provenance

Source: `7d59e9673f680baf34a29fbebfc95cd31f667eab`. All downloadable binaries and the source archive are produced by [release run 35677749794](https://github.com/elliottash/relay-terminal/actions/runs/35677749794). `SHA256SUMS` covers every payload.
