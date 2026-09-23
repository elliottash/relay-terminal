<!-- relay:entry 20260923T172518Z-wk author=codex kind=note -->
### Codex · 2026-09-23 17:25
Filed the owner's save/load workspace request. A one-time manual snapshot of the live workspace was made separately under `~/.local/share/relay/diagnostics/workspace-snapshot-2026-09-23T172427Z/`; this card is for the product function.

<!-- relay:entry 20260923T180510Z-7p author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260923T180510Z-mx author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260923T180729Z-sf author=agent kind=event model=kimi-k3 pane=switchboard turn=43c54f7d164b96f83a81f425bcf046c0/e815846f49e649c68ab35d51dc58bd03 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260923T180823Z-me author=agent kind=event model=kimi-k3 pane=switchboard turn=43c54f7d164b96f83a81f425bcf046c0/e815846f49e649c68ab35d51dc58bd03 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T180836Z-jt author=agent kind=comment mode=plan model=kimi-k3 turn=43c54f7d164b96f83a81f425bcf046c0/e815846f49e649c68ab35d51dc58bd03 -->
I'll read the card, the related card, and the code behind workspace persistence.

Now let me read the actual persistence code in `src/` and the architecture notes.

Commands are refused in a Plan turn — I'll keep reading with the file tools. Let me see how actions are registered and where session files live.







One more read to pin down the session store layout.

I have the full picture. Writing the revised `## Done means` first.

Now the plan itself.

**Done:** #6WKR now has a revised `## Done means` and a full `## Plan`. The plan adds a pure `src/WorkspaceSnapshot.h/.cpp` module that packages `state/windows.json`, the referenced scrollback and prompt-history files, and the session files they point at into a versioned, portable `snapshots/<name>/` folder, plus two palette actions — save (snapshot current state) and load (confirm what will be replaced, report missing session data, stage atomically, then restore).

Two decisions are flagged in Risks for you: live load closes current windows behind a confirmation (vs load-on-next-start), and the plan copies session `.blobs/` so `/rewind-code` survives a load. Say the word if either should go the other way; otherwise the card is ready for Execute.

<!-- relay:entry 20260923T180836Z-ju author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
