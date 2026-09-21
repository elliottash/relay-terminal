# Card-link regression audit — #K9KC

## Introducing commits

1. **8147cc55b77000eebdf21cdeff5acdd5dc53d5b9**, 2026-09-19 18:00 EDT, “src and engine: the sessions' in-flight code as the shared tree holds it”. This 27-file batch introduced prose replacement rows (`setProseBlock`, `FoldLayer::setProse`, `paintProseRow`). At widths other than the print width, ordinary replies started using FoldLayer rows. `linkAt` only handled explicit span links there; `frameRowOf` returned -1 and skipped the ordinary text scanner. Thus plain card references, paths and URLs stopped being mouse links after resize. The older `frameRowOf` behavior dates to b56850d1, when folds were inserted tool details; **8147cc55 exposed it to ordinary replies**. Fixed by 1f71d605 under this card.

2. **195f2442**, 2026-09-21 07:57 EDT, “transcript: the pane hands the renderer the block a label will sit in (#MDKN)”. This enabled `MarkdownAnsi::setLinkAnchor` for actual pane replies. It completed **a7ba08d8** (07:42, emit label OSC links) and **70a288c9** (07:48, resolve those links). A reply such as `[#GWXM: title](/absolute/card.md)` now followed its explicit file target instead of scanning the visible `#GWXM` text. The saved session contains exactly that form. The file dispatcher lacked card-file recognition; 1f71d605 added it.

Attribution is based on parent/commit diffs and runtime controls isolating the newly introduced rendering path, **not a full historical-binary bisect**.

## Additional findings

| Finding | Reproduction and result | Origin / status |
| --- | --- | --- |
| Moved card-file target disables a known card label after resize | `[#GWXM](/no-such-card-audit.md)` links at the print width, but returned an empty target at 62 columns. | 70a288c9's fold-label branch returned immediately when resolving the target failed, unlike the native-row fallback. Fixed in dae7a639 during this audit, with the `#M0VD` regression case in ViewTest. |
| Copying after a wide character returns different text | Select `ABC` in `中ABCDEF`: copies `ABC` before resize and `BCD` after resize. | #C7WP, filed. 8147cc55 extended an older fold-selection assumption to prose: grid columns are added to grapheme indices without accounting for cell width. |
| Copy drops a space at a wrapped edge | At 20 columns, `alpha beta gamma xyz delta` displays on two rows and copies as `alpha beta gamma xyzdelta`. | Existing #8SBD, evidence appended without changing its plan. 8147cc55 added edge-space dropping in `wrap::rows`; copying joins only displayed cell ranges. This is separate from the native trailing-space trimming already described on that card. |
| Keyboard link selection disappears visually | The walker returns `relay://card/GWXM` before and after resize. Its selection background is visible at print width and absent after rewrap. | #J4WK, filed. 8147cc55 added a replacement paint path that reads visual selection, while `showWalkLink` still selects the hidden emulator rows. The link target remains correct. |

## Controls and validation

`run-audit-probe.py` compiles `audit-probe.cpp` in a temporary directory against the existing Qt5/libvterm build. It uses synthetic output, no provider requests, and an isolated configuration directory. The selection tests wait past Qt's double-click interval so repeat drags cannot accidentally select a whole word.

- `AUDIT_NATIVE_ROWS=1 python3 docs/qa_evidence/2026-09-21-card-file-links/run-audit-probe.py`: all four cases pass with replacement disabled. See `audit-native-control.txt`.
- The same four cases with replacement enabled initially failed. See `audit-before-link-fix.txt` (initial reproduction, before the added double-click timing/control refinement).
- After the moved-label fix, that case passes; the three filed findings still fail. See `audit-after-link-fix.txt`. These failures are intentional audit evidence, not tests added to the normal green suite.
- `RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests markdownLinkLabelsAreClickable`: passes, including known and unknown IDs, live and stale file targets, resizing and clicking.
- `ctest --test-dir build -R '^(markdown|wordwrap|transcriptgaps|panestatus|calllines|board)$' --output-on-failure`: **6/6 pass**. See `audit-targeted-suites.txt`.

## Scope and limits

Reviewed the introducing diffs across prose layout/painting/selection/search/link handling and the Markdown label collector/renderer/dispatcher. Also reviewed the batch's adjacent changes to board sorting/reasoning, folded diffs, sharing chrome, model rows, transcript spacing and shortcut descriptions; the targeted suites above cover the still-current pure logic. Several of those surfaces have since been replaced, particularly board reasoning and model picking. No additional fault in those areas was confirmed in this audit.

The original tests exercised rendering and explicit Markdown labels but did not cover ordinary links after resize, selection after a wide character, selection across a dropped wrap-space, or keyboard-walk highlighting on replacement rows. The lower-row compression fault in this same reflow area was already fixed separately by a5e16137 (#B7SP).

The three remaining findings are recorded for implementation, rather than silently expanding this commit-tracing request into selection and keyboard-navigation changes. Ghostty and full desktop behavior outside these focused paths were not verified.

The focused existing ViewTest cases `compressedProseKeepsFollowingOutputVisible` and `aBlockThatOpensWithALabelStillKnowsItsFirstRow` also pass. The moved-label fix passed the exact-tree Relay build gate.
