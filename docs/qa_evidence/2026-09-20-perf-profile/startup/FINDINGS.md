# #PF4K — process startup, idle, memory and shutdown

Area: the `relay` app as a whole — exec to usable window, what it does while nobody touches it,
what a pane costs in memory, and what a quit waits on. Everything here is measured on a clean
export of `main` at `ccb31a8e`; line numbers are from that export and the paths are repo-relative.
No product code was changed.

## Machines and state

| | spark | sphinxpad |
|---|---|---|
| CPU | aarch64, 20 cores (NVIDIA DGX) | x86_64 i7-1365U, 12 threads |
| OS / Qt | Ubuntu 24.04, Qt 5.15 | Ubuntu 26.04, Qt5 **and** Qt6 builds |
| Binary | `/tmp/claude-1000/pf4k/build/relay`, RelWithDebInfo | `~/relay-perf/build{,-qt6}/relay`, RelWithDebInfo |
| Power | mains | **AC online, governor `performance`**, battery "Not charging" |
| Load during runs | 0.9 – 3.5 (four other #PF4K agents) | 0.2 – 1.0 |
| Display | Xvfb `:231`, 1600x1000x24, no window manager | Xvfb `:231`, same |

`~/relay-perf/configure.log` says "Relay: building against Qt5"; `configure-qt6.log` says Qt6. The
Qt6 column below is the third machine column the orchestrator asked for, because the Ubuntu 26.04
`.deb` is what a user there actually gets.

Every run uses a fresh isolated profile (`XDG_CONFIG_HOME`/`DATA`/`STATE`/`CACHE`, `TMPDIR`,
`RELAY_KEYRING=off`, `--clean-shell --fresh`). `XDG_RUNTIME_DIR` is deliberately **not** isolated
except in the restore test — see "Harness notes" for why that matters.

Harness: `harness/` in this directory. Raw numbers: `RAW.md`.

---

# Findings, ranked by user-visible impact

## 1. A pane that is doing nothing wakes 13 times a second, and minimising the window does not stop it

**What the user feels.** On a laptop, Relay never lets the CPU idle deeply. Four panes sitting at a
shell prompt with nothing running cost ~1.4 % of a core and ~50 wakeups a second, for ever. A
Relay that is minimised and forgotten costs the same as one being looked at.

**Measured** (60 s samples, relay process only; children add nothing — see finding 4):

| | spark Qt5 | sphinxpad Qt5 | sphinxpad Qt6 |
|---|---|---|---|
| 1 pane, CPU | 0.53 % | 0.48 % | 0.52 % |
| 1 pane, wakeups/s | 20.9 | 18.5 | 20.7 |
| 4 panes, CPU | 1.43 % | 1.37 % | 1.50 % |
| 4 panes, wakeups/s | 59.8 | 47.0 | 48.0 |

So roughly **13 wakeups/s and 0.3 % of a core per visible pane**, linear, on both architectures and
both Qt versions. All of it is on the GUI thread: per-thread deltas at one pane were main 18.05/s,
`QXcbEventQueue` 2.00/s, everything else 0.

Unmapping the window (what minimising does) changes almost nothing:

| 4 panes | spark | sphinxpad |
|---|---|---|
| mapped | 50.5 wk/s, 1.65 % | 44.0 wk/s, 1.17 % |
| **unmapped** | **43.5 wk/s, 1.00 %** | **44.8 wk/s, 1.00 %** |
| remapped | 47.5 wk/s, 1.15 % | 48.2 wk/s, 1.13 % |

Reproduce: `harness/unmap.py 4`, and `harness/idle.py <tag> <panes> <tabs> <seconds>`.

**Where the work goes.** `perf record -g --call-graph fp` over 25 s of four idle panes, bucketed by
the innermost Relay frame in each sample (243 samples):

```
 23.9%  Pane::pollGuestEvents
  3.7%  relay::usage::walkTrees
  2.5%  Pane::pollShell
  2.5%  relay::usage::metersEnabled
  1.2%  Pane::processBusy
 60.9%  no Relay frame (kernel and Qt internals under the above)
```

And the syscalls, counted with bpftrace over 30 s at **one** idle pane: 145 `statx`/s, 57 `ioctl`/s,
44 `read`/s, 37 `close`/s, 37 `openat`/s, 36 `faccessat`/s, 35 `getdents64`/s, 27 `newfstatat`/s,
24 `ppoll`/s — about **470 syscalls a second for one pane with nothing happening in it**. The
per-path breakdown (20 s) names them exactly; see 1a and 1b.

### 1a. `Pane::pollGuestEvents()` lists a directory 12.5 times a second per pane

`src/Pane.h:1781`, called unconditionally from `pollShell()` at `src/Pane.h:14905`, which runs on
`m_poll` at `kPollFastMs = 80` ms (`src/Pane.h:14889`, started at `:476`).

```cpp
void pollGuestEvents() {
    QDir spool(guestEventsDir());
    if (!spool.exists()) return;
    const QStringList names = spool.entryList(QStringList{"*.json"}, QDir::Files, QDir::Name);
```

bpftrace over 20 s at one idle pane: `$PROFILE/tmp/relay-XXXXXX/guest-events` was `statx`'d 250
times and `openat`'d 251 times, and `getdents64` ran 700 times — 12.5 scans a second, of a
directory that is empty unless a guest agent (Claude Code / Codex) is running in that pane, which
is the rare case.

Microbenchmark (`harness/b.cpp`, spark, same Qt): `QDir::exists()` + `entryList()` on an empty
directory costs **13.3 µs**; a bare `stat()` of the same directory costs **0.45 µs** — 30× less.

**Fix.** Gate the listing on the directory's own mtime: keep the `struct stat` of the spool
directory beside the `state.json` one that `pollShell()` already keeps (`src/Pane.h:14908-14914`
does exactly this pattern for `state.json`), and only call `entryList()` when `st_mtim`/`st_ino`
changed. Writing or removing a file in a directory bumps the directory mtime, so nothing is missed.
The cleaner version is a `QFileSystemWatcher` on the spool directory and no poll at all, but the
mtime gate is a five-line change with the same effect and no watcher-descriptor budget.

**Expected gain.** Removes the top named Relay frame at idle (24 % of sampled Relay CPU) and
~50 syscalls/s per pane — a third of the 145 `statx`/s and essentially all 35 `getdents64`/s.
Estimated from the per-path bpftrace counts and the 13.3 µs → 0.45 µs microbenchmark.
**Risk:** low. A guest event would be delivered one tick late only if a directory mtime failed to
change across a create-then-rename, which it cannot.

### 1b. `QSettings` is constructed (1 + panes) times every 400 ms, and each one re-stats eight paths

`RelayWindow::refreshPaneStatus()` (`src/RelayWindow.h:7248`) runs on `m_statusTimer` at
`kStatusPollMs = 400` (`:7245`). It calls `relay::usage::metersEnabled()` at `:7255`, and then for
**every pane in every tab** calls `pane->refreshUsage()` (`:7275`), whose first line
(`src/Pane.h:702`) calls `metersEnabled()` again. `metersEnabled()` is

```cpp
bool metersEnabled() {            // src/PaneUsage.cpp:457
    return QSettings().value(QStringLiteral("appearance/pane_usage"), true).toBool();
}
```

Each default-constructed `QSettings` rebuilds its fallback chain and stats every candidate file.
bpftrace, one idle pane, 20 s, showed exactly that: `relay.conf` 100 times plus 200 each for
`/etc/xdg/RelayTerminal.conf`, `$XDG_CONFIG_HOME/RelayTerminal.conf`,
`~/.config/kdedefaults/RelayTerminal.conf`, `/etc/xdg/xdg-plasma/RelayTerminal/relay.conf`,
`~/.config/kdedefaults/RelayTerminal/relay.conf`, `/etc/xdg/RelayTerminal/relay.conf` and
`/etc/xdg/xdg-plasma/RelayTerminal.conf` — **1500 `statx` in 20 s = 75/s, for one boolean**, at one
pane. At four panes it is 12.5 constructions/s. `QVariant::toBool()` was 3.24 % of all CPU in the
startup profile, which is the same call.

Microbenchmark: `QSettings().value(...).toBool()` costs **14.8 µs**.

**Fix.** Read it once and cache it, invalidating where it is written. `relay::theme::notifier()`
already exists as the pattern for "a setting changed, tell everyone" (`src/Pane.h:508-509` connects
to it). Concretely: a file-static `int g_meters = -1` in `PaneUsage.cpp` filled on first use, and a
`relay::usage::metersChanged()` call from the one place in Options that writes
`appearance/pane_usage`. Even without a notifier, passing the `meters` bool that
`refreshPaneStatus()` already computed at `:7255` down into `refreshUsage()` removes the per-pane
constructions, which are the majority.

**Expected gain.** Removes ~75 `statx`/s at one pane and ~100/s at four — about half the idle
`statx` traffic — and 5–12 `QSettings` constructions a second. ~0.2 ms/s of CPU at four panes from
the microbenchmark, which is small; the syscall count is the real saving and it is what keeps a
sleeping disk awake. **Risk:** low, but the cache must be invalidated or the Options toggle stops
taking effect until restart — that is the whole risk and it is testable.

### 1c. `tunePoll()` only asks `isVisible()`, so a minimised or fully covered window polls at full rate

`src/Pane.h:14890`:

```cpp
void tunePoll() {
    const bool quiet = !isVisible() && !m_loading && !m_activeValid && m_entries.isEmpty();
    const int wanted = quiet ? kPollQuietMs : kPollFastMs;      // 400 : 80
```

That correctly slows panes in background *tabs*. It does not slow anything when the whole window is
minimised, because Qt keeps `isVisible()` true for an iconified window's children — which the unmap
measurements above confirm: 44.0 → 44.8 wk/s on sphinxpad, no change at all.

**Fix.** Add the window's own state to the `quiet` test: `window()->isMinimized()`, and
`!QApplication::applicationState().testFlag(Qt::ApplicationActive)` for the "another app is in
front" case. Both are already-cached Qt state, not new probes. A minimised window has no one
waiting on its answer, which is exactly the argument the existing comment at `src/Pane.h:14886`
makes for background tabs.

**Expected gain.** 5× fewer ticks while minimised: at four panes, 50 → ~14 wakeups/s and 1.65 % →
~0.4 % of a core, by the same 80 ms → 400 ms ratio the code already uses. On a laptop that is the
difference between Relay keeping the package C-states shallow all afternoon and not.
**Risk:** low; the failure mode is a first tick 400 ms late on restore, and `showEvent` already
re-tunes.

### 1d. The prompt box's caret blinks for ever, focused or not

`RichEditor::setCaretColor()` (`src/RichEditor.cpp:208-222`) creates `m_caretBlink` at
`cursorFlashTime()/2` (≥ 200 ms, 500 ms on a default desktop) and calls `start()`. Nothing ever
stops it — `RichEditor` has no `focusInEvent`/`focusOutEvent` at all. Every tick calls
`viewport()->update()`; `paintEvent` (`:225-227`) then checks `hasFocus()` and draws nothing. So
every **visible** pane's composer repaints twice a second whether or not it has the keyboard, and
only Qt's "hidden widgets do not repaint" rule saves background tabs.

By contrast `TerminalView` does this correctly: `focusOutEvent` stops `m_blinkTimer`
(`engine/view/TerminalView.cpp:2713-2715`).

**Fix.** Give `RichEditor` the same two overrides: start the timer in `focusInEvent`, stop it in
`focusOutEvent` and in `hideEvent`, matching `TerminalView`.

**Expected gain.** At four visible panes, 8 → 2 repaints and wakeups a second (6 of the ~50). Small
on its own; it is here because it is a two-line fix with a working reference implementation six
files away.

---

## 2. Every extra pane costs ~30 MB, and almost all of it is a second Python interpreter

**What the user feels.** Six panes is 220 MB of Pss. On an 8 GB laptop a dozen panes is a real
fraction of what is left after a browser.

**Measured** (spark; `Pss`/`Private_Dirty` from `smaps_rollup`, whole process tree):

| step | relay Pss | relay Priv.Dirty | tree Pss | fds | threads | children | /tmp/relay-* dirs |
|---|---|---|---|---|---|---|---|
| just started, 1 pane | 40.2 MB | 30.4 MB | 73.2 MB | 25 | 49 | 2 | 2 |
| 4 panes, 1 tab | 43.2 | 33.4 | 160.8 | 46 | 52 | 8 | 5 |
| 6 panes, 3 tabs | 46.4 | 36.6 | **221.0** | 60 | 54 | 12 | 7 |
| closed back to 1 pane | 47.4 | 36.5 | 80.9 | 21 | 45 | 2 | 2 |
| + 20 s | 46.4 | 36.5 | 80.0 | 21 | 45 | 2 | 2 |

Per extra pane, in the tree: **~27 MB python worker + ~1.0 MB bash + ~1.2 MB in relay itself ≈
29.6 MB.** sphinxpad agrees: 74.1 MB at one pane, 165.4 MB at four (Qt5), i.e. 30.4 MB/pane.

The workers are *entirely* idle — 0.00 % CPU and 0.0 wakeups/s each, on both machines, in every
run. They cost memory, not power.

**Root cause of the 27 MB.** `Pane::startWorker()` (`src/Pane.h:8851`) runs
`python3 -S -u backend/worker.py` per pane. That is one CPython heap, one set of imported modules
and one copy of the model catalog per pane. `-S` is already there, so the cheap win has been taken.

**Why this is listed rather than fixed.** Whether the worker should be one process per pane at all
is the worker owner's call. The obvious shape is one worker process serving several panes over the
same stdio protocol, keyed by the `RELAY_PANE_ID` the protocol already carries
(`src/Pane.h:8858`); that trades the per-pane `MemoryMax` scope in `isolation::wrap()` against
~25 MB a pane, and it is a product trade-off, not a loose end. Measuring where a fresh worker's
27 MB goes (`python3 -X importtime`, `tracemalloc`) is inside the Python-worker agent's area.

**Sub-finding: relay's own memory does not come back.** After closing five panes, relay's Pss was
46.4 MB — above the 43.2 MB it had with *four* panes open, and 6.2 MB above the 40.2 MB it started
with — and it stayed there 20 s later. That is ~1.2 MB retained per pane ever opened. It is
bounded, not a runaway (finding 6), and the recently-closed store (`src/ClosedList.cpp`, which
deliberately keeps a closed pane's text so it can be reopened) is the obvious explanation for most
of it. Proving what those bytes are needs a heap profiler, and the brief records that neither
valgrind nor heaptrack exists on these machines.

---

## 3. Startup: ~210–240 ms to a mapped window, and about half of it is Qt before Relay runs a line

**What the user feels.** Relay opens in a quarter of a second. That is fine; this section exists so
nobody spends a week on it, and to name the one thing that can make it much worse.

**Measured**, median of 3, ms from `execve`:

| phase | spark Qt5 | sphinxpad Qt5 | sphinxpad Qt6 |
|---|---|---|---|
| worker process spawned | 209 | 115 | 96 |
| **window mapped** | **236** | **212** | **158** |
| shell at its first prompt | 255 | 160 | 136 |
| agent prompt box ready (`type=ready`) | 322 | 263 | 199 |
| same, cold (binary + all 44 libs evicted) | 244 | 257 | 179 |

"Cold" evicts the binary and every `ldd` dependency with `posix_fadvise(POSIX_FADV_DONTNEED)` —
no sysctl, nothing system-wide (`harness/matrix.py`). Cold and warm are within noise on spark and
~45 ms apart on the laptop.

Note the ordering: the worker is spawned *before* the window is mapped. `Pane`'s constructor calls
`buildUi(); startWorker(); startTerminal();` (`src/Pane.h:470-472`) and
`WindowManager::newWindow()` builds every tab before `window->show()`
(`src/WindowManagerImpl.h:211-219`). Nothing on the GUI thread waits for the worker's reply, so the
window is up ~30 ms after the spawn and the agent box is usable ~90 ms later.

**Phase breakdown**, from `strace -f -tt -T` (which inflates everything ~2.5×, so read the
proportions, not the absolute numbers) — full timeline in `RAW.md`:

```
   0.0 ms  execve relay
   1.0 ms  first Qt library opened  (44 shared objects)
  28.1 ms  connect() to the X server
  35 – 163 ms  platform-theme plugin probing
 179 – 260 ms  fontconfig: 440 openat, 432 more under /fonts/
 276.7 ms  first relay.log write — i.e. gui_start, after QApplication + applyDarkTheme
 376 – 507 ms  font files actually loaded
 411 – 473 ms  three execve of systemd-run  (isolation probe, worker scope, shell scope)
 467.0 ms  /dev/ptmx
 630.2 ms  the shell's state.json appears
```

872 of the 1936 `openat` calls in the first 700 ms are font-related. A bare Qt5 widgets app on the
same machine (`harness/p.cpp`) gives the floor:

```
QApplication ctor 77–94 ms | QFontDatabase::systemFont 0.0 ms | QFontDatabase().families() 12 ms (595 families)
QIcon::fromTheme 3.5–4.8 ms | first widget show 4–7 ms | total to a mapped window ~100 ms
```

So **~100 ms of the ~220 ms is Qt's own floor** and cannot be recovered without leaving Qt. Relay's
own ~120 ms is the theme stylesheet, the settings migrations, `QFontDatabase().families()` in
`TerminalView`'s constructor (`engine/view/TerminalView.cpp:197`, 12 ms, once per process because
Qt caches the database), the icon lookup, the pane widget tree, and the three `systemd-run` execs.

### 3a. `isolation::available()` blocks the GUI thread on a subprocess with a 3-second timeout

`src/Isolation.h:29-42`, reached from `startWorker()` (`src/Pane.h:8869`) during the first pane's
construction, i.e. before the first window is shown:

```cpp
probe.start(tool, {"--user", "--scope", "--quiet", "--", "true"});
if (probe.waitForFinished(3000) && ...) state = 1;
```

Measured cost when it works: `systemd-run --user --scope --quiet -- true` takes 0–10 ms here, and
turning isolation off moves the medians by about 30 ms end to end (window 218 → 204 ms, shell
234 → 200 ms, agent ready 290 → 260 ms, median of 4 each — within noise for the window, real for
the shell). So on a healthy machine this is cheap.

The finding is the tail: on a machine with no user manager, a `systemd --user` that is busy, or a
D-Bus that is not answering, that `waitForFinished(3000)` is **up to three seconds of a frozen,
window-less Relay** with nothing on screen to say why. The result is cached in a `static`, so it is
paid once — but it is paid before the first window.

**Fix.** Run the probe off the GUI thread and start the first worker unisolated if the answer has
not come back yet, re-isolating on the next worker start; or at minimum drop the timeout to ~300 ms,
since a local `systemd-run` that has not answered in 300 ms is not going to be usable anyway.
**Expected gain:** nothing on a healthy machine, and it removes a 3 s worst case that looks like a
hang. **Risk:** low; the fallback path (plain `exec`) is the same one used whenever `systemd-run` is
absent, and it is exercised — `NO_ISOLATION=1` in the harness runs entirely on it.

### 3b. The 98 MB binary is 90 MB of debug info and costs nothing to load

`size` says 8.58 MB of text; `readelf -S` puts `.debug_*` from offset 0x8606cd to the end of the
file. Debug sections are not `SHF_ALLOC`, so they are never mapped. Measured: `relay --version`
(which builds a `QApplication` and applies the theme, then exits) took a median 167 ms with the
debug info and 148 ms stripped, across 5 runs each at load ~3 — a difference inside the noise.
Cold-start with the binary and all 44 libraries evicted was 244 ms vs 236 ms warm on spark.

**Conclusion: not a problem.** Do not strip for speed. (It does make `perf report` on this binary
take many minutes, which is a profiling annoyance, not a user one.)

`LD_DEBUG=statistics` reports 66,020 final relocations, 54,872 from cache. The startup profile puts
`do_lookup_x` + `_dl_lookup_symbol_x` + `strcmp` in `ld-linux` at **5.5 % of all CPU in the first
two seconds**. `-Wl,-z,now` plus `-fvisibility=hidden` would move and shrink that work; at ~10 ms of
a 220 ms startup it is not worth a link-flag change on its own, and it is a build decision rather
than a code one.

---

## 4. Qt6 on the laptop is 25 % faster to a window and 12 MB smaller resident

The third column the orchestrator asked for. Same source, same machine, same display, both
RelWithDebInfo.

| | sphinxpad Qt5 | sphinxpad Qt6 | change |
|---|---|---|---|
| window mapped (warm) | 212 ms | **158 ms** | −25 % |
| shell at first prompt | 160 ms | 136 ms | −15 % |
| agent box ready | 263 ms | 199 ms | −24 % |
| window mapped (cold) | 257 ms | 179 ms | −30 % |
| relay Pss, 1 pane | 39.5 MB | **27.3 MB** | −12.2 MB |
| relay Pss, 4 panes | 40.9 MB | 31.8 MB | −9.1 MB |
| tree Pss, 4 panes | 165.4 MB | 157.9 MB | −7.5 MB |
| idle CPU / wakeups, 4 panes | 1.37 % / 47.0 | 1.50 % / 48.0 | same |
| SIGTERM to exit, 1 pane | 63.6 ms | 63.9 ms | same |

Idle behaviour is identical, so every finding in section 1 applies to both. The Qt6 build's text is
9.54 MB vs 9.01 MB. `~/relay-perf/build-qt6.log` notes that only `relay-wordwrap-tests` fails to
compile on Qt6 — a test-only fault the orchestrator is tracking.

**This is a shipping decision, not a fix I can make**: whether the Ubuntu 26.04 package builds
against Qt6 is the owner's call and it needs that test fixed first. The numbers say it is worth
doing.

---

## 5. A fresh profile opens two modal dialogs a second after launch

Not a performance number, but it is on the startup path and it broke the harness before it was
found. On a profile with no settings, the first pane's `configure` event schedules
`openInstructions()` at +400 ms (`src/Pane.h:4322-4325`) and the approvals screen at +800 ms
(`src/Pane.h:4331-4333`). Both are `QDialog::exec()` modals, so about a second after the window
appears the user is looking at a 760x520 dialog they did not ask for, and every keystroke until
they dismiss it goes to it. On this machine the instructions dialog fires because Relay finds the
owner's `~/.claude/CLAUDE.md`, which any Claude Code user will have.

Whether two stacked first-run modals is the wanted onboarding is a product decision, so it stays
listed. The harness works around it with `SKIP_FIRSTRUN=1`, which seeds
`instructions/onboarded=true` and `security/approvals_chosen=true`.

---

## 6. Measured and fine — do not re-profile these

- **Shutdown.** SIGTERM to process exit, medians of 3, same script on both machines
  (`harness/shutdown.py <panes> <tabs> <reps> <tag>`, isolation on in every run):

  | | spark Qt5 | sphinxpad Qt5 | sphinxpad Qt6 |
  |---|---|---|---|
  | 1 pane | 63.5 ms | 63.6 ms | 63.9 ms |
  | 6 panes in 3 tabs | 163.8 ms | 214.1 ms | not measured — see below |

  So ~17–25 ms per extra pane. It waits on `aboutToQuit` → `saveScrollbacks()` + `flushClosed()` +
  `saveLayoutNow()` (`src/main.cpp:328-340`) and then on each `~Pane` telling its worker to shut
  down. Nothing hangs, nothing times out, no pane is left behind. (An earlier 215 ms figure for
  spark came from the restore test, which runs with `isolation/enabled=false`; the table above is
  the like-for-like re-measurement.)

  **The Qt6 six-pane cell could not be filled.** Three attempts each ran past their budget inside
  `build_layout`, not inside the quit: the harness drives Ctrl+T / Ctrl+E through `xdotool` and then
  re-asserts focus by hand (there is no window manager on the Xvfb display), and with six panes on a
  laptop that another #PF4K agent was also driving, one repetition took minutes rather than the
  ~35 s it takes on spark. The relay process was healthy every time — six panes, twelve children,
  the python driver asleep in its own `time.sleep`, never in the post-SIGTERM `wait()`. So this is a
  harness/contention limit, not a Relay one, and chasing it further was not worth the machine time.
  The one-pane row above is measured on all three configurations and shows Qt5 and Qt6 quitting
  identically (63.6 vs 63.9 ms), which is the claim the table is used for.
- **Open/close churn.** 20 split-then-close cycles (`harness/churn.py churn 20`): relay Pss 40.4 →
  42.3 MB and flat from cycle 10 on; open fds 25 → 21; threads 49 → 45; child processes back to 2;
  `$TMPDIR/relay-*` runtime directories back to 2 every time. **No leaked children, fds, runtime
  directories or unbounded RSS.**
- **The python workers while idle.** 0.00 % CPU and 0.0 wakeups/s each, every run, both machines.
  They are a memory cost only (finding 2).
- **Restoring a saved layout.** Six panes across three tabs, quit with SIGTERM and relaunched
  without `--fresh`: window mapped at **317 ms** against 194–236 ms for a single fresh pane, with
  all six workers already spawned at 265 ms. About 15 ms per extra pane, and the panes are built
  before `show()` so the window appears complete rather than filling in. Restored 6/6 panes.
- **Cold start, page cache, binary size, dynamic linking.** See 3b. 28 ms of the ~220 ms, 44
  libraries; not worth attacking.
- **`QFontDatabase().families()` in `TerminalView`'s constructor.** 12 ms, once per process because
  Qt caches the database — it is not 12 ms per pane. Fine as it is.

---

## Handed to other agents

- **The 27 MB per python worker** — where it goes, and whether `-S -u worker.py` can import less.
  Worker internals, not the app level. (Finding 2.)
- **`relay::usage::walkTrees`** (`src/PaneUsage.cpp:109`) was 3.7 % of idle samples: it walks
  `/proc` for the shell and worker trees of *every pane in every tab* at 2.5 Hz, including panes in
  tabs nobody is looking at — `src/RelayWindow.h:7263-7275` iterates `m_tabs->count()`, not just the
  current index. Skipping non-current tabs is a three-line change in `refreshPaneStatus()`, but that
  function belongs as much to the pane-chrome/Switchboard agent as to me; named here so it is not
  lost.

## Harness notes, and the one thing that could not be pinned down

- `XDG_RUNTIME_DIR` must **not** be isolated for a realistic run: `isolation::available()` needs the
  real `systemd --user`, and with a fake runtime dir every worker exits 1. The one exception is the
  restore test, because `windows.lock` lives in `XDG_RUNTIME_DIR` (`src/WindowState.cpp:43-48`) and
  a shared one means the owner's live Relay owns the layout, so the test instance saves and restores
  nothing at all. That test therefore runs with `isolation/enabled=false` (`NO_ISOLATION=1`).
- The Xvfb display has no window manager, so `xdotool` input needs an explicit
  `windowfocus --sync` on the real top-level (`harness/idle.py:focus`).
- Spark's `relay` shows 45–54 threads against sphinxpad's 4–7. Twenty of them are `llvmpipe-*`
  (Mesa's software rasteriser under Xvfb) and the rest are pool threads; sphinxpad's Xvfb does not
  load llvmpipe. **An Xvfb artefact, not a Relay finding** — Relay's own threads are the main
  thread, `QXcbEventQueue`, `QDBusConnection`, one `relay:disk$0`, and one pooled thread per pane.
- Two runs out of about fifteen lost their agent worker to an external `SIGTERM` (`worker_exit
  code=15 crashed=1`) two seconds after `configure`, once on each machine. Both machines are shared
  with four other profiling agents and the owner's live session, and no Relay timer fires at +2 s,
  so this reads as interference from another session rather than a Relay fault. It is recorded
  because it could not be reproduced on demand and so could not be ruled out.
