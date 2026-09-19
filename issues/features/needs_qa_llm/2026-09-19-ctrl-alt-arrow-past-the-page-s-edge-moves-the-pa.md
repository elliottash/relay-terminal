---
id: 8G7E
type: work
status: needs-qa-llm
labels: [feature, gui, keyboard]
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzi
created: '2026-09-19'
source: recovered from the stopped "Moving panes past page edges" agent session (81abd1b4), 2026-09-19; landed by the continue session
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-pane-move-past-page-edge/], related: [JXWT], github: null}
---
# Ctrl+Alt+arrow past the page's edge moves the pane into a column or row of its own

## Issue
for stacked panes at the right edge of the screen, if you press ctrl alt right it says "no pane in that diretiction". but instead, it should move the pane to the right in its own column. plan out this fix and the equivalent behavior for other screen edges
