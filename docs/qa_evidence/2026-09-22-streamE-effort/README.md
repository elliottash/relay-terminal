<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# #EFT9, stream E: the reasoning level on the wire — what was measured

The wire and desktop half of card #EFT9 (`issues/changes/2026-09-22-phone-reasoning-level-chip-stale-and-mis-drawn.md`),
the stream E row of `docs/qa_evidence/2026-09-22-phone-ux-drive/WORKSTREAM.md`. Stream A fixed the
client's render guard and the chip's own words (`44a46ca1`); this is the half that decides what the
phone is *told*, and what it is told when its tap is refused.

Commits: `2ee01f0c` (the hub's cleaner and the tests), `d329d88a` (the desktop's builder, the
refusal answer), `a0882679` (the protocol section and this folder).

## The seam, measured rather than described

Nothing below is a fixture of the fields under test. `effort_probe.cpp` links the **real**
`librelay-panestate.a`, so every `model` block starts in `relay::panestate::build()`, the function
the desktop publishes through; `seam.py` runs each block through **`remote/pane_state.py`**, the
hub's own cleaner, twice — once as it is now and once as `git show 2ee01f0c^:remote/pane_state.py`,
so what changed is a diff and not a claim; and `drive.py` puts the cleaned state into the **real
`app/pane.js`** in headless Chrome at 390×844 and reads the chip back off the DOM.

```
                 effort_probe (librelay-panestate.a)   →  seam.py (remote/pane_state.py)  →  drive.py (app/pane.js)
```

### `seam.log` — the desktop's block, before and after, per capability

The card's measured fault is the `provider-words` line:

```
=== provider-words
  desktop      {"effort":"very_high","effort_fixed":false,"efforts":["low","very_high"], …}
  hub (before) {"effort": null, "efforts": ["low"]}
  hub (now)    {"effort": "very_high", "efforts": ["low", "very_high"], "effort_fixed": false, …}
```

`EFFORT = ^[a-z][a-z0-9-]{0,15}$` dropped `very_high` on its own and kept `low`, so the phone drew
one level with nothing ticked and the pane's real level was unreachable from it. The pattern is now
the shape a provider's word really has, and — the part that matters more — the block is all of it
or none of it: a word that fails the shape, more levels than the cap, or an `effort` that is not
one of the levels drops `effort` and `efforts` together.

`seam.log` also shows the `view` row empty on every case: the capability strip covers all four
fields. That strip was untestable before this stream — see **What could not fail before** below.

### `drive.log` and the five screenshots — what the phone does with it

| Case | hub sends | the chip (`app/pane.js`, read off the DOM) | picture |
|---|---|---|---|
| `live` | three levels, `high`, not fixed | `disabled:false`, reads `high` | `EFT9-live.png` |
| `relay-free` | two levels, `high`, **`effort_fixed:true`** | **`disabled:true`**, reads `high` | `EFT9-relay-free.png` |
| `provider-words` | `["low","very_high"]` on `very_high` | `disabled:false`, reads **`very_high`**, ticked | `EFT9-provider-words.png` |
| `no-levels` | no effort block at all | `hidden:true` — no chip | `EFT9-no-levels.png` |
| `relay-free`, as a `view` device | no effort block at all | `hidden:true` — no chip | `EFT9-viewer.png` |

The second row is the seam with stream A: `app/pane.js:1206` reads `m.effort_fixed === true` and
nothing else, and that is exactly what the desktop now writes and the hub now passes. Relay Free
keeps its two levels — they are worth showing, `Entry::effortFixedReason` says so — so the phone
draws the greyed chip the desktop draws rather than no chip at all.

## What could not fail before

`remote/pane_state.py`'s `EXAMPLE` — the module's own words for it are "the contract's own example",
and it is the state `tests/test_remote_pane_state.py` feeds to `CleanTests`, all of
`CapabilityTests` and the live capability test — had never gained `effort`/`efforts`. Measured
before this stream: **delete both `pop`s in the viewer strip and every assertion in the suite still
holds**, so an edit that dropped the strip would have landed green. Measured after: with `EXAMPLE`
carrying the block and the four `assertNotIn`s added, deleting those lines fails
`CapabilityTests.test_a_view_device_is_offered_nothing_to_press`:

```
AssertionError: 'effort' unexpectedly found in {'label': 'fake · local', 'effort': 'high',
  'efforts': ['low', 'medium', 'high'], 'effort_fixed': False, 'effort_fixed_reason': ''}
```

## The refused pick

`src/RemoteShare.cpp` threw away the `bool` from `remoteEffortPick()`, whose own comment said the
opposite. A refusal changes nothing on the pane, so no state was republished, so the phone kept the
word the pane rejected for the rest of the session. Of the two shapes section 16 already uses —
answer the one device (`conversation_id`) or republish the pane's state (`queue_resume`,
`queue_remove`) — this is the second: a reasoning level is not secret, it is already in every
state, and every watching device drew the same stale chip, so one forced `publishPaneState()`
corrects all of them with no new message type. `ActionTests.test_a_refused_pick_is_answered_by_the_panes_own_state`
holds the hub to its half of that (the state reaches the device that tapped, with a `seq` that
makes it the newer of the two, even though its body is the one that device already drew).

## Reproducing

```
scripts/relay-build --target relay-panestate-tests
g++ -std=c++20 -fPIC $(pkg-config --cflags Qt5Core) -Isrc \
    -o /tmp/effort_probe docs/qa_evidence/2026-09-22-streamE-effort/effort_probe.cpp \
    build/librelay-panestate.a $(pkg-config --libs Qt5Core)
python3 docs/qa_evidence/2026-09-22-streamE-effort/seam.py /tmp/effort_probe   # seam.log
python3 docs/qa_evidence/2026-09-22-streamE-effort/drive.py /tmp/effort_probe   # drive.log, the pictures
RELAY_KEYRING=off python3 -m unittest tests.test_remote_pane_state -v            # tests.log, 45 tests
ctest --test-dir build -R panestate                                             # the C++ builder's own tests
```

`tests.log` is that run: 45 tests, up from 42. `land.py`'s build gate built the exact tree it put
on `main` for `d329d88a` (`verify: the exact tree builds`), so the `relay` target compiles with
these changes in it.

## Left to the verifier

A live drive of the **real** desktop, hub and phone together — a paired phone tapping a level on a
Relay Free pane and watching the chip go back — is the workstream's own last step
(`WORKSTREAM.md`, "The order of the end"), after every stream has landed. What is here proves each
join of the seam with the real code on both sides of it, not the three processes wired together.
