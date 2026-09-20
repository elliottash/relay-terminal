# #GMCF decision 5 — the isolation probe stops holding the first window

Finding 3a of `docs/qa_evidence/2026-09-20-perf-profile/startup/FINDINGS.md`: `isolation::available()`
ran `systemd-run --user --scope --quiet -- true` and sat in `waitForFinished(3000)` on the GUI
thread, from the first pane's constructor — before any window existed. On a healthy machine the
probe costs 0–10 ms; on one whose `systemd --user` or D-Bus is not answering it was up to three
seconds of window-less Relay with nothing on screen to say why.

**Now**: `main()` calls `isolation::beginProbe()` before it builds anything and does not wait. The
first consumer waits at most `isolation::kProbeWaitMs` (300 ms); a probe still outstanding then
leaves the answer *unknown*, so that pane runs unisolated down the existing no-systemd-run path —
the same "Per-pane memory isolation is unavailable" notice, unchanged — and the next pane picks up
the late answer. A probe with no answer after `kProbeGiveUpMs` (10 s) is killed and called
unavailable, which is what the old 3 s timeout did. Answered once per process, as before.

## Machine

spark (aarch64, 20 cores, Ubuntu 24.04, Qt 5.15), Xvfb `:291` 1600x1000x24, load 2.7–4.5 (five
other #PF4K sessions building at the same time — which is why the absolute numbers below sit above
FINDINGS.md's 236 ms; before and after were measured back to back under the same load).

- before = `/tmp/claude-1000/pf4k/build/relay` (clean export of `main` at `ccb31a8e`)
- after = this change, `build/relay`
- harness = `docs/qa_evidence/2026-09-20-perf-profile/startup/harness/harness.py`, driven by
  `measure.py` (kept beside this file); fresh isolated profile per run, `--clean-shell --fresh`.

## exec → window mapped (ms, median of N)

| scenario | before | after |
|---|---|---|
| healthy `systemd-run` (N=5) | **280.0** (274.0–320.2) | **262.2** (226.6–267.6) |
| a `systemd-run` whose probe takes 5 s (N=3) | **3275.0** (3274.0–3278.5) | **564.8** (554.5–576.8) |
| a `systemd-run` whose probe exits 1 (N=3) | — | **246.1** (245.1–268.1) |

The healthy case does not regress (it is 18 ms faster, inside the run-to-run spread). The slow case
drops by 2.7 s: what is left of it is the one 300 ms wait the first consumer is allowed, plus the
ordinary ~260 ms startup. `shell` and `agent ready` move with it — 3274 → 566 ms and 3331 → 618 ms.

The old shape, measured directly against the same 5 s fake (`old/old_probe.cpp` beside this file,
the pre-change `available()` body compiled on its own):

```
old available=0 waited_ms=3003      # the 5 s fake: three seconds on the calling thread
old available=1 waited_ms=0         # a fake that answers at once
```

## The two behaviours, live

`late_answer.py` (beside this file) starts Relay with a fake `systemd-run` that delays only the
`-- true` probe by 5 s and delegates every real scope to the real one, then reads each pane shell's
`/proc/<pid>/cgroup` — `systemd-run --scope` execs in place, so an isolated shell's cgroup names a
`relay-pane-*.scope` and an unisolated one does not:

```
first pane shells at t=2s:  [(1234066, False, 'app-dev.warp.Warp-….scope')]
all pane shells at t=13s:   [(1234066, False, 'app-dev.warp.Warp-….scope'),
                             (1234505, True,  'relay-pane-ad077613-shell-1.scope')]
VERDICT first-pane-unisolated: True
VERDICT second-pane-isolated:  True
```

`slow-probe-unisolated-notice.png` is that first pane at t=2.6 s: the window is up and it says
"Per-pane memory isolation is unavailable (no systemd user session); panes run unisolated." — the
existing notice, reached through the existing code path in `startTerminal()`, which is unchanged.

## Test

`tests/isolation_test.cpp` (`ctest -R isolation`, 5 s). Each scenario runs in a child process,
because the probe is deliberately answered once per process, with a fake `systemd-run` as the only
thing on that child's PATH:

- a probe that takes 5 s answers the first ask in **0 ms**, unisolated, and three asks in a row
  (what one pane makes: worker environment, worker scope, shell scope) still cost one wait, not
  three — then the real answer is picked up and the next pane is isolated;
- a probe that answers at once is available, and is run **once** however many times it is asked;
- a probe that exits 1 is a fast, cached "no";
- no `systemd-run` on PATH needs no subprocess and no wait at all.

Against the old code the first two fail: `waited_ms` is 3003 rather than 0, and the late answer is
never picked up.
