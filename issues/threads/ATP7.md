<!-- relay:entry 20260922T232928Z-a4 author=codex kind=note -->
### Codex · 2026-09-22 23:29
Filed the user's sphinxpad report verbatim for triage. Not reproduced or investigated in this intake turn.

<!-- relay:entry 20260922T233216Z-a4 author=codex kind=plan -->
### Codex · 2026-09-22 23:32
Claimed #ATP7; recorded Done means and Plan before code. Board/delegation bridge tools unavailable in guest; using policy file fallback. Own only app icon block and bounded chrome widget.

<!-- relay:entry 20260922T233501Z-a4 author=codex kind=evidence -->
### Codex · 2026-09-22 23:35
Implementation and live-app scaling checks complete; moved #ATP7 to needs-verification with Execution Summary, Tests, and docs/qa_evidence/2026-09-22-ATP7/. Both DPRs retain 18×18 icon bounds and 26×26 controls. Local screenshot evidence only; sphinxpad not accessed.

<!-- relay:entry 20260922T233630Z-a4 author=codex kind=evidence -->
### Codex · 2026-09-22 23:36
Linked implementation commit dcf941a726cdb35669aac435e4c0aa698c41d198; exact materialized-tree build passed. Reviewed and selected only the app-icon hunk from RelayWindow.h, leaving seven unrelated hunks untouched. Card remains needs-verification.

<!-- relay:entry 20260922T233840Z-a4 author=codex kind=plan -->
### Codex · 2026-09-22 23:38
Root requested direct Qt DPR investigation. Probe confirms Qt6 double scaling: old QLabel 22/33/44 logical pixels at DPR 1/1.5/2, while Qt5 stays 22. Updated Plan before follow-up code: retain 22 logical pixels, superseding initial 18px design reduction; preserve paint-time device scaling and add fractional evidence. No edits to shared RelayWindow.h needed.

<!-- relay:entry 20260922T234043Z-a4 author=codex kind=evidence -->
### Codex · 2026-09-22 23:40
Follow-up confirms Qt6 double scaling and @2x metadata behavior. Restored intended 22px size; live app geometry at 1/1.5/2 and direct Qt5/Qt6 probes pass. Inspected Qt6 before/after images at 1.5× and 2×. Updated Execution Summary and Tests; retains needs-verification.
