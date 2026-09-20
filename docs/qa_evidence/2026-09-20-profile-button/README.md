# Profile button — live evidence (#7BM4 phase 5, 2026-09-20)

The Profile button on the Switchboard's tool row, the target menu it opens, and the result pane it
fills, driven in a real Relay under Xvfb; plus two runs of `scripts/relay-profile` by hand, so the
numbers the pane shows can be checked against the script that produced them.

Everything here was made by `drive.sh` and by the two commands named below. The Relay it drove is
the binary `land.py` built from the exact tree it committed
(`/tmp/claude-1000/land/profile/verify/build/relay`), never this shared checkout's `build/relay`,
which carries other sessions' in-flight edits.

## The live run — `drive.sh`

```
PROFILE_X=1554 PROFILE_Y=841 BUILD_X=1400 BUILD_Y=744 \
  docs/qa_evidence/2026-09-20-profile-button/drive.sh
```

A throwaway `$HOME/project` holding a Switchboard (one card) **and** a three-target CMake project —
`alpha.cpp` (maps and strings), `beta.cpp` (`<regex>`, deliberately the slow one) and a `main.cpp`
that links them — so the build target has something real to time. Isolated `HOME`, `XDG_*` and
`TMPDIR` under a short `/tmp` path (the 108-byte socket limit), `RELAY_KEYRING=off`, the app started
`--clean-shell --fresh`, xdotool chords and clicks only, and nothing typed into a terminal pane.

| file | what it shows |
|---|---|
| `00-board.png` | the Switchboard open on the fixture, its tool row reading **Check · Clean up · Tests · Profile** |
| `implementer-01-menu.png` | the target menu under the button: *Build (this machine)*, *Build (sphinxpad)*, *Python tests*, *The app*, each with the line that says what it does and how long |
| `02-started.png` | a moment after *Build (this machine)* was chosen |
| `implementer-02-result.png` | the result pane, "Profile: Build (this machine)": the header line, the table (Output / Compile / Share, sorted by compile time, `beta.cpp.o` 85.1 %), the evidence path, **Open flame graph** and **Attach to card…** — and the same sentence in the board's own notice line, where Clean up's progress goes |
| `03-card-picker.png` | **Attach to card…**: the board's own card picker, the one the composer's `#` and the Test suites pane use |
| `implementer-03-attached.png` | after Enter — "Added the profile to #5WTE." |
| `card-after-attach.md` | that card afterwards: a `## Profile` section holding the dated heading and the table, and `links.evidence: [docs/qa_evidence/2026-09-20-profile-build]` |
| `evidence-listing.txt` | what the run left in the fixture project's `docs/qa_evidence/2026-09-20-profile-build/` |
| `fixture-summary.md` | that run's `summary.md`, as the worker read it back |

The result pane hides the Total column for a build: a build step has no call stack, so its total
*is* its compile time, and printing the same number twice cost the output paths their room. Three
things in these shots were changed because of them — that column, the menu's wrap width (the
descriptions were being squeezed to five lines each and the menu ran off the screen) and the log
tail's height (a pixel cap cut its top line through the middle of the glyphs — the shots predate
that last one, and it is the half-line at the bottom of the pane).

## The script by hand

```
scripts/relay-profile build --build-dir <a throwaway Ninja build dir> --out build-mode/
scripts/relay-profile tests tests.test_test_history --out tests-mode/
```

`build-mode/` is the same three-target project built with Ninja outside Relay: `summary.md`,
`rows.json` (the table the wire carries), `build.json` (every step), `build.trace.json` (the build
as a Chrome trace, which is what the flame graph opens), `ninja_log` and `meta.json`.

`tests-mode/` is `py-spy record --subprocesses --rate 250 --format speedscope` over one unittest
module under `scripts/test.sh`'s isolation: `tests.speedscope.json`, the folded `rows.json`, and a
`summary.md` whose top rows are `importlib._bootstrap` — that module's time is import time, which is
the honest answer for a suite that runs in 60 ms. (py-spy samples at 250 Hz here; a suite this short
is a handful of samples, which is why `docs/PROFILING.md` says to profile a suite rather than a
module.) `console.txt` in each is what the script printed.

The app target is not shot here: it opens a second Relay under `perf`, which needs a display of its
own and a person to use it before stopping. Its commands are the ones `scripts/relay-tooling-setup`
smoke-tests and `docs/PROFILING.md` section 3 records, and `sudo -n perf record → chown → perf
script → stackcollapse-perf.pl` was re-checked on this machine while this landed.
