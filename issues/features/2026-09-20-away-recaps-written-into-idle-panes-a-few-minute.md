---
id: D54R
type: work
status: needs-verification
labels: [feature, agent]
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzy
created: '2026-09-20'
source: pane 2, 2026-09-20
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-idle-pane-recaps/], related: [], github: null}
---
# Away recaps written into idle panes a few minutes after work ends (Claude Code-style)

## Issue
check how recaps are written. it seems like they arent written in idle panes after a few minutes like claude code does it. research how claude code does recaps and help me add a similar feature

## QA checklist
Verifier: the evidence is in `docs/qa_evidence/2026-09-20-idle-pane-recaps/` (README.md has the full write-up; `run-final.log` the OCR).

- [ ] A turn finishing in a *background tab* gets its recap written while the tab stays hidden (`implementer-04`, checks 03→04: watched pane quiet, hidden pane recapped, recap block reads `[end of message]` / `finished at HH:MM` / `Recap · span`).
- [ ] A pane that stays *watched* past the threshold gets nothing (check 03).
- [ ] No duplicate recaps without new work (check 05), and a new turn in an unwatched split earns a fresh one (check 06).
- [ ] The old on-return path still fires for a window-inactive away stretch (kept, guards shared; #MVGR evidence pattern still applies).
- [ ] Sanity: `relay/away` off in Options means neither path asks for a recap.
