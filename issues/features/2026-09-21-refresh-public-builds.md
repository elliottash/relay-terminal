---
id: R6BS
type: work
status: discussing
waiting_on: owner
labels: [feature, packaging, website]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex user request, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [W9ST, P4GP, 3AZG, YJK8], github: null}
---
# Publish refreshed builds and website downloads

## Issue
update a clean commit, push, and update the linux and windows builds available on the web site

## Planning notes
Published releases end at v0.1.0-beta.2 and contain six Linux packages, source and checksums. The release pipeline builds Linux only. `engine/pty/PtyWin.cpp` explicitly returns “ConPTY backend not implemented yet”; Windows native packaging is absent. Asked whether Windows means WSL or a native installer while preparing the independent Linux work.

## Plan
**Goal:** Push committed work, publish verified updated packages, and expose accurate downloads on the site.
**Findings:** `.github/workflows/release.yml`, `packaging/deb/`, `docs/RELEASING.md`. The working tree includes other sessions’ unfinished changes; only committed source will be released.
**Steps:**
1. Prepare beta.3 metadata, push main and inspect CI on that exact revision.
2. Resolve release build failures within scope, cut a fresh tag and verify package jobs and smoke tests.
3. Publish release notes and update website download links only after assets exist; handle Windows according to the owner's clarification.
**Risks:** Existing CI failures may block release. Native Windows support is a port, not a rebuild; no Windows binary will be advertised without a working build.
**Verify:** CI, package smoke tests, release asset checksum verification, website links and deployed bytes.
