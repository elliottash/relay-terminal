# Verify K7MP — import keys from other apps (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean worktree for python/C++ sources). Commit 4de23c46 is an ancestor.

## Code at HEAD
- `src/RelayWindowModels.cpp:490-510`: one "import keys" row below the provider groups ("Copy API keys from Warp, OpenCode, Claude Code or Codex into Relay's keyring. Subscription sign-ins are not API keys."); its action opens a QInputDialog "Import API keys from" with sources Warp / OpenCode / Claude Code / Codex API keys → `import_warp` / `import_opencode` / `import_agent_tools` worker events.
- `backend/relay_core/keystore.py:306` `import_from_opencode(auth.json)`: imports `type: api` records for supported provider IDs only.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_keystore` — **OK** (OpenCode importer constraints: OAuth/unknown skipped, existing keys preserved, names/counts only). Without PYTHONPATH=backend the module can't import — environmental, not a defect.
- `relay-settings-tests` — run this sweep in the same clean worktree: 51/1, the 1 being the #E8V1 stale string (#SYTR).

## Live drive (Xvfb :97, isolated profile)
- `01-sources-bottom.png`: Sources scrolled — "import keys" row below the provider groups (after the "API keys and endpoints" section).
- `05-import-dialog.png`: clicking "import keys…" opens the "Import API keys from" dialog with the source combobox (Warp default; OpenCode and Claude Code / Codex API keys in the list per code).
- Note: the drive also shows the profiles section's own "import…" button nearby — a different action; one mis-click opened the profiles picker instead, dismissed before the real capture.

## Verdict
PASS.
