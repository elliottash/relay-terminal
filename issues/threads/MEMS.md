<!-- relay:entry 20260922T235900Z-mq author=agent kind=question model=claude-opus-5-5 -->
Replacing guest memory with Relay memory works per launch: Claude's `autoMemoryEnabled` and Codex's `memories.*` keys can both be passed with the flags Relay already uses. Should this be one setting, "Guests use: their own memory / Relay memory / both", and what should it default to? My recommendation: default to "both" (import their memories and inject Relay's, leaving their own memory on) until the user has done one import review, then offer "Relay memory".

