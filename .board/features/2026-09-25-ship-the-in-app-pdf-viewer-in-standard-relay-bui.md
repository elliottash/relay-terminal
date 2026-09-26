---
id: 9Y7X
type: work
status: executing
labels: [feature, pdf, ui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: cfad5dd0-a071-4afc-9db8-b1699f0bc04e
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: visual, primary: probe, also: [script, ai-visual], human: optional, criteria: 'Open local and SSH-hosted multi-page PDFs in an installed build; pages render inside Relay, scroll normally, and remain visible after reload.', sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [f9c7d5904584, 0d5ac28e32d7, 4b4dd33621a3, 5e58c8fe3fba], evidence: [], related: [WQFS, P2W8], github: null}
---
# Ship the in-app PDF viewer in standard Relay builds

## Issue
Make PDF files open and render inside Relay in the builds the user installs. The QPdfView code exists, but Qt PDF is disabled in the current Debian/Ubuntu Qt 6 and Arch package recipes, so those builds cannot provide the viewer.

> new card: i need the in-app pdf viewer.
> — elliott · [session:f32c83e1970648d581feee4ae3447445](relay://session/f32c83e1970648d581feee4ae3447445) · 2026-09-25

## Plan
**Goal.** A PDF opened from Relay's file explorer or an artifact workspace renders in a Relay pane in an installed build, for local and SSH files.

**Findings.** `src/FilePanes.cpp` already creates a `QPdfDocument`/`QPdfView` and handles local and remote PDFs; the Qt 6 scoped enum branch is present. `CMakeLists.txt` enables it only when Qt Pdf and PdfWidgets are found. `packaging/deb/build-deb.sh` omits `qt6-pdf-dev` for Debian 13 and Ubuntu 26.04, with an outdated enum warning. Both Arch `PKGBUILD` files explicitly disable Qt PDF because its Arch package pulls in Qt WebEngine. `tests/filepanes_test.cpp` currently accepts either a rendered remote PDF or an unavailable notice. The decision on #P2W8 says to bundle Qt PDF when packaging is small and smooth, otherwise provide a plugin adapter and external opening.

**Steps.**
1. Check each supported package route and its actual Qt PDF build/runtime dependencies, including `packaging/deb/build-deb.sh`, both Arch `PKGBUILD` files, and the Windows/macOS release workflows. Record the dependency and size tradeoff before changing package requirements.
2. Enable Qt Pdf/PdfWidgets in the standard package routes where the dependency is reasonable. Remove stale disable flags and comments, declare runtime dependencies, and make package smoke checks fail if a package intended to include PDF support reports it disabled. Keep CMake's optional source-build path intact.
3. If a platform's dependency is too large for the standard package, specify and package the PDF viewer as an installable adapter, using #7WGJ for its missing-viewer install action. Keep `Open externally` usable until the adapter is installed; record that platform's dependency on #7WGJ.
4. Strengthen `tests/filepanes_test.cpp` to require `FilePreview::Kind::Pdf` in a PDF-enabled build for both local and remote valid sample files, and to check a clear fallback in a deliberately PDF-disabled build. Cover a malformed PDF separately so missing support is not mistaken for a corrupt file.
5. Update `docs/BUILDING.md` and the package documentation with which builds include the viewer and how to obtain it where optional.

**Risks.** Arch's Qt PDF dependency currently brings Qt WebEngine; apply #P2W8's existing size decision rather than silently adding it. Qt PDF availability and deployment differ across OS packages. Remote loading keeps a `QBuffer` alive for `QPdfDocument`, so test the remote path after a reload. #7WGJ owns the generic install-button UI.

**Verify.** Build and run the targeted `relay-filepanes-tests` on a Qt PDF-enabled build and a disabled build. Inspect configure output and installed package dependencies for each affected package route. Under an isolated GUI profile, open a two-page local PDF and an SSH-hosted PDF in Relay, scroll both, and capture a screenshot of the rendered pane. Check the external-open fallback on a build without PDF support.

## Done means
- An installed Relay build advertised as PDF-enabled opens a valid local PDF in a pane and renders every page with scrolling.
- The same build renders a valid PDF fetched from an SSH host; a corrupt PDF reports a load error.
- Package smoke checks catch a missing Qt PDF dependency or a build that silently disables the viewer.
- Where the viewer is intentionally optional, the preview identifies missing support and retains Open externally; the installation action is tracked by #7WGJ.
