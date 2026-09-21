# Recently closed modal — #C8KM

Built with `scripts/relay-build --target relay relay-closedlist-tests`.
`ctest --test-dir build -R '^closedlist$' --output-on-failure` passed.

Isolated XDG configuration under Xvfb, using a temporary workspace:
1. Created a tab, named it `qa-closed`, and closed it.
2. Searched Actions for Recently closed. Only the launcher appears (actions.png).
3. Activated the launcher. The modal shows the closed tab, filter, preview arrow and list controls (picker.png).
4. Pressed Enter. The modal closed and the named tab reopened with restored scrollback (restored.png).

The Sessions tab uses the same extracted list factory. Its existing filter, preview and discard behavior remains covered by the closedlist tests.
