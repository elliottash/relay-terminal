<!-- relay:entry 20260921T204745Z-c1 author=codex kind=progress -->
### Codex · 2026-09-21 20:47
Created and claimed this card; confirmed the saved session links #GWXM to its Markdown file. Implementing card-file routing.

<!-- relay:entry 20260921T205454Z-c2 author=codex kind=progress -->
### Codex · 2026-09-21 20:54
Added the owner follow-up verbatim: "seems like # linking regressed totally". Reproduced plain-card hit-test failure after rewrapping; fixed it and the card-file dispatch. Targeted tests and Relay build pass.

<!-- relay:entry 20260921T205813Z-c3 author=codex kind=evidence -->
### Codex · 2026-09-21 20:58
Landed 1f71d605 after the exact-tree Relay build gate. Moved to needs-verification with tests and QA checklist. Isolated GUI drive: 3 PASS, 0 FAIL; Markdown card-file label opens the matching card. Resize/click regression also passed under Xvfb. Evidence: docs/qa_evidence/2026-09-21-card-file-links/.

<!-- relay:entry 20260921T205854Z-c4 author=codex kind=rewrite -->
### Codex · 2026-09-21 20:58
Corrected the newly assigned ID from L9KC to K9KC: L is not in the board alphabet. Renamed the thread with all earlier entries intact. The implementation and evidence commits retain the original ID in their messages.
