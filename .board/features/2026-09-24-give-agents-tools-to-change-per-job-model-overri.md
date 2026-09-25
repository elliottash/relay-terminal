---
id: WK7C
type: work
status: discussing
labels: [feature, switchboard, options]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: system, primary: script, also: [], human: none, criteria: 'an agent lists and clears a job''s model override through the app tools; notification with undo appears; rolestore keys change; no dialog opens, sign_off: none, effort: medium} source: pane 1, 2026-09-24 links: {plans: [], commits: [], evidence: [], related: [], github: null'}
---
# Give agents tools to change per-job model overrides (and settings like them)

## Issue
i already changed it -- but we need to give agents tools to change settings like that.

What happened: the helpers role was pinned to the `kimi` PAYG preset (`[roles] switchboard\preset=kimi` in relay.conf — the "helper agent" row of Options › Models › Agent jobs). No key for `kimi` exists in any source, so every helper console printed `helpers: no stored key for kimi; using main.` and fell back to main. The agent had no way to fix it: `app_option_set` has no row for the pin, and the only actions (`agent.modelRoles`, `row:models/agent.modelRoles`) are `agent_safe: false` — they open the pane, they set nothing. Owner cleared it by hand in the UI at 20:17.

## Planning notes
Why it is not agent-settable today (asked 2026-09-24, "why isnt it safe"):

- `actionIsAgentSafe` (src/AppCommands.cpp:257) is an **opt-in table** — owner decision 2, 2026-09-20: only read actions, the named `writingActions()` set, and one-picked-model/effort actions pass; everything else, including every action added later, is off. `agent.modelRoles` was never added.
- What it does is the other half: it *opens the Models pane's jobs tab* — a view-opening action pending the modal-pass design (`waitingOnTheModalPass`), not a value write. Even if allowed, it would not have changed the setting.
- The actual write path (`rolestore::setOverride` / JobsTab `clearOverride`, keys `roles/<role>/{preset,model,effort,tier,candidates}`) exists only inside the Jobs tab's own widgets. There is no `SettingRow` for it, so the agent catalog (`app_option_list`, 161 rows) has nothing to point `app_option_set` at.

Assets that make this cheap: `SettingRow` carries per-row reader/writer closures with QSettings as the single source of truth (SettingsPane.h:70); `set_option` in AppCommands::execute already does findRow → writeRow → read-back → "Agent changed <row>" notification with Undo → change list; `SettingsWatch::notify()` keeps the GUI from going stale; `JobsTab::jobs()` is the one table of job roles and labels.

## Done means
- An agent (pane agent or helper) can list every job's current model override and change or clear it through the app tools, without opening a dialog for the person.
- The change goes through the same path as other option writes: `writes_enabled` gate, "Agent changed <row>: before → after" notification with Undo, agent-change marker on the row, change list, GUI not one write behind (`SettingsWatch`).
- The GUI Jobs tab and the agent surface write the same rolestore keys — one source of truth; the pin the owner cleared by hand at 20:17 is exactly the kind of thing an agent could then clear on request.
- Secrets stay unsettable (no keys, no tokens); nothing widens `agent_safe` for view-opening actions.

## Plan
Recommended shape — **role override rows in the options catalog** (not a new command):

1. In `RelayWindowModels.cpp`, where the `agent.modelRoles` button row is built, add one row per job from `JobsTab::jobs()`: id `roles.<role>` (e.g. `roles.switchboard`), label the job's own ("helper agent"), Choice-or-Text row whose value encodes the override: empty = follows its tier, `tier:<tier>` for the legacy tier follow, `preset|model` for a pin (the encoding `Pane::rolesObject` already speaks). Completions/offers built from the tiers table and the provider catalog. Writer = `rolestore::setOverride` / clear keys (the Jobs tab's own functions), so GUI and agents share one write path; reader = the same rolestore read the Jobs tab uses.
2. Rows sit under a collapsible heading ("per-job models", `collapsedByDefault = true`) so Options › Models is not cluttered; the Jobs tab remains the rich view (resolved state, ranked candidates).
3. v1 scope: tier-follow and preset|model pins only. Ranked candidate lists (`candidates` key) stay Jobs-tab-only — a text row is the wrong shape for an ordered list; extend later if asked.
4. Tests: extend the `set_option` tests (AppCommands) — set `roles.switchboard` to a pin, assert the rolestore keys and the notification/undo path; clear it, assert keys removed; secret rows unaffected; `writes_disabled` still refuses.

Alternative (if rows-in-Options is unwanted): a `role` app command (list/set/clear, like `reminder`) talking to rolestore, with the same notification/undo wired by hand. Diverges from §30.2's "the row catalog is the agent's settings surface", so it is the fallback, not the plan.

Out of scope: making `agent.modelRoles` (the pane-opening action) agent-safe — that is the modal-pass question, not this.

## Tasks

- [ ] Owner picks the shape: rows in the options catalog (recommended) or a role command <!-- t:3n blocked_by=#WK7C -->
- [ ] Add per-job override rows (roles.<role>) with rolestore writers in RelayWindowModels.cpp <!-- t:kv -->
- [ ] Verify notification + undo + SettingsWatch + writes_disabled on the new rows <!-- t:gc -->
- [ ] Tests: set/clear pin via set_option path; keys land in rolestore; no secret widening <!-- t:ct -->
- [ ] Build via scripts/relay-build, run targeted tests, land with scripts/land.py <!-- t:x7 -->
