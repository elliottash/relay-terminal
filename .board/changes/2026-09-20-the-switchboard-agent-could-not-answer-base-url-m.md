---
id: GH5T
type: work
status: needs-verification
labels: [bug, switchboard, guest, models]
assignee: agent
rank: m8
created: '2026-09-20'
source: 'owner, terminal + screenshot of a Switchboard card page, 2026-09-20'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-helper-on-guest-main], related: [4NXH, FEJQ, BRD3], github: null}
---
# The helper agent cannot run on a guest Main: it fell over on the harness base URL

## Issue
The Switchboard agent could not answer: Base URL must be an HTTPS URL without credentials, query, or fragment. Your message is kept in the thread.

## Findings
Reproduced by driving `backend/worker.py` over NDJSON as the tab's helper (`agent_role:
"switchboard"`) with the owner's own preset, `guest:claude`:

```
configured {"model": "claude-fake", "agent_role": "main", "guest": "claude"}
error      {"text": "Base URL must be an HTTPS URL without credentials, query, or fragment."}
guest harness starts: 1
```

- The helper worker is configured by the window, not by a pane, with the window's
  `provider/preset` (`RelayWindow::startBoardWorker`). The owner's Main is **Claude Code**, a Tier A
  guest harness (protocol 29.3), so the helper became a guest worker: `worker.py`'s `configure`
  started a whole `claude` process and gave the pane agent `provider=guest_provider`.
- That agent is only ever a template. `BoardCommands._build_card_agent` and `_build_page_agent`
  build the agent a card turn or a page turn runs on as `Agent(main.config, …)` with **no**
  `provider=`, so `ChatProvider` validated `harness://claude` and raised the sentence in the
  screenshot (`ProviderConfig.validate`, `backend/relay_core/provider.py`).
- So every helper surface failed the same way whenever Main was a guest — card Discuss/Plan, the
  page agent, and the Options/Actions/Sessions helper panels of `#FEJQ` — and the worker also
  started a guest process it could never use.
- The helper **cannot** run on a guest: its whole job is Relay's own `board_*` and `app_*` tools,
  which a guest does not take (`#4NXH`). `#4NXH` is the neighbour, not the same root: that card is
  about a guest *session* having no board tools; this one is about the helper *worker* being built
  on a guest config.

## Decisions
- When `agent_role` is `switchboard` and the preset is a `guest:` one, the worker **starts no
  guest**. Before any role is resolved it walks the Options › Models priority list (the
  `fallbacks` option) in the user's own order, on exactly the terms a failover walks it
  (`RoleResolver.fallback_candidate`): guest rows, entries whose key has gone and Relay Free unless
  the list names it and `hosted.available()` are all skipped. The first entry that can take a turn
  becomes the resolver's Main.
- The `configured` event tells the truth: "Follow Main — kimi-k3" in the model box, with
  `roles.switchboard.note` (and `tiers.main.note`) carrying *"Main is Claude Code, a guest session
  the helper agent cannot run on, so it fell back to kimi-k3."* into its tooltip. No C++ change was
  needed: `relay::helpermodel::fill` already reads both.
- A role pick of a real provider or tier in the helper's model box wins over the fallback, which is
  new — the guest branch used to force `agent_role: "main"` and ignore the role table entirely.
- When the list holds nothing usable the worker is still configured, because the Switchboard is
  files: the pane opens and its cards are read. Its agent is built on a stand-in provider that is
  never called, and a turn answers one sentence — *"The helper agent cannot run on Claude Code. Add
  a provider under Options › Models, or pick a model for the helper in its model box."*
- A **pane** on a guest preset is untouched: 29.3 stands exactly as written.

## QA checklist
- [ ] With Main set to Claude Code (or Codex) and at least one keyed provider ranked in Options ›
      Models, open a Switchboard, ask the page agent something and get an answer. No `claude`
      process is started for the helper.
- [ ] The helper's model box reads "Follow Main — <the fallback model>", and its tooltip says Main
      is Claude Code, a guest session the helper cannot run on, so it fell back.
- [ ] Pick a provider row in the helper's model box while Main is still Claude Code: the pick is
      honoured and the box stays on it.
- [ ] Ask from Options, Actions and Sessions on the same tab: the same helper answers.
- [ ] A terminal pane on the `guest:claude` preset still starts the guest and answers as before.
- [ ] With Main on Claude Code and the priority list empty (or every entry keyless), an ask answers
      "The helper agent cannot run on Claude Code. Add a provider under Options › Models, or pick a
      model for the helper in its model box." — not the base-URL error.
