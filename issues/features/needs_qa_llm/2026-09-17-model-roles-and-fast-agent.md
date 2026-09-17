# Model roles in settings: main, fast, terminal-use, subagent, Switchboard, chores, vision

- **Status**: needs-qa-llm
- **Component**: worker, gui
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: each role's model is pickable in Agent options; unset roles follow the main agent; the fast agent's per-provider default is applied; side calls and subagents visibly use their role's model
- **Assignee**: implemented by Claude Opus 5 (Claude Code, model-roles worktree), 2026-09-17
- **Source**: owner in chat, 2026-09-17: "these should all be pickable in settings. main agent, terminal use agent, subagent, switchboad agent. the default is they are the same (the main agent). ... you can also designate a fast agent, which is used by default in the panes, which would be deepseek for example, or gemini 3.8 flash. later on we can add routing between the main agent and fast agent in the main terminal based on estimated task difficulty." and "if you have glm as your agent, the fast agent is glm 5.3 flash by default. if openrouter, you can pick deepseek v4.1 flash or gemini 3.8 flash. does kimi have a fast model?"

## Roles
| Role | Default |
|---|---|
| Main agent | the chosen preset |
| Terminal-use agent (drives programs, fixes commands) | main |
| Subagent | main |
| Switchboard agent (card threads) | main |
| Fast agent (default in panes) | per provider, below |
| Chores (Switchboard duplicate checks, labels, titles, note scans) | `google/gemini-3.8-flash` on OpenRouter; without an OpenRouter key, the fast agent |
| Vision (image turns on non-vision presets) | GLM: GLM 5.3 Flash; otherwise unset |
| Route assist | `google/gemini-3.5-flash-lite` (unchanged, latency budget under 1 s) |

Fast agent defaults by main provider: GLM → `glm-5.3-flash`; OpenRouter → `deepseek/deepseek-v4.1-flash` (alternative
`google/gemini-3.8-flash`); Kimi → owner decision (2026-09-17): **K2.7 Code HighSpeed for now** (`kimi-k2.7-code-highspeed` on the `kimi`
platform preset, `kimi-for-coding-highspeed` on the `kimi-code` preset). Upgrade to K2.8 (`kimi-for-coding`) when it is
available through the Coding Plan API key; measure it at `reasoning_effort: low` first.

Later: route between main and fast agent in the main terminal by estimated task difficulty.

## Measurements (2026-09-17, 3 short prompts each, streaming, from this machine)
| Model | First token | Total |
|---|---|---|
| OpenRouter `deepseek/deepseek-v4.1-flash` | 0.5–1.6 s | 1.2–4.8 s (124–195 tok/s; OpenRouter picked a different host each call) |
| OpenRouter `google/gemini-3.8-flash` | 2.3–4.9 s (thinks before answering) | 2.6–4.9 s |
| OpenRouter `google/gemini-3.5-flash-lite` | 0.5–0.6 s | 0.7–1.0 s |
| Kimi `kimi-k2.7-code-highspeed` | 1.0–1.1 s | 1.0–2.3 s |
| Kimi `kimi-k3`, effort low | 3.2–3.8 s | 4.3–6.7 s |
| Z.AI coding `glm-5.3-flash`, thinking on | 3.0–3.1 s | 3.1–19.2 s |
| Z.AI coding `glm-5.3-flash`, thinking off | 1.7–3.4 s | 1.7–5.0 s |
| Z.AI coding `glm-5-turbo` | 2.7–3.6 s | 3.9–15.9 s |

## Implemented (2026-09-17)

Landed as described above, except difficulty-based routing. Implementation notes, protocol summary and the
QA checklist: `issues/features/needs_qa_llm/2026-09-17-model-roles.md`. Protocol:
`docs/AGENT-SESSIONS-PROTOCOL.md` section 13. Evidence: `docs/qa_evidence/2026-09-17-model-roles/`.

Deliberately left for later (owner: "later on we can add routing between the main agent and fast agent in
the main terminal based on estimated task difficulty"): no difficulty estimate and no automatic main↔fast
routing. A pane is on one agent at a time; the user switches with Alt+F or the palette, and new panes start
on the fast agent. When that routing is picked up, it should build on `roles.RoleResolver.resolve("fast")`
and the existing `set_agent_role` command rather than a second resolution path.
