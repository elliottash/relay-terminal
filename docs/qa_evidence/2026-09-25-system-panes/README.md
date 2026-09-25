# System panes: a docked agent on Tests and Sharing — live pass (#3B1B)

2026-09-25, Xvfb display `:109`, isolated profile (fresh `$HOME`, off keyring), the build's
own tree as the workspace so the Tests pane lists this repo's suites from `build/`. The
provider is the stub (`stub-provider.py`, an OpenAI-compatible endpoint on 127.0.0.1:8869)
that answers by echoing the pane's `On screen now:` line back — so an answer in a shot is
proof the screen reached the model, and the answer cites the visible row by construction.

## What the shots show

| shot | what it is |
|---|---|
| `01-tests-pane.png` | The Actions palette opened the Tests pane (`tests.open`); the tab was attached to the workspace first (the Switchboard's Ctrl+Shift+A attach), because `sendToHelper` refuses an unattached tab and never starts its helper. |
| `01b-tests-row-selected.png` | ↓ selects the first row: the summary line and the detail column name it (`unittest:tests.test_board_protocol`, last result pass 2 d ago, 50% reliable, gone, last failure scope execution). |
| `02-tests-agent-row.png` | Alt+Q on the Tests pane: the collapsed "Agent (Alt+Q)" row expanded — the first expand is what builds the console — with the Tests context's screen and the actions Run selected (r), Run failed (f), Attach to card (a). |
| `03-tests-answer-cites-the-row.png` | The answer to "why did the last run fail?" cites the screen line: *"On screen now: Selected: test:collection:unittest:tests.test_board_protocol (unittest:tests.test_board_protocol) · last result pass · 2 d ago · 50% reliable · gone … Last failure (2 d ago at 126afd1406e3): scope execution … 5351 tests shown Summary: 5351 tests · 2052 passed (37 slow) · 2 failed · 3297 never run · 2.6 m Failing on screen: test:ctest:consolemode, test:unittest:unittest.loader._FailedTest.test_board_protocol"*. `stub-requests.log` line 1 is that request as the provider saw it. |
| `04-sharing-pane.png` | The palette's "sharing" filter opened the Sharing pane (`pane.sharing`). |
| `05-sharing-agent-row.png` | Alt+Q on the Sharing pane: its own docked agent, paired devices, guests, shared panes. |
| `06-sharing-answer.png` | The answer to "what is being shared?" cites *"On screen now: Sharing › People Remote control off Opened from: pane:3d1be610-b6c2-48a9-98c7-b3c2b84aecf4 (3d1be610-b6c2-48a9-98c7-b3c2b84aecf4), not shared Paired devices: none Shared panes: none"*. |

`stub-requests.log` is the provider-side log of every ask (scene, the `On screen now:` line
the last message carried). `relay.out` is the app's stderr from the run. The two consoles
share the tab's helper conversation (`helper` / the tab id), which is why the later log
lines carry both panes' screens: each console's ask appended to the same conversation.

## Suite state under this build

The summary and per-row history (`2 d ago`, `50% reliable`, `126afd1406e3`) come from the
board's run ledger for this project, not from anything the pass ran. One pytest-discovered
row shows as an import failure (`unittest:unittest.loader._FailedTest.test_board_protocol`)
— the working tree's current state on other cards' uncommitted files, not this card's.

## Replay

    bash docs/qa_evidence/2026-09-25-system-panes/drive.sh

It builds nothing (run `scripts/relay-build` first), starts its own Xvfb on `:109` and its
own stub, and kills all three when done. `RELAY_QA_DISPLAY` / `RELAY_QA_PORT` move the
display and port if they are taken.
