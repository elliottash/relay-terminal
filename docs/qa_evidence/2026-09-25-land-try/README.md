# QA evidence: `land.py try` builds the exact landing tree (card #76QW)

Session `qw76-land`, landed commit `f5896f3d20d2e20e6718f0df7424642f5ef9e181`
(scripts/land.py, scripts/relay-build, tests/test_land.py). Evidence collected on this
checkout immediately after that landing.

## Test run

    python3 -m pytest tests/test_land.py -q
    113 passed in 28.31s   (100 before the card; 13 new: 9 Try, 3 VerifySlots, 1 Who)

## Real `try` of the landed commit, with the project's own tests

    python3 scripts/land.py try qw76-land --tests land          # exit 0, 267 s

stdout (the machine-readable summary, exactly as the contract orders it):

    tip f5896f3d20d2e20e6718f0df7424642f5ef9e181
    src /home/elliott/.local/state/relay/land/verify-slots/relay-terminal-1006c7a3-0/src
    build /home/elliott/.local/state/relay/land/verify-slots/relay-terminal-1006c7a3-0/build
    binary /home/elliott/.local/state/relay/land/verify-slots/relay-terminal-1006c7a3-0/build/relay

stderr (logs; the tree merged and built, then `ctest -R land` ran green):

    verify: all 1 build slot(s) busy; waiting for relay-terminal-1006c7a3-0
    verify: tree e8f5e9b0eb2d materialised in .../src (22 file(s) refreshed)
    verify: RELAY_JOBS=8 lowered to 4: this build may use 8.0 GiB and one compile job peaks near 2 GiB
    verify: cmake --build .../build --parallel 4
    verify: ctest --test-dir .../build -R land --output-on-failure
    verify: the exact tree builds

The "no change since your snapshot" notes are expected here: the session had just landed,
so its working copy equals the tip it merged against — the tree `try` built is the landed
commit itself, whole.

## `land.py who` after the same run (holder, hold age, last tree per slot)

    disk: /home/elliott/.local/state/relay/land holds 4.4 GB, of which verify build slots 4.2 GB (at most 1 per repository; `land.py gc` reclaims stale sessions)
        slot relay-terminal-1006c7a3-0: session 9fx8-s3 (pid 1404515), held 0m, last tree ff22d48c892f
        slot relay-terminal-1006c7a3-1: free, no tree yet

The pool is 1 on this host because the pane's cgroup memory limit (about 8 GiB) bounds it —
the new `verify_slots()` sizing at work; the other session's slot shows its holder, pid,
hold age and the last tree it built, and the unused slot reads free.

## Whole-card check on a clean export (parent session, 2026-09-25)

Commits: `f5896f3d` (land.py try, slot configure, pool sizing, who holders), `d88d10bc` (this
evidence), `da4294d6` (CLAUDE.md, RELAY.md, deliver skill + POLICY regeneration, Try-it binary from
the card's landed commit, briefs, protocol), `e491e055` (`land_try` agent tool).

Run on `git archive 68776e05` in a scratch directory, never in the checkout:

```
PYTHONPATH=backend python3 -m pytest tests/test_land.py -q             113 passed
PYTHONPATH=backend python3 -m pytest tests/test_tryit_protocol.py -q    37 passed
PYTHONPATH=backend python3 -m pytest tests/test_tools.py -q             57 passed
```

Known, not this card's: `tests/test_system_prompt.py::SizeTests` — the tool list is 455 bytes over
its budget after `land_try` (and #FYEY's `agent_stop`); recorded on #K54A, which already tracks the
prompt's own overage.
