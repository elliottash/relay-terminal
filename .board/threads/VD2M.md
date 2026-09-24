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
