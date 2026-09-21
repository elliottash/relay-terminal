# Card #CTRN, steps 1–3: a card turn is an ordinary supervised turn (backend)

The backend half of *"a card turn is an ordinary console turn"*, driven end to end through
`backend/worker.py` over NDJSON. No network, no keyring, no provider: the only endpoint that
answers is `stub-provider.py` on 127.0.0.1, configured into the worker as a plain-HTTP loopback
URL (`provider_config` treats that as a local model server, so no key is looked up), with
`RELAY_KEYRING=off` and an isolated `HOME` / `XDG_*` / `TMPDIR` under a short path.

```
python3 drive.py                                           # the run below
python3 drive.py --tree <clean export> --only discuss --label baseline
```

Commits: `8f5322a9` (step 1), `1b9571ce` (step 2), `cb866888` (step 3), `ab0834e5` (the queue
row's preview, which this drive is what found).

## What the run shows — `logs/run-notes.txt`, `logs/run.ndjson`

1. **`configure` with a workspace and a tab id.** The tab is what keys each card's conversation.
2. **A second prompt on a busy card queues.** `board_ask` on `#CRD1` while the first turn runs
   answers with `queue_changed {surface: "card:CRD1", items: [{mode: "discuss", card_id: "CRD1",
   surface: "card:CRD1", preview: "and what about the queue? #CRD1"}]}` and **no `board_busy`**
   — 0 of them in the whole run. Before this card it was refused: *"The Switchboard agent is busy
   with a question on #CRD1."*
3. **Two cards at once.** A Plan on `#CRD2` starts while the Discuss on `#CRD1` is still running:
   `running at once: CRD1, CRD2` (#DR4K, #0Z13 — nothing counts them).
4. **A Plan that calls `write_file` is refused at call time, in a sentence that names Execute.**
   The tool list is the console's (23 tools, `write_file` among them, byte-identical to a
   Discuss's and to an ordinary console turn's); the refusal is `CardScope.refusal`, unchanged:
   > write_file is not available in a Plan turn on #CRD2: it reads the repository (read_file,
   > list_directory, search_files) and writes only through the board tools. Writing code is
   > Execute's job — the owner hands the card to a terminal pane for that.

   The turn carries on and says what it was told, which is what lands on the card's thread.
5. **Stop is per card.** `cancel {surface: "card:CRD3"}` ends `#CRD3` `cancelled` while `#CRD4`
   goes on running and ends `done` on its own. `#CRD3` wrote nothing to its thread, which is what
   a stopped turn has always left.
6. **One conversation per (tab, card), persisted.** Four files under the helper store, named from
   `tab-smoke/card:<ID>` — not in `relay/sessions/`, which is the person's own conversations.

## The thread format did not move — `logs/thread-format.diff`

The same driver ran against a clean export of `17f082fe8310`, the tip before step 1, and the two
`CRD1.md` files were normalised (entry ids, `model=`, `turn=`, which differ every run) and
diffed. The diff is **additions only** — the second question and its answer, because the run
asked twice and the baseline could not. Not one attribute, attribute order or entry body changed,
and there is still no heading line:

```
<!-- relay:entry <id> author=owner kind=comment mode=discuss -->
why this card? #CRD1

<!-- relay:entry <id> author=owner kind=event -->
- ✦ owner moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry <id> author=agent kind=comment mode=discuss model=<model> turn=<session>/<turn> -->
This card is about card turns being ordinary console turns.
```

`relay-board.py check` does not validate `author`, `mode`, `model` or `turn` (`board.py:1285`),
which is why this is a byte check against a real run of the old code rather than a `check`.

## What the drive found

* **The queue row showed the model's prompt.** `board_ask` submits the prompt it builds out of
  the card's seed block and the mode's brief, so the §12 strip on a card read "[Switchboard card
  #CRD1 — 2026-09-21-crd1.md] You are Relay's Switchboard agent…". Fixed in `ab0834e5`:
  `submit {preview}` is what the row and the request ledger show, the model still gets the whole
  prompt.
* **A stub that sleeps and then answers is a turn no Stop can catch.** The first run reported
  `cancel surface=card:CRD3 -> #CRD3 done`: cancellation reaches the provider between chunks, and
  the stub was answering in one piece. It streams SSE with heartbeats now, which is what a real
  provider does; the cancel lands mid-turn.

## Files

| file | what |
|---|---|
| `drive.py` | the driver: fixture board, worker on a pipe, the six checks above |
| `stub-provider.py` | the loopback endpoint and its four scenes |
| `logs/run.ndjson` | every event of the run, as the GUI would receive it |
| `logs/run-notes.txt` | the run's own summary (the lines quoted above) |
| `logs/baseline.ndjson` | the same Discuss against the tip before step 1 |
| `logs/run-thread-CRD1.md`, `logs/baseline-thread-CRD1.md` | the thread files, raw |
| `logs/thread-format.diff` | the normalised diff of the two |

## Tests

`RELAY_KEYRING=off PYTHONPATH=backend:tests python3 -m unittest tests.test_queue
tests.test_board_turns tests.test_board_protocol tests.test_board_tools tests.test_agent_context
tests.test_agent tests.test_board_chat tests.test_app_tools tests.test_roles` — **717 tests, OK**,
run both in the checkout and on a clean `git archive` export of the landed tree. The same set was
green before step 1 (716 then; the #8EJ4 failure set is empty on this tree).
