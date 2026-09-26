---
id: YJK8
type: work
status: deferred
labels: [feature]
component: [gui]
milestone: cross-platform
workstream: terminal
rank: 5t
created: '2026-09-17'
acceptance: Relay runs on macOS and Windows with tabs, panes, composer, inline agent output and clickable paths
source: 'owner, 2026-09-17: "relay having to work on mac and windows (not necessarily now)"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Portable terminal engine for macOS and Windows

## Issue

Make Relay's terminal and agent panes usable on macOS and Windows. The original Linux-only
KonsolePart constraint is gone: Relay's own engine replaced it on 2026-09-18.

## Planning notes

Owner, 2026-09-26: "yjk8 not yet". Do not schedule native-platform acceptance or
notarization work now. Resume when the owner asks to pursue cross-platform delivery; the
installed-app checks in the plan remain the gate.

Freshness check, 2026-09-26: `docs/ENGINE.md` describes the engine as the sole backend and
`engine/pty/PtyWin.cpp` now implements ConPTY. `docs/BUILDING.md` and the native GitHub workflows
build Windows and both macOS installers; the beta.4 release publishes all three. The old
libvterm/Konsole migration plan is therefore historical. The remaining question is whether the
installed apps meet this card's end-to-end acceptance on each platform, not whether packaging
exists. The macOS signing/notarization and platform input paths still need explicit evidence.

## Done means

On a clean macOS and Windows installation, Relay opens a terminal tab and split pane, accepts
typing and resizing, runs an agent from its composer, renders inline output, and opens a clickable
path. Native smoke evidence names the installed artifact and platform. A package that launches but
cannot run its terminal or agent does not pass.

## Plan

**Goal.** Prove and close the platform acceptance against the existing native installers.

**Findings.** The engine and native package workflows exist (`docs/ENGINE.md`,
`.github/workflows/{macos,windows}.yml`, `docs/BUILDING.md`). The beta.4 release has macOS arm64,
macOS x64 and Windows x64 installers. `docs/ENGINE.md` still calls some platform paths untested;
that statement must be reconciled with current native smoke results.

**Steps.**
1. Collect the latest native workflow and installed-smoke results for all three artifacts, and
   compare their actual checks with the Done means above.
2. Run the missing installed-app interaction checks on each platform: terminal input/resize,
   split, composer turn, inline output, and path click. Record screenshots/logs by artifact SHA.
3. File distinct platform bugs for measured failures and fix them on those cards; keep this card
   as the cross-platform acceptance tracker. Update `docs/ENGINE.md` where its status table is stale.
4. Close this card only when all three artifacts pass; record any macOS signing/notarization
   distribution work separately if it is outside the chosen installation path.

**Risks.** Native CI can build an installer without proving a live agent turn. Apple notarization
affects distribution, but its requirement for this card needs an owner decision.

**Verify.** Native installed-smoke output plus a live interaction record for macOS arm64, macOS
x64 and Windows x64. Do not substitute a Linux engine test for a platform acceptance check.
