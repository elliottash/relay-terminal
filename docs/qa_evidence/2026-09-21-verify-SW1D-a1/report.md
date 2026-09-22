# Independent bounded verification of #SW1D

2026-09-21 local / 2026-09-22 UTC, Codex verifier a1. **PASS for row/format-check/Performance routing and automated cleanup transitions; live model preview/apply NOT VERIFIED.** No source/card/thread changes.

Reviewed e325845d and 7133156a, the implementer README and screenshot evidence descriptions, and independently staged a new disposable Board. Existing screenshots were not counted as independent proof. This run used named socket controls without coordinate input.

| Done means | Verdict and fresh evidence |
|---|---|
| Exactly Hygiene, Tests, Performance in order | PASS: `ui.png` visually inspected; `transcript.json` reads their actual labels and verifies Clean up is initially hidden. No separate Board Check/Clean up in the tool row. |
| Hygiene checks format first; second stage runs cleanup preview/apply | PARTIAL live, PASS widget transitions: `boardChatCheck` produced `Hygiene · Format check: nothing to fix`, then exposed Clean up in the same area. Pressing Clean up opened cleanup preview but the helper refused the unconfigured guest provider. `hygieneChecksBeforeCleanup` independently rerun passes format-only request, dry_run=true, Stop/cancel, supplied summary and explicit Apply dry_run=false. A model-generated preview and successful Apply were not demonstrated. |
| Performance retains target behavior on row | PASS routing/label: opened boardProfile then pressed profileTarget:build; actual Performance pane and matching title appeared (`performance.png`). It reported no CMakeLists.txt in this minimal fixture, exit 3; that is the fixture limitation, not a successful performance measurement. Four-target behavior covered by profilepane tests; this live run selected Build only. |
| Per-card Check unchanged | PASS supporting independent evidence: preceding #74Y5 verification at ece752ef drove boardTestsCheck and read its real result; this follow-up changes PaneStatus/tests only. Board/card widget suite remains passing. |
| Visible Hygiene/Performance wording; stable identifiers | PASS on observed row, findings, preview, notice and Performance pane/chrome. Stable keys boardChatCheck, boardCleanup, boardProfile/profileTarget:build retained. No exhaustive source-string audit claimed. |

## Tests and provenance

`QT_QPA_PLATFORM=offscreen build/relay-boardpane-tests hygieneChecksBeforeCleanup` passes. `ctest --test-dir build -R '^(board|boardpane|profilepane|panestatus)$' --output-on-failure` passes 4/4 (2.65 s); full output in tests.txt. These are existing shared test binaries, separately identified from the exact live gate. No rebuild competed with the parent's current #7BM4 work.

Live binary `/tmp/claude-1000/land/sw1d-follow/verify/build/relay`, SHA256 `90b0a5aba3eab1510009c7aebd5161ba7efd9d8416946270e0492d5a18a6ffbe`, matching backend `/tmp/claude-1000/land/sw1d-follow/verify/src`. Independently compared 391 scoped manifest entries against `7133156a0d1955c4ecf35c68ac705b2cd4a218d0`: zero differences. Xvfb `:300`, fresh sandbox `/tmp/vsw1d-z1y315d2`, isolated HOME/XDG/runtime/tmp, keyring off, RELAY_NO_ISOLATION=1. App and Xvfb stopped after capture. All 16 named operations and responses are in transcript.json; drive.py reproduces the fixture.

## Remaining limit

A supported model is required to complete the real cleanup preview/apply. The deterministic tests supply worker events and verify the UI's requests; they do not prove a real model produces a valid proposal or that its application completes. No code failure observed in this bounded check; do not describe this as complete end-to-end cleanup verification. Rate limit respected: evidence only, no card or thread writes.
