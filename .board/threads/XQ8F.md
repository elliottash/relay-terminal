<!-- relay:entry 20260924T233815Z-9r author=agent kind=event model=claude-fable-5-1 pane=c51c64a9 turn=ff5289b78ec24f1b99f57162e21ff33a/ee481f13ff8e46f4b859fcecf5b5387f -->
- ✦ agent created this card in Planned · .board/features/2026-09-24-persistent-remote-panes-every-ssh-pane-survives.md

<!-- relay:entry 20260924T233827Z-0s author=agent kind=event model=claude-fable-5-1 pane=c51c64a9 turn=ff5289b78ec24f1b99f57162e21ff33a/ee481f13ff8e46f4b859fcecf5b5387f -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by anthropic/claude-fable-5-1 via claude-code, session c51c64a9

<!-- relay:entry 20260924T233827Z-dt author=agent kind=progress model=claude-fable-5-1 pane=c51c64a9 turn=ff5289b78ec24f1b99f57162e21ff33a/ee481f13ff8e46f4b859fcecf5b5387f pane_token=c51c64a9-9994-48e2-884a-650d7ece36b3 -->
Claimed (c51c64a9) · working on it from a terminal pane

Design settled in the pane with the owner: tmux/screen on the host as an invisible per-pane session holder, ssh link first with mosh as an option and marks rerouted through OSC 52. Planning on the card, then five subagents on disjoint file areas.

<!-- relay:entry 20260924T233838Z-2n author=agent kind=event model=claude-fable-5-1 pane=c51c64a9 turn=ff5289b78ec24f1b99f57162e21ff33a/ee481f13ff8e46f4b859fcecf5b5387f -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": ["probe", "ai-text"], "human":…; replaced `## Decisions`

<!-- relay:entry 20260924T233901Z-n8 author=agent kind=event model=claude-fable-5-1 pane=c51c64a9 turn=ff5289b78ec24f1b99f57162e21ff33a/ee481f13ff8e46f4b859fcecf5b5387f -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T233944Z-k3 author=agent kind=event model=claude-fable-5-1 pane=c51c64a9 turn=ff5289b78ec24f1b99f57162e21ff33a/ee481f13ff8e46f4b859fcecf5b5387f -->
- ✦ agent updated this card · replaced `## Plan`; tasks: 0/6 done

<!-- relay:entry 20260924T234403Z-tq author=agent kind=progress model=claude-fable-5-1 pane=c51c64a9 turn=ff5289b78ec24f1b99f57162e21ff33a/ee481f13ff8e46f4b859fcecf5b5387f -->
Plan, Done means and verify block are on the card. Wave 1 running as three Relay subagents on disjoint files: A (shell/remote-holder.sh + wrapper + tests/test_ssh_shell.py), B (src/SshConfig rules + sshconfig_test), D (marks over mosh via OSC 52: remote-integration.sh, RemoteSession bootstrapLine flag, engine cores, tests). Wave 2 (C1 window, C2 pane runtime) starts when B and D land.

<!-- relay:entry 20260925T001839Z-gs author=agent kind=event model=claude-fable-5-1 pane=c51c64a9 turn=ff5289b78ec24f1b99f57162e21ff33a/ee481f13ff8e46f4b859fcecf5b5387f -->
- ✦ agent updated this card · tasks: 2/6 done

<!-- relay:entry 20260925T004446Z-k2 author=agent kind=event model=claude-fable-5-1 pane=c51c64a9 turn=ff5289b78ec24f1b99f57162e21ff33a/ee481f13ff8e46f4b859fcecf5b5387f -->
- ✦ agent updated this card · tasks: 3/6 done
