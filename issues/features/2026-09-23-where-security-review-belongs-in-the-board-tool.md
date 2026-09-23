---
id: R246
type: work
status: discussing
labels: [feature, switchboard, qa]
rank: zzzzzzzzzzzzzzzzzr
created: '2026-09-23'
source: conversation in Relay, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [SJTR, SW1D, 3KB7], github: null}
---
# Where security review belongs in the Board tool row

## Issue
besides hygiene and performance, should we have security

## Discussion points
**Question raised.** “besides hygiene and performance, should we have security” asks whether Security should join the Board’s tool row. The user has not decided this placement.

**Recommendation from this conversation.** Keep the row **Hygiene · Tests · Performance** for now. Put security review under **Tests** as an investigation type alongside fuzzing, mutation testing, and adversarial review. Tests is already the proposed entry point for agents that try to find project defects ([card #SJTR](2026-09-21-tests-becomes-the-front-door-to-a-qa-attack-system.md)); the current row is recorded in [card #SW1D](needs_qa_llm/2026-09-21-hygiene-and-performance-the-tool-row-goes-to-three-buttons.md).

**Distinction to preserve.** Security review examines code and project behavior for vulnerabilities. **Options › Security** controls agent permissions and access; that work belongs to [card #3KB7](2026-09-19-a-security-section-in-options.md). A second Board control called Security would give the same label to two different jobs.

**When to revisit.** A dedicated Security pane may make sense once there is a substantial security product to open there, such as a findings dashboard, dependency and secret scans, and threat-model history. This is a suggested threshold, not an approved implementation plan.

For the broader Tests/QA design and evidence, see [Relay QA attack system](../../reports/Relay%20QA%20attack%20system.md).
