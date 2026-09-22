# Codex, OpenCode, and VS Code/GitHub Copilot: user memory for Relay

Research retrieved **2026-09-22**. Assignment label: #UMRY; the local parent card currently identifies itself as M7RY. This is a research handoff, not an implementation specification. All web references below are official live documentation opened during this research. Findings describe the retrieved documentation, not a hands-on test of each product. Recommendations are explicitly marked as inferences.

Local inspection: the installed Codex package is version 0.155.1; its JavaScript launcher selects a native executable. No memory implementation source was present in the inspected package, so this inspection does not establish runtime memory behavior. Evidence: [installed package](/home/elliott/.npm-global/lib/node_modules/@openai/codex/package.json), [launcher](/home/elliott/.npm-global/lib/node_modules/@openai/codex/bin/codex.js). The research did not read personal memory contents or change configuration.

## How does Codex distinguish instructions from learned memory and expose control?

### Takeaway

Codex documents a separate local learned-memory system alongside durable instruction files. Relay should retain that distinction: stable working agreements need predictable application, while inferred recollections need review and provenance. [Memories](https://learn.chatgpt.com/docs/customization/memories)

### Cited Findings

- **Authored scope and retrieval:** Codex reads global instructions from `CODEX_HOME` (normally `~/.codex`), preferring `AGENTS.override.md` over `AGENTS.md`. Project discovery proceeds from repository root to working directory, selecting one instruction file per directory. Later, more specific guidance overrides earlier guidance. The chain is built once per run. Empty files are skipped; the combined default limit is 32 KiB via `project_doc_max_bytes`. [AGENTS.md documentation](https://learn.chatgpt.com/docs/agent-configuration/agents-md)
- **Creation and maintenance:** Users create and edit global/project Markdown files; removing a global override restores the base file. These are explicit working agreements, distinct from automated extraction. [AGENTS.md documentation](https://learn.chatgpt.com/docs/agent-configuration/agents-md)
- **Learned store:** Local Codex memories are off by default and separate from ChatGPT web memory. CLI and desktop expose `/memories`; the IDE uses its connected host's store. Memory files live under `~/.codex/memories/`. Background generation skips active/short-lived chats and redacts secrets. Files are inspectable generated state; manual editing is discouraged as the main control surface. [Memories](https://learn.chatgpt.com/docs/customization/memories)
- **Read versus learn controls:** Per-chat choices separately control using memories and contributing to future memories, without changing global settings. Desktop global control is Settings → Personalization. Required team guidance belongs in checked-in instructions, not solely in memory. [Memories](https://learn.chatgpt.com/docs/customization/memories)
- **Limits and privacy switches:** `generate_memories` and `use_memories` default true within the enabled feature. `disable_on_external_context` defaults false; enabling it excludes chats that used MCP/web/tool-search context from generation. Defaults include six idle hours before extraction, 30-day source-chat age, 16 rollout candidates per startup, 256 recent raw memories for consolidation, and 25% minimum remaining rate-limit quota. An unused memory becomes ineligible for consolidation after 30 days by default; this wording does **not** establish immediate physical deletion. [Configuration reference](https://learn.chatgpt.com/docs/config-file/config-reference)
- **Onboarding:** The app gained `/init` for generating project instructions using the CLI initialization workflow. The June 16 regional rollout entry discusses memory availability, but the current dedicated memory page is the stronger source for current controls/defaults. [Changelog](https://learn.chatgpt.com/docs/changelog)

### Inferences

- **Relay recommendation:** Label records by origin: “You told Relay,” “Imported instruction,” or “Suggested from prior work.” Do not let an inferred preference silently replace an explicit working agreement. This follows the documented division between required guidance and recall. [Memories](https://learn.chatgpt.com/docs/customization/memories)
- **Relay recommendation:** If automatic learning is added, offer separate “Use saved memory” and “Learn from this conversation” controls. They answer different user intentions: private work can still benefit from existing preferences without becoming future knowledge. [Configuration reference](https://learn.chatgpt.com/docs/config-file/config-reference)
- **Relay recommendation:** Display when edits will affect context: next turn, next conversation, or matching paths. A visible saved record is insufficient if the user cannot tell whether an already-running agent has reloaded it. Codex's once-per-run instruction behavior demonstrates this distinction. [AGENTS.md documentation](https://learn.chatgpt.com/docs/agent-configuration/agents-md)

### Gaps

- The reviewed Codex documents do not establish a complete per-entry edit/delete UI, deletion propagation, a dedicated user-versus-project learned-memory permission boundary, or exact memory injection token budget. These are **uncertainties**, not evidence that those capabilities do not exist. The documented host-local store should not be described as account-wide synchronized memory. [Memories](https://learn.chatgpt.com/docs/customization/memories)
- No dedicated personal-preference interview was identified in the reviewed material; `/init` is evidence for project setup, not for a personal interview. [Changelog](https://learn.chatgpt.com/docs/changelog)

## What does OpenCode provide natively, and what does its setup flow teach Relay?

### Takeaway

OpenCode's documented native mechanism is explicit rules with global/project scope. Its official ecosystem also lists a persistent-memory plugin; that is not proof of a native learned-memory service. [Rules](https://opencode.ai/docs/rules/), [Ecosystem](https://opencode.ai/docs/ecosystem/)

### Cited Findings

- **Scope:** Project `AGENTS.md` applies in the project and descendants; `~/.config/opencode/AGENTS.md` supplies personal rules across sessions. The global file is not normally committed/shared with teammates. [Rules](https://opencode.ai/docs/rules/)
- **Creation/update:** `/init` inspects repository files, asks targeted questions when needed, and creates or improves existing `AGENTS.md`. It captures commands, verification order, architecture, conventions, and setup pitfalls. [Rules](https://opencode.ai/docs/rules/)
- **Retrieval:** Rule files enter model context. Native files take precedence over Claude-compatible fallbacks in each category. The `instructions` configuration accepts files, globs, and remote URLs; remote fetches have a five-second timeout. References inside `AGENTS.md` are explicitly **not automatically parsed**; users can configure loading or instruct the agent to read them. [Rules](https://opencode.ai/docs/rules/)
- **Maintenance:** Rules are directly authored Markdown, making file editing the documented customization surface. The rules page does not describe an automatic personal-memory review queue. [Rules](https://opencode.ai/docs/rules/)
- **Learned extensions:** The official ecosystem lists `opencode-supermemory` as a plugin for persistence across sessions. Its listing establishes extensibility, not the plugin's retention, privacy guarantees, or default installation. [Ecosystem](https://opencode.ai/docs/ecosystem/)
- **Privacy:** OpenCode says processing occurs locally or through direct provider API calls. Optional `/share` sends conversation-associated data to its hosting service; `share: "disabled"` disables that feature. Provider handling therefore remains distinct from local instruction storage. [Enterprise/data handling](https://opencode.ai/docs/enterprise/)

### Inferences

- **Relay interview recommendation:** Begin by reading existing saved preferences and approved instruction sources. Ask only for unknowns, then propose small additions or corrections. Avoid re-interviewing users about facts the software can inspect. OpenCode's targeted `/init` questions are a useful model, although Relay's interview should concern the person rather than regenerate repository guidance. [Rules](https://opencode.ai/docs/rules/)
- **Relay review recommendation:** In the user-memory section, show the editable text and effective scope together. A preference such as “Use pnpm” should have a clear choice between a personal default and a particular repository's requirement. Explicit global/project files make that distinction comprehensible. [Rules](https://opencode.ai/docs/rules/)
- **Relay privacy recommendation:** Explain separately where records reside, which model receives them when applied, and whether export/sharing includes them. “Stored locally” alone does not answer data-flow questions. [Enterprise/data handling](https://opencode.ai/docs/enterprise/)

### Gaps

- **Uncertain, not documented absence:** No native automatic learned-user-memory service, per-item memory UI, expiry policy, or retrieval ranking was established by these pages. A plugin listing does not prove the native feature is absent. [Rules](https://opencode.ai/docs/rules/), [Ecosystem](https://opencode.ai/docs/ecosystem/)
- **Documented absence:** Automatic parsing of references within `AGENTS.md` is explicitly unsupported; configured instruction loading and prompted reads are alternatives. No numerical total rule-context cap was established. [Rules](https://opencode.ai/docs/rules/)

## How do VS Code and GitHub Copilot differ, and what should Relay adopt?

### Takeaway

Keep three concepts separate in the comparison: VS Code instruction files, VS Code's local memory tool, and GitHub's Copilot Memory service. Their scope, controls, and retention are not interchangeable. [VS Code instructions](https://code.visualstudio.com/docs/agent-customization/custom-instructions), [VS Code memory](https://code.visualstudio.com/docs/agents/run/memory), [Copilot Memory](https://docs.github.com/en/copilot/concepts/agents/copilot-memory)

### Cited Findings

- **Instructions:** VS Code supports always-on workspace instructions and selectively applied `.instructions.md` files. User locations include `~/.copilot/instructions` and `~/.claude/rules`; Agent Host sessions read supported folders rather than VS Code profile data. User instructions can sync with Settings Sync. The documented conflict priority is personal, repository, then organization. [Custom instructions](https://code.visualstudio.com/docs/agent-customization/custom-instructions)
- **Instruction editing/interview:** The Agent Customizations editor manages files. `/create-instructions` asks clarifying questions and generates targeted instructions; users can extract a convention from a correction in the current chat. `/init` discovers existing conventions and generates workspace guidance. [Custom instructions](https://code.visualstudio.com/docs/agent-customization/custom-instructions)
- **Local memory scopes:** VS Code documents user `/memories/`, workspace `/memories/repo/`, and conversation `/memories/session/` scopes, stored locally. User memory crosses workspaces; repository memory persists within its workspace; session memory does not survive the conversation. The first 200 lines of user memory load automatically at session start. [Use memory](https://code.visualstudio.com/docs/agents/run/memory)
- **Local lifecycle:** The agent can save notes while working or following “remember” requests, choosing scope and creating/updating files. `Chat: Show Memory Files` lists records; `Chat: Clear All Memory Files` removes all scopes. Individual updates/deletions are requested through the agent, and memory references in chat are clickable. [Use memory](https://code.visualstudio.com/docs/agents/run/memory)
- **GitHub service:** Copilot Memory is a paid-plan public preview used by cloud agent, code review, and CLI. Repository facts stay within their repository and can be shared among eligible users; personal preferences follow the same user across repositories. Code review applies repository facts only. Repository facts require write-access user activity to be created and carry code citations validated against the current branch; personal preferences can cite user statements. [About Copilot Memory](https://docs.github.com/en/copilot/concepts/agents/copilot-memory)
- **Retention/ownership:** Unused service entries expire after 28 days, with successful validation/use potentially resetting that timer. On managed plans, personal preferences belong to the active billing entity, whose administrators can export/delete them; retrieval respects that owner. Personal does not mean inaccessible to the organization. [About Copilot Memory](https://docs.github.com/en/copilot/concepts/agents/copilot-memory)
- **Service control:** Individual paid accounts have memory enabled by default; managed accounts depend on administrative policy. Users can disable it in personal Copilot settings. Users review/delete personal preferences under Copilot → Memory; repository owners review/delete facts under repository Settings → Copilot → Memory. Multiple managed licenses require selecting a default billing entity. [Manage for yourself](https://docs.github.com/en/copilot/how-tos/use-copilot-agents/copilot-memory/manage-for-yourself)
- **Historical caution:** VS Code's January 2026 release notes describe a preview memory integration using `github.copilot.chat.copilotMemory.enabled` and GitHub settings. Current VS Code documentation instead describes local scoped files. Do not combine that old preview's controls with the current local-memory behavior as if they were one stable interface. [January release notes](https://code.visualstudio.com/updates/v1_109), [Current memory guide](https://code.visualstudio.com/docs/agents/run/memory)

### Inferences

- **Relay review surface:** Give every saved fact a readable statement, scope, origin/source, last correction, and active/retired state. Make editing as direct as deleting. Add an explanation of when it is applied. Citation-backed Copilot memories suggest a useful provenance pattern without requiring Relay to copy its hosted architecture or automatic expiry. [About Copilot Memory](https://docs.github.com/en/copilot/concepts/agents/copilot-memory)
- **Relay retrieval:** Reuse the existing pinned/path-based machinery described in the assignment. Show “Always included” versus “Included for matching paths,” and expose the effective loaded set. Do not market a stored entry as universally recalled if selection or a budget excludes it. VS Code's explicit 200-line preload illustrates why retrieval limits deserve user-facing clarity. [Use memory](https://code.visualstudio.com/docs/agents/run/memory)
- **Relay interview proposal:** Offer a short, skippable interview, then an editable preview of proposed records. Each proposed record should state scope and why it helps. Use the existing Globals save workflow; an unsaved answer should not silently become durable state. The reviewed clarification-and-generation flow provides a precedent for producing reviewable instructions. [Custom instructions](https://code.visualstudio.com/docs/agent-customization/custom-instructions)

Suggested interview topics below are **Relay design proposals**, not competitor features or established facts about this user:

| Topic | Example question | Proposed storage decision |
| --- | --- | --- |
| Communication | How concise should updates be, and when is a fuller explanation helpful? | User preference, with contextual exceptions |
| Work mix | What work do you mainly use Relay for? | Short user context; skip personal biography |
| Tool defaults | Which shell, languages, package managers, and common tools do you prefer? | Personal default; keep repository requirements local |
| Autonomy | When should an agent proceed, ask a question, or present a plan? | Explicit working agreement; never bypass enforced permissions |
| Verification | What evidence makes a task feel finished to you? | User default; repository-specific commands remain project-scoped |
| Environment | Which machines or remote environments do you commonly use? | Names/roles only; no credentials or secret values |
| Boundaries | What should Relay avoid remembering? | Memory preference and exclusion; implementation must define enforcement |
| Correction | Which saved preference is wrong or no longer useful? | Update or retire the existing record, avoiding duplicates |

The design principle behind the table is to turn clarified preferences into small, inspectable records, following the documented targeted-instruction workflow. [Custom instructions](https://code.visualstudio.com/docs/agent-customization/custom-instructions)

### Gaps

- GitHub documents viewing/deleting service memories, but the reviewed management page does not establish direct editing or a user-authored creation form. This is **unknown**, not proof of unsupported editing. [Manage for yourself](https://docs.github.com/en/copilot/how-tos/use-copilot-agents/copilot-memory/manage-for-yourself)
- The current VS Code page does not establish an overall storage cap, exact repository retrieval algorithm, per-entry expiry, cross-device memory sync, or correspondence between its local store and GitHub's hosted service. Settings Sync for instruction files is not evidence that learned memories sync. [Use memory](https://code.visualstudio.com/docs/agents/run/memory), [Custom instructions](https://code.visualstudio.com/docs/agent-customization/custom-instructions)
- A full onboarding interview is a Relay opportunity, not a documented feature shared by these competitors. The strongest observed precedent is targeted clarification while generating instructions. [Custom instructions](https://code.visualstudio.com/docs/agent-customization/custom-instructions)
