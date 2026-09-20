# #GMCF decision 3 — the session file is written fewer times per turn

Owner's decision on finding 4 of `docs/qa_evidence/2026-09-20-perf-profile/worker/FINDINGS.md`:
keep the format and the atomic whole-file rewrite, coalesce the ~6 file opens a turn costs.

Machine: spark (aarch64, Python 3.12.3). `before` and `after` are clean `git archive` exports of
the tip the work started from, `after` with only these hunks applied, so no other session's
uncommitted code is in either arm.

## Where the six opens a turn went

`strace -f -e trace=openat` of `worker.py` under the profiler's own
`docs/qa_evidence/2026-09-20-perf-profile/worker/turnbench.py`, 20 turns with a tool call:

```
before: 58 opens of a `.session-*` temp file (one per file written) + 58 opens of `<id>.meta.json`
        = 116, i.e. 5.8 per turn, over 29 saves
after:  50 temp-file opens + 2 opens of `<id>.meta.json` = 52, i.e. 2.6 per turn, over 25 saves
```

Composition of the 29 saves before: 20 at the end of a turn, 4 from `set_title`, 4 from
`set_summary`, 1 mid-turn `_autosave_soon` (its 10 s throttle collapses the rest).

Each save used to open `<id>.meta.json` **twice** to read it back — once in `read_meta`, once in
`conv_index.read_user_fields` — for bytes the same process had written itself at the previous
save. It now reads that file only when someone else has been at it (inode + mtime_ns + size say
so), and writes it only when something the sessions list shows has changed.

The four saves that went are the title and the summary of one cadence point: `maybe_title()`
starts both cheap calls together, and whichever comes back last now saves the session for both.

## One turn, counted (tests/test_sessions.py::SaveCostTests)

A turn with three tool calls, with `MID_TURN_SAVE_S = 0` so every durability point saves:

```
before   7 session writes   7 meta writes   14 meta reads   = 28 file operations
after    7 session writes   2 meta writes    0 meta reads   =  9 file operations
```

The seven session writes are the seven things a crash must not take with it: the user's message,
the assistant message that asked for the tools, each of the three tool results, the final message,
and the end of the turn. None of them was removed; `tests/test_sessions.py::CrashDurabilityTests`
kills the worker with SIGKILL at three of them and reads the session back.

## Per save, on a read-only copy of the owner's largest session

`savebench.py` beside this file (1574 KiB, 594 messages; the copy is never read for its content,
and the owner's own store is never opened for writing). `RELAY_INDEX=off`, so this is the file
work alone:

```
                                     before      after
a save that carries a new message   10.23 ms    9.08 ms    -11 %
a save with nothing new in it       10.46 ms    8.95 ms    -14 %   (the meta write goes too)
```

## 300 stub turns

```
                        before       after
turnbench 300           3.34 s CPU   3.24 s CPU    -3 %
turnbench 300 --tools   4.84 s CPU   4.66 s CPU    -4 %
```

Small, because the stub session is 389 KB and what was removed is small-file I/O; the win is in
the opens and in the per-save figure above, which is what the owner feels on a real conversation.

## Measured and NOT taken: skipping a save whose content did not change

Implemented first, then removed. An O(1) fingerprint of the session (counters, title, usage,
checkpoint and request counts) **never matched once in 25 saves** over a 20-turn run: every save
in a turn carries a new message, a new usage total or a finished checkpoint. Forced to match, the
skip has to re-serialise the session to compare it with the file, which measured 8.95 ms against
the 8.95 ms write it would have saved — a wash at best, and a fingerprint that matched while the
bytes differed (the end of a turn stamps `ended` on a checkpoint without changing any count)
would have cost a whole wasted serialisation. `_atomic_json` is unchanged: temp file, fchmod
0600, `os.replace`.

The cheap serialisation win the profiler measured (`out.write(json.dumps(...))` for
`json.dump(..., out)`, identical bytes) is already on main: #0TJ9 landed it in `73d50015` as part
of `_atomic_text`.

## Commands

```
# opens per turn
cd <arm>/docs/qa_evidence/2026-09-20-perf-profile/worker   # or a copy of turnbench.py
python3 turnbench.py <arm-root> 20 --tools --strace        # --strace added locally
grep -oE 'openat\([^,]+, "[^"]*"' strace.txt | grep -cE 'sessions/.*(\.meta\.json|\.session-)'

# per-save cost
python3 savebench.py <arm-root> <dir holding one read-only copy of a session>

# the tests
PYTHONPATH=backend python3 -m unittest tests.test_sessions.SaveCostTests \
    tests.test_sessions.CrashDurabilityTests
PYTHONPATH=backend python3 -m unittest tests.test_sessions tests.test_session_protocol \
    tests.test_session_threads tests.test_conv_index tests.test_guest_sessions
```
