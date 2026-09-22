<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# The phone app, driven again after the eight cards were fixed — 2026-09-22

The same two drives as `docs/qa_evidence/2026-09-22-phone-ux-drive/`, re-run unchanged against the
tree the workstream left. Same benches, same viewport (390×844, touch, real headless Chrome), same
scripts — only the code under them moved. This folder is the *after* half of that folder's
pictures.

## What the drive measures now, against what it measured then

| | before | after |
|---|---|---|
| a row with two URLs | `see https://a.example.comsee https://a.example.com  now` | the row, exactly as sent |
| `WWW.EXAMPLE.COM` | a relative href into the client's own origin | `https://www.example.com/` |
| `curl "…/v1"`, `<…/a>` | `%22`, `%3E` in the href | neither |
| `/var/www.old/index.html` | a live link to a host that does not exist | text |
| the level chip, closed | `✓▾high` | `high ▾`, its own chevron, the model keeping its own |
| a `pane_state` that moved only the level | ignored | repaints the chip |
| a fixed level (Relay Free) | a live picker for a level the pane refuses | visible and `disabled` |
| Send on a touch mount | focus stays in the box, keyboard up | focus to the body, keyboard down |
| the Copy id toast | topmost at its centre was a sheet row | the toast, `z-index: 20` |
| the conversations list on a clock tick | `scrollTop 2010 → 0` | `1975 → 1975`, same nodes |
| `#term-note` at 390 px | `scrollWidth 178` in `clientWidth 91` | `366` in `366` |
| an `agent` device | "This device is paired for viewing only." | "You can ask the agent here. …" |
| `\tmake all` in a card body | `ake all` | `make all` |
| `__init__` in a card body | **init** | `__init__` |
| Back from a thread | closed the app | closes the sheet, then the pane, then leaves |

## The suites, re-run here rather than only by the sessions that wrote them

```
tests.test_web_screen         10 / 10
tests.test_pane_view          36 / 36
tests.test_board_view         32 / 32
tests.test_remote_browser     24 / 24
tests.test_remote_pane_state  45 / 45
scripts/relay-build           built in 32 s
```

## One line in the log that is not a regression

`app_drive.py` still prints `focused after send: TEXTAREA`. That script looks for a send button by
`#prompt-send` or the first button matching `/send/i`, and finds neither the client's
`#composer-send` nor the pane view's `.rp-send` — it was written before either fix and its finder
was always wrong. The blur is measured properly in `/tmp/send_probe.py`'s shape and in
`tests/test_pane_view.py`: focus goes to the body on a touch mount and stays in the box on a mouse
one. The script is left as it was so the before and after are the same script.
