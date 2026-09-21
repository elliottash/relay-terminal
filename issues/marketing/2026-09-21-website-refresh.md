---
id: W9ST
type: work
status: needs-verification
labels: [feature, website]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex user request, 2026-09-21'
links: {plans: [], commits: [e24a7d066c3da9a8b7c2f8439e5ef427134c0d65], evidence: [docs/qa_evidence/2026-09-21-website-refresh/README.md], related: [K13B, P4GP], github: null}
---
# Refresh the website from changes since its last update

## Issue
review the repo changes since the last web site change, update the web site. see if you see ways to improve it as well

## Plan
**Goal:** Bring the public website up to date and make recent changes easier to discover.
**Findings:** Last site commit is `58aeb17f`; `site/index.html`, `site/free.html` and `site/style.css` are static. Recent commits change models, consoles, Switchboard, approvals and keyboard workflows.
**Steps:**
1. Review committed changes since the baseline and check customer-facing claims against implementation.
2. Update copy, add a dated development summary, and improve shared theme controls and navigation.
3. Check local links, mobile/desktop layouts and keyboard interaction; deploy through the existing site-only script and verify live content.
**Risks:** Main is ahead of packaged releases; label development features explicitly. Other sessions are changing app files; use committed behavior and edit only website/evidence/card paths.
**Verify:** Browser checks at mobile and desktop widths in both themes, local assets/anchors, no-script behavior, and live HTTP/content checks.

## Execution Summary
Updated the website for committed model switching, embedded agent consoles, recaps, approvals, phone pairing and phone Switchboard features since `58aeb17f`. Added a dated development section that distinguishes main from packaged releases. Corrected privacy and board-folder claims; shared the theme controls with Relay Free; preserved all mobile navigation links and improved keyboard radio behavior. Existing screenshots are explicitly labeled as earlier betas. Published with `./deploy.sh`; apex and www return 200 and all four deployed files match the committed bytes.

## Tests
manual: docs/qa_evidence/2026-09-21-website-refresh/README.md

## QA checklist
- Read the dated development summary and check that it does not promise every feature in the packaged beta.
- Inspect both pages at mobile and desktop sizes in beige and copper; navigate radio groups with arrows.
- Check privacy/approval wording, provider job distinctions and the new-board folder against the linked source evidence.
- Open the live site and confirm the development section and working Free-page theme controls.
