# #GH5T — the helper agent on a guest Main

Owner, 2026-09-20, with a screenshot of a Switchboard card page:

> The Switchboard agent could not answer: Base URL must be an HTTPS URL without credentials,
> query, or fragment. Your message is kept in the thread.

His Main is **Claude Code**, a Tier A guest harness (protocol 29.3). The tab's helper worker is
configured by the window with the window's `provider/preset`, so it became a guest worker: it
started a `claude` process it could never use, and `BoardCommands._build_page_agent` /
`_build_card_agent` then built the agent a page or card turn runs on as `Agent(main.config, …)`
with no provider — handing `harness://claude` to `ChatProvider`, whose first line is
`config.validate()`. That is the sentence in the screenshot.

The helper cannot run on a guest at all: its whole job is Relay's own `board_*` and `app_*` tools,
which a guest does not take (`#4NXH`). So it no longer follows Main onto one.

## Re-running this

```
./drive.sh          # writes logs/ beside this file; ~40 s
```

No network, no keyring, no real provider, no real guest:

- `run-worker.py` is the real `backend/worker.py` protocol loop with **one** seam replaced —
  `guest_harness_provider.make_harness`, which 29.3 names as the seam for exactly this — pointed at
  `tests/guest_harness_fake.FakeHarness`. A `configure` that *would* start a guest records a start
  on the fake, and the last stderr line says how many. "guest harness starts: 0" is the claim.
- `stub-provider.py` is a loopback-only OpenAI-compatible endpoint, registered as the local model
  server `local:stub` and ranked in the priority list. It is the only endpoint that answers.
- Every key is an environment variable with "stub" in it, and `RELAY_KEYRING=off`.
- Scenes 1 and 3 stop at `configured` on purpose: their stub keys belong to real hosts, and the
  point of those scenes is the event, not a turn. Scene 5 is the one that answers.

## What each transcript shows

| file | scene | the claim |
|---|---|---|
| `logs/00-before.txt` | the failure as reported, at `318beedb` | one guest started, and `board_chat` answers the base-URL error |
| `logs/01-priority-list.*` | helper, Main `guest:claude`, `kimi` ranked and keyed | **0 guests started**; `configured.model` is `kimi-k3`, no `guest` field; `roles.switchboard.note` and `tiers.main.note` say why |
| `logs/02-nothing-usable.*` | the same, with no key anywhere | 0 guests started, the worker is still configured, and the ask answers *"The helper agent cannot run on Claude Code. Add a provider under Options › Models, or pick a model for the helper in its model box."* |
| `logs/03-role-pick.*` | `roles.switchboard = {preset: glm-coding}` under a guest window preset | the pick is honoured: `agent_role` stays `switchboard`, `source: "configured"`, 0 guests started |
| `logs/04-pane-unchanged.*` | a **pane** `configure` on `guest:claude` | 1 guest started, `configured.guest = "claude"` — 29.3 untouched |
| `logs/05-live.*` | helper on a guest Main, `local:stub` ranked second | falls back, and answers a real question about the fixture board through the tab helper's own `board_chat` |
| `logs/summary.txt` | the whole run's console output | |

## What the model box says

`relay::helpermodel::fill` (`src/HelperModelBox.cpp`) already draws "Follow Main — <model>" from
`tiers.main.model` and appends `roles.switchboard.note` to the box's tooltip, so no C++ changed.
With the owner's setup it now reads

    Follow Main — kimi-k3

with the tooltip ending

    Main is Claude Code, a guest session the helper agent cannot run on, so it fell back to kimi-k3.

## Unit tests

```
XDG_DATA_HOME=$(mktemp -d) RELAY_KEYRING=off PYTHONPATH=backend \
  python3 -m unittest tests.test_roles.LeaveGuestTests \
                     tests.test_guest_harness_provider.WorkerProtocolTests \
                     tests.test_board_chat
```
