# Independent review of sphinxpad bug fixes

Root review of four Relay subagents delivering #VPR7, #CDP7, #AZP7, #SRA7, and #ATP7. Local checks do not claim a sphinxpad retest.

## #ATP7
Independently reran the final live application staging driver with output redirected to ATP7/. All 1x, 1.5x and 2x runs passed: icon 22x22 logical pixels, bell 26x26, icon inside corner. Reviewed the fractional screenshot. Separately compiled and ran the supplied Qt6 DPI probe at all three scales: old QLabel hints grow 22/33/44 logical pixels; QIcon painting stays within 22 DIP. This confirms the double-scaling cause, rather than relying solely on the Qt5 live build.

## #AZP7
Independently reran the full-app live-drive.py with output redirected to AZP7/. Real xdotool Ctrl+=, Ctrl+minus and Ctrl+0 inputs reach Activity; reviewed before/plus/reset screenshots. Activity grows and resets, owner terminal stays unchanged. Driver also sends keys with Ask-chip focus. Synthetic worker payload, actual application key dispatch; no provider call.

## #SRA7
Independently ran the implementer's exact-tree engine binary with Ghostty enabled: four CoreTest functions on both engines (10 results including init/cleanup), seven ViewTest functions on both engines (16 results including init/cleanup), all passed. After the blank-wrapped-row follow-up, reran resizeKeepsBlankWrappedTop on the local libvterm build (3 results passed). Also reran the fold resize staging with captures. The initial exact-tree binary was removed when its land session was released; rerun that configuration by enabling Ghostty as documented in the implementer evidence.

Core functions: resizePreservesHistoryTop, resizeClampsTrimmedHistoryTop, reflowOnResize, rowsOnlyResizeKeepsCursorBelowAFullRow.
View functions: resizeKeepsTopContent, resizeKeepsFoldTopContent, resizeKeepsProseTopContent, proseReflowsOnResize, aFoldStaysUnderItsLineAcrossAResize, aResizeKeepsTheMatchesInsideTheFold, compressedProseKeepsFollowingOutputVisible.

Commands use RELAY_ENGINE_TEST=CoreTest or ViewTest and the corresponding binary with function names. Logs are in SRA7/. Offscreen platform warnings are expected and no assertions failed.

## Model cards
Independent full-app stage.py rerun passed: with the terminal worker catalog deliberately empty, the helper sends key_stored and updated presets; an already-open OpenRouter Priorities search gains late-model without restarting or losing its search. Codex provider summary changes to 7 of 7 available, and the Available tab shows its seven models. Actual worker pipes, window event dispatch, SettingsWatch and UI, with a synthetic worker and no credential/network call. Event log and screenshots are in models/.

Also independently ran `QT_QPA_PLATFORM=offscreen build/relay-modelcatalog-tests sevenCodexModelsRemainAvailableByDefault anOpenEndedProvidersRecommendedRowsAreTheDefault`: 4 results passed including init/cleanup. This covers explicit opt-out and keeps aggregator curation behavior unchanged.

The Codex fix exempts finite guest-harness catalogs from the size>6 open-ended-provider heuristic; the prior rule defaulted all seven untiered Codex models to unavailable. The catalog routing change uses the served pane consistently and falls back to the same helper snapshot used by Providers.

All five implementations reviewed locally, with no failing checks found. Cards retain needs-verification for the board handoff; no sphinxpad retest was possible (the implementing agent's SSH attempt timed out). Existing unrelated board diagnostics and shared checkout edits were left untouched.
