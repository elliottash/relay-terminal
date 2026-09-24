<!-- relay:entry 20260921T230800Z-c1 author=codex kind=progress -->
### Codex · 2026-09-21 23:08
Created and claimed this UI change using the file fallback: relay_board tools are unavailable. Implementing recall in the composer, related to the earlier in-place editing behavior in #9V1F.

<!-- relay:entry 20260921T230800Z-c2 author=codex kind=decision -->
### Codex · 2026-09-21 23:08
Owner approved recall as a draft: "ok, you agree then. make that change"

<!-- relay:entry 20260921T231300Z-c1 author=codex kind=evidence -->
### Codex · 2026-09-21 23:13
Implemented recall and updated hints. Both targeted CTest cases and the isolated real-GUI shell regression passed. Moved to needs-verification with evidence and a QA checklist.

<!-- relay:entry 20260921T231600Z-c1 author=codex kind=evidence -->
### Codex · 2026-09-21 23:16
Landed 3ebf3673 after the exact-tree build gate passed, excluding all other sessions' hunks. Linked the commit. The tests_check implementation reports no findings, failures, or blocking signals. A follow-up GUI run was invalidated by premature cleanup of its temporary build; repeating from a stable exported commit.

<!-- relay:entry 20260921T231715Z-c1 author=codex kind=evidence -->
### Codex · 2026-09-21 23:17
The clean exported commit built and passed the real-GUI recall regression. Refreshed screenshots show the final queue hint. Evidence is complete; provider-specific manual checks remain on the QA checklist.
