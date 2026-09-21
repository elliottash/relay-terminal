# Card #GP1N — Guest instructions and delegation research

## Implemented

`guest_instructions.GUEST_INSTRUCTIONS` is a stable supplement describing Relay's terminal,
actual guest tool availability, project instruction discovery, Switchboard fallback, shared
checkout care, progress and evidence-based reporting. The approved delegation guidance weighs
independent work against coordination costs and sizes the group to worthwhile assignments.
It does not copy the native agent prompt, which names tools and a ledger guests do not have.

`start_provider` passes it through the harness `instructions` argument on start/resume/fork.
Claude appends it using `--append-system-prompt` and keeps it through both model and effort
relaunches, even before a session has its first turn. Codex sends `developerInstructions` on
all three thread lifecycle methods. Guest defaults remain intact. User messages do not carry
this supplement; the pre-existing bridge discovery hint remains unchanged.

New sessions receive it before their first turn and resumed sessions receive the same supplement.
No old-session migration is required. In installed Claude 2.1.278, system-prompt snapshots are
on by default: the first rendered prompt is reused across turns/resumes until compaction.
The append flag remains present when the prompt is rendered again. No changes to users'
CLAUDE.md/AGENTS.md, global settings, or guest feature flags.

## Verification

- `PYTHONPATH=backend:tests python3 -m unittest test_guest_harness_provider test_guest_harness_claude test_guest_harness_codex test_guest_board_bridge`
- 221 tests pass, including provider recreation/resume/fork, multiple turns without supplement
  injection in user text, adapter lifecycle transport, preserved base prompts and Claude relaunches.
- `claude --version`: 2.1.278; `claude --help`: confirms append and snapshot semantics.
- `codex --version`: codex-cli 0.155.1; generated JSON schema confirms `developerInstructions`
  on ThreadStartParams, ThreadResumeParams and ThreadForkParams, absent on TurnStartParams.
- `relay-board.py check`: no findings for #GP1N; repository-wide check still reports the
  existing invalid #MDL1 card/thread identifiers (2 errors) and unrelated warnings.
- No paid guest/model turns were used. These establish instruction transport and lifecycle
  behavior in the adapters, not a model's obedience or precedence over its own restrictions.

## How installed Claude Code frames delegation

Read-only inspection of the installed 2.1.278 binary at
`/home/elliott/.npm-global/lib/node_modules/@anthropic-ai/claude-code/bin/claude.exe`
found the `Agent` tool prompt builder's `When to use` section and two variants.
The relevant embedded function anchors are `AUo`, `Ple` and `YPt` (version-specific).
This is model-visible tool guidance, not evidence of one invariant system prompt across
all accounts/models. The builder selects variants dynamically and can take supplied text;
we did not capture a live model request to determine which one this user receives.

The basic variant favors matching specialist agents, independent parallel work and reading
across multiple files to keep raw exploration out of the main context. Known-target,
single-fact lookups should be searched directly, and delegated searches should not be duplicated.

The more cautious variant leads with handoff/context loss, token expense, uncertain reports
and confirmation bias. It weighs those costs against useful parallel work and broad reading;
a few inline calls stay with the parent. Reviews should receive the code without the parent's
conclusion, and each delegation needs a narrow scope and a clear brief. Its short decision rule
is: “When in doubt, don't spawn.” This is substantially more selective than blanket proactive
parallelism.

## Online guidance (read 2026-09-21)

- [Anthropic prompting best practices: subagent orchestration](https://platform.claude.com/docs/en/build-with-claude/prompt-engineering/claude-prompting-best-practices#subagent-orchestration):
  models can delegate without extra encouragement when tools are well described. Its damping
  example favors parallel or isolated independent work and keeps simple, sequential or
  context-dependent tasks in the main conversation.
- [Claude Code: choosing subagents versus the main conversation](https://code.claude.com/docs/en/sub-agents#choose-between-subagents-and-main-conversation):
  subagents help contain verbose output or perform self-contained work; quick changes,
  frequent back-and-forth and work sharing substantial context favor the parent.
- [Anthropic multi-agent research engineering](https://www.anthropic.com/engineering/multi-agent-research-system):
  ambiguous assignments cause overlap and gaps. Give each worker an objective, boundaries,
  sources/tools and an expected output. Scale effort to task complexity rather than using a
  fixed worker count. Its research-specific counts are not a recommendation for Relay coding.

## Approved delegation wording (2026-09-21)

The owner approved the full proposed supplement: “looks good, also relay isnt linux only”.
The introduction now calls Relay a terminal application without restricting it to Linux.
The owner then approved the shorter version: “yeah thats better”.
The attached delegation paragraph is:

```text
Consider subagents for independent parallel tasks or investigations that benefit from isolated context. Handle small or tightly coupled tasks directly. Match the number of agents to worthwhile independent assignments, within harness limits. Give each a clear scope and relevant constraints, avoid duplicating work, and verify results.
```

The wording expresses when delegation is warranted without a fixed quota or frequent-spawning
requirement. The existing guest harness still determines which tools and limits are available.
Follow-up verification: `PYTHONPATH=backend:tests python3 -m unittest test_guest_harness_provider.StartTests`.
