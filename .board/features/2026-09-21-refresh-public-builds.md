---
id: R6BS
type: work
status: needs-verification
labels: [feature, packaging, website]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex user request, 2026-09-21'
links: {plans: [], commits: [aa104e92, 92563ceb, ff18787e, f2216313, 2cfcb3f0, bb769e98, a5535f4c, 460e8063, fcf44e13, df9ea698, 5c4a321b, f2888f05, e1000574, 82b837c5, 7d59e967, d4854390, 06418a97, 7d0f1718, 8e6094d9], evidence: [docs/qa_evidence/2026-09-21-refresh-public-builds/README.md], related: [W9ST, P4GP, 3AZG, YJK8, PF14, 1SP1], github: null}
---
# Publish refreshed builds and website downloads

## Issue
update a clean commit, push, and update the linux and windows builds available on the web site

can we build for mac as well

## Decisions
"Native Windows installer" — Windows means a native application, not WSL.

"ok, sounds good, deliver it with a subagent" — owner accepted PowerShell as the Windows default and delegated implementation.

## Planning notes
Published releases end at v0.1.0-beta.2 and contain six Linux packages, source and checksums. The release pipeline builds Linux only. `engine/pty/PtyWin.cpp` explicitly returns “ConPTY backend not implemented yet”; Windows native packaging is absent. Asked whether Windows means WSL or a native installer while preparing the independent Linux work.

## Done means
Verified Linux packages, native Windows installer and native Apple Silicon/Intel Mac DMGs are published from a fixed tested source. The live website describes Relay accurately, keeps its heading decoration clear of text, and links to those downloadable assets with platform requirements and signing limitations. Release checksums and deployed bytes are recorded for independent verification.

## Plan
**Goal:** Push committed work, publish verified Linux, native Windows and native macOS packages, and expose accurate downloads on the site.
**Findings:** `.github/workflows/release.yml`, `packaging/deb/`, `docs/RELEASING.md`. The working tree includes other sessions’ unfinished changes; only committed source will be released.
**Steps:**
1. Prepare beta.3 metadata, push main and inspect CI on that exact revision.
2. Resolve release build failures within scope, cut a fresh tag and verify package jobs and smoke tests.
3. Implement and smoke-test ConPTY on a native Windows runner; establish the native application/installer build, including platform-specific runtime and shell integration. Use bundled PowerShell 7 as the default shell, with native ConPTY and private Python runtime.
4. Add native macOS builds for Apple Silicon and Intel: Qt6 application bundle, private modern Bash/Python runtimes, native Keychain and Darwin process support. Package DMGs and smoke-test the real app on native runners. Ad-hoc signing is available; notarization requires Apple signing credentials not currently configured.
5. Publish release notes and update website download links only after assets exist. Keep each platform artifact tied to its tested source; do not delay already-verified Linux/Windows assets unnecessarily while adding macOS.
**Risks:** Existing CI failures may block release. Native Windows support is a port, not a rebuild; no Windows binary will be advertised without a working build.
**Verify:** CI, package smoke tests, release asset checksum verification, website links and deployed bytes.

## Tasks
- [x] Review website changes, correct product description and heading decoration. <!-- t:a1 -->
- [x] Implement native ConPTY, PowerShell integration and Windows runtime support with subagents. <!-- t:a2 -->
- [x] Build and exercise a per-user Windows installer on a native runner. <!-- t:a3 -->
- [x] Implement and smoke-test native macOS application bundles for Apple Silicon and Intel. <!-- t:a6 -->
- [x] Pass final release gates, publish supported platform assets and verify checksums. <!-- t:a4 -->
- [x] Deploy versioned website downloads and verify the live page. <!-- t:a5 -->

## Execution Summary
Published [beta.4](https://github.com/elliottash/relay-terminal/releases/tag/v0.1.0-beta.4) from 7d59e967 after every Linux, Windows and Mac package gate passed in run 35677749794. Native Mac DMGs include Bash, Python and Qt; Keychain and Darwin integration work on both architectures. Website 06418a97 is committed, pushed and deployed at https://relay-terminal.ai/, with verified asset links and corrected heading layout. All 11 files match their release API digests and all 10 payloads match SHA256SUMS; both hostnames match the committed HTML/CSS. Evidence: `docs/qa_evidence/2026-09-21-refresh-public-builds/README.md`. Mac bundles are ad-hoc signed, not notarized; Windows installer is unsigned. Independent desktop QA remains the next stage.

## Tests
- `python3 -m unittest discover -s tests -p test_platform_storage.py -v`
- `python3 -m unittest discover -s tests -p test_powershell.py -v`
- `python3 -m unittest discover -s tests -p test_windows_open.py -v`
- `ctest --test-dir build-windows -C Release --output-on-failure`
- `manual: docs/qa_evidence/2026-09-21-refresh-public-builds/README.md`

- `manual: docs/qa_evidence/2026-09-21-refresh-public-builds/beta4-publication.json`
- `manual: docs/qa_evidence/2026-09-21-refresh-public-builds/macos-arm64-checks.txt`
- `manual: docs/qa_evidence/2026-09-21-refresh-public-builds/macos-intel-checks.txt`
