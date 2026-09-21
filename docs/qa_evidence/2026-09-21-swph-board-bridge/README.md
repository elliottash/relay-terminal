# #SWPH task 1 — the desktop bridge, live

`run-live.sh` starts `build/relay` under Xvfb with an isolated HOME, remote control on, and
`stub_gui_host.py` standing where the sidecar stands (`RELAY_REMOTE_DIR`). The stub plays the hub:
once the window has published its first pane it sends nine `board_request` lines as "Elliott's
iPhone", one after the other, and logs everything the GUI writes back. The desktop has **no
Switchboard pane open** and its tab is **not attached** to the project when the first request
arrives.

- `board-events.jsonl` — every `hub->gui` request and every `gui->hub` `board_event`, in order.
- `sidecar.jsonl` — everything else the GUI sent the stub (frames left out, lines cut at 400).
- `after.png` — the window afterwards: the Execute pane beside the first one, wearing `#K7Q2`,
  and the status line "Execute on #K7Q2 from Elliott's iPhone".

What the run shows (2026-09-21, tip 38659350 + this commit):

| rid | request | answer |
|---|---|---|
| 1 | `board_open` | `board` with the card rows, `rid: 1`; the tab was attached and the worker started on demand |
| 2 | `board_card_get {id: K7Q2}` | `board_card`, `rid: 2`, no `path` |
| 3 | `board_comment {kind: decision}` | written as `owner, from Elliott's iPhone: “…”`; `board_written`, `rid: 3` |
| 4 | `board_move {status: ready, reason}` | `board_written` `rid: 4`, `board_activity` and `board_changed` as broadcasts (`rid: null`) |
| 5 | `board_create {tab, title, request, labels}` | `board_written` `rid: 5` with the new card's id |
| 6 | `board_search {query}` | `board_search {ids}`, `rid: 6` |
| 7 | `board_delete` | `error {code: board_refused}`, `rid: 7`; never reached the worker |
| 8 | `board_card_get` with a `path` | `error {code: board_refused}`, `rid: 8`; never reached the worker |
| 9 | `board_action {action: execute}` | `board_action_result {ok: true, pane: <session token>}`, `rid: 9`; then the `board_claim` write's broadcasts |

No `board_event` line contains a path of this machine (the script checks for the run's root), and
the card file and its thread at the end read as the desktop's own Execute would have left them.

"The agent worker exited" in both panes of the screenshot is the isolated environment, not this
change: a pane's worker runs in a `systemd --user` scope, which the throwaway `XDG_RUNTIME_DIR`
cannot give it (`relay.log`: "Failed to reset failed state of unit relay-pane-…"). The Switchboard
worker has no isolation unit and runs normally. The provider is pointed at a closed port so that
Execute's pane could not have reached a real model either way.
