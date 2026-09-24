<!-- relay:entry 20260921T230501Z-aa author=codex kind=progress -->
Claimed via file fallback; moving share beside the composer folder while preserving sharing behavior.

<!-- relay:entry 20260921T230635Z-bb author=codex kind=evidence -->
Build and isolated GUI inspection passed. Evidence and QA checklist recorded; moved to needs-verification.

<!-- relay:entry 20260921T230817Z-cc author=codex kind=evidence -->
Landed 42b1f54ff234. Exact committed tree builds; unrelated Windows hunks excluded. tests_check returned no findings or blocks.

<!-- relay:entry 20260921T231131Z-aa author=codex kind=decision -->
Owner: "no need to have the purple outline on it, make it look like the other buttons". Removed the share button’s agent destination property, which selected the purple border in Theme.cpp; retained its ordinary stripChip styling and live tooltip.

<!-- relay:entry 20260921T231612Z-dd author=codex kind=evidence -->
Neutral styling landed in de8714c131a8; exact committed tree builds. Local build 2026-09-21.19H.06 and isolated GUI inspection passed. An unrelated hunk accidentally landed in ede03c45 was reversed by a54d8ea2 without touching the working tree; final exact-tree build includes that repair.
