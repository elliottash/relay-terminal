<!-- relay:entry 20260924T163302Z-dd author=agent kind=event model=claude-opus-5-5 pane=dd137998 turn=ff5289b78ec24f1b99f57162e21ff33a/d037c89c88964e9f9ee4cf43be61683a -->
- ✦ agent created this card in Discussing · issues/features/2026-09-24-zellij-support-and-mosh-plus-zellij-as-relay-s-p.md

<!-- relay:entry 20260924T163312Z-da author=agent kind=note model=claude-opus-5-5 pane=dd137998 turn=ff5289b78ec24f1b99f57162e21ff33a/d037c89c88964e9f9ee4cf43be61683a -->
Exploration, 2026-09-24 (Claude Code session). The code and docs were read. Zellij is not installed on this machine, so the zellij behaviour below is still unverified.

Where things stand:
- mosh is already built (#S5SH, docs/SSH-AND-MOSH.md §1, §3b, §6). The wrapper adds ControlMaster through --ssh and --experimental-remote-ip=remote, with a fallback for hosts behind NAT. The login is detected from mosh-client's argv. The alternate screen is not treated as a full-screen program, the prompt box types into the host and the agent gets a host. Limits: mosh drops OSC 7/133, so a mosh login is never enhanced and the agent's reply stays in the side panel. mosh also survives a network drop but not a client restart.
- zellij appears nowhere in src/, backend/, shell/ or docs/.

Gaps found in the code that zellij would hit:
1. shell/remote-integration.sh detects only $TMUX and $STY/screen. It does not check $ZELLIJ. Zellij has no DCS passthrough like tmux's, so OSC 133/7 inside zellij probably never reach Relay (unverified).
2. Keymap::actsInsidePrograms (src/Keymap.h:116) keeps Alt+arrows and Alt+-/Alt+= for Relay while a program has the keyboard. These are zellij's default focus and resize keys, so zellij cannot receive them inside a Relay pane.
3. relay::screen::isShellPrompt reads the whole cursor row. Zellij pane frames, and tmux vertical splits, put │ or the next pane's text after the prompt, so the remote prompt is probably missed. Clipping the row at the cursor column would fix both multiplexers.
4. #87HB option 3 ('use an existing keeper') fits zellij: `mosh host -- zellij attach -c <name>`, re-run by window restore, would give remote persistence across a Relay restart without a helper binary.

Proposed tiers:
A. Zellij works correctly inside Relay: add the $ZELLIJ check, clip the prompt row at the cursor, and give zellij a key carve-out (or suggest zellij's unlock-first preset). A test would run a real zellij as the tmux tests in tests/test_ssh_shell.py run tmux.
B. A persistent remote pane: 'Connect to host (persistent)' in the palette runs mosh (or ssh) plus `zellij attach -c relay-<pane>` or tmux new -A. Window restore re-attaches.
C. Structured integration through `zellij action` / a WASM plugin + `zellij pipe` (the counterpart of tmux -CC): map zellij panes onto Relay splits and read pane cwd and command. This is large; defer it until A and B have been used.

<!-- relay:entry 20260924T163315Z-nh author=agent kind=question model=claude-opus-5-5 pane=dd137998 turn=ff5289b78ec24f1b99f57162e21ff33a/d037c89c88964e9f9ee4cf43be61683a -->
1. Which tiers do you want? Recommendation: A now (it is small and fixes real conflicts), B next, C deferred.
2. For B, should the persistence layer be zellij only, or zellij or tmux, whichever the host has? Recommendation: whichever the host has, preferring zellij, with no install on the host.
3. Should Relay's Alt+arrow pane focus give way to zellij (and tmux) when one of them owns the pane? Recommendation: yes, keep Ctrl+Alt+arrows as Relay's fallback there.

<!-- relay:entry 20260924T214255Z-m4 author=agent kind=decision model=kimi-k3 pane=a5e3e378 turn=ff5289b78ec24f1b99f57162e21ff33a/7ddc5da964e74a86b2c7c47763470447 -->
Owner, 2026-09-24, answering the three questions verbatim: "1 A and B yes / 2 yes / 3 i agree, but shift+alt+arrows for relay". Meaning: tiers A and B are approved (C stays deferred); the persistence layer is zellij or tmux, whichever the host has; and when a program owns the pane's keyboard, Alt+arrows go to the program (so zellij/tmux get their focus keys) while Shift+Alt+arrows become Relay's pane navigation fallback.

<!-- relay:entry 20260924T214255Z-m5 author=agent kind=event model=kimi-k3 pane=a5e3e378 turn=ff5289b78ec24f1b99f57162e21ff33a/7ddc5da964e74a86b2c7c47763470447 -->
- ✦ agent updated this card · appended to `## Decisions`; verify defaulted from skill deliver

<!-- relay:entry 20260924T214318Z-4g author=agent kind=event model=kimi-k3 pane=a5e3e378 turn=ff5289b78ec24f1b99f57162e21ff33a/7ddc5da964e74a86b2c7c47763470447 -->
- ✦ agent updated this card · verify: {"artifact": "code", "primary": "script", "also": [], "human": "none", "sign_off… → {"artifact": "code", "primary": "script", "also": ["ai-text"], "human": "optiona…; appended to `## Done means`

<!-- relay:entry 20260924T214327Z-qg author=agent kind=event model=kimi-k3 pane=a5e3e378 turn=ff5289b78ec24f1b99f57162e21ff33a/7ddc5da964e74a86b2c7c47763470447 -->
- ✦ agent claimed this card · assignee agent, Discussing → Executing, implemented_by kimi/kimi-k3, session a5e3e378

<!-- relay:entry 20260924T214327Z-w0 author=agent kind=progress model=kimi-k3 pane=a5e3e378 turn=ff5289b78ec24f1b99f57162e21ff33a/7ddc5da964e74a86b2c7c47763470447 pane_token=a5e3e378-525d-4383-a6ad-a17507924ee9 -->
Claimed (a5e3e378) · working on it from a terminal pane

Decisions recorded. First step: install zellij locally and verify empirically whether it passes OSC 133/7 through (determines the integration-script design), then tier A (screen classifier prefix, keymap give-way, $ZELLIJ detection), then tier B (persistent mosh+multiplexer palette entry).

<!-- relay:entry 20260924T214745Z-fr author=owner kind=event pane=a5e3e378 -->
Released (a5e3e378) · the pane closed

<!-- relay:entry 20260924T215051Z-2d author=agent kind=event model=glm-5.3 pane=133ad9fd turn=ff5289b78ec24f1b99f57162e21ff33a/d733b0316c694e96896f02bd4987f1ff -->
- ✦ agent claimed this card · implemented_by glm/glm-5.3, session 133ad9fd

<!-- relay:entry 20260924T215051Z-xv author=agent kind=progress model=glm-5.3 pane=133ad9fd turn=ff5289b78ec24f1b99f57162e21ff33a/d733b0316c694e96896f02bd4987f1ff pane_token=133ad9fd-4106-45fd-aeb8-7f3860f41b90 -->
Claimed (133ad9fd) · working on it from a terminal pane

Re-claiming in the new pane session (the previous pane was closed mid-verification; the card was released). Resuming: finish the zellij passthrough probe, then tiers A and B.

<!-- relay:entry 20260924T215102Z-58 author=owner kind=event pane=133ad9fd -->
Released (133ad9fd) · the pane closed

<!-- relay:entry 20260924T220648Z-72 author=agent kind=event model=glm-5.3 pane=8c549c94 turn=ff5289b78ec24f1b99f57162e21ff33a/1927799a538844c089d07c114f89db6f -->
- ✦ agent claimed this card · session 8c549c94

<!-- relay:entry 20260924T220648Z-73 author=agent kind=progress model=glm-5.3 pane=8c549c94 turn=ff5289b78ec24f1b99f57162e21ff33a/1927799a538844c089d07c114f89db6f pane_token=8c549c94-8513-4d49-be07-82c38d276ed9 -->
Claimed (8c549c94) · working on it from a terminal pane

Re-claiming after another pane restart. Resuming the zellij passthrough probe (as a background job this time so the pane stays responsive), then tiers A and B.

<!-- relay:entry 20260924T220909Z-tv author=agent kind=event model=glm-5.3 pane=8c549c94 turn=ff5289b78ec24f1b99f57162e21ff33a/1927799a538844c089d07c114f89db6f -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260924T222334Z-2h author=agent kind=event model=glm-5.3 pane=8c549c94 turn=ff5289b78ec24f1b99f57162e21ff33a/1927799a538844c089d07c114f89db6f -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260924T222341Z-wm author=agent kind=event model=glm-5.3 pane=8c549c94 turn=ff5289b78ec24f1b99f57162e21ff33a/1927799a538844c089d07c114f89db6f -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260924T222344Z-1s author=agent kind=event model=glm-5.3 pane=8c549c94 turn=ff5289b78ec24f1b99f57162e21ff33a/1927799a538844c089d07c114f89db6f -->
- ✦ agent moved this card · Executing → Needs verification · Tiers A and B implemented, committed (0afa8dd1) and tested (ctest screen/keymap/sshconfig green, tests/test_zellij.py 2/2 against real zellij 0.45.1); evidence under docs/qa_evidence/2026-09-24-zellij-VD2M/. · evidence docs/qa_evidence/2026-09-24-zellij-VD2M/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260924T222346Z-qn author=agent kind=progress model=glm-5.3 pane=8c549c94 turn=ff5289b78ec24f1b99f57162e21ff33a/1927799a538844c089d07c114f89db6f -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260925T011613Z-a2 author=claude-code kind=progress -->
### Claude Code · 2026-09-25 01:16
Salvage (#3BM5): expected.md and stage.sh for the zellij Try it (docs/qa_evidence/2026-09-24-zellij-VD2M/), left untracked, landed in 87bf2f6d beside the evidence already committed in 68a9a730.
