<!-- relay:entry 20260925T140251Z-a1 author=codex kind=event -->
- ✦ codex created this card in Inbox · .board/changes/2026-09-25-scratch-watcher-flags-guest-harness-home-files.md

<!-- relay:entry 20260925T140252Z-a1 author=codex kind=note -->
### Codex · 2026-09-25 14:02
Observed 2026-09-25 in a Claude Code guest session (Relay guest harness) on relay-terminal. At the end of two consecutive turns the scratch watcher (#DVV2) posted: 'These unledgered entries in the temp dir or directly under your home directory appeared during your turn: /home/elliott/.claude, /home/elliott/.claude.json. Relay owns agent scratch (#DVV2): ledger each one ... or delete it.'

Both are Claude Code's own state, not agent scratch: ~/.claude (credentials, history.jsonl, plugins/, session-env/, file-history/, projects/; 3.1 GB; created 2026-09-15) and ~/.claude.json (user settings, 77 KB). The harness rewrites files in them at every turn end (.last-update-result.json, backups/), which is why they look new each turn. Deleting or moving them would break the guest session.

Workarounds applied so far, both wrong in kind: a subagent hand-appended an install-class row for ~/.claude.json to ~/.local/state/relay/scratch-ledger.jsonl (id sc1a8f0), and the parent session did the same for ~/.claude (id sc1a8f1). The ledger's `new` verb cannot adopt an existing path and `adopt` is for the pre-ledger backlog, so there is no supported way to say 'this is the harness's own home, leave it'.

Expected: the watcher skips the guest harness's own home files for the harness that is running (Claude Code: ~/.claude, ~/.claude.json; Codex: ~/.codex; whatever the Relay-managed guest profile uses), the way it must already skip Relay's own ~/.config/relay and ~/.local/state/relay. Ideally the skip list comes from the guest profile (backend/relay_core/guest*.py) rather than a hard-coded pair, and the ledger gains a supported way to mark an existing path as an application's own (install-class, lifetime user) instead of a hand-edited JSONL row.

<!-- relay:entry 20260925T140941Z-47 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T140941Z-st author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T141342Z-ev author=agent kind=event model=glm-5.3 pane=switchboard turn=f65f108f53360f1f61b7b3e9398f3736/0ac7591370b14b748d1d1ddd55ff121b -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T141403Z-kf author=agent kind=event model=glm-5.3 pane=switchboard turn=f65f108f53360f1f61b7b3e9398f3736/0ac7591370b14b748d1d1ddd55ff121b -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T141414Z-ke author=agent kind=comment mode=plan model=glm-5.3 turn=f65f108f53360f1f61b7b3e9398f3736/0ac7591370b14b748d1d1ddd55ff121b -->
I'll read the card and its related card, then the code the card is about.

Now the code. Let me find the sweep, the skip list, and the guest profiles.

Continuing with the read-only tools. Reading the sweep function and finding the guest profiles.





`agent.py` is too large to read whole; I'll pull the relevant regions with a multi-line search.

`agent.py` refuses ranged reads (over the size limit), so I'll extract what I need by targeted searches.

Key facts emerging: `guest.py` has a `GuestSpec` with the harness home dir, and `guest_accounts.py` knows `CLAUDE_CONFIG_DIR`/`CODEX_HOME`. Now the guest registry, the scratch CLI verbs, and the tests.

Now the ledger internals and `new_dir`, plus how the sweep site is reached.

Now: how Relay's own dirs are kept out of the sweep today, and whether the sweep site knows the guest.

The sweep site can see the harness. Let me check `guest_harness_provider.py` and the claude harness's env-chain helper.



One last look — the existing guest tests, to name where the new ones go.

I have everything I need: the sweep (`scratch.py:703`), its call site (`agent.py:2816`), the guest-side knowledge of the harness home, and why `new`/`adopt` couldn't express this. Writing `## Done means` first.



Now the plan.

Plan written to #WZ3K. The sweep never had a skip list — Relay's own dirs escape only by not being top-level — so the plan adds a profile-sourced `own_home_paths()` (honouring `CLAUDE_CONFIG_DIR`/`CODEX_HOME` accounts) that the turn-end sweep passes as a `skip` list, plus an idempotent `relay-scratch own` verb so an existing path can be ledgered as install-class without hand-editing the JSONL. No owner decision is needed; it's ready for **Run**.

<!-- relay:entry 20260925T141414Z-tf author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T141921Z-te author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session ed6889a9

<!-- relay:entry 20260925T141921Z-yw author=owner kind=progress pane=switchboard pane_token=ed6889a9-4d9d-4075-9b39-8c8c0dc3cc15 -->
Claimed (ed6889a9) · working on it from a terminal pane

<!-- relay:entry 20260925T142444Z-gb author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Executing → Done, session ed6889a9 released · marked done in the Board

<!-- relay:entry 20260925T143047Z-md author=agent kind=event model=claude-opus-5-5 pane=ed6889a9 turn=69ab8d2543a84e28903f984724d1ffd6/a3df11326f4c44b9aedaa7a04031a5d4 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["DVV2"], "github": null… → {"plans": [], "commits": ["02e76d3"], "evidence": ["docs/qa_evidence/2026-09-25-…; appended to `## Tests`

<!-- relay:entry 20260925T143050Z-cv author=agent kind=evidence model=claude-opus-5-5 pane=ed6889a9 turn=69ab8d2543a84e28903f984724d1ffd6/a3df11326f4c44b9aedaa7a04031a5d4 -->
Landed in `02e76d3`. Evidence: docs/qa_evidence/2026-09-25-wz3k/README.md. At the guest turn end, the sweep passes `skip=guest_harness_provider.own_home_paths(config)`, which lists the account's config dir, the default dir and `~/.claude.json`. `relay-scratch own <path>` records a live install/user row, returns the same row if run again, and refuses a missing path. The sweep note now points at `own`. One change beyond the plan: without that note line, agents would never learn the verb exists. You had already moved the card to Done while this was running, so I left its status as it was. Separately: `TmpdirHookTests.test_session_tmpdir_points_inside_session_root_and_is_ledgered` fails whenever the shell exports `RELAY_SESSION_TOKEN` (any Relay pane). That is a test-isolation gap and unrelated to this card.
