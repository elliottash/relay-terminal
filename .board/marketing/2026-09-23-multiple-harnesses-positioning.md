---
id: H4RN
type: work
status: planned
labels: [feature, marketing]
rank: m
created: '2026-09-23'
source: 'Owner in a Relay guest session, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [W9ST, GT7X, D3M0], github: null}
---
# Make multiple agent harnesses the lead product message

## Issue
evaluate the business / marketing to dos. i think that the big selling point is (1) use multiple harnesses seamlessly. (2) randomize across models to produce in-the-field eval data. then put up the steps as cards

## Done means
The homepage and introductory copy lead with one workspace that lets people use Claude Code, Codex, and Relay's own agent together. Every claim names a workflow that works in a current build; switching and continuity claims are checked across the relevant harness boundaries. Release availability is clear.

## Planning notes
Current evidence: `site/index.html` lists model switching and guest harnesses in separate sections; `docs/ARCHITECTURE.md` §11a describes headless Claude Code and Codex as pane agents; `backend/relay_core/session_protocol.py` preserves a conversation on model switches. Validate the actual cross-harness experience before wording it as seamless. Keep the research and data business hypothesis in private planning until it has a consent design and evidence.

## Tasks
- [ ] Audit the present website and onboarding claims against a current build. <!-- t:a1 -->
- [ ] Write and test a lead message centered on using multiple harnesses in one workspace. <!-- t:a2 -->
- [ ] Update public copy only after the demonstrated workflow supports the wording. <!-- t:a3 -->
