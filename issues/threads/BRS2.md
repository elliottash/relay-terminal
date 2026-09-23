<!-- relay:entry 20260923T004154Z-b1 author=codex kind=progress -->
Claimed resume-path defect; no bridge tools are available in this session. Current app-server PID 2252518 launched with no MCP overrides. Normal peer app-servers carry relay_board overrides and have a proxy child. Code confirms replacement.start in resume_session omits board_bridge and instructions.

<!-- relay:entry 20260923T004446Z-b2 author=codex kind=evidence -->
Regression failed before for Claude and Codex, passed after. Real temporary-board discovery and board_list calls succeed over the retained bridge across two replacements; failed replacement cleanup also verified. 234 targeted tests passed in a clean source export. Moved to needs-verification; live session not restarted.

<!-- relay:entry 20260923T004513Z-b3 author=codex kind=evidence -->
Landed cbcf9e3f5071404866687415e189a6aa5703581f; linked implementation commit.
