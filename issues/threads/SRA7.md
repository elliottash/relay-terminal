<!-- relay:entry 20260922T232928Z-a3 author=codex kind=note -->
### Codex · 2026-09-22 23:29
Filed the user's sphinxpad report verbatim for triage. Not reproduced or investigated in this intake turn.

<!-- relay:entry 20260922T233222Z-a3 author=codex kind=plan -->
### Codex · 2026-09-22 23:32
Claimed #SRA7 via file fallback; recorded Done means and Plan before code. Own core resize and terminal fold viewport handling, with focused tests. Board/delegation tools are unavailable in this guest session.

<!-- relay:entry 20260922T233749Z-a3 author=codex kind=evidence -->
### Codex · 2026-09-22 23:37
Implemented content anchoring in core resize and fold layout. Four focused core and seven view cases pass with libvterm; staged Xvfb captures are in docs/qa_evidence/2026-09-22-SRA7/. Prepared needs-verification with Execution Summary and Tests. Exact-tree landing gate will build Ghostty and run the focused cases before commit.

<!-- relay:entry 20260922T234114Z-a3 author=codex kind=evidence -->
### Codex · 2026-09-22 23:41
Landed a7fd665d48d598aabb056e1dc869a9af7b8cd5f2 after exact-tree app/engine build with Ghostty and focused core/view checks on both cores. Final review found and fixed the blank wrapped-row mapping edge case with a dedicated regression. Added commit link and both-core output evidence.

<!-- relay:entry 20260922T234323Z-a3 author=codex kind=evidence -->
### Codex · 2026-09-22 23:43
Linked follow-up f1f8126a0b96a35cb7c0a77c3413eb73ffbc6a4a. Exact-tree app and engine build passed with Ghostty; all 5 core and 7 view cases pass on both backends (24 cases). Local card check has no findings or blocking signals. #SRA7 remains needs-verification for independent QA.
