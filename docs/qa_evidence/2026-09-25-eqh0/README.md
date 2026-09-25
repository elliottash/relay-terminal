# #EQH0 — Models › Sources: refresh usage, and each login's email

- `tests.txt` — `tests/test_usage_refresh.py`: `login_email` for Codex (`auth.json` id_token) and
  Claude Code (`.claude.json` oauthAccount), default logins via CLAUDE_CONFIG_DIR / CODEX_HOME,
  re-read after a sign-in rewrites the file, malformed values empty; and the real `worker.py`
  answering `usage_refresh` with `usage_refreshed` (empty home: no token is sent anywhere).
- Live drive (`drive.py` + fixture `worker.py`, under Xvfb, isolated XDG dirs, the verify-slot
  build of this change): `1-sources-before.png` shows "usage · refresh" above the groups and
  "logged in as elliott.ash@gess.ethz.ch / e@elliottash.com / ashe@ethz.ch"; clicking refresh
  sent `usage_refresh` (fixture log) and `2-sources-after-refresh.png` shows Codex's weekly figure
  redrawn from 20% left to 88% left. `result.json` is the drive's summary.
- Real machine, read-only: `guest_harness_provider.preset_rows()` gives guest:claude
  elliott.ash@gess.ethz.ch, guest:codex e@elliottash.com, guest:codex:ashe-ethz-ch ashe@ethz.ch.
- ctest `settings` / `modelspane` in the verify slot: all pass except the two "Helper Agent (Alt+Q)"
  vs "Agent (Alt+Q)" label checks, which #WBFM reproduced on pristine main.
