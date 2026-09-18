# Cleanup quick wins — implementer evidence (2026-09-17)

Implemented by the Relay agent (model self-unidentified; commits carry
`Co-Authored-By: Relay <agent@relayterminal.ai>`) in five commits:

- `5a6c669` Remove uncalled getters and helper methods
  (`RequestLedger::canReask`, `SubagentsPanel::mainContextTokens`,
  `RemoteShare::paneCount`, `FilePane::showHidden`,
  `FilePreview::hasSyntaxHighlighting`/`hasPdfSupport`,
  `Conversations Dialog::setError`). Each verified to have zero call sites
  across `src/`, `tests/` and the build files before removal.
- `89ad2e0` Log three previously silent failure paths (window-icon load in
  `main()`, session-index update in `backend/relay_core/sessions.py`,
  revoke callbacks in `remote/identity.py`). Behaviour unchanged.
- `a5d3b08` Drop the deprecated `build-spike` bridge fallback in
  `remote/terminal.py`.
- `19ebe5a` docs/README.md index: ENGINE, ENGINE-PERF, AGENT-SESSIONS-PROTOCOL
  added; ENGINE-SPIKE row (a redirect stub) replaced.
- `dde3e6a` Add `deploy.sh` (reviewed: no secrets; rsync mirror of `site/`
  with dry-run and verification).

Also deleted (untracked/gitignored, verified unreferenced): `build-editor/`,
`build-theme/`, `build-files/`, `build-spike/` (~37 MB), `/.swp` (no vim
process running), `tmp/stress_interrupt.py`.

## Validation

- `cmake --build build -j$(nproc)` — clean build (100%).
- `ctest --test-dir build` — 24/24 passed (56.7 s).
- `./scripts/test.sh` — 848/848 Python tests OK (53.2 s).
- `bash -n deploy.sh` — syntax OK.
- `git status` after committing: only the owner's pre-existing in-flight work
  remains uncommitted (MarkdownAnsi, ink colours, remote-share refuse button);
  verified the remaining diffs in `src/RemoteShare.h` and `src/main.cpp`
  contain none of the cleanup hunks (they were staged selectively with
  `git apply --cached`).

## QA checklist

- [ ] Grep confirms each removed symbol has no remaining references.
- [ ] `relay` builds and launches; window icon still loads (or logs
      `window_icon_unavailable` if not).
- [ ] Session save/delete still works; a failed index update now logs instead
      of passing silently.
- [ ] `remote/terminal.py` still finds the screen bridge in `build-engine/` or
      `build/`.
- [ ] docs/README.md links all resolve.
- [ ] `./deploy.sh -n` dry run lists transfers without deploying.
