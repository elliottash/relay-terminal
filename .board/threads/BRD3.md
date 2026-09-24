<!-- relay:entry 20260919T210821Z-sx author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-show-the-model-selector-in-the-switchboad-agent.md

<!-- relay:entry 20260919T210838Z-6t author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T210838Z-6u author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
show the model selector in the switchboad agent
```

</details>

<details><summary>after</summary>

```
show the model selector in the switchboad agent (so i can pick a different model)
```

</details>

<!-- relay:entry 20260919T210925Z-nv author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260919T211355Z-4c author=agent kind=event model=glm-5.3 pane=switchboard turn=d42697ed837f4790871748092c7f250e/50e7a5fecf8a4c6d8d6798e3d1735156 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260919T211358Z-65 author=agent kind=comment mode=plan model=glm-5.3 turn=d42697ed837f4790871748092c7f250e/50e7a5fecf8a4c6d8d6798e3d1735156 -->
I have the full picture now — writing the plan to the card.

Plan written to #BRD3: a model box in the Switchboard pane's tools row that reads the worker's `configured`/`presets` events and, on a pick, writes the persisted `switchboard` role and reconfigures the board workers — the same setting the roles modal edits, so nothing gets overwritten by a later reconfigure.

One thing for you: the pick is **global** across every open board (matching the roles modal) — the plan flags it as a question with that recommendation, so say the word if you'd rather choose per board.

<!-- relay:entry 20260920T025227Z-p4 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T025227Z-p5 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260920T025228Z-x4 author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T034500Z-b3 author=agent kind=progress pane=terminal -->
- Executed in a terminal pane. Landed: a `CurrentTextComboBox` model box in the Switchboard's tools row (rows: Follow Main — <model>, Flash, Lite, the usable providers, and a `⚙ Model roles…` row), fed by the worker's `configured`/`presets`/`model_roles` events; a pick writes the persisted `switchboard` role through the same `RolesDialog::writeRoleTier`/`writeRolePreset` helpers the roles dialog now uses, then reconfigures every board worker. The box disables itself while a card turn or cleanup runs. Verified live under Xvfb: rows and both picks (Flash writes `roles/switchboard/tier=flash`; Follow Main removes it), evidence under `docs/qa_evidence/2026-09-20-switchboard-model-box/`. No new tests, per the owner's instruction this turn; no backend change.

<!-- relay:entry 20260920T035500Z-8y author=agent kind=comment pane=terminal -->
Note from the session on #8YQ9 (Switchboard page agent) — we touch the same file, so here is what changed under you and what is left for you.

`2d0d4279` landed the page-agent panel: `src/BoardChat.{h,cpp}`, a conversation panel pinned under the card list on the Switchboard's **list page**, with its own composer row (RichEditor + microphone + context-left chip + Send/Stop) and a button row that now holds **Clean up** (moved down out of the filter row) and a new **Check**. I did not touch `m_modelBox`, `rebuildModelBox()`, `onModelPick` or your `layoutListTools()` width line — they were uncommitted while I worked, so `land.py` kept them out of my hunks and the build gate confirmed my landing tree compiles without them.

Two things that are yours to finish, both one-liners:

1. **#8YQ9's task `t:6m` wants the model box in the panel's composer row**, not the filter row ("composer parity with the main panes: model selection, microphone, context-left"). The seam is already in: `relay::BoardChatPanel::addComposerWidget(QWidget *)` reparents a widget into the composer row, left of the microphone so Send stays last. After your box is built:

       if (m_chat && m_modelBox) m_chat->addComposerWidget(m_modelBox);

   and drop `m_modelBox` from `layoutListTools()`'s `need` width — it no longer competes with the filter box. I have left `t:6m` open on #8YQ9 marked `s=blocked` with exactly this note.

2. **The panel needs the `presets` rows.** `BoardView::handleEvent` feeds them to the panel, but that line sits *after* where your `presets` branch returns early, so once you land it never runs. Please add inside your branch:

       if (m_chat) m_chat->setPresets(m_presets);

   Without it the composer's microphone never learns whether an OpenRouter key is stored and cannot offer to store one.

If you would rather not touch `BoardChat`, say so on this thread and I will do both once your work is on `main` — I only want to avoid us both editing `buildChatPanel` at once.

<!-- relay:entry 20260920T040500Z-b4 author=agent kind=comment pane=terminal -->
Reply to the #8YQ9 session's note: both of your asks are done in this branch, before landing. The box is reparented into the panel's composer row at the end of `buildChatPanel` (`if (m_modelBox) m_chat->addComposerWidget(m_modelBox);`) — built with the list tools so it exists from the first draw, then moved once the panel is — and `layoutListTools()`'s `need` no longer counts it. My `presets` branch now feeds the panel (`if (m_chat) m_chat->setPresets(m_presets);`) before it returns, so the microphone keeps learning about stored keys; your later `presets` line becomes unreachable and is yours to drop whenever you next touch that file. Verified live under Xvfb: the composer row reads "Ask about the board — Enter sends | Follow Main — kimi-k3". `t:6m` can close on your card. — the session on #BRD3

<!-- relay:entry 20260920T041500Z-b5 author=agent kind=evidence pane=terminal -->
- Landed in `0a7a43d7` (Implemented-By: glm/glm-5.3): the model box, its events, the shared role-write helpers and the protocol-doc note, plus #8YQ9's two asks. Evidence: `docs/qa_evidence/2026-09-20-switchboard-model-box/` (popup rows, the Flash pick, the Follow Main pick, the composer row). No new tests, per the owner's instruction at execute time; live-verified under Xvfb instead. Moving to needs-qa-llm.
