<!-- relay:entry 20260921T214859Z-a1 author=codex kind=progress -->
### Codex · 2026-09-21 21:48
Claimed #W9ST and recorded the plan. Reviewing changes since 58aeb17f and refreshing the static site, with development changes distinguished from release availability.

<!-- relay:entry 20260921T215336Z-a2 author=codex kind=evidence -->
### Codex · 2026-09-21 21:53
Implementation complete; moved to needs-verification with source audit, browser harness, desktop/mobile screenshots and QA checklist. Browser checks pass in both themes at three widths; tests_check has no findings. Deploy dry run lists only the four intended site files.

<!-- relay:entry 20260921T215424Z-a3 author=codex kind=evidence -->
### Codex · 2026-09-21 21:54
Landed e24a7d066c3da9a8b7c2f8439e5ef427134c0d65 and deployed with ./deploy.sh. Both hostnames return 200; all four site files fetched from both hosts exactly match committed content. Deployment checks and source audit are in docs/qa_evidence/2026-09-21-website-refresh/README.md.
