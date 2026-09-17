# Review opencode for agent features and logic

- **Status**: done
- **Component**: agent
- **Milestone**: 0.1-preview
- **Workstream**: agent
- **Acceptance evidence**: `docs/OPENCODE-NOTES.md`
- **Assignee**: Claude Opus 5 research subagent, 2026-09-16
- **Source**: `issues/feature_intake.txt`, "can also look at opencode for features / logic on how the agent should work"

## Context

The request was research, not implementation: compare opencode's agent with Relay's and
decide what to adopt.

## Resolution

Closed 2026-09-17. `docs/OPENCODE-NOTES.md` ranks eleven recommendations with opencode
source paths, Relay's current behavior, size and security trade-offs. opencode is not
installed locally; the review used a shallow clone of its source.

Items already acted on elsewhere:

- P4 (accept messages while busy): `features/2026-09-17-queue-or-interrupt-agent-prompts.md`.
- P2 (rule-based permissions): superseded when per-action approvals were removed on 2026-09-17.

The remaining recommendations are not filed yet. Candidates, in the notes' order: an
exact-match edit tool (P1), context accounting and compaction (P3), project instructions
and an environment block (P5), a provider-quirk table (P6), read-only grep/glob tools (P7),
and plan vs build modes (P8). File each when it is scheduled.
