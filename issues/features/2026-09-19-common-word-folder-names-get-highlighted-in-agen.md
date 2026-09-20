---
id: SFZC
type: work
status: needs-verification
labels: [bug, gui]
assignee: agent
implemented_by: glm/glm-5.3
priority: 1
rank: zzzzzzzr
created: '2026-09-19'
links: {commits: [a7fc3ca2, 281c81e8], evidence: [docs/qa_evidence/2026-09-20-prose-bare-folder-links/], github: null, plans: [], related: [R2WQ]}
---
# common-word folder names get highlighted in agent messages

## Issue
common-word folder names keep getting highlighted in agent messages, eg tests and remote. any good way to fix that?

## Direction
Owner sketch (2026-09-20), read against the code:

1. **Terminal output keeps highlighting bare folder names** — already today's behaviour and deliberate (`restLinkColumns`, engine/view/TerminalView.cpp: an `ls` of extension-less folders is the commonest link-bearing line). Nothing to do.
2. **Agent messages: a folder links only when the token carries a `/`** (`tests/`, `src/Pane.h`). The false positives are all bare English words that happen to be directories relative to the pane's cwd — `tests`, `remote`, and equally `docs`, `build`, `data`, `site`. Mechanism today: the bare-token pass in `relay::links::candidates` (src/OutputLinks.cpp stage 6) plus the filesystem probe. Fix locus: a prose mode there (bare token must contain `/` to be a folder candidate), applied on surfaces the view knows are Relay-printed prose — the `relay://prose/` anchor runs (#R2WQ) already mark those rows; hover, at-rest colouring and the Ctrl+Shift+L walk all share the one scan, so they follow for free. Tests: tests/outputlinks_test.cpp.
3. **Teach the agent the habit**: one line in the pane agent's system prompt (backend/relay_core/agent.py, the formatting block ~line 188) — name a folder with a trailing `/`. Polish, not the fix: a prompt is a nudge, the scanner rule is what holds for guests and other models too.

Decided (2026-09-20): a bare *file* name in agent prose keeps its link — the `/` requirement is for folders only.

## Decisions
- 2026-09-20, owner: "file names still link" — bare *file* names keep linking in agent prose; the `/` requirement is for folders only.

## QA checklist
Verifier: an LLM lane is enough (GUI behaviour is covered pixel-level by ViewTest offscreen).

- [ ] Agent reply containing a bare folder word that exists relative to the pane's cwd (`tests`, `docs`, `build`): the word is plain text — no underline on hover, no link colour at rest, skipped by Ctrl+Shift+L. Applies at the print width and with the prose block re-wrapped after a resize.
- [ ] Same reply naming a folder with its slash (`tests/`) and a file (`src/Pane.h`, `README.md`): both link (folder opens the explorer, file the preview). A bare *file* name still links (owner decision, 2026-09-20).
- [ ] Program output unchanged: `ls` showing extension-less folders still links every name, at rest and on hover.
- [ ] Quoted bare folder in agent prose (`'src'`) does not link; `'src/'` does.
- [ ] A pasted traceback or `file:line` inside an agent reply still links.
- [ ] Targeted suites: `ctest -R outputlinks` and `ctest -R engine-tests` green.

Evidence: docs/qa_evidence/2026-09-20-prose-bare-folder-links/ (fix commit a7fc3ca2; prompt line landed via 9628872d — see README's provenance note).
