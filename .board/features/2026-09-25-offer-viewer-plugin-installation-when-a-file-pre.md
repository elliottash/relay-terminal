---
id: 7WGJ
type: work
status: inbox
labels: [feature, plugins, file-preview, ui]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [9Y7X, E85D, C0Q8], github: null}
---
# Offer viewer plugin installation when a file preview is unavailable

## Issue
When a user opens a file whose in-app viewer is missing, keep its preview pane usable: show Open externally and an Install <file type> viewer plugin action. Resolve the appropriate viewer plugin for the file type, install it through Relay's plugin flow, and let the pane render the file after installation. Apply this pattern across supported file types, starting with PDF.

> new card: plugin install buttons. so if you click on a pdf, and you dont havce the plug in. it has a "open externally" button and an "install pdf viewer plugin". we will need that for all the file types
> — elliott · [session:f32c83e1970648d581feee4ae3447445](relay://session/f32c83e1970648d581feee4ae3447445) · 2026-09-25
