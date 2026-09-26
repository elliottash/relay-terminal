<!-- relay:entry 20260925T175353Z-sq author=agent kind=event model=glm-5.3 pane=97149268 turn=9698842186c7475288ae58fe80f77b2e/e9cd5ca937b84e6bbff8f7efa0c877a0 -->
- ✦ agent created this card in Planned · .board/features/2026-09-25-land-py-s-default-root-moves-to-a-relay-owned-lo.md

<!-- relay:entry 20260925T175401Z-zb author=agent kind=event model=glm-5.3 pane=97149268 turn=9698842186c7475288ae58fe80f77b2e/e9cd5ca937b84e6bbff8f7efa0c877a0 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3, session 97149268

<!-- relay:entry 20260925T175401Z-zc author=agent kind=progress model=glm-5.3 pane=97149268 turn=9698842186c7475288ae58fe80f77b2e/e9cd5ca937b84e6bbff8f7efa0c877a0 pane_token=97149268-97e8-4cc9-bf13-249118806781 -->
Claimed (97149268) · working on it from a terminal pane

Moving land.py's default root to $XDG_STATE_HOME/relay/land with fallbacks for both old claude roots; then auditing other borrowed names.

<!-- relay:entry 20260925T180618Z-kh author=agent kind=event model=glm-5.3 pane=97149268 turn=9698842186c7475288ae58fe80f77b2e/e9cd5ca937b84e6bbff8f7efa0c877a0 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T180710Z-j6 author=agent kind=event model=glm-5.3 pane=97149268 turn=9698842186c7475288ae58fe80f77b2e/e9cd5ca937b84e6bbff8f7efa0c877a0 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented in aaecaa7c (+1a8429a0 card record): new $XDG_STATE_HOME/relay/land default, adoption of both old roots, idle-slot reclaim, scratch.py managed listing, docs, tests. Live on this machine: 26 sessions adopted. · evidence docs/qa_evidence/2026-09-25-hrf6-relay-land-root/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260925T180716Z-q7 author=agent kind=progress model=glm-5.3 pane=97149268 turn=9698842186c7475288ae58fe80f77b2e/e9cd5ca937b84e6bbff8f7efa0c877a0 -->
Landed as aaecaa7c (card record 1a8429a0). The change went live the moment the tree edit landed: the first land.py command after it adopted 26 sessions from /tmp/claude-1000/land into ~/.local/state/relay/land, and the old root now holds only one verify build still being written — reclaim takes it 30 minutes after it goes idle. Note for the verifier: `who` with no overrides must list the other live sessions under ~/.local/state/relay/land, and `python3 -m unittest tests.test_land` must pass.

<!-- relay:entry 20260925T180859Z-5b author=agent kind=note model=glm-5.3 pane=97149268 turn=9698842186c7475288ae58fe80f77b2e/e9cd5ca937b84e6bbff8f7efa0c877a0 -->
Audit that followed this change (owner asked for other borrowed claude/warp/codex names). Deliberate, left alone: guest support for Claude Code/Codex (guest_bridge/guest_hook, CLAUDE_CONFIG_DIR, CODEX_HOME, .claude/projects reads, anthropic/claude-* model ids, "Claude Code"/"Codex" UI labels), CLAUDE.md/AGENTS.md cross-tool convention, the Warp settings-import flow (warp_imported event, "Warp" source label), the "warp" keymap preset, and scratch.py sweeping Claude Code's own $TMPDIR/claude-<uid> container (kinds claude-session/claude-scratch describe Claude's files accurately). Found and flagged to the owner: WARP.md is Relay's own instructions file under a Warp filename; untracked warp_changelog.txt (361K) and warp_terminal.txt (339K) knowledge dumps sit in the repo root; an empty .claude/worktrees/ dir lingers from the pre-ban worktree era. Relay's own state paths are otherwise all relay-named (~/.config/relay, ~/.cache/relay, ~/.local/state/relay).

<!-- relay:entry 20260926T003548Z-5c author=agent kind=event mention=3MH4 model=claude-fable-5-1 pane=f35051fe turn=dbc9af56def4427c91f4fecd2d277da4/1a6acee91a96495087b757d1ef3ee4e7 -->
mentioned in #3MH4 · 2026-09-26 · agent
