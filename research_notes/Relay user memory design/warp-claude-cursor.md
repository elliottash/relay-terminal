# Warp, Claude Code, and Cursor user memory

Retrieval date for every linked source: **2026-09-22**. Research for Relay card #M7RY. Official live documentation takes priority over historical announcements. Vendor-hosted forum statements are identified separately. This is desk research, not hands-on verification. Relay's existing global memory cards, pinned/path-scoped injection, and Globals editor are assignment-provided context, not independently audited here. Recommendations below are inferences, not competitor capabilities or measured user outcomes.

## Warp: how are personal rules and learned memories scoped and controlled?

### Takeaway

Warp provides a useful distinction between personal instructions and automatically accumulated knowledge. Its learned memory service is a restricted preview. [Rules](https://docs.warp.dev/agents/capabilities/rules/), [Agent Memory](https://docs.warp.dev/agents/agent-memory/).

### Cited Findings

- **Scope and explicit rules:** Global Rules cover projects; repository/subdirectory `AGENTS.md` or legacy `WARP.md` provides project guidance. More specific project rules outrank global ones. Warp may suggest global rules from usage. [Rules](https://docs.warp.dev/agents/capabilities/rules/).
- **Creation and maintenance:** Personal → Rules → Global supports adding, editing, and deleting named/described rules; `/add-rule` opens that flow. Project `/init` generates or links an instruction file. Applied rules appear in conversation references. Inclusion is relevance-based; subdirectory discovery outside the current directory is described as best effort. [Rules](https://docs.warp.dev/agents/capabilities/rules/).
- **Learned memory:** Agent Memory is enabled per design-partner team. Personal, agent, and team stores distinguish ownership; agents receive attached stores with read-only/read-write access and usage instructions. New agents receive an auto-memory store by default, with an opt-out at creation. [Agent Memory](https://docs.warp.dev/agents/agent-memory/).
- **Creation, update, retrieval:** After conversations, Warp extracts durable information, merges or supersedes conflicting knowledge, and records provenance and changes. Explicit “remember” requests also work. Relevant memories are searched and injected at task start; agents can retrieve more during work. [Agent Memory](https://docs.warp.dev/agents/agent-memory/).
- **Storage and availability:** Storage and processing run on Warp. The local Warp Agent and cloud agents are supported; local third-party harnesses are excluded from the preview. Self-hosting and programmatic memory management are explicitly future capabilities. [Agent Memory](https://docs.warp.dev/agents/agent-memory/).
- **Privacy context:** Warp advertises no external-provider training on user data, telemetry opt-out, and enterprise ZDR/BYO LLM options. These are product-level claims, not a specification of memory deletion or retention. [Warp Drive](https://www.warp.dev/drive).

### Inferences

- A dedicated Relay **User memory** section can make ownership understandable while retaining the existing storage/editor. Keep personal preferences distinct from facts tied to one repository and shared organizational knowledge.
- Use separate labels for **user-authored instruction** and **agent-proposed learning**. For inferred entries, show source conversation, date, and proposed change; a reviewable merge is preferable to silently replacing a standing preference.
- Show which memory cards affected a turn and why: pinned, matching path, or retrieved. Scope, ownership, and retrieval mode should be separate attributes.
- A helper can start from existing Globals, propose missing preferences, and save selected answers as existing memory cards. It need not introduce a second memory backend.

### Gaps

- Unspecified: exact edit/delete UI, deletion propagation, retention, encryption, and per-write approval. [Agent Memory](https://docs.warp.dev/agents/agent-memory/).
- No dedicated personal interview is documented in the reviewed rules or memory pages. `/init` is evidence for assisted project setup, not proof of personal-profile onboarding. [Rules](https://docs.warp.dev/agents/capabilities/rules/).
- Privacy-policy navigation resolved to a page dominated by telemetry information; memory-specific privacy remains unresolved. [Privacy and data control](https://docs.warp.dev/support-and-community/privacy-and-security/privacy/).

## Claude Code: what distinguishes user instructions from learned project memory?

### Takeaway

Claude Code separates authored instructions from automatic notes. Crucially, a learned fact **about a user** is not necessarily **global user scope**. [Memory](https://code.claude.com/docs/en/memory).

### Cited Findings

- **Scope:** `~/.claude/CLAUDE.md` carries cross-project instructions; project `CLAUDE.md` carries shared guidance; gitignored `CLAUDE.local.md` carries private project instructions. Auto memory defaults to `~/.claude/projects/<project>/memory/`, shared across repository worktrees but machine-local. Its types include user, feedback, project, and reference. [Memory](https://code.claude.com/docs/en/memory).
- **Lifecycle and retrieval:** Claude writes notes from useful corrections/preferences, including explicit remember requests. Startup loads the first 200 lines or 25KB of `MEMORY.md`, whichever is smaller; topic files load on demand. Markdown entries can be edited/deleted. `/memory` opens files and toggles automatic memory; memory survives transcript cleanup. [Memory](https://code.claude.com/docs/en/memory).
- **Onboarding:** `/init` produces a project guide. `CLAUDE_CODE_NEW_INIT=1` enables an interactive flow including skills, hooks, and personal memory files. The command reference recommends `/memory` to refine initial setup. [Commands](https://code.claude.com/docs/en/commands).
- **Privacy:** Consumer training depends on the model-improvement setting. Commercial usage is not used to train generative models unless the customer opts into providing data. Feedback submissions have distinct retention terms. Local persistence should not be confused with inference data remaining local. [Data usage](https://code.claude.com/docs/en/data-usage).

### Inferences

- Relay should visibly distinguish **what this says about me** from **where it applies**. Interview answer “I use Python for this research project” should not automatically become an all-project language preference.
- An optional helper interview is supported by the documented assisted setup pattern, but its proposed questions are Relay design choices: role/expertise, preferred response detail, recurring tools/workflows, and what should apply across projects.
- Suggested flow: show existing entries; ask only uncovered questions; allow skip; present individual proposed cards with scope and injection mode; save chosen cards; provide a direct return path for editing or retiring them. This is a recommendation, not a documented Claude flow.
- Separate controls for remembering new information, using existing information, and removing information. A single “memory off” label would leave their effects ambiguous.
- Given Relay's current cards, start with a compact personal summary plus selectively loaded detail. Expose actual inclusion rather than promising that all remembered facts always influence responses.

### Gaps

- These sources provide no measured evidence that a personal interview improves retention or task success. Test discoverability and resulting corrections with users.
- Custom storage can change scope; this comparison uses defaults. [Memory](https://code.claude.com/docs/en/memory).
- Memory is advisory; deterministic enforcement needs hooks. [Memory](https://code.claude.com/docs/en/memory).

## Cursor: what remains current, and what can Relay learn from the older Memories feature?

### Takeaway

Current documentation strongly supports an explicit personal-versus-project rules UI. Historical learned Memories must be identified as historical rather than presented as today's standard capability. [Rules](https://cursor.com/docs/rules), [1.0 announcement](https://cursor.com/en-US/changelog/1-0), [vendor-hosted removal response](https://forum.cursor.com/t/are-my-memories-gone/144057/3).

### Cited Findings

- **Current scope:** User Rules in Customize → Rules apply across projects to Agent Chat. Project `.cursor/rules/*.mdc` files are version-controlled; team rules also exist. Conflict precedence is team, project, then user. Rules do not apply to Tab or Inline Edit. [Rules](https://cursor.com/docs/rules).
- **Creation/update/retrieval:** `/create-rule` generates a project rule; Customize exposes rules and status. Rules can always apply, attach for file patterns, be selected by relevance descriptions, or be manually mentioned. Applied content enters the model context. The docs recommend updating rules after repeated mistakes. [Rules](https://cursor.com/docs/rules).
- **Historical learned memory:** The June 4, 2025 release introduced a beta that remembered conversation facts, individually scoped per project, enabled and managed in Settings. [1.0 announcement](https://cursor.com/en-US/changelog/1-0).
- **Removal evidence, lower authority than current product docs:** A November 25, 2025 response by Dean Rie on Cursor's support forum says Memories was removed starting in 2.1.x. It recommends “Export memories” from the command palette, yielding `.mdc` content for User or Project Rules. This is an attributed vendor-hosted support statement, not a independently tested migration. [Response](https://forum.cursor.com/t/are-my-memories-gone/144057/3).
- **Privacy:** With Privacy Mode, Cursor says customer data is not used for its training and provider ZDR agreements apply, with documented abuse-investigation and non-ZDR-model exceptions. Without it, data may be stored/used to improve features and train models. BYOK requests still traverse Cursor's backend. [Data Use, updated September 3, 2026](https://cursor.com/data-use).

### Inferences

- A visible **User** section is justified by the need to locate persistent personal guidance without opening a repository. Relay should expose its current user memory cards directly instead of asking users to infer that Globals contains their personal profile.
- Keep authored rules, factual profile notes, and inferred preferences distinguishable. Offer a one-click path from repeated correction to a proposed card, with an explicit user/project scope choice.
- Preserve a portable representation and migration path. The historical removal is evidence of feature lifecycle risk; it is not evidence that learned memory itself failed or that users disliked it.
- The helper should not demand a complete biography. Offer a short, optional interview and a reviewable result; let users enrich it from actual recurring corrections. Suitable initial prompts: “What do you work on?”, “How much explanation helps?”, “Which preferences apply everywhere?”, “What should remain project-specific?”
- Suggested acceptance checks for #M7RY: users can find personal memory without a project; tell authored from inferred entries; see where each applies; revise/remove one; skip setup; and verify which entries influenced a turn. These are proposed design checks, not empirical findings.

### Gaps

- Current reviewed docs do not establish a replacement learned-memory service, a personal interview, or exact User Rules deletion controls. Editing/removing project files is structurally possible, but UI deletion semantics and synchronization were not tested. [Rules](https://cursor.com/docs/rules).
- Current search results coexist with older documentation and launch announcements. The removal post establishes a dated claim; without running specific versions, this research cannot guarantee every client/channel's behavior. [Removal response](https://forum.cursor.com/t/are-my-memories-gone/144057/3).
- General privacy settings do not establish memory-specific retention, export completeness, or deletion from past conversations. [Data Use](https://cursor.com/data-use).
