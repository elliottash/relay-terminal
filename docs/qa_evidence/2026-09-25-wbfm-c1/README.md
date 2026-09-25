# #WBFM (track C1) — Models pane Sources page: two per-provider rows removed

`models-sources-no-extra-rows.png`: the models pane's **Sources** tab (verify-slot build of
tip + this card's hunks, run under Xvfb with isolated XDG config/data and the fixture
`worker.py`, which answers `presets` with two guest CLIs and one keyed provider).

Every provider is a single row now:

- the **"x of n enabled…"** link into the Enabled tab is gone (it duplicated the Enabled tab);
- the guest **"when it wants to use a tool"** choice row under `guest:claude` is gone — a guest
  always just runs it, like Relay's own agent (owner rule 29.1), and `Pane::takeGuestRequest`
  no longer stages a `permissions` key (absent is the worker's bypass);
- `guests/<cli>/permissions` values stored while the row existed are removed where the row used
  to read them.

OCR check on the screenshot: `Sources Enabled Pick order` tabs, `claude code`, `codex`,
`import keys`, `add account…`, `change login` — and no `enabled…` link and no
`when it wants to use a tool` row.

`drive.py` is the driver (`RELAY_BIN=<binary> xvfb-run -a python3 drive.py out.png`); the tab
captions are drawn, not widgets, so the Sources tab is clicked by position.
