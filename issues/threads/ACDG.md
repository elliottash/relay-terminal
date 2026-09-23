<!-- relay:entry 20260923T003653Z-ac author=codex kind=note -->
### Codex · 2026-09-23 00:36 UTC
Filed at the owner's explicit request to card the keyboard survey's identified gaps. Rechecked the catalog and canonical slash mapping at c2a4b370. This records remaining work; no application code changed and no implementation is claimed.
<!-- relay:entry 20260923T012533Z-k2 author=claude-code kind=note -->
### Claude Code · 2026-09-23 01:25 UTC
#MAGP (Actions as a modal palette: search box, group buttons with dropdowns, Ctrl+Shift+P) builds on this card: its catalog must come from Keymap::instance(), settingsSections() and Pane::slashCommands(), not rootItems()'s hand list, so the coverage fix here is step 1 of that plan. Sequence: ACDG first, then MAGP.
