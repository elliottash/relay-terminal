<!-- relay:entry 20260921T220412Z-a1 author=codex kind=progress -->
### Codex · 2026-09-21 22:04
Claimed release refresh; recorded plan and inspected Linux-only pipeline and Windows ConPTY stub. Preparing a release from committed main.

<!-- relay:entry 20260921T220412Z-a2 author=codex kind=question -->
### Codex · 2026-09-21 22:04
For Windows, does the owner mean WSL or a native Windows installer? WSL reuses Linux packaging; native Windows needs the unfinished port. Continuing independent Linux release preparation while awaiting clarification.

<!-- relay:entry 20260921T220549Z-a3 author=codex kind=progress -->
### Codex · 2026-09-21 22:05
Main pushed at 5f0d7efe. Reproduced release gates: Qt6 rejects findChild<RichEditor> without Q_OBJECT, remote wire has 22 unclassified desktop events, and the board policy exceeds its 3 KiB prompt budget. Fixing the typed lookup, explicitly withholding those events (no expanded remote access), and compacting repeated policy prose without relaxing the test.

<!-- relay:entry 20260921T220805Z-a4 author=codex kind=progress -->
### Codex · 2026-09-21 22:08
Corrected card ID R6LS (L is outside the board alphabet) to R6BS. Gate fixes landed e4bfa994 after exact-tree build and consolemode check. Updated the board test helper to use the named QPlainTextEdit too, and kept the existing source guard aligned with that Qt6-compatible lookup. Windows clarification remains pending; Linux work continues.

<!-- relay:entry 20260921T221042Z-a5 author=codex kind=decision -->
### Codex · 2026-09-21 22:10
Owner: "Native Windows installer". This requires the native port rather than a WSL wrapper. Starting the independent ConPTY backend and native smoke-test job; asked whether the installer should use PowerShell or bundled Bash before implementing shell integration. Linux CI is running at a59525c0.

<!-- relay:entry 20260921T225943Z-a6 author=codex kind=decision -->
### Codex · 2026-09-21 22:59
Owner: "ok, sounds good, deliver it with a subagent". PowerShell 7 accepted. Delegated shell/command integration, Windows platform APIs, and Python locking/credentials to bounded subagents; parent owns application startup, installer, CI, release and site.
