# Model roles in settings: main, fast, terminal-use, subagent, Switchboard, chores, vision

- **Status**: open
- **Component**: worker, gui
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: each role's model is pickable in Agent options; unset roles follow the main agent; the fast agent's per-provider default is applied; side calls and subagents visibly use their role's model
- **Assignee**: unassigned
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
`google/gemini-3.8-flash`); Kimi → owner decision (2026-09-17): **K2.8**. K2.8 Preview is only on the Kimi Code subscription
(`https://api.kimi.ai/coding/v1`, model id `kimi-for-coding`, needs Plus or above, API key from the Kimi Code Console;
https://www.kimi.com/code/docs/en/kimi-code/models.html), not on the Moonshot platform API behind the `kimi` preset.
A `kimi-code` preset now exists. For the platform `kimi` preset the fast default is `kimi-k2.7-code-highspeed`. K2.8
always thinks, so measure it at `reasoning_effort: low` before making it the default; fallback
`kimi-for-coding-highspeed` (K2.7 Code HighSpeed, 262K context, "~5–6× faster output" per the docs).

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
