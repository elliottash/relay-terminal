---
id: NK66
type: work
status: needs-qa-llm
component: [gui, worker, providers]
milestone: desktop-alpha
workstream: providers
assignee: implemented by Claude Opus 5 (Claude Code, model-settings worktree), 2026-09-17
rank: i1
created: '2026-09-17'
acceptance: '`docs/qa_evidence/2026-09-17-model-settings/` (29 screenshots + `drive.sh`, live under Xvfb)'
source: 'owner in chat, 2026-09-17: "i think we need to improve the model settings. make model keys a modal where it asks for GLM, Kimi, and Openrouter credentials. also add minimax coding plan, then openai / claude / gemini for PAYG keys. model roles is another modal where you can select which models come in and what they are used for. lets have main, flash, and lite presets. this should be accessible from the options menu and then also an option in the model selection dropdown: [gear] model options... first you pick default provider: glm, kimi, openrouter, anthropic, claude, gemini. recommended settings are glm + openrouter or kimi + openrouter. then you get defaults assigned: glm: glm 5.3, glm 5.3 flash, gemini 3.8 flash; kimi: kimi k3, kimi k2.7, gemini 3.8 flash. think through this to give me a good solution. and while you are at it, try to make the options menu better and more compact." Refined the same day: "have the three rows main, flash, lite, but then advanced options, which would then reveal the specific actions that could be further customized."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Model settings: an API-keys modal, a Main/Flash/Lite roles modal, and a compact Settings window

## What landed

**Keys modal** (`Settings › Models › API keys…`, palette `agent.modelKeys`, or the button inside the roles
modal). One row per provider, grouped Subscriptions / Aggregator / Pay-as-you-go, each showing whether its
key is in the keyring, comes from `RELAY_<PRESET>_API_KEY` or is missing, plus where to get one.
Add/replace, Remove (hidden for an environment key, confirmed before it deletes), **Test** (one two-word
call, reports ok or the HTTP status), Import from Warp, Import from Claude Code / Codex.

**New presets**, each verified against the provider's own docs on 2026-09-17 (doc URLs are in
`backend/relay_core/presets.py` next to each entry): `minimax`, `openai`, `anthropic`, `gemini`.

**Roles modal** (`Settings › Models › Model roles…`, palette `agent.modelRoles`, or the ⚙ at the bottom of
the pane's model box). Pick one default provider; three rows — Main, Flash, Lite — fill in from it and are
editable (model, provider, effort). **Advanced options** reveals one row per job, each showing the model it
resolves to and following its tier until pinned. Command routing is its own pinned row with the latency
reason. A tier whose provider has no key steps down towards Main and says so inline.

**Settings window** (Ctrl+, , palette `app.settings`). Sections General, Models, Terminal, Agent, Privacy,
Shortcuts, built from one catalog that the actions palette also renders, so every setting keeps a keyboard
path. The old flat "Agent options" list is gone.

Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` sections 13.7 (tiers) and 13.8 (key commands). Architecture:
"Model roles and the Main / Flash / Lite tiers", "Keys, keyring and imports", "Settings window".

## Deliberate decisions worth checking

- Three new role ids — `summaries`, `suggestions`, `audit` — were split out of `fast` and `chores` so the
  Advanced list can name one job per row. Their defaults resolve to the same models as before.
- `terminal_use` moved from "same as the main agent" to the Flash tier.
- GLM's Flash tier no longer sends `thinking: {"type": "disabled"}`: Z.AI rejects it on GLM-5.3 and
  GLM-5.3-Flash (<https://docs.z.ai/guides/capabilities/thinking>). It sends `reasoning_effort: low` with
  thinking enabled instead. **This was a live bug before this change.**
- Anthropic and MiniMax use a new `none` effort style: their OpenAI-compatible endpoints have no usable
  effort knob, so Relay sends no effort field and the GUI offers no effort choice for them.
- A tier step-down is a `note`, not a `warning`: it never reaches `model_roles.warnings` and so never
  changes the event order after `configure`.

## QA checklist

Run with an isolated `XDG_CONFIG_HOME`. `RELAY_KEYRING=off` keeps a run away from the real keyring; the
keys modal then shows every provider as "Not set" and Remove does nothing, which is itself worth checking.

**Keys modal**
1. `Ctrl+,` → Models → API keys…. Rows are grouped Subscriptions (GLM Coding Plan, Kimi Code, MiniMax),
   Aggregator (OpenRouter), Pay-as-you-go (Kimi, Z.AI standard, OpenAI, Anthropic, Google). Every row has a
   https key URL.
2. With `RELAY_OPENROUTER_API_KEY` set in the environment, the OpenRouter row reads
   "From RELAY_OPENROUTER_API_KEY" and **Remove is disabled**.
3. Add a key: it is asked for with masked input, never shown again, and the row flips to "Stored in
   keyring". Confirm with `secret-tool lookup service org.relayterminal.Relay provider <id>`.
4. Grep the whole config dir and any log for the key you typed: it must not appear anywhere.
   `grep -r "<key>" ~/.config/RelayTerminal/` must find nothing.
5. **Test** on a provider with a key reports "key works — <model> answered in N ms". On a reasoning model
   it may add "(it spent the test budget thinking, which still proves the key)" — that is a pass.
6. Test with a deliberately wrong key reports a failure whose text contains an HTTP status and **no part of
   the key and no provider response body**.
7. Remove asks for confirmation first; No leaves the key in place.
8. Import from Claude Code / Codex: with an OAuth-only `~/.codex/auth.json` it must skip and say so, not
   import the OAuth token.

**Roles modal**
9. Open it three ways — the ⚙ at the bottom of the model box, Actions › Model roles…, Settings › Models —
   and all three show the same state.
10. Switch the default provider. Every tier row, every Advanced row and the pane's own model box change
    together, and the conversation is kept (check the terminal: no "New agent conversation").
11. Per-provider defaults match protocol 13.7: GLM → glm-5.3 / glm-5.3-flash / google/gemini-3.8-flash;
    Kimi → kimi-k3 / kimi-k2.7-code-highspeed / google/gemini-3.8-flash; Anthropic → Opus 5 / Sonnet 5 /
    Haiku 4.5; OpenAI → gpt-6-astra / gpt-5.6-terra / gpt-5.6-luna.
12. With a GLM key but **no** OpenRouter key, the Lite row says "No stored key for the Lite model; using
    Flash." and resolves to glm-5.3-flash. Nothing errors, and no warning is printed into the terminal.
13. Advanced options: every row shows "Tier · model" and a "Same as tier (X)" state. Pinning one row to a
    different tier moves only that row. The disclosure stays open the next time the modal opens.
13b. "Pin to a model…" on an Advanced row asks for a provider (only ones with a stored key) and a model id,
    and the row then reads "Pinned · <provider>". Choosing a tier again clears the pin. This is the
    replacement for the old Actions › Agent options › Model roles submenu, so check that a role pinned that
    way still resolves — `roles/<role>/preset` and `model` keep their old meaning in QSettings.
14. **Command routing** stays `google/gemini-3.5-flash-lite` with the Pinned checkbox ticked, and changing
    the Lite row does not move it.
15. Effort: set the Flash tier's effort to low, then confirm with a turn on the fast agent (Alt+F) that the
    model call carries it. Anthropic and MiniMax show no effort control at all.

**Settings window**
16. Ctrl+, opens it; the six sections are General, Models, Terminal, Agent, Privacy, Shortcuts.
17. Every setting that used to be in Actions › Agent options still exists and still works: show thinking,
    shortcut hints, recap, plans folder, compaction threshold, automatic turns, step limit, tool-call limit,
    audit requests, skills and excluded skills, instructions, default effort, new panes use the fast agent,
    the two suggestion toggles.
18. The turn limits (step limit, tool-call limit, audit) apply to the **running** agent at once; the rest
    apply to the next New chat.
19. The actions palette still reaches each of them: Ctrl+Shift+A → "Settings" → a section → the row.
    Toggling from the palette and from the window must agree.
20. Changing a row that another row describes (a tier's model) updates the other row without reopening.
21. Ctrl+, in the Warp, VS Code and Konsole shortcut presets still opens "edit keyboard shortcuts", not
    Settings, and reports no keybinding conflict.

**End to end**
22. With only a keyring key (nothing in the environment), configure a provider from the roles modal and run
    a real turn. It must answer on the tier's Main model, and the key must never appear in the worker's
    stdout/stderr.
23. `./scripts/test.sh` (423) and `ctest --test-dir build` (11) pass.

## Not done / follow-ups

- The Main tier row is read-only ("this pane's model"): its model changes by picking a different default
  provider or through Settings › Models › Advanced provider settings. Editing a Main model id inline would
  need `set_model`, which restarts nothing but does need the busy-turn guard.
- `MiniMax-M2.1` is a valid pay-as-you-go model id, but no MiniMax page confirms it is inside the
  Token Plan quota, so the preset ships `MiniMax-M3` (which every Token Plan tool config shows) with
  `MiniMax-M2.7-highspeed` as Flash.
- Gemini's Pro model is still `gemini-3.1-pro-preview`; there is no GA 3.x Pro.
- The keys modal has no per-provider "spend" or quota display.
