<!-- relay:entry 20260921T214859Z-a1 author=codex kind=progress -->
### Codex · 2026-09-21 21:48
Claimed #W9ST and recorded the plan. Reviewing changes since 58aeb17f and refreshing the static site, with development changes distinguished from release availability.

<!-- relay:entry 20260921T215336Z-a2 author=codex kind=evidence -->
### Codex · 2026-09-21 21:53
Implementation complete; moved to needs-verification with source audit, browser harness, desktop/mobile screenshots and QA checklist. Browser checks pass in both themes at three widths; tests_check has no findings. Deploy dry run lists only the four intended site files.

<!-- relay:entry 20260921T215424Z-a3 author=codex kind=evidence -->
### Codex · 2026-09-21 21:54
Landed e24a7d066c3da9a8b7c2f8439e5ef427134c0d65 and deployed with ./deploy.sh. Both hostnames return 200; all four site files fetched from both hosts exactly match committed content. Deployment checks and source audit are in docs/qa_evidence/2026-09-21-website-refresh/README.md.

<!-- relay:entry 20260921T220036Z-a4 author=codex kind=decision -->
### Codex · 2026-09-21 22:00
Owner correction: "its not a linux terminal". Changed the hero, search description and social description to "A workspace for agentic coding". Platform availability remains in installation/status details.

<!-- relay:entry 20260921T220131Z-a5 author=codex kind=progress -->
### Codex · 2026-09-21 22:01
Owner: "this line overlapping the words looks bad". Replaced the heading’s font-sized box-drawing characters with a bounded CSS rule inside the existing gutter, leaving 0.6rem clear of the text at every breakpoint.

Verified in Chrome at 360, 720 and 1280px in both themes: every heading retains at least 9.6px between its rule and text. Visually inspected /tmp/relay-heading-fixed.png.
