---
id: WXT6
type: work
status: needs-qa-llm
labels: [feature, terminal]
implemented_by: glm/glm-5.3
rank: zzzzzzzzzy
created: '2026-09-19'
source: pane relay-terminal, 2026-09-19
links: {plans: [], commits: [8147cc55, 3e12fa58, 7558c9b2, 16749b94], evidence: [docs/qa_evidence/2026-09-19-edit-diffs-fold-collapsed/], related: [TK9C], github: null}
---
# Edit diffs fold under the row, collapsed by default

## Issue
for edit_file snippets, keep those collapsed by default.

## Decisions
- 2026-09-19, owner: "for edit_file snippets, keep those collapsed by default." Every write/edit
  diff — small or big — folds behind the row's click; the auto-print under the row (the 2026-09-18
  decision of #TK9C) is retired.
- A small diff's fold is served from the diff the pane already stores, so it answers even when the
  worker's fifty-turn log has scrolled past the call. A big diff still opens the diff pane.

## Notes
- This landed in four pieces on 2026-09-19. The source (`src/Pane.h`, `src/CallLines.{h,cpp}`,
  `src/SubagentTranscript.{h,cpp}`) went in the 18:00 tree sweep `8147cc55`, the headless tests in
  the next sweep `3e12fa58`, and the protocol/architecture wording in the docs sweep `7558c9b2` —
  all three committed this session's in-flight edits along with other sessions' work. The card and
  the QA evidence land in its own commit `16749b94`.
- Live evidence: `docs/qa_evidence/2026-09-19-edit-diffs-fold-collapsed/` — a real Relay under
  Xvfb against the #TK9C loopback stub: the folded pane, the small edit's fold opened by one
  Ctrl+click (from the stored diff), and the big edit's diff pane. Headless: `calllines`
  (`aStoredDiffFoldsWithoutAWorker`), `subagents` (collapsed until clicked), `toollabel`,
  `turntranscript` — 4/4.

## QA checklist
- [ ] Terminal pane, live: after a turn with a small edit, the row `▸ edited x.py · +2 −1` has
      **no diff printed under it**; a Ctrl+click unfolds the diff in place (hunk header, red/green
      rows, `open in pane · open x.py`); a big edit's row still opens the diff pane.
      (`docs/qa_evidence/2026-09-19-edit-diffs-fold-collapsed/after-02`, `-03`, `-04`)
- [ ] An old row still answers after the worker's log has scrolled: the small-diff fold is served
      from the pane's stored diff (`foldRequested` → `foldForDiff`), no `tool_output_get` on the
      wire.
- [ ] Subagent transcript: a small edit's row is collapsed; the click expands the diff (its detail
      already holds it). `tests/subagents_test.cpp` 4/4 with the updated case.
- [ ] Headless: `ctest --test-dir build -R "calllines|subagents|toollabel|turntranscript"` passes,
      including the new `aStoredDiffFoldsWithoutAWorker`.
- [ ] Protocol §23.2/§23.6 and `docs/ARCHITECTURE.md` describe the fold, not the auto-print.
