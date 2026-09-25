# AI software verification and human approval practices

Scope: primary sources published or current by 2026-09-24. Dated posts are identified below; living documentation is marked “undated, accessed 2026-09-24.” A team's example describes that team, not an industry requirement.

## Is a separate human UI click required after explicit chat approval?

### Takeaway
No general software engineering rule requires a second UI click after an authorized person explicitly approves an action in chat. The operative requirements are the team's approval policy, the platform's permission and branch rules, and whether the approval has been recorded in the form those rules require. A chat “yes” can authorize an agent to execute a permitted merge; it is not automatically a GitHub approving review or a substitute for a required reviewer.

### Cited Findings
- OpenAI's Codex team says engineers interact “almost entirely through prompts,” human PR review is optional in its experimental repo, and agents often squash and merge their own PRs. This is direct evidence of a workflow without a mandatory extra human merge click. [OpenAI, “Harness engineering,” 2026-02-11](https://openai.com/index/harness-engineering/).
- OpenAI's Symphony specification explicitly does **not** require one approval or operator confirmation policy across implementations. It says a successful run may stop at a workflow-defined “Human Review” handoff rather than “Done,” and ticket writes are typically handled by the coding agent's tools. [OpenAI, “An open-source spec for Codex orchestration: Symphony,” 2026-04-27](https://openai.com/index/open-source-codex-orchestration-symphony/).
- OpenAI's Codex security writeup distinguishes the execution sandbox from an approval policy that decides when the agent must ask. It says a user may approve a specific action once or approve that type of action for the session. [OpenAI, “Running Codex safely at OpenAI,” 2026-05-08](https://openai.com/index/running-codex-safely/).
- GitHub exposes API operations to submit an `APPROVE` pull request review and to merge a PR, with documented token permissions and error states. These are tool operations, not inherently browser clicks. GitHub's branch protection can still require an eligible reviewer and successful status checks. [GitHub REST reviews, undated, accessed 2026-09-24](https://docs.github.com/en/rest/pulls/reviews); [GitHub REST pulls, undated, accessed 2026-09-24](https://docs.github.com/en/rest/pulls/pulls); [GitHub protected branches, undated, accessed 2026-09-24](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches).
- GitHub's `/pr automerge` in Copilot CLI enables auto merge after review requests, conflicts, and CI are resolved; GitHub then merges when all remaining repository requirements, including required approvals and merge queue, are satisfied. Its `/pr auto` workflow deliberately stops at green and does not merge. [GitHub Copilot CLI docs, undated, accessed 2026-09-24](https://docs.github.com/en/copilot/how-tos/copilot-cli/use-copilot-cli/manage-pull-requests).
- GitHub's Copilot app “agent merge” asks its workspace agent to fix blockers and merge when GitHub permits it. This is a product mechanism; it does not waive repository rules. [GitHub Copilot app docs, undated, accessed 2026-09-24](https://docs.github.com/en/copilot/how-tos/github-copilot-app/managing-issues-and-pull-requests).
- A different GitHub product, **Copilot cloud agent**, cannot mark its own PR ready for review, approve it, or merge it. Its generated PR may also require a person to click “Approve and run workflows” before GitHub Actions will run. These are concrete product limits, not a universal approval norm. [GitHub cloud-agent risk docs, undated, accessed 2026-09-24](https://docs.github.com/en/copilot/concepts/security-governance-and-network-settings/risks-and-mitigations); [GitHub review-output docs, undated, accessed 2026-09-24](https://docs.github.com/en/copilot/how-tos/copilot-on-github/use-copilot-agents/review-copilot-output).
- GitHub auto merge can complete a PR after required reviews and checks pass, so a later manual merge click is unnecessary where enabled. [GitHub auto-merge docs, undated, accessed 2026-09-24](https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/automatically-merging-a-pull-request).

### Inferences
- Treat “human approval” as a decision and audit event, while the UI click is only one possible way to express or record it. If a policy explicitly demands a GitHub review, an authorized GitHub review event must be submitted; a chat message alone will not satisfy branch protection.
- An agent executing after an explicit chat authorization is compatible with the examples above when its identity has permission, repository rules pass, and the organization has not mandated a separate review channel. The approval should be tied to the concrete artifact or action, and the execution receipt retained.
- Do not infer from OpenAI's case study that human review is dispensable for all code. Do not infer from GitHub cloud agent's restriction that all agents are unable to merge.

### Gaps
- No primary source found establishing a cross-platform, cross-team rule that a separate human UI click is mandatory after explicit chat approval.
- Public docs do not establish whether a particular team's chat approval is accepted as its formal sign-off; that depends on local policy and audit design.

## How do AI-forward teams verify and merge agent changes?

### Takeaway
Documented practices range from automated, low-friction merge paths to human approval gates. Common verification evidence includes tests and CI, independent agent review, UI or browser validation, and risk based escalation; the exact gate varies by team and repository.

### Cited Findings
- OpenAI's experimental Codex repo instructed agents to review their own changes, request additional local and cloud agent reviews, handle human or agent feedback, and iterate until agent reviewers were satisfied. It gave agents isolated runnable app instances, browser tooling, logs, metrics, and traces to validate behavior. [OpenAI, “Harness engineering,” 2026-02-11](https://openai.com/index/harness-engineering/).
- The same OpenAI team reports minimal blocking merge gates and short lived PRs, with flaky tests often handled through follow-up runs; agents often merge their own PRs. The post also says humans prioritize work, translate feedback into acceptance criteria, and validate outcomes. This was a specific internal beta experiment, not a blanket OpenAI policy. [OpenAI, “Harness engineering,” 2026-02-11](https://openai.com/index/harness-engineering/).
- OpenAI's Symphony describes an issue tracker control plane with continuous agents and human review of results, while leaving workflow-specific handoff and approval policy to each implementation. [OpenAI, “Symphony,” 2026-04-27](https://openai.com/index/open-source-codex-orchestration-symphony/).
- Anthropic says its Code Review system runs on nearly every Anthropic PR, dispatches multiple agents to identify and verify bugs, and posts an overview plus inline comments. It explicitly says Code Review will not approve PRs because approval remains a human call. [Anthropic, “Code Review for Claude Code,” 2026-03-09](https://claude.com/blog/code-review).
- Anthropic's desktop Claude Code can review local diffs, monitor GitHub CI via `gh`, auto-fix CI failures, and, when the user enables auto merge, attempt a merge once checks pass. This describes a configurable merge mechanism alongside Anthropic's separate human PR approval practice. [Anthropic, “Bringing automated preview, review, and merge to Claude Code,” 2026-02-20](https://claude.com/blog/preview-review-and-merge-with-claude-code).
- Cursor's agent guidance says to review AI generated code, offers local review and Bugbot PR review, and directs users to review changes and merge when ready. Cloud agents run tests and attach screenshots, video, and logs to PRs as validation artifacts. [Cursor, “Best practices for coding with agents,” 2026-01-09](https://cursor.com/blog/agent-best-practices); [Cursor Cloud Agent docs, undated, accessed 2026-09-24](https://prod.cursor.com/help/ai-features/background-agents).
- Cursor's published Amplitude customer case says Amplitude uses Bugbot and a custom risk classifier, automatically merges some low-risk PRs, and routes higher-risk PRs to engineers; it reports roughly 60–70% of low-risk PRs merged without additional developer work. This is Amplitude's reported practice, not a Cursor-wide default. [Cursor/Amplitude case study, 2026-04-15](https://cursor.com/blog/amplitude).
- GitHub's enterprise guidance recommends requiring an approved PR for production or important branches, and GitHub branch protection can require qualifying reviews, status checks, conversation resolution, and other conditions. This is guidance plus configurable enforcement, not a claim that every repository uses these settings. [GitHub codebase standards guidance, undated, accessed 2026-09-24](https://docs.github.com/en/copilot/tutorials/roll-out-at-scale/govern-at-scale/maintain-codebase-standards); [GitHub branch protection docs, undated, accessed 2026-09-24](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches).
- As of GitHub's 2026-09-01 announcement, Copilot code review's approval assessment by itself does not count toward merge requirements. When admins enable actual Copilot approving reviews, those reviews can count toward a required approval; a subsequent push dismisses the approval. [GitHub changelog, 2026-09-01](https://github.blog/changelog/2026-09-01-copilot-code-review-can-now-approve-pull-requests/).

### Inferences
- “Verified” should name its oracle: passing tests/CI, independent review, UI evidence, release observation, or a required person's judgment. A green CI check and a human approval answer different questions.
- Team policies can select risk based human escalation. Amplitude's example shows no per-PR human step for a subset, whereas Anthropic's current Code Review approval practice retains one.

### Gaps
- Public case studies do not expose complete incident rates, selection rules, or all branch protections. They cannot establish that automated merging is equally safe across teams or stakes.

## When is a work item closed, and does merge equal done?

### Takeaway
Closing a work item is a workflow state choice, often automated from a merged PR but sometimes delayed until release or human validation. Agent completion, PR approval, merge, and customer availability are separate events.

### Cited Findings
- GitHub closing keywords such as `Closes #10` link a PR to an issue and automatically close the issue when the PR merges. [GitHub issue and PR keyword docs, undated, accessed 2026-09-24](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/using-keywords-in-issues-and-pull-requests).
- Linear's GitHub integration can automate issue status from PR and commit activity. Its release guidance suggests moving an issue to a started “Merged” status on PR merge and marking it done at release, when the change is available to customers. [Linear GitHub integration docs, undated, accessed 2026-09-24](https://linear.app/docs/github-integration); [Linear release docs, undated, accessed 2026-09-24](https://linear.app/docs/releases).
- Linear's “ready for merge” automation accounts for repository approvals and passing checks according to branch protection rules; its 2019 workflow guidance says multiple associated PRs can delay the merge-triggered issue transition until all are merged. [Linear changelog, 2023-11-15](https://linear.app/changelog/2023-11-15-github-workflow-updates); [Linear changelog, 2019-07-12](https://linear.app/changelog/2019-07-11-github-workflow-configuration).
- OpenAI's Symphony specification explicitly permits a run to complete at a handoff such as `Human Review`, instead of terminal `Done`, and leaves ticket transitions to the coding agent's workflow tools. [OpenAI, “Symphony,” 2026-04-27](https://openai.com/index/open-source-codex-orchestration-symphony/).

### Inferences
- A sensible state machine can distinguish implemented, independently verified, approved, merged, released, and closed. Whether a separate human action is required at any transition follows local policy and external platform constraints.
- A merged PR can close an issue mechanically, but that does not prove the shipped result met a human acceptance criterion unless that criterion was checked and recorded before closure or at a later release gate.

### Gaps
- No universal published definition of “done” for coding-agent work was found; GitHub, Linear, and Symphony deliberately allow different transitions.
