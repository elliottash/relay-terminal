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

<!-- relay:entry 20260921T232856Z-a7 author=codex kind=progress -->
### Codex · 2026-09-21 23:28
Native desktop and installer ran on Windows Server 2022 (35666469644); inspected screenshot and logs, found and fixed a masked PowerShell cwd fixture failure. Final release 35667599551 uses immutable fcf44e13, with native tests, actual install/start/uninstall and all six Linux package gates required before publication. Desktop/mobile download layouts checked; links remain unpublished until assets exist.

<!-- relay:entry 20260921T234250Z-a8 author=codex kind=progress -->
### Codex · 2026-09-21 23:42
Native installer launch and worker startup passed again with clean logs. Runtime tests found module-heavy command discovery truncation (fixed 694ab9a8) and then an event publication failure under a deliberately held Windows file lock. Test cleanup now preserves the original assertion (e8fb83a3); shell integration owner is fixing publication. Linux builds continue; website links remain staged until all release gates pass. These failures occur on Windows x64, not the owner’s ARM64 system.

<!-- relay:entry 20260922T002724Z-a9 author=codex kind=progress -->
### Codex · 2026-09-22 00:27
Native Windows full gates and upload passed on 6fe3554f in 35670149891. Linux and local broad checks exposed stale fixtures from recent repository changes; corrected and targeted checks pass. New Try-it functionality received native Windows path/PowerShell support. Current release candidate 59a51d9 is running in 35671890154; previous artifacts are isolated. PF14 records the unresolved intermittent profiling assertion without weakening it. Updated evidence README with exact scope and results.

<!-- relay:entry 20260922T003311Z-b1 author=codex kind=progress -->
### Codex · 2026-09-22 00:33
Owner added: "can we build for mac as well". Expanded release scope to native Apple Silicon and Intel macOS bundles/DMGs. Plan uses Qt6, private modern Bash/Python, native Keychain and Darwin platform support; delegated platform, packaging and Python parts to the existing subagents. Existing Linux/Windows candidate 59a51d9 continues while the Mac port is implemented.

<!-- relay:entry 20260922T004951Z-b2 author=codex kind=progress -->
### Codex · 2026-09-22 00:49
Native macOS implementation and DMG packaging landed through df9ea698; both native architectures are running in 35673472747. Bundles contain modern Bash, Python and Qt, use Keychain, Darwin process APIs and macOS URL handling. Exact Linux app build passes. Windows release gates on 59a51d9 pass; Ubuntu 24 ARM encountered the known PF14 intermittent profiler assertion, with remaining Linux package jobs being checked before publication. No unverified download links deployed.

<!-- relay:entry 20260922T012743Z-b3 author=codex kind=evidence -->
### Codex · 2026-09-22 01:27
Published beta3 Linux/native Windows assets from verified59a51d9 after all platform gates passed. Bot publication failed403; authorized SSH tag plus gh publication succeeded. All uploaded hashes/sizes and eight direct downloads verified. Deployed site d4854390, exact HTML/CSS on apex+www. Mac source/build/runtime tests pass, with packaging audits being completed; final Mac downloads remain pending. Evidence: beta3-publication.txt.

<!-- relay:entry 20260922T013821Z-m4 author=codex kind=progress -->
### Codex · 2026-09-22 01:38
Beta.4 release run 35676338727 is building fixed source c4c1c50a for six Linux packages, native Windows and both Mac architectures. Mac DMG signing/mounting/private TLS and shell checks have passed in preceding native runs; corrected shutdown fixture and stronger GUI runtime discovery check are included in this candidate. Desktop/mobile download previews recorded; live beta.3 links remain until successful publication.
