<!-- relay:entry 20260923T130000Z-a1 author=codex kind=progress -->
### Codex · 2026-09-23 13:00
Claimed the narrow Models pane review. Inspecting all four tabs and their width-dependent controls before editing.

<!-- relay:entry 20260923T162100Z-a2 author=codex kind=decision -->
### Codex · 2026-09-23 12:21
Owner: "available" doesnt need anything about effort / reasoning. Available will show model availability and selection without a reasoning control.

<!-- relay:entry 20260923T162230Z-a3 author=codex kind=decision -->
### Codex · 2026-09-23 12:22
Owner: "same with priorities, lets make effort a separate tab". Adding a dedicated Effort tab for ranked models' reasoning levels.

<!-- relay:entry 20260923T164300Z-a4 author=codex kind=progress -->
### Codex · 2026-09-23 12:43
Narrow layout and Effort tab landed in `e326182dbae13d6f8bedbb168cf195b9cfd32c92`. Captures cover all five tabs and the Jobs chooser. The exact commit tree built. The final helper context update makes Effort report its focused class; the Models pane tests passed again (23/23). Ready for visual verification at a narrow split.

<!-- relay:entry 20260925T211245Z-nh author=agent kind=event mention=WBFM model=claude-opus-5-5 pane=1948e00e turn=6173581165764472b6c340786cfdcc26/82e12aaa9e394a2c85db01f3095e8848 -->
mentioned in #WBFM · 2026-09-25 · agent
<!-- relay:entry 20260925T221040Z-5z author=agent kind=note -->
Verification (rev 54018502): PASSED — all five tabs usable at ~400 px (screenshots), no effort/reasoning controls on Enabled/Order, Effort tab holds levels, resize restores wide layout and keeps selection, keyboard tab-walk works. Tests: modelpicker 62/0; modelspane 25/1 and jobstab 23/1 where both failures are stale expectations from #E8V1's rename, not this change. Evidence: docs/qa_evidence/2026-09-25-verify-N4PW/. Moved needs-verification → needs-qa-llm.

