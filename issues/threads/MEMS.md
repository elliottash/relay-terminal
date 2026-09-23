<!-- relay:entry 20260922T235900Z-mq author=agent kind=question model=claude-opus-5-5 -->
Replacing guest memory with Relay memory works per launch: Claude's `autoMemoryEnabled` and Codex's `memories.*` keys can both be passed with the flags Relay already uses. Should this be one setting, "Guests use: their own memory / Relay memory / both", and what should it default to? My recommendation: default to "both" (import their memories and inject Relay's, leaving their own memory on) until the user has done one import review, then offer "Relay memory".

<!-- relay:entry 20260923T025517Z-sb author=agent kind=decision model=claude-opus-5-5 pane=39c73713 turn=b3fbd4e048c84efc8ddba61d26db94fe/d37870e54f094f52b4bcd3762df62a40 -->
Owner, 2026-09-22: "yes to 1-4. guests use relay memory, and thats the default." Steps 1–4 approved; Relay-launched claude/codex sessions default to Relay memory (their own memory off, Relay user memory injected).
