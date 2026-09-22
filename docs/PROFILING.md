# Profiling and test tooling: machine setup and the commands behind the buttons

Card #7BM4 (2026-09-20). The Board's **Test suites** pane and **Profile** button run
ordinary tools; this page says which, how a machine is set up for them, and the exact commands,
so everything the buttons do can also be done by hand and checked. Research and sources:
[`SWITCHBOARD-TOOLING-RESEARCH.md`](SWITCHBOARD-TOOLING-RESEARCH.md). The measured profile of
Relay itself is `docs/qa_evidence/2026-09-20-perf-profile/REPORT.md` (#PF4K).

## 1. Setting a machine up

```
scripts/relay-tooling-setup                 # check: a table of tools, then five smoke tests
scripts/relay-tooling-setup --install       # install what is missing, then check
ssh laptop.local 'bash -s -- --install' < scripts/relay-tooling-setup     # any second machine
```

Idempotent, and the same script on every runner. It installs `perf`, `hotspot`, `heaptrack`,
`hyperfine`, `clang` and `ninja` through apt where sudo is passwordless, and into `~/.local`
without root: `py-spy`, FlameGraph's perl scripts, speedscope's static bundle and
ClangBuildAnalyzer. Its smoke tests prove whole paths rather than that a binary exists: ctest can
write JUnit XML; py-spy records a Python child into speedscope JSON; `cProfile` works;
`sudo -n perf record` → `chown` → `perf script` → folded stacks; `clang -ftime-trace` writes a trace.

Nothing in the repository names a machine: the second runner is whatever `--host` or
`RELAY_REMOTE_HOST` says, and the Profile menu asks for it once and remembers it in the settings
(`tests/remote_host`). The two machines it was written against both pass all five smoke tests
(2026-09-20):

| | the desktop (aarch64) | the laptop (x86_64) |
|---|---|---|
| Platform | aarch64, Ubuntu 24.04, 20 cores | x86_64, Ubuntu 26.04, 12 cores |
| Compilers | g++ 13.3, clang 18 | g++ 15.2, clang |
| Qt | 5 | 5 and 6 (the .deb ships 6) |
| Python | 3.12 | 3.14 |
| `build/` generator | Unix Makefiles (shared by every session) | Ninja (`~/relay-ci/build*`) |
| Missing | — | `hotspot` (26.04 no longer packages it; optional, profiles are viewed on spark) |

**What it deliberately leaves alone.** `kernel.perf_event_paranoid` is 4 on both machines and
stays 4: passwordless `sudo -n perf` works on both, so the tools record through sudo and hand the
file back. Lowering a kernel security setting system-wide is the owner's decision, not a setup
script's, and nothing here needs it. It also edits no shell profile; everything is in
`~/.local/bin`.

Two traps the smoke tests were written around:

- `perf` refuses, **even under sudo**, a `perf.data` that neither root nor the caller owns. Record
  with sudo, `sudo chown $(id -u)` the file, then run `perf script` **as yourself** — reading needs
  no perf access at all.
- py-spy samples at 100 Hz by default, so a test module that finishes in a few milliseconds
  yields an empty profile. Profile a suite, or raise `--rate`.

## 2. The second runner

```
scripts/relay-remote-tests --host laptop.local --qt 6      # main, on that machine
scripts/relay-remote-tests --rev <sha> -R board    # one commit, ctest names matching
scripts/relay-remote-tests --build-only            # just the build and its .ninja_log
```

It exports the **committed** tree (`git archive`, never the shared working tree), rsyncs it to
`~/relay-ci/src` by checksum and without timestamps so the remote build stays incremental,
configures once with Ninja, builds, runs ctest with `--output-junit` and the Python suite with
`scripts/test.sh --junit`, and fetches the results into `<board>/.private/tests/incoming/<host>-<run>/`
(`ctest.xml`, `unittest.xml`, `meta.json`, `ninja_log`, logs). That folder is gitignored; the
history store ingests from it, and every execution carries its `host`, so the pane shows both
machines' runs of the same test side by side. Remote tests run with `RELAY_KEYRING=off`,
`QT_QPA_PLATFORM=offscreen` and every XDG dir and `TMPDIR` under a short fresh `/tmp/rci-*`
(the 108-byte socket limit), so they touch nothing of the owner's.

## 3. The three profile targets, by hand

"Profile the project" is three different things here; the button asks which.

**The build** (first target). Per-target wall time from Ninja's log; on a clang build, the
per-header and per-template breakdown as well:

```
scripts/relay-remote-tests --build-only            # leaves ninja_log in the results folder
# per-header / per-template, on a machine with clang:
cmake -S . -B /tmp/relay-tt -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-ftime-trace
ClangBuildAnalyzer --start /tmp/relay-tt && cmake --build /tmp/relay-tt --target relay
ClangBuildAnalyzer --stop /tmp/relay-tt /tmp/relay-tt/cba.bin && ClangBuildAnalyzer --analyze /tmp/relay-tt/cba.bin
```

First measurement (2026-09-20, the laptop, cold Qt 6 build of `c8b0a8d2`, 12 cores): 240 s wall,
904 steps, and `src/main.cpp.o` alone is 181 s — three quarters of the wall time is the one
translation unit, with `tests/boardmodel_test.cpp.o` (68 s) and `tests/conversations_test.cpp.o`
(56 s) next. That table is what the build target shows.

g++ has no `-ftime-trace`, and the shared `build/` is Makefiles, so the fine-grained report comes
from a separate clang+Ninja build directory, never from `build/`.

**The Python tests.** No root: py-spy is the parent of what it samples.

```
XDG_DATA_HOME=$(mktemp -d) RELAY_KEYRING=off PYTHONPATH=backend \
  py-spy record --subprocesses --rate 250 --format speedscope -o tests.speedscope.json \
  -- python3 -m unittest tests.test_board_tools
```

Fallback with nothing installed: `python3 -m cProfile -o tests.prof -m unittest …`.

**The app.** Measure a clean export, not the shared `build/` (#PF4K's practice), and stop it with
`kill -TERM`, which is a clean quit:

```
sudo -n perf record -g --call-graph dwarf -o app.perf.data -- ./relay --clean-shell --fresh
sudo -n chown $(id -u) app.perf.data
perf script -i app.perf.data | stackcollapse-perf.pl > app.folded
```

A fresh profile auto-configures Relay Free, so a line typed into a test pane can become a real
hosted agent turn; harnesses guard against it (#PF4K).

## 4. Looking at a profile

```
scripts/relay-speedscope tests.speedscope.json     # also .folded, .cpuprofile, Chrome traces
hotspot app.perf.data                              # native Qt viewer, spark only
flamegraph.pl app.folded > app.svg                 # no browser at all
```

Relay has no QtWebEngine, so the flame graph is the one view that leaves the app: the functions
table is drawn natively, and `relay-speedscope` opens the rest in the system browser from the
local static bundle. Nothing is uploaded. Every profiling product shows the table first; so does
the button.

## 5. Where results go

The raw profile goes under `docs/qa_evidence/<date>-profile-<target>/` and is named in the card's
`links.evidence`; the summary table (top functions or targets, self, total, wall time) is
appended to the card. Numbers are committed, raw samples are not — the same split
github-action-benchmark, CodSpeed and Bencher use. Test execution history is a store, not source:
`<board>/.private/tests/history.jsonl`, gitignored, with the numbers that matter committed into
cards by **Check**.
