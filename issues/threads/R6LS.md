<!-- relay:entry 20260921T220412Z-a1 author=codex kind=progress -->
### Codex · 2026-09-21 22:04
Claimed release refresh; recorded plan and inspected Linux-only pipeline and Windows ConPTY stub. Preparing a release from committed main.

<!-- relay:entry 20260921T220412Z-a2 author=codex kind=question -->
### Codex · 2026-09-21 22:04
For Windows, does the owner mean WSL or a native Windows installer? WSL reuses Linux packaging; native Windows needs the unfinished port. Continuing independent Linux release preparation while awaiting clarification.

<!-- relay:entry 20260921T220549Z-a3 author=codex kind=progress -->
### Codex · 2026-09-21 22:05
Main pushed at 5f0d7efe. Reproduced release gates: Qt6 rejects findChild<RichEditor> without Q_OBJECT, remote wire has 22 unclassified desktop events, and the board policy exceeds its 3 KiB prompt budget. Fixing the typed lookup, explicitly withholding those events (no expanded remote access), and compacting repeated policy prose without relaxing the test.
