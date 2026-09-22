# Public build refresh — #R6BS

The owner authorized native Windows and accepted PowerShell 7 as its default. Work is committed to
main through land.py; the shared checkout's unrelated unfinished changes are excluded.

## Verified milestones

- Website refresh and corrected heading decoration deployed in e24a7d06, de16c983 and f1982f4e.
- Native ConPTY lifecycle smoke passed on Windows runner [35665510167](https://github.com/elliottash/relay-terminal/actions/runs/35665510167).
- Native identity, process telemetry and crash-log smoke passed in [35665925721](https://github.com/elliottash/relay-terminal/actions/runs/35665925721).
- PowerShell multiline load/hash acknowledgement/execute was driven through a real PSReadLine terminal locally. Native test caught a Windows file-sharing race; fixed 2cfcb3f0 and GUI reader closes promptly.
- Qt6 exact-tree builds and relevant theme, SSH, voice, board, model and console tests passed. Qt6 terminal palette tests were made deterministic after pixel evidence showed antialiasing rounding, not wrong colors (337a2d53).
- Targeted Python storage, locking, keys, sessions, routing, jobs, prompt and release-fixture tests passed. Native Windows-only cases run in windows.yml.

## Final release gates

Native Windows source `6fe3554f` passed the complete runtime, console lifecycle, desktop build,
installer packaging, bundled Unicode worker, installed GUI launch and normal exit/uninstall gates.
The release run's final installer upload received a GitHub HTTP 403; an identical-source standalone
[rerun](https://github.com/elliottash/relay-terminal/actions/runs/35670149891) passed and uploaded it.
The screenshot `windows-installed.png` shows the first-run instructions dialog. Logs confirm the
worker configured and shut down cleanly; stderr and crash output were empty. This is launch evidence,
not full interactive desktop QA.

PowerShell discovery now keeps core commands on module-heavy machines (694ab9a8). Atomic prompt-state
replacement retries Windows access-denied/sharing failures (a613f215); the deliberate file-lock test
passes natively. These failures occurred on GitHub's Windows x64 runner, unrelated to the owner's ARM64 host.

The six Linux package builds exposed stale fixtures for root file permissions, native path separators,
queue recall, Ghostty's documented approximate byte budget, project initialization and newer Switchboard
features. Corrections preserve the product contracts and assert the new behavior. A local committed
snapshot ran 4,960 backend tests with three identified failures; all three were corrected and their
110 targeted checks pass. An isolated Qt5 build ran 88 C++ tests with one stale QA-action fixture;
that fixture was corrected, and board/console/continue tests pass. #PF14 separately records an intermittent
live profiling assertion; it remains enabled, with raw profiler diagnostics added.

Newer Try-it staging now handles native Windows temporary paths, PowerShell scripts and paths containing
spaces; the native release gate includes its tests. New desktop-only events remain explicitly withheld
from the remote terminal stream, with the board's existing scrubbed owner bridge handling queue replies.

The current immutable release candidate is `59a51d92578b2c77284e2ecb9ffcde98f7671b7d`,
[release run 35671890154](https://github.com/elliottash/relay-terminal/actions/runs/35671890154).
All six Linux build/test/install gates and the native Windows gates must pass before publication.
No beta.3 release or new website links have been published yet. Earlier-source artifacts are kept
separate and will not be mixed into this release.

## Native macOS implementation (verification in progress)

- Source df9ea698: App bundle, private Python/Bash paths, portable signal shutdown, native process argv, Darwin timestamps and Mac update/link handling. Exact-tree Linux GUI build passed.
- f2888f05 and ed47caac: native libproc identity/usage, sysctl memory/argv and standalone native smoke; targeted Linux platform tests pass.
- 5c4a321b, cd0f6792, 4b730020: native Keychain, file opening, Bash jobs, Try-it and platform-aware main/delegated/router prompts. Targeted Python regressions pass; Keychain awaits native runner.
- e1000574: two native Mac runners, pinned private runtimes, DMG packaging, relocation/signature audit and installed GUI/PTY/worker checks. Local script syntax and Bash patch application checks pass.
- Native workflow: https://github.com/elliottash/relay-terminal/actions/runs/35673472747 (Apple Silicon and Intel). Downloads remain pending successful native gates.

## Linux and Windows published

`v0.1.0-beta.3` is live at source `59a51d9`, after all six Linux package jobs and both Windows gates passed. Uploaded digests, package metadata, source provenance and live site checks are recorded in `beta3-publication.txt`. Site downloads deployed from `d4854390`; all direct assets return 200 and both hostnames match the committed HTML/CSS. Mac verification continues separately.

## macOS release candidate and website preview

Candidate `c4c1c50aa571cfd184b5f92f905cffe8507da8c8` is fixed for [beta.4 release run 35676338727](https://github.com/elliottash/relay-terminal/actions/runs/35676338727). It includes native process/Keychain support, private runtimes, macOS bundle relocation and TLS fixes, corrected terminal test synchronization, and a GUI smoke that removes runtime overrides before launch. Publication awaits all native and Linux package gates.

`macos-downloads-desktop.png` and `macos-downloads-mobile.png` show the prepared download section at 1440px and 390px. Playwright finds 15 installation-section links and no horizontal overflow at either width. This is a local preview; beta.3 stays live until beta.4 payloads are published and verified. The heading decoration remains separated from the text.

## Beta.4 published — all platforms

[Release v0.1.0-beta.4](https://github.com/elliottash/relay-terminal/releases/tag/v0.1.0-beta.4) is live from immutable source `7d59e9673f680baf34a29fbebfc95cd31f667eab`. [Run 35677749794](https://github.com/elliottash/relay-terminal/actions/runs/35677749794) passed all six Linux build/test/install gates, both Windows gates, both native Mac gates and automatic publication. The AUR job remains intentionally disabled. No artifacts were mixed from earlier candidates.

Mac installed-app screenshots and concise logs are `macos-arm64-installed.png`, `macos-intel-installed.png`, and corresponding `*-checks.txt`. Both DMGs were mounted, copied elsewhere and ejected before exercising private Bash/Python, UTF-8 command handling, 128-root HTTPS trust, GUI startup, real file-opening IPC, normal shutdown and strict signature verification after use. Keychain and Darwin process tests also passed natively. The DMGs are ad-hoc signed, not notarized; the Windows installer is unsigned. These checks are installation/runtime evidence, not exhaustive independent desktop QA.

`beta4-publication.json` records 11 public asset sizes and SHA256 digests, 10 live download links, exact source/tag provenance, and HTML/CSS hashes for both site hostnames. All local payload hashes match both the release API and SHA256SUMS. The source archive’s Git PAX comment and the remote tag both match 7d59e967. Website commit 06418a97 is pushed and deployed; apex and www each match its HTML/CSS byte for byte. The recorded desktop/mobile previews have no horizontal overflow and keep the heading line clear of text.

Release notes are preserved in `beta4-release-notes.md`. Earlier milestone sections above are chronological records, superseded by this publication. #PF14 records profiler fixture isolation; #LSP1 records the separately reproduced catalog-listener test contamination. The frozen beta.4 run passed every gate without a retry; the later test-only #LSP1 fix remains on main for future runs.
