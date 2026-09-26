---
id: R660
type: work
status: planned
labels: [feature, panes, artifacts, preview]
rank: zzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (D6)'
links: {plans: [], commits: [488ff0c853f7, def2cf0b4130], evidence: [docs/qa_evidence/2026-09-25-tex-chains/], related: [P2W8, E85D, WYGY, F8R7, SJ00], github: null}
---
# Linked pane chains: shell → TeX editor → PDF preview as one group that opens, restores and closes together

## Issue
also note and build the case of multiple linked panes -- eg shell -> TEX -> PDF.

## Plan
**Goal.** Deliver the linked shell → TeX editor → PDF preview chain as one restorable group.

**Current state (2026-09-26).** The chain model, open-beside offer, member chips, close/move/restore behavior, documentation and live drive landed in `def2cf0b`. Evidence is in `docs/qa_evidence/2026-09-25-tex-chains/` (offer, three-member chain, build generations, restart and close flow). The older plan's untracked-file hold is obsolete. A defect found during review registered the PDF with the source adapter; #SJ00 fixes it in submitted commit `9fe22760`, pending publication. Forward/inverse SyncTeX remains #WYGY's responsibility.

**Next steps.** After #SJ00 publishes, run the existing chain drive on the published build, inspect the PDF status on a build without Qt PDF, and move this card to needs-verification with fresh evidence. A separate session verifies the chain's Done means.

**Verify.** `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^(artifactworkspace|filepanes)$' --output-on-failure` passed in the #SJ00 workspace (2/2). The live drive is `docs/qa_evidence/2026-09-25-tex-chains/drive.sh`.

## Tasks

- [x] Ordered members with upstream and head; serialization (`def2cf0b`) <!-- t:9s blocked_by=#E85D -->
- [x] Open-the-next from shell/editor; inverse SyncTeX remains #WYGY (`def2cf0b`) <!-- t:vq blocked_by=9s,#WYGY -->
- [x] Chain chip and presets across the chain (`def2cf0b`) <!-- t:qr blocked_by=9s -->
- [x] Close/move/restore lifecycle (`def2cf0b`) <!-- t:fy blocked_by=9s -->
- [x] Docs and live evidence (`docs/qa_evidence/2026-09-25-tex-chains/`) <!-- t:mk blocked_by=vq,qr,fy -->
- [ ] Rerun the chain drive after #SJ00 PDF adapter fix publishes; hand to separate verification <!-- t:vy blocked_by=#SJ00 -->
