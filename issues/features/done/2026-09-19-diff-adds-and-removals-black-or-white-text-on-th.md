---
id: BH3R
type: work
status: done
labels: [feature, theme]
implemented_by: glm/glm-5.3
verified_by: openai/gpt-5.6-terra via codex
rank: zzzzzzzzy
created: '2026-09-19'
source: pane 2, 2026-09-19
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-diff-fills/], related: [], github: null}
---
# Diff adds and removals: black-or-white text on the green/red fill, not green/red text

## Issue
for adds and deletions in diffs, make it black/white text with a green or red background, rather than green / red text

## QA checklist
Implementer: GLM 5.3 (commit 7ef6469ffb82). Recommended verifier: Codex (`guest:codex`).

- [ ] The five PNGs in docs/qa_evidence/2026-09-19-diff-fills/ each show the add lines as black-or-white text on the theme's own green band and the remove lines on its red band — never green or red text (dark themes: black ink; relay-light and ibm-beige: white ink).
- [ ] In a live Relay on any dark theme, a file edit of ≤ 12 changed lines prints under the tool row with the fills behind the +/- lines, and a larger edit's diff pane shows the same pair; the same picture in a tool-call fold (click a ▸ row) and in the Activity pane's fold.
- [ ] Switch the theme (Options › Appearance) with a diff on screen: the diff pane re-renders in the new theme's green/red with the ink that reads on it.
- [ ] `ctest --test-dir build -R "diffview|theme"` passes, including `addAndRemoveLinesAreInkOnTheirFills` and `diffFillsTakeABlackOrWhiteInkThatReads`.
- Known: `buttonfit` fails at HEAD for an unrelated reason (#QAJQ, another session's uncommitted 8.5pt `tabProjectChip` rule; no font rule is touched here).
- Known: one ARCHITECTURE.md sentence ("a diff as black-or-white ink on the green/red fills", the fold-content bullet) is still uncommitted in the working tree — it is fused into another session's #BDXG paragraph hunk and lands with that session's commit.

## Verdict
Reviewed commit 7ef6469ffb82 across every named unified-diff surface; the focused `diffview|theme` tests (including both new cases) passed, and I re-ran the evidence drive and pixel-checked all five theme PNGs for the exact fills and black/white glyph ink. Passed.

Known items remain: `buttonfit` / #QAJQ is unrelated, and the one ARCHITECTURE.md sentence is still riding another session's hunk.
