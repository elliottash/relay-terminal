<!-- relay:entry 20260919T212800Z-mq author=agent kind=event model=glm-5.3 pane=ebb8fbd1 turn=30873d1d932a4e21a3cfff37d305af37/8f55432379584ebba5693a300657b546 -->
- ✦ agent created this card in In progress · issues/changes/2026-09-19-ctrl-e-then-ctrl-arrow-should-place-the-pane-ctr.md

<!-- relay:entry 20260919T224615Z-0y author=agent kind=event model=glm-5.3 pane=e31585f3 turn=2c313d6dc34648a4ba5ebb9191e1d6ef/fc435bffed644f61a8d70a1cc8cb5fb9 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T224640Z-n0 author=agent kind=event model=glm-5.3 pane=e31585f3 turn=2c313d6dc34648a4ba5ebb9191e1d6ef/fc435bffed644f61a8d70a1cc8cb5fb9 -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Code on main in 087659d7 (PaneLayout keyPress + tests + eventFilter wiring) and presets doc in 7558c9b2; card and QA checklist landed in 039570db; evidence shared with #8G7E. · evidence docs/qa_evidence/2026-09-19-pane-move-past-page-edge/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260919T231135Z-12 author=agent kind=evidence model=glm-5.3 pane=65eddc43 turn=30873d1d932a4e21a3cfff37d305af37/65310edf4999409dbe2809945c309bef -->
Live Xvfb verification (second session, the one that wrote the keyPress half): docs/qa_evidence/2026-09-19-ctrl-held-placement/ (7b952ed6). drive.sh runs the built app under Xvfb+xdotool and asserts the layout through the new pane's own shell ($COLUMNS $LINES into a file): 8/8 PASS — Ctrl+Down, Ctrl+Shift+Down and bare Down all place below; Ctrl+Right stays; Alt+Down dismissed; Ctrl+Down after the window expires is passed on; with pane.focusDown bound to Ctrl+Shift+Down (konsole's chord) the bound shortcut wins and free Ctrl+Down still places. relay-panes-tests 42/42 incl. placementAcceptsArrowsWithCtrlHeld. Unrelated flakes seen in the shared tree: ctest 'paneusage' (another session's uncommitted PaneUsage edits) and test_web_viewport asyncio timeout under load; both pass standalone.
