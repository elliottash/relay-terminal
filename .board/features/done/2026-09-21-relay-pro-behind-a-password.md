---
id: MKZ0
aliases: [RLP7, RPR7]
type: work
status: done
labels: [feature, models, gateway]
assignee: codex
rank: n
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [06d0936a902c14f860471233e3af2ce8973a7e22, 1cf0f5f68778d42fa0ddb4114acc44ccf42dffe4, 380d8c66c8f9ada774186901891393ab96566898, 6668999edd9c917790c473e08c72671087a82936, cdaf887e4611c14ab618c8a2d6cf6bba01f80e76, f8937979f543745d5764c54a35eaec1244f23a6a, eb2a2671b315e6711c0c6b36052e890ea172c6da, 0263794b483e0a7f593d93785f2e5f91510a9f44, 82e164cc17d41de7ef153cf30964a7da89ac3eef, 50c23e65ba48f3b37d9eab33e64d56bada680278], evidence: [docs/qa_evidence/2026-09-22-RPR7/README.md, docs/qa_evidence/2026-09-22-verify-RPR7/README.md], related: [4BPE], github: null}
---
# Hosted provider access controls

## Issue
Technical implementation record for hosted provider access controls. The detailed owner discussion and planning record are held in the private Relay internal repository.

## Execution Summary
The gateway checks entitlement on each hosted request; the desktop supports code activation and removal. The linked commits contain the implementation.

## Tests
Targeted gateway, provider, and model-setting tests were recorded in the original implementation.
