# #A11T implementer evidence

The sharing dialog now has **Share all tabs** below **Share the whole tab**. The selection is
process-wide: each Relay window publishes its existing panes, and the existing once-per-second
sharing reconciliation catches panes and tabs created later. An all-tabs invite uses the explicit
`all-tabs` dynamic-scope token; it is not inferred from a normal tab id or a pane-only invitation.

The sidecar persists that token on the invite and participant, takes the initial pane list from its
trusted desktop source, grows it as any pane is published, and shrinks it when a pane disappears.
`scope_end` ends just that dynamic scope, leaving panes needed by narrower shares published.

## Automated checks

- `PYTHONPATH=. python3 tests/test_remote_tab_share.py`
- `scripts/relay-build`
- `python3 scripts/relay-board.py check`

The first suite covers every current pane across two tabs, a pane in a later third tab, screen
access to that pane, ending the all-tabs scope without withdrawing the pane, persistence across a
restart, and the existing pane-only / whole-tab isolation cases.

## Human verification still needed

Open the sharing dialog, select **Share all tabs**, invite a browser, then open a new tab and a new
pane. The browser should gain both without reloading. Turn **Share all tabs** off while another tab
has an ordinary whole-tab share; the broad guest should leave and the tab-scoped guest should stay.
