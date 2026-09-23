# Sessions and Projects shortcut/layout revision

Captured from `SessionManager` under isolated Xvfb and `XDG_CONFIG_HOME` with:

```bash
RELAY_SHOT_DIR=docs/qa_evidence/2026-09-23-sessions-projects-keys xvfb-run -a ./build/relay-conversations-tests sessionsActivationRefreshesAndClosedIsNested
```

- `sessions-first.png`: Sessions is the first tab; Recently closed and Background are buttons beside the search field.
- `sessions-background.png`: the Background button opens a nested Sessions page with a Back to sessions button.

The test uses a placeholder background item to exercise the layout. Runtime background-session listing is supplied by `RelayWindow::registerClosedTab` and is covered by the existing background-session behavior.
