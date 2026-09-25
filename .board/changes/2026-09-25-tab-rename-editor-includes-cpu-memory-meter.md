---
id: QWWD
type: work
status: inbox
labels: [bug, tabs]
rank: m
created: '2026-09-25'
source: 'owner in Relay chat, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Tab rename editor includes the CPU and memory meter

## Issue
Double-clicking a tab to rename it fills the editor with the displayed tab label, including the CPU/memory meter suffix. The editor should contain only the title so accepting or editing the name cannot save meter text as part of the tab name.

> bug card -- when you double click on a tab to change the label, it include the cpu / mem bit, not just the title content
> — elliott · [session:4d6e445ff4be42d7b919298467a71474](relay://session/4d6e445ff4be42d7b919298467a71474) · 2026-09-25

## Done means
With CPU/memory meters visible, double-clicking a tab opens a rename field containing only its title. Pressing Enter without changes does not store a CPU/memory suffix in the manual name; Esc leaves the name unchanged.

## Planning notes
`RelayWindow::renameTab` in `src/RelayWindow.h` seeds the field from `m_tabNames.value(page, m_tabs->tabBar()->tabText(index))`. The fallback is rendered tab text, which can include the suffix. Use the underlying title as the editor value and verify both automatic and manually named tabs.
