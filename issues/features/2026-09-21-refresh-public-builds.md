---
id: R6BS
type: work
status: executing
labels: [feature, packaging, website]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex user request, 2026-09-21'
links: {plans: [], commits: [aa104e92, 92563ceb, ff18787e, f2216313, 2cfcb3f0, bb769e98, a5535f4c, 460e8063, fcf44e13], evidence: [docs/qa_evidence/2026-09-21-refresh-public-builds/README.md], related: [W9ST, P4GP, 3AZG, YJK8], github: null}
---
# Publish refreshed builds and website downloads

## Issue
update a clean commit, push, and update the linux and windows builds available on the web site

## Decisions
"Native Windows installer" — Windows means a native application, not WSL.

"ok, sounds good, deliver it with a subagent" — owner accepted PowerShell as the Windows default and delegated implementation.

## Planning notes
Published releases end at v0.1.0-beta.2 and contain six Linux packages, source and checksums. The release pipeline builds Linux only. `engine/pty/PtyWin.cpp` explicitly returns “ConPTY backend not implemented yet”; Windows native packaging is absent. Asked whether Windows means WSL or a native installer while preparing the independent Linux work.

## Plan
**Goal:** Push committed work, publish verified updated packages, and expose accurate downloads on the site.
**Findings:** `.github/workflows/release.yml`, `packaging/deb/`, `docs/RELEASING.md`. The working tree includes other sessions’ unfinished changes; only committed source will be released.
**Steps:**
1. Prepare beta.3 metadata, push main and inspect CI on that exact revision.
2. Resolve release build failures within scope, cut a fresh tag and verify package jobs and smoke tests.
3. Implement and smoke-test ConPTY on a native Windows runner; establish the native application/installer build, including platform-specific runtime and shell integration. Use bundled PowerShell 7 as the default shell, with native ConPTY and private Python runtime.
4. Publish release notes and update website download links only after assets exist; handle Windows according to the owner's clarification.
**Risks:** Existing CI failures may block release. Native Windows support is a port, not a rebuild; no Windows binary will be advertised without a working build.
**Verify:** CI, package smoke tests, release asset checksum verification, website links and deployed bytes.

## Tasks
- [x] Review website changes, correct product description and heading decoration. <!-- t:a1 -->
- [x] Implement native ConPTY, PowerShell integration and Windows runtime support with subagents. <!-- t:a2 -->
- [x] Build and exercise a per-user Windows installer on a native runner. <!-- t:a3 -->
- [ ] Pass final release gates, publish Linux and Windows assets and verify checksums. <!-- t:a4 -->
- [ ] Deploy versioned website downloads and verify the live page. <!-- t:a5 -->

## Tests
- `python3 -m unittest discover -s tests -p test_platform_storage.py -v`
- `python3 -m unittest discover -s tests -p test_powershell.py -v`
- `python3 -m unittest discover -s tests -p test_windows_open.py -v`
- `ctest --test-dir build-windows -C Release --output-on-failure`
- `manual: docs/qa_evidence/2026-09-21-refresh-public-builds/README.md`
