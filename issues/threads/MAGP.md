<!-- relay:entry 20260923T012600Z-m1 author=claude-code kind=decision -->
### Claude Code · 2026-09-23 01:26 UTC
Owner: "i think we need something like ctrl+shift+p for warp. what about ctrl+shift+1 or ctrl+shift+r? i think it should open a magic action modal with text search and some buttons that show dropdowns that give you quick intuitive access to everything."
<!-- relay:entry 20260923T012600Z-m2 author=claude-code kind=question -->
### Claude Code · 2026-09-23 01:26 UTC
1. Key: Ctrl+Shift+P, freed when Projects joins Sessions (#SPSG)? Recommendation: yes; it is the palette chord people already know and a comfortable two-handed press. Ctrl+Shift+R is the fallback once restart loses its key.
2. The Actions pane: keep its list under Options › Keyboard as the shortcut reference and editor, with the palette as the only entry point? Recommendation: keep it there; retiring it loses the printable overview.
3. A modal is an exception to "panes, not overlays". Accept it for this transient surface? Recommendation: yes; if not, the same design becomes a splitter pane.
4. First version searches actions, Options sections and slash commands only; cards and sessions stay on their own keys. Recommendation: yes; add them later if the palette is where people look for them.
<!-- relay:entry 20260923T012600Z-m3 author=claude-code kind=note -->
### Claude Code · 2026-09-23 01:26 UTC
First proposal in Planning notes and a plan, read against `src/RelayWindow.h` and `src/SettingsPane.cpp` at 275ac2ea. Builds on #ACDG (registry-driven catalog), #A9QR (groups), #A7SC (slash labels), #S3JH (section search); #XAME's reopen finding applies. No code changed.
