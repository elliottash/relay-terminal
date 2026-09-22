# Regression trace for #HCR2

User follow-up: “can you trace that regression”

## Commit history

- `683ff722` (2026-09-19 10:45 EDT) added automatic link coloring to the grid painter. `restLinkColumns` resolves paths, URLs and card references; `paintRow` applies the link ink over plain foregrounds.
- `8147cc55` (2026-09-19 18:00 EDT, shared-tree sweep) introduced `paintProseRow`, replacement folds, and pane prose registration. `FoldLayer::takenOver` switches from the original grid to replacement rows when the current width differs from the print width. The new painter colored only explicit `c.link` spans, omitting automatic resolution. This is the first committed source of the regression, determined by inspecting the parent and commit diffs, not by a historical binary bisect.
- `121fd9a1` (2026-09-19 18:47 EDT, #R2WQ) completed/documented the resize feature and added `proseReflowsOnResize`. Its assertions cover text layout, hanging indentation and prose-anchor behavior, not automatic link pixels. The renderer itself had already landed in the sweep.
- `1f71d605` (2026-09-21 16:57 EDT, originally #L9KC, now #K9KC) restored plain-text card hit testing in replacement rows. It added scanner/target and actual click checks, but no color assertion and no change to the painter. Thus references became clickable again while remaining plain.
- `ca46089c` (2026-09-21 21:56 EDT, #HCR2) adds cached resolved-link coloring to replacement prose and fold painting, with a pixel-and-target regression test across widths.

## Controlled reproduction

Exported `ca46089c^` to an isolated temporary directory, without changing the shared checkout or making a branch/worktree. Installed `engine/tests/ViewTest.cpp` from `ca46089c` into that export and added a diagnostic `qInfo` immediately before its pixel assertion to print width, coloring setting and ID. Built only `relay-engine-tests` through that export's `scripts/relay-build`.

Executed under Xvfb with isolated XDG_CONFIG_HOME, `QT_QPA_PLATFORM=xcb`, `RELAY_ENGINE_TEST=ViewTest`:

```
build/engine/relay-engine-tests hashReferencesKeepLinkInkAfterRewrap
```

Before: original width 100 passes for known/unknown references with coloring on/off. At width 40 with coloring enabled, the first known bold reference (#PRM2) fails the link-color pixel check.

Then replaced only `engine/view/TerminalView.cpp` and `.h` with their `ca46089c` versions in the same export, rebuilt and ran the same test. After: all three widths (100, 40, 120), both coloring settings, known/unknown IDs and click-target checks pass. This isolates the fix to the renderer changes; the earliest introducing commit is established separately by source history above.

The supplied screenshot alone cannot prove which width transition occurred in the user's pane. This reproduction establishes the missing-color failure and its resize trigger.

## Before log

```
********* Start testing of ViewTest *********
Config: Using QtTest library 5.15.13, Qt 5.15.13 (arm64-little_endian-lp64 shared (dynamic) release build; by GCC 13.3.0), ubuntu 24.04
PASS   : ViewTest::initTestCase()
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled true id "#PRM2"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled true id "#SDR1"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled true id "#ZZZZ"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled false id "#PRM2"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled false id "#SDR1"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled false id "#ZZZZ"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 40 enabled true id "#PRM2"
FAIL!  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) Compared values are not the same
   Actual   (rowHasColor(crop, 0, ch, scheme.link))   : 0
   Expected (enabled && id != QStringLiteral("#ZZZZ")): 1
   Loc: [/tmp/relay-hash-trace-dx090uto/engine/tests/ViewTest.cpp(1703)]
PASS   : ViewTest::cleanupTestCase()
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 227ms
********* Finished testing of ViewTest *********
```

## After log

```
********* Start testing of ViewTest *********
Config: Using QtTest library 5.15.13, Qt 5.15.13 (arm64-little_endian-lp64 shared (dynamic) release build; by GCC 13.3.0), ubuntu 24.04
PASS   : ViewTest::initTestCase()
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled true id "#PRM2"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled true id "#SDR1"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled true id "#ZZZZ"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled false id "#PRM2"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled false id "#SDR1"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 100 enabled false id "#ZZZZ"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 40 enabled true id "#PRM2"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 40 enabled true id "#SDR1"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 40 enabled true id "#ZZZZ"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 40 enabled false id "#PRM2"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 40 enabled false id "#SDR1"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 40 enabled false id "#ZZZZ"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 120 enabled true id "#PRM2"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 120 enabled true id "#SDR1"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 120 enabled true id "#ZZZZ"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 120 enabled false id "#PRM2"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 120 enabled false id "#SDR1"
QINFO  : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm) width 120 enabled false id "#ZZZZ"
PASS   : ViewTest::hashReferencesKeepLinkInkAfterRewrap(libvterm)
PASS   : ViewTest::cleanupTestCase()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 412ms
********* Finished testing of ViewTest *********
```
