---
id: W7DC
type: work
status: discussing
labels: [bug, terminal]
assignee: null
rank: m7w
created: '2026-09-21'
source: Codex investigation of B7SP, 2026-09-21
links: {plans: [], commits: [], evidence: [], related: [B7SP], github: null}
---
# Double-click selection on a real row in a folded view returns one character

## Issue
Discovered while testing B7SP: a double click on column 2 of the real terminal row `after six`, following compressed prose, returned `t` instead of `after` from selectedText().

## Planning notes
Measured under Xvfb with libvterm in ViewTest::compressedProseKeepsFollowingOutputVisible. The trial used QTest::mouseDClick followed by mouseRelease at the same point. The final B7SP regression checks drag selection instead. Investigate independently; no word-selection fix is included in B7SP.
