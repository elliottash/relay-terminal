# #9M96 — Options › Security › MCP servers, and the `mcp-servers` skill

Implementer evidence, 2026-09-25 (anthropic/claude-opus-5-5 via claude-code).

## Tests

- `tests/test_mcp.py`: 29 passed (`tests.txt`). New: `OptionsCliTests` covers the add → list --json →
  trust → disable/enable --global → remove round trip, pending project rows carrying the command
  they would launch, `--secrets-stdin` keeping env values out of argv, and the import `--json` preview
  and add. `SkillTests` loads the bundled skill and checks that every `mcp_config` subcommand it
  names exists.
- ctest `mcpsettings` (`tests/mcpsettings_test.cpp`) checks the rows drawn from a `list --json`
  document, which CLI call each button makes and which ones confirm first, and that the Add form
  puts values on stdin. It passed on a private export of tip plus exactly the landed hunks, whose
  `relay` target also built (`tests.txt`).

## Live, under Xvfb (isolated XDG_CONFIG_HOME/DATA/STATE; that exported build, `RELAY_DATA_DIR` at it)

Seeded: a global trusted stdio `github` with `env.GITHUB_TOKEN = "sekrit-token"`, a global
untrusted URL `docs` with an `Authorization` header, and a project `.mcp.json` with `repo-tools`.
Opened Options (Ctrl+Shift+O) and searched "MCP".

- `options-mcp.png` shows the block. github is trusted and shows `env: GITHUB_TOKEN`, with no
  value anywhere. docs is untrusted and asks first. repo-tools is "not enabled · this project's
  .mcp.json · node tools/mcp.js", with Enable / Enable as trusted. Add server… and Import… are
  below.
- `trust-confirm.png`: Trust on docs asks `Trust "docs"? Its tools will run without asking you first.`
- `after-trust.png` is after Yes. The global file has `docs.trust == "trusted"` (read back from the
  file), the row redraws as trusted with Untrust, and the status bar says "docs is now trusted
  (global).".

Not driven live: the Add and Import dialogs. The unit test covers their CLI calls, and so do the
CLI tests.
