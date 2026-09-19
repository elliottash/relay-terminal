# Two-way sync with GitHub issues: the acceptance walked, and the lost write it found

Card [`#GDQN`](../../../issues/features/2026-09-17-github-issues-sync.md). Acceptance: *a shared
work card and its GitHub issue stay in sync both ways (body, comments, status, labels) across
edits on either side, with conflicts surfaced rather than lost.*

## Where the card actually stood

The 2026-09-19 board sweep said the engine was not wired to the worker. It had been by then: the
engine landed as `f878b6a` (→ `ae206932` after the AGPL relicense rewrote hashes), the back-end
review fix as `a17a262` (→ `ac7abe64`), and the worker messages (`forge_sync_plan` /
`forge_sync_run`, protocol 19.14, `board_protocol._forge_sync` + `remote/wire.py`) in the 09-18/19
batch (`9e628ce3`). All of that is on `main` and green. What the card still lacked was an
acceptance-level run and the truth of its conflict promise — and the run found a hole.

## The lost write (found by the acceptance walk, fixed here)

Walk the acceptance through the worker messages against the in-process fake GitHub
(`tests/fake_github.py`): create a card, sync, edit the prose on both sides, sync — the conflict
is surfaced correctly on the card's thread and in `forge_sync_done`. Then sync again with
**nothing changed anywhere**. The second run answered `pushed: 1`, `conflicts: []` and pushed the
local prose over the remote edit — the exact write the merge exists to prevent:

- The conflicted card's baseline is deliberately left behind (§4 of `docs/GITHUB-SYNC.md`).
- The listing then answers `304` (nothing changed since the last listing), so the card's issue is
  absent from the changed-issues index.
- `_plan_card` read "issue absent" as "issue equals the baseline" — but for a card whose baseline
  was left behind, an *unchanged listing* is not an *unchanged issue*: the issue still says what
  the conflict was about. The merge saw card=changed, issue=unchanged → push → the remote edit
  was gone.

Reproduction (before the fix): run
`tests.test_board_protocol.ForgeSyncAcceptanceTests.test_a_conflict_is_surfaced_rather_than_lost`;
it failed at its second sync with `conflicts: []` where the first had reported the prose conflict,
and the issue body had been overwritten with the card's words.

## What changed

`backend/relay_core/forge_sync.py`:

- A per-card `current` marker in `.private/forge-sync.json`: `true` only when the stored baseline
  records everything the run saw of the issue. Written `true` on create / import / a clean merge;
  `false` when a conflict keeps fields behind, and on every error path (a card whose plan or apply
  errored — `_mark_lagging` — including a rate limit that stopped mid-card).
- In `_sync`'s planning loop, a linked card that is absent from the changed-issues index **and**
  not `current` (or without a baseline at all) gets one real `get_issue` — no `If-None-Match`,
  because a 304 would only prove the issue unchanged since a view the baseline does not hold. The
  merge then sees the issue's true fields, the conflict is re-reported (the thread note is not
  duplicated: the fingerprint is unchanged), and nothing is pushed over. A state file from before
  the marker existed simply has no `current` keys: every linked card is read once, marked, and the
  runs after that are conditional again.
- Idle boards stay free: a `current` card absent from an unchanged listing still costs nothing
  beyond the conditional listing.

`tests/test_forge_sync.py`: `MergeTests.test_a_conflict_survives_a_quiet_sync_instead_of_pushing_over_it`
(engine-level regression test; fails on the old code at its second `sync()`).

`tests/test_board_protocol.py`: `ForgeSyncAcceptanceTests` — the acceptance walked end to end
through `BoardCommands.dispatch`, i.e. through the worker messages a Switchboard pane will send:
both directions at once (body, labels, status, a thread note out, a web comment in, the link in
front matter, `board_changed` after the pull, a quiet run that touches nothing), and the conflict
lifecycle (surfaced with both versions on the thread, re-reported without thread spam, resolved by
one side's edit). Everything on the Relay side is a dispatched message; the GitHub side is
`web_edit` / `web_comment`, which is a human in a browser.

`docs/GITHUB-SYNC.md`: §3 documents `current` and the re-read; §4 says a quiet board does not
resolve a conflict by accident.

## Re-running

```bash
XDG_DATA_HOME=$(mktemp -d) RELAY_KEYRING=off \
  PYTHONPATH="$PWD/backend:$PWD/tests" python3 -m unittest \
  tests.test_forge_sync tests.test_forge_github tests.test_board_protocol
```

Logs: `logs/test_forge.txt` (147 tests), `logs/test_board_protocol.txt` (145 tests). The fake
listens on 127.0.0.1 only; no request leaves the machine and no model is called.

## Not here

The Switchboard's own surface — the Sync action, the plan-before-first-run flow, the visible
conflict marker with keep-mine / keep-GitHub's — is card `#ZKR0` (`component: gui`). Pointing this
repository's own board at `github.com/elliottash/relay-terminal` is an owner decision; nothing
here syncs this board for real.
