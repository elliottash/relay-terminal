<!-- relay:entry 20260919T034600Z-a1 author=agent kind=event -->
- ✦ agent created the card · status in-progress · assignee agent · implementation orchestrated from Warp plan 72204e45-c3d4-419f-b94e-f5f5b045a480

<!-- relay:entry 20260919T034600Z-b2 author=agent kind=plan -->
Foundation lands on branch `feature/guest-agents` (guest registry, foreground detection, pane guest state), then
local child agents per workstream in worktrees off that branch: claude-bridge, claude-hooks, codex-tierb,
sessions-index (wave 1), composer-translator (wave 2). Lead integrates, full validation, evidence under
`docs/qa_evidence/2026-09-19-claude-codex-guest-integration/`.

<!-- relay:entry 20260919T042557Z-n7 author=agent kind=progress model=glm-5.3 pane=ab81944f turn=9a6ed662c79645e38e7f49206e0691f6/c0dadbc1a78c4d288467077e00af741f -->
Foundation landed (d166332, was bccba64 before the close-out session's rewrite): registry, foreground classification, `guest` in program_state — tests/test_guest.py 11/11 green. The cross-phase contracts are now fixed in protocol 26.3–26.8 (commit 72f4224): one guest event channel (guest-event.py → guest.json, inode-polled beside state.json, no-op-without-env invariant), guest_model/guest_context_pct/guest_busy pane fields, additive marked-entry settings rules, and per-phase file ownership. Wave 1 running in four worktrees off 72f4224 (relay-wt-gt7x-{hooks,bridge,codex,sessions}), one subagent each: hooks+channel, claude IDE bridge with blocking openDiff, codex Tier B hooks+rollout tail, and the claude/codex session sources. Composer translator (wave 2) starts once the channel lands; lead integrates on feature/guest-agents, then full validation + evidence.

<!-- relay:entry 20260919T051058Z-c9 author=agent kind=progress -->
- Wave-1 delegation failed structurally: the subagent model's 8k-token output budget cannot emit
  file writes, so all four phase agents (hooks/bridge/codex/sessions) ended mid-research with zero
  filesystem changes across two rounds (~2.7M tokens). Hooks phase implemented solo instead.
  Hooks landed on branch gt7x in worktree relay-wt-gt7x-hooks: 4498fa2 on 467138f — the guest
  event channel (shell/guest-event.py, atomic guest.json beside state.json, no-env no-op), the
  claude hook/statusline shim (guest_hook.py: hook forwarding, statusline passthrough, permission
  answer-file round-trip timing out to the guest's own TUI, never auto-approved; additive
  relay-guest-marked settings writer), pane polling of guest.json, guest_model/guest_context_pct/
  guest_busy in program_state, program-bar chips, notifications, the slash list stored for the
  composer, and the handleGuestBridgeEvent seam. Build green; test_guest_hook +
  test_program_input 52/52; full scripts/test.sh + ctest not run (owner: "skip tests").
  Not done: bridge, codex, sessions, composer phases; merging gt7x onto feature/guest-agents
  (a Warp session landed 68f9a41/4af9d0a and more concurrently — Pane.h conflicts to resolve);
  full validation + QA evidence.
