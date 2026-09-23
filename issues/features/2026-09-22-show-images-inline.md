---
id: 1MGS
type: work
status: discussing
waiting_on: owner
labels: [feature, terminal, agent-ui]
assignee: claude-code
rank: m
created: '2026-09-22'
source: 'Claude Code in a Relay pane, 2026-09-22'
links: {plans: [], commits: [], evidence: [], related: [EM1E], github: null}
---
# Show images inline: in the terminal and in the agent conversation

## Issue
can relay show me images in line? i think we need that

## Discussion points
Relay cannot show an image anywhere today (checked on main, 2026-09-22):

- **Terminal output.** `docs/ENGINE.md` lists "Sixel / kitty graphics" as ❌ on all three cores. The
  libvterm core, the only one that builds here, has no image support at all; libghostty-vt parses
  kitty graphics but nothing renders them. So `kitten icat`, `imgcat`, `chafa --format=kitty`,
  `timg`, matplotlib's kitty/sixel backends and yazi/ranger previews all show nothing or escape
  garbage.
- **Agent conversation.** Images go *to* the agent (#EM1E: paste, drop, `@path`, "Screenshot this
  pane"), but the transcript shows only the `@path` token and a 🖼 mark: no thumbnail of what you
  attached, and nothing for an image the agent reads, writes or links in Markdown (`![](…)`).
