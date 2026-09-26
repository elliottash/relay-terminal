---
id: CWDZ
type: work
status: planned
labels: [feature, files, plugins]
blocked_by: [WYGY]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-26'
source: conversation, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [WYGY], github: null}
---
# Later: build TeX documents on an SSH host

## Issue
Extend the TeX workspace to build on the pane’s SSH host after the local TeX flow ships. Keep generated files outside the project and tie diagnostics, PDF output, and SyncTeX to one generation.

> yes to all.
> — elliott · [session:54948a7060344df2abef877a514ad9bf](relay://session/54948a7060344df2abef877a514ad9bf) · 2026-09-26

## Plan
After #WYGY local build/SyncTeX ships, reuse its protocol and generation model over the pane's SSH connection. Put outputs under a host cache directory, avoid project writes, then validate against a real SSH TeX host.

## Done means
A TeX file on an SSH host builds on that same host; generated files stay outside the project; diagnostics and the PDF reflect one generation, and failure preserves the previous good PDF.
